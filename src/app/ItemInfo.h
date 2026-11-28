#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace sf::ui {

// The item just drawn takes a value from a drag or from typing (a slider, a drag box, a text box): ImGui sets Inputable on it (the patched
// imgui_widgets.cpp, cmake/Dependencies.cmake, keeps the flag; the item hook that gets it alone is only on in recordings and tests).
// Read from ImGui's own item state, never from the hook (tests/test_hooks_off.cpp draws with the hook off).
inline bool ItemIsValueBox() { return (ImGui::GetCurrentContext()->LastItemData.StatusFlags & ImGuiItemStatusFlags_Inputable) != 0; }

} // namespace sf::ui
