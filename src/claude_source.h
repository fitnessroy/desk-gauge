#pragma once

// Polls Claude plan usage straight from Anthropic (device login, see claude_auth.h) and,
// if configured, today's token counts from the optional bridge. Updates g_claude.
namespace claude_source {
void loop();
void pollNow();
}
