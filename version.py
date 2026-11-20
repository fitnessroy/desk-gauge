# PlatformIO pre-build script: FW_VERSION = "1.0.<commits> (<hash>[-dirty])" from git, so every
# build is numbered without bumping anything by hand. Falls back to "dev" outside a git checkout.
import subprocess

Import("env")


def git(*args):
    return subprocess.check_output(["git", *args], cwd=env["PROJECT_DIR"], text=True).strip()


try:
    count = git("rev-list", "--count", "HEAD")
    sha = git("rev-parse", "--short", "HEAD")
    dirty = "-dirty" if git("status", "--porcelain", "--untracked-files=no") else ""
    version = f"1.0.{count} ({sha}{dirty})"
except Exception:
    version = "dev"

env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
print(f"desk-gauge firmware {version}")
