#include "claude_auth.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#include "config.h"
#include "net.h"

namespace claude_auth {

static String refreshToken, access;
static uint32_t accessExp = 0;         // epoch seconds
static String pkceVerifier, pkceState;  // pending login, RAM only

static String base64url(const uint8_t* data, size_t len) {
  size_t outLen = 0;
  char buf[96];
  mbedtls_base64_encode((unsigned char*)buf, sizeof buf, &outLen, data, len);
  String s;
  for (size_t i = 0; i < outLen; ++i) {
    char c = buf[i];
    if (c == '=') break;
    s += c == '+' ? '-' : c == '/' ? '_' : c;
  }
  return s;
}

static String randomToken() {
  uint8_t b[32];
  esp_fill_random(b, sizeof b);
  return base64url(b, sizeof b);
}

static String urlEncode(const String& s) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (char c : s) {
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(uint8_t)c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

static void store() {
  Preferences p;
  p.begin("auth", false);
  p.putString("rt", refreshToken);
  p.putString("at", access);
  p.putULong("exp", accessExp);
  p.end();
}

void begin() {
  Preferences p;
  p.begin("auth", true);
  refreshToken = p.getString("rt", "");
  access = p.getString("at", "");
  accessExp = p.getULong("exp", 0);
  p.end();
}

bool loggedIn() { return refreshToken.length() > 0; }

void logout() {
  refreshToken = access = "";
  accessExp = 0;
  store();
}

void invalidateAccess() { accessExp = 0; }

String startLogin() {
  pkceVerifier = randomToken();
  pkceState = randomToken();
  uint8_t hash[32];
#if MBEDTLS_VERSION_MAJOR >= 3
  mbedtls_sha256((const unsigned char*)pkceVerifier.c_str(), pkceVerifier.length(), hash, 0);
#else
  mbedtls_sha256_ret((const unsigned char*)pkceVerifier.c_str(), pkceVerifier.length(), hash, 0);
#endif
  String url = CLAUDE_OAUTH_AUTHORIZE;
  url += "?code=true&client_id=" CLAUDE_OAUTH_CLIENT_ID "&response_type=code";
  url += "&redirect_uri=" + urlEncode(CLAUDE_OAUTH_REDIRECT);
  url += "&scope=" + urlEncode(CLAUDE_OAUTH_SCOPES);
  url += "&code_challenge=" + base64url(hash, sizeof hash);
  url += "&code_challenge_method=S256&state=" + pkceState;
  return url;
}

// Shared by code exchange and refresh: POST JSON to the token endpoint, keep the result.
static bool tokenRequest(JsonDocument& req, String& err) {
  String body;
  serializeJson(req, body);
  net::Response r = net::request("POST", CLAUDE_OAUTH_TOKEN, body,
                                 {{"Content-Type", "application/json"},
                                  {"User-Agent", "desk-gauge/1.0"}});
  JsonDocument res;
  if (r.status != 200 || deserializeJson(res, r.body) || !res["access_token"].is<const char*>()) {
    err = "login " + r.error();
    const char* code = res["error"] | "";
    const char* desc = res["error_description"] | code;
    if (*desc) err += String(": ") + desc;
    // A rejected refresh token means the login is gone for good; ask for a new one.
    if (strcmp(code, "invalid_grant") == 0 && req["grant_type"] == "refresh_token") logout();
    return false;
  }
  access = res["access_token"].as<const char*>();
  if (res["refresh_token"].is<const char*>()) refreshToken = res["refresh_token"].as<const char*>();
  accessExp = time(nullptr) + (res["expires_in"] | 3600);
  store();
  return true;
}

bool finishLogin(const String& pastedRaw, String& err) {
  if (!pkceVerifier.length()) {
    err = "start the login again";
    return false;
  }
  // The callback page shows "code#state". Also accept a pasted callback URL.
  String pasted = pastedRaw;
  pasted.trim();
  int q = pasted.indexOf("code=");
  if (q >= 0) {
    String rest = pasted.substring(q + 5);
    int amp = rest.indexOf('&');
    String code = amp >= 0 ? rest.substring(0, amp) : rest;
    int st = pasted.indexOf("state=");
    String state = st >= 0 ? pasted.substring(st + 6) : pkceState;
    if (state.indexOf('&') >= 0) state = state.substring(0, state.indexOf('&'));
    pasted = code + "#" + state;
  }
  int hash = pasted.indexOf('#');
  String code = hash >= 0 ? pasted.substring(0, hash) : pasted;
  String state = hash >= 0 ? pasted.substring(hash + 1) : pkceState;
  if (state != pkceState) {
    err = "code is from another login attempt, start again";
    return false;
  }

  JsonDocument req;
  req["grant_type"] = "authorization_code";
  req["code"] = code;
  req["state"] = state;
  req["redirect_uri"] = CLAUDE_OAUTH_REDIRECT;
  req["client_id"] = CLAUDE_OAUTH_CLIENT_ID;
  req["code_verifier"] = pkceVerifier;
  if (!tokenRequest(req, err)) return false;
  pkceVerifier = pkceState = "";
  return true;
}

String accessToken(String& err) {
  if (!loggedIn()) {
    err = "not signed in";
    return "";
  }
  if (access.length() && (uint32_t)time(nullptr) + 300 < accessExp) return access;

  JsonDocument req;
  req["grant_type"] = "refresh_token";
  req["refresh_token"] = refreshToken;
  req["client_id"] = CLAUDE_OAUTH_CLIENT_ID;
  req["scope"] = CLAUDE_OAUTH_SCOPES;
  if (!tokenRequest(req, err)) return "";
  Serial.println("[auth] access token refreshed");
  return access;
}

}  // namespace claude_auth
