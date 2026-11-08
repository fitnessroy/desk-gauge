#!/usr/bin/env python3
"""Claude usage bridge for desk-gauge.

Serves a small JSON document at http://<host>:8787/claude.json for the ESP32:

    {
      "session":   {"pct": 42.0, "resets_in": 7980},   # 5-hour window
      "week":      {"pct": 18.0, "resets_in": 401234}, # 7-day window
      "week_opus": {"pct": 3.0,  "resets_in": 401234} | null,
      "today_tokens": 12345678,          # all Claude Code tokens since local midnight
      "today_output_tokens": 234567,
      "error": null
    }

Sources:
  * Plan limits come from the endpoint Claude Code's /usage screen uses, authenticated with
    the Claude Code OAuth login on this machine (macOS keychain or ~/.claude/.credentials.json).
    It is undocumented and may change. The token never leaves this process.
  * Token counts are summed from Claude Code transcripts in ~/.claude/projects/.

Standard library only. Usage:  python3 claude_usage_bridge.py [--port 8787]
"""

import argparse
import datetime as dt
import glob
import json
import os
import platform
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
CACHE_S = 60
PROJECTS_DIR = os.path.expanduser(os.environ.get("CLAUDE_PROJECTS_DIR", "~/.claude/projects"))


def oauth_token():
    if tok := os.environ.get("CLAUDE_OAUTH_TOKEN"):
        return tok
    raw = None
    if platform.system() == "Darwin":
        res = subprocess.run(
            ["security", "find-generic-password", "-s", "Claude Code-credentials", "-w"],
            capture_output=True, text=True,
        )
        if res.returncode == 0:
            raw = res.stdout
    if raw is None:
        path = os.path.expanduser("~/.claude/.credentials.json")
        if os.path.exists(path):
            with open(path) as f:
                raw = f.read()
    if not raw:
        raise RuntimeError("no Claude Code login found (run `claude` and log in)")
    return json.loads(raw)["claudeAiOauth"]["accessToken"]


def parse_window(w, now):
    if not w or w.get("utilization") is None:
        return None
    resets_in = -1
    if w.get("resets_at"):
        reset = dt.datetime.fromisoformat(w["resets_at"].replace("Z", "+00:00"))
        resets_in = max(0, int((reset - now).total_seconds()))
    return {"pct": round(float(w["utilization"]), 1), "resets_in": resets_in}


def fetch_limits():
    req = urllib.request.Request(USAGE_URL, headers={
        "Authorization": f"Bearer {oauth_token()}",
        "anthropic-beta": "oauth-2025-04-20",
        "User-Agent": "desk-gauge-bridge/1.0",
    })
    with urllib.request.urlopen(req, timeout=10) as resp:
        data = json.load(resp)
    now = dt.datetime.now(dt.timezone.utc)
    return {
        "session": parse_window(data.get("five_hour"), now),
        "week": parse_window(data.get("seven_day"), now),
        "week_opus": parse_window(data.get("seven_day_opus"), now),
    }


def tokens_today():
    midnight = dt.datetime.now().astimezone().replace(hour=0, minute=0, second=0, microsecond=0)
    cutoff = midnight.timestamp()
    seen = set()
    total = output = 0
    for path in glob.iglob(os.path.join(PROJECTS_DIR, "**", "*.jsonl"), recursive=True):
        try:
            if os.path.getmtime(path) < cutoff:
                continue
            with open(path, encoding="utf-8", errors="replace") as f:
                for line in f:
                    if '"usage"' not in line:
                        continue
                    try:
                        entry = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    msg = entry.get("message") or {}
                    usage = msg.get("usage")
                    ts = entry.get("timestamp")
                    if not usage or not ts:
                        continue
                    if dt.datetime.fromisoformat(ts.replace("Z", "+00:00")).timestamp() < cutoff:
                        continue
                    # Streaming writes the same message several times; count it once.
                    key = (msg.get("id"), entry.get("requestId"))
                    if key in seen:
                        continue
                    seen.add(key)
                    out = usage.get("output_tokens", 0) or 0
                    output += out
                    total += out + sum(usage.get(k, 0) or 0 for k in (
                        "input_tokens", "cache_creation_input_tokens", "cache_read_input_tokens"))
        except OSError:
            continue
    return total, output


class State:
    lock = threading.Lock()
    payload = None
    fetched = 0.0


def build_payload():
    payload = {"session": None, "week": None, "week_opus": None, "error": None}
    try:
        payload.update(fetch_limits())
    except urllib.error.HTTPError as e:
        payload["error"] = f"usage HTTP {e.code}" + (" (open Claude Code to refresh login)" if e.code == 401 else "")
    except Exception as e:  # noqa: BLE001 - report anything to the device
        payload["error"] = f"usage: {e}"
    payload["today_tokens"], payload["today_output_tokens"] = tokens_today()
    return payload


def current_payload():
    with State.lock:
        if State.payload is None or time.time() - State.fetched > CACHE_S:
            fresh = build_payload()
            # Keep the last good limits if this fetch failed.
            if fresh["error"] and State.payload and State.payload.get("session"):
                for k in ("session", "week", "week_opus"):
                    fresh[k] = State.payload[k]
            State.payload, State.fetched = fresh, time.time()
        return State.payload


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] not in ("/claude.json", "/"):
            self.send_error(404)
            return
        body = json.dumps(current_payload()).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--once", action="store_true", help="print the payload and exit")
    args = ap.parse_args()

    if args.once:
        print(json.dumps(current_payload(), indent=2))
        return
    print(f"desk-gauge bridge on http://{args.host}:{args.port}/claude.json")
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
