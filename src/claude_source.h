#pragma once

// Polls Claude plan usage straight from Anthropic (device login, see claude_auth.h) and,
// if configured, today's token counts from the optional bridge. Updates g_claude.
namespace claude_source {
void begin();   // restores the last reading from flash (after claude_auth::begin)
void loop();
void forget();  // drops the stored reading (on sign-out)
void pollNow();
}
