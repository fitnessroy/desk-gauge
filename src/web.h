#pragma once
#include <functional>

#include <LovyanGFX.hpp>

// http://desk-gauge.local/ : live preview of the faces, Claude sign-in, and settings.
// Also serves /state.json.
namespace web {
// Renders face `index` (0 = Claude, 1 = printer) into the given sprite.
using RenderFn = std::function<void(int index, LGFX_Sprite& sprite)>;
void begin(RenderFn render, int faceCount);
void loop();
}
