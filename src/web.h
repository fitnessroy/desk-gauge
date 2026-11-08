#pragma once
#include <functional>

#include <LovyanGFX.hpp>

// Web preview: http://desk-gauge.local/ shows both gauge faces live, so the UI can be
// developed without panels attached. Also serves /state.json.
namespace web {
// Renders face `index` (0 = Claude, 1 = printer) into the given sprite.
using RenderFn = std::function<void(int index, LGFX_Sprite& sprite)>;
void begin(RenderFn render);
void loop();
}
