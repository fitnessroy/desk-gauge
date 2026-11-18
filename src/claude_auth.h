#pragma once
#include <Arduino.h>

// Device-owned Claude login (OAuth 2 + PKCE, manual code paste).
//
//   1. startLogin() gives a claude.com URL; the user approves there and gets a code.
//   2. finishLogin(code) trades it for an access + refresh token, stored in flash.
//   3. accessToken() returns a valid access token, refreshing it when it is close to expiry.
//
// This is a separate login from Claude Code on your computer, so refreshing here never logs
// that one out.
namespace claude_auth {
void begin();
bool loggedIn();
String startLogin();
bool finishLogin(const String& pasted, String& err);
void logout();
String accessToken(String& err);
void invalidateAccess();  // call after a 401 so the next accessToken() refreshes
}
