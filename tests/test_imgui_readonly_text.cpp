// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// gh#184 (0.14.4): the app closed (SIGSEGV in strcmp) when a slider's typed number lost the focus while the field was read-only
// (Alt held over a palette: ui::BeginPaletteItems; a finger or pen holding it: ui::SliderInside) and was writable again the frame it
// was seen to end. ImGui keeps no text for a read-only field and compared that null text (patched in cmake/Dependencies.cmake).
// Run with the item hook off, as for every user (tests/HooksOff.h): the guard in ui::NoteItem is called by that hook only.
// Only built with the app (the imgui library).
#include "TestHarness.h"

#ifdef SF_TEST_IMGUI

#include "HooksOff.h"

// The hook functions ImGui calls when TestEngineHookItems is on (src/app/Demo.cpp has the app's); never called here (the hook is off).
// (tests/test_hooks_off.cpp defines the same stubs when it is in the tree: keep one set.)
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID, const ImRect&, const ImGuiLastItemData*) { ++sf::test::g_hookCalls; }
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID, const char*, ImGuiItemStatusFlags) { ++sf::test::g_hookCalls; }
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) { ++sf::test::g_hookCalls; }
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

namespace {

// One frame: a text field or a slider in a window, read-only or not.
struct Probe {
    char text[32] = "60";
    float size = 60.0f;
    void frame(bool readOnly, bool slider) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(300, 120));
        ImGui::Begin("probe");
        if (readOnly) ImGui::PushItemFlag(ImGuiItemFlags_ReadOnly, true);
        ImGui::SetNextItemWidth(-1);
        if (slider)
            ImGui::SliderFloat("##size", &size, 0.5f, 800.0f, "Size %.0f px", ImGuiSliderFlags_Logarithmic);
        else
            ImGui::InputText("##text", text, sizeof text);
        if (readOnly) ImGui::PopItemFlag();
        ImGui::End();
        ImGui::EndFrame();
    }
};

void mouse(float x, float y, bool down) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, down);
}

} // namespace

// A text field pressed while read-only (Alt held over a palette), then drawn writable the next frame (Alt let go): ImGui kept no text.
TEST(imgui_text_field_pressed_read_only_then_writable_does_not_compare_null_text) {
    sf::test::HooksOffContext context;
    Probe p;
    p.frame(false, false);
    mouse(100, 42, false);
    p.frame(false, false); // (hovered a frame first)
    mouse(100, 42, true);
    p.frame(true, false); // the press, read-only: the field takes the focus without text
    mouse(100, 42, false);
    p.frame(true, false);
    CHECK(GImGui->ActiveId != 0);
    p.frame(false, false); // Alt let go: the same field, writable
    p.frame(false, false);
    mouse(600, 400, true); // a press away from it closes it
    p.frame(false, false);
    mouse(600, 400, false);
    p.frame(false, false);
    CHECK(GImGui->ActiveId == 0);
    CHECK(sf::test::g_hookCalls == 0);
}

// The same on a slider's typed number (the double-click patch opens it; here Ctrl+click, as ImGui does).
TEST(imgui_slider_number_opened_read_only_then_writable_does_not_compare_null_text) {
    sf::test::HooksOffContext context;
    ImGui::GetIO().ConfigMacOSXBehaviors = false; // (on a Mac ImGui reads Ctrl as Cmd; Ctrl+click is what opens the typed number)
    Probe p;
    p.frame(false, true);
    mouse(100, 42, false);
    p.frame(false, true); // (hovered a frame first)
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    mouse(100, 42, true);
    p.frame(true, true);
    mouse(100, 42, false);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    p.frame(true, true);
    p.frame(true, true);
    CHECK(GImGui->TempInputId != 0); // (its number is being typed)
    p.frame(false, true);
    p.frame(false, true);
    mouse(600, 400, true);
    p.frame(false, true);
    mouse(600, 400, false);
    p.frame(false, true);
    CHECK(GImGui->ActiveId == 0);
    CHECK(sf::test::g_hookCalls == 0);
}

#endif // SF_TEST_IMGUI
