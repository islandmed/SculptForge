// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guards that read what an item is must hold with the item hook off (tests/HooksOff.h; docs/testing-flow.md). Only built with the
// app (the imgui library). Add a case per guard that depends on item info.
#include "TestHarness.h"

#ifdef SF_TEST_IMGUI

#include "HooksOff.h"
#include "app/ItemInfo.h"

#include <string>

// The hook functions ImGui calls when TestEngineHookItems is on (src/app/Demo.cpp has the app's). Here they count: with the hook off
// they are never called, so a pass cannot come from them.
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID, const ImRect&, const ImGuiLastItemData*) { ++sf::test::g_hookCalls; }
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID, const char*, ImGuiItemStatusFlags) { ++sf::test::g_hookCalls; }
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) { ++sf::test::g_hookCalls; }
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

using namespace sf;
using sf::test::guardAfter;

namespace {
bool isValueBox() { return ui::ItemIsValueBox(); }
} // namespace

TEST(hooks_off_value_box_is_true_on_a_slider_a_drag_box_and_a_text_box) {
    float v = 0.5f;
    int n = 3;
    char text[32] = "abc";
    CHECK(guardAfter([&] { ImGui::SliderFloat("##slider", &v, 0.0f, 1.0f); }, isValueBox));
    CHECK(guardAfter([&] { ImGui::SliderInt("##sliderint", &n, 0, 10); }, isValueBox));
    CHECK(guardAfter([&] { ImGui::DragFloat("##drag", &v, 0.01f); }, isValueBox));
    CHECK(guardAfter([&] { ImGui::InputText("##text", text, sizeof text); }, isValueBox));
}

TEST(hooks_off_value_box_is_false_on_a_button_a_checkbox_and_a_label) {
    bool on = false;
    CHECK(!guardAfter([] { ImGui::Button("Go"); }, isValueBox));
    CHECK(!guardAfter([&] { ImGui::Checkbox("Check", &on); }, isValueBox));
    CHECK(!guardAfter([] { ImGui::TextUnformatted("label"); }, isValueBox));
    CHECK(!guardAfter([] { ImGui::Selectable("Row"); }, isValueBox));
}

TEST(hooks_off_the_context_really_runs_without_the_hook) {
    float v = 0.0f;
    CHECK(!guardAfter([&] { ImGui::SliderFloat("##s", &v, 0.0f, 1.0f); }, [] { return GImGui->TestEngineHookItems; }));
}

#endif // SF_TEST_IMGUI
