// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A guard that reads item info (what a widget just drawn is) must hold with the item hook off, as it is for every user: UI tests
// run with it on.
// This is the harness: a headless ImGui context (no window, no backend) with TestEngineHookItems = false, one frame, and a
// helper that draws a widget and asks a guard about it right after. A new guard adds cases to tests/test_hooks_off.cpp.
//
//   CHECK(guardAfter([] { float v = 0; ImGui::SliderFloat("##v", &v, 0, 1); }, [] { return ui::ItemIsValueBox(); }));
#pragma once

#include "TestHarness.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>

namespace sf::test {

// Calls of the item hook functions in this run (tests/test_hooks_off.cpp defines them as stubs that count): with the hook off, none.
inline int g_hookCalls = 0;

// A one-frame ImGui context with the hook off, for the scope of the object.
class HooksOffContext {
public:
    HooksOffContext() {
        ctx_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h); // (builds the atlas: NewFrame refuses without one)
        ctx_->TestEngineHookItems = false;             // ImGui's default, said again: it is the point of the test
        g_hookCalls = 0;
    }
    ~HooksOffContext() { ImGui::DestroyContext(ctx_); }
    HooksOffContext(const HooksOffContext&) = delete;
    HooksOffContext& operator=(const HooksOffContext&) = delete;

private:
    ImGuiContext* ctx_ = nullptr;
};

// Draws `draw` (one widget) in a window of a hook-off frame, then returns what `guard` says about it, called before the next item.
inline bool guardAfter(const std::function<void()>& draw, const std::function<bool()>& guard) {
    HooksOffContext context;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(400, 300));
    ImGui::Begin("hooks-off probe");
    draw();
    const bool answer = guard();
    CHECK(g_hookCalls == 0); // the hook stayed off: no pass can come from what it collects
    ImGui::End();
    ImGui::EndFrame();
    return answer;
}

} // namespace sf::test
