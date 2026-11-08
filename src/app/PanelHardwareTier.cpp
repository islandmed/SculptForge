// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hardware tiers in the app (core/HardwareTier.h, docs/performance.md "Hardware tiers"):
//  - Preferences > Performance, at the top: the tier and why ("RTX 3060, 12 GB VRAM, 32 GB RAM: High"), what it turns
//    on, Auto / each tier; the graphics card (on machines with two); Compute: Auto / CPU / GPU and each task's path;
//  - the first-run card: once per machine (after the first calibration, or a new GPU / memory size), never blocking;
//  - the tier chip in the top bar (Shell.cpp) opens the panel; Turbo (View > Turbo, Ctrl+Alt+T, the bolt in the top
//    bar): antialiasing off and a lighter view while you sculpt and orbit, the detected tier's top budgets;
//    everything comes back as it was when the view stops, and Off changes nothing the user set;
//  - the memory guard: once a second, free RAM (and video memory where the driver says); tight -> the tier in effect
//    steps down a level (with a status message), roomy again for a minute -> back up.
#include "app/App.h"
#include "app/Calibration.h"
#include "app/HwProfileUi.h"
#include "app/I18n.h"
#include "app/ShellGeometry.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include "core/Benchmark.h"
#include "core/HardwareTier.h"
#include "core/MemoryGuard.h"
#include "core/ThreadPool.h"

#include <IconsLucide.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace sf {

namespace {

constexpr const char* kPath = "Preferences > Performance";

void dimWrapped(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void bullet(const std::string& text) {
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text.c_str());
}

std::string upperCase(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool envTurbo() {
    const char* v = std::getenv("SF_TURBO");
    return v && *v == '1';
}

} // namespace

bool App::turboOn() const { return turbo_; }

std::string App::tierChipLabel() const {
    if (!hwProfile_ || !hwProfile_->active) return {};
    return hwTierDisplayName(hwProfile_->tier.hwTier);
}

void App::saveProfileOverrides(const ProfileOverrides& o) {
    if (!hwProfile_) return;
    const HardwareProfile& cur = hwProfile_->profile;
    const HardwareProfile next = mapProfile(cur.hardware, cur.calibration, o, &cur.measuredTier);
    std::string error;
    if (!saveHardwareProfile(hardwareProfilePath(), next, error)) SDL_Log("Hardware profile not saved: %s", error.c_str());
    applyHardwareProfile(next, false);
}

void App::setTurbo(bool on) {
    turbo_ = on;
    turboToggledAt_ = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    if (hwProfile_) {
        ProfileOverrides o = hwProfile_->profile.overrides;
        o.turbo = on; // remembered per machine (hardware_profile.json)
        saveProfileOverrides(o);
    }
    if (!on) {
        status_ = "Turbo off: everything is as you set it";
        return;
    }
    // One line, what it changed (only what this machine and these settings get).
    std::string what;
    auto add = [&](const std::string& s) { what += (what.empty() ? "" : ", ") + s; };
    if (uiPrefs_.antialiasing > 1) add("antialiasing off while you sculpt and orbit");
    add("a lighter view while the view moves");
    if (hwProfile_) {
        const HardwareProfile& p = hwProfile_->profile;
        if (p.detected.tier > p.hwTier) add(std::string("the full ") + hwTierDisplayName(p.detected.tier) + " budgets");
        if (hwProfile_->tier.memoryStepDown > 0) add("within the memory free now");
    }
    status_ = "Turbo on: " + what + ". Full quality the moment you stop";
}

void App::registerHardwareTierUiActions() {
    if (envTurbo()) turbo_ = true; // (benches: SF_TURBO=1)
    Action a;
    a.id = "view.turbo";
    a.label = "Turbo";
    a.palette = "View";
    a.shortcut = "Ctrl+Alt+T";
    a.run = [this] { setTurbo(!turbo_); };
    a.checked = [this] { return turbo_; };
    a.description = "Everything this computer can safely give to sculpting speed, in one click: antialiasing off and a "
                    "lighter view while you sculpt and orbit (full quality the moment you stop), and the top budgets of "
                    "the tier this machine carries. It never goes past what the memory holds. Off puts everything back "
                    "as you set it. Remembered on this computer.";
    actions_.add(std::move(a));
    Action t;
    t.id = "perf.open_tier";
    t.label = "Performance tier";
    t.palette = kPath;
    t.run = [this] { openPerformancePrefs(); };
    t.description = "The hardware tier this computer runs at, why, and what it turns on (Preferences > Performance).";
    actions_.add(std::move(t));
}

void App::updateTierMemoryGuard() {
    if (!hwProfile_) return;
    // UI tests share the PC with other jobs, whose memory use would step the tier and change the status line
    // (ui_hardware_profile saw 52 MB of video memory free during a bench); SF_TIER_GUARD_IN_TESTS=1 turns it on.
    static const bool offInTests = !options_.uiTest.empty() && !std::getenv("SF_TIER_GUARD_IN_TESTS");
    if (offInTests) return;
    HwProfileUi& hw = *hwProfile_;
    const double now = double(SDL_GetTicksNS()) * 1e-9;
    if (now < hw.nextMemoryCheck) return;
    hw.nextMemoryCheck = now + 1.0;
    const uint64_t freeMB = uint64_t(availableMemoryBytes() >> 20);
    const uint64_t gpuFreeMB = hw.profile.hardware.gpuKind == GpuKind::Discrete ? queryFreeVideoMemoryMB(hw.profile.hardware.vramSource) : 0;
    const HwTier chosen = hw.tier.chosenHwTier;
    const int change = hw.memoryGuard.update(now, freeMB, hw.profile.hardware.ramMB, gpuFreeMB, int(chosen));
    if (!change) return;
    const HwTier was = hw.tier.hwTier;
    const HardwareProfile p = hw.profile;
    applyHardwareProfile(p, false);
    SDL_Log("Hardware tier: memory guard %s (%llu MB free, GPU %llu MB): %s -> %s", change > 0 ? "stepped down" : "stepped back up",
            (unsigned long long)freeMB, (unsigned long long)gpuFreeMB, hwTierName(was), hwTierName(hw.tier.hwTier));
    status_ = change > 0 ? std::string("Memory is tight: ") + hwTierDisplayName(hw.tier.hwTier) +
                               " budgets for now (detail streams in a little later; nothing is lost)"
                         : std::string("Memory freed up: back to ") + hwTierDisplayName(hw.tier.hwTier) + " budgets";
}

void App::sampleMemoryShown() {
    const double now = ImGui::GetTime();
    if (memShown_.at >= 0.0 && now - memShown_.at < 1.0) return;
    memShown_.at = now;
    memShown_.appBytes = residentBytes();
    memShown_.freeBytes = availableMemoryBytes();
    memShown_.totalBytes = size_t(std::max(SDL_GetSystemRAM(), 0)) << 20;
    // Video memory where the driver reports it (NVIDIA; AMD gives free only); integrated graphics share the RAM above.
    memShown_.gpuTotalMB = memShown_.gpuFreeMB = 0;
    if (hwProfile_ && !hwProfile_->calibrator) {
        const HardwareInfo& h = hwProfile_->profile.hardware;
        memShown_.gpuFreeMB = queryFreeVideoMemoryMB(h.vramSource);
        if (h.gpuKind == GpuKind::Discrete && h.vramSource.rfind("GL_NVX", 0) == 0) memShown_.gpuTotalMB = h.vramMB;
    }
}

void App::drawHardwareTierSection() {
    if (!hwProfile_ || hwProfile_->calibrator) return; // (the profile section says why, or shows the progress)
    HwProfileUi& hw = *hwProfile_;
    const HardwareProfile& p = hw.profile;
    const TierConfig& tc = hw.tier;
    ImGui::TextUnformatted(tr("Tier"));
    ui::HelpText("Tier", nullptr,
                 "What this computer can carry beyond the essentials: bigger Focus Detail caches, 16K textures and "
                 "the levels to come switch on by themselves where they fit, never where they don't. Every tier "
                 "sculpts smoothly; nothing is sent anywhere.",
                 kPath);
    if (fontBold_) ImGui::PushFont(fontBold_);
    ImGui::TextWrapped("%s", tierSummary(p.detected, tc.hwTier).c_str());
    if (fontBold_) ImGui::PopFont();
    ImGui::LogText("%s", tierSummary(p.detected, tc.hwTier).c_str());
    if (tc.memoryStepDown > 0)
        ImGui::TextColored(theme::accent, "%s", (std::string("Stepped down from ") + hwTierDisplayName(tc.chosenHwTier) +
                                                 " while memory is tight; it steps back up when there is room")
                                                    .c_str());
    HwTier simulated;
    if (simulatedHwTier(simulated)) dimWrapped(std::string("Forced by SF_HW_SIMULATE=") + std::getenv("SF_HW_SIMULATE"));
    else if (p.hwTier != p.detected.tier) dimWrapped(std::string("Set by hand; this computer was detected as ") + hwTierDisplayName(p.detected.tier));
    if (!p.detected.limit.empty()) dimWrapped(p.detected.limit);
    ImGui::TextUnformatted(tr("What this turns on:"));
    for (const std::string& line : tierTurnsOn(tc.hwTier, tc.budgets)) {
        bullet(line);
        ImGui::LogText("%s", line.c_str());
    }

    // Auto / each tier.
    {
        std::vector<std::string> names;
        names.push_back(std::string("Auto (") + hwTierDisplayName(p.detected.tier) + ")");
        for (int i = 0; i < kHwTierCount; ++i)
            names.push_back(hwTierDisplayName(HwTier(i)) + std::string(i > int(p.detected.tier) ? "  (more than detected)" : ""));
        int index = 0;
        HwTier set;
        if (!p.overrides.level.empty() && hwTierFromName(p.overrides.level, set)) index = int(set) + 1;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##perf.level", names[size_t(index)].c_str())) {
            for (int i = 0; i < int(names.size()); ++i)
                if (ImGui::Selectable(names[size_t(i)].c_str(), i == index) && i != index) {
                    ProfileOverrides o = p.overrides;
                    o.level = i == 0 ? std::string() : std::string(hwTierName(HwTier(i - 1)));
                    saveProfileOverrides(o);
                    status_ = i == 0 ? std::string("Tier: automatic (") + hwTierDisplayName(p.detected.tier) + ")"
                                     : std::string("Tier: ") + hwTierDisplayName(HwTier(i - 1)) +
                                           (i - 1 > int(p.detected.tier) ? " (the memory guard still steps down if it runs low)" : "");
                }
            ImGui::EndCombo();
        }
    }
    bool turbo = turbo_;
    if (ImGui::Checkbox(tr("Turbo"), &turbo)) setTurbo(turbo);
    ui::HelpAction(*this, "view.turbo");

    // The graphics card.
    ImGui::Spacing();
    if (!hw.gpusListed) {
        hw.gpus = listGpus();
        hw.gpusListed = true;
    }
    const std::string current = p.hardware.glRenderer.empty() ? std::string("unknown") : shortGpuName(p.hardware.glRenderer);
    if (hw.gpus.size() >= 2) {
        ImGui::TextUnformatted(tr("Graphics card"));
        ui::HelpText("Graphics card", nullptr,
                     "Which graphics card draws SculptForge, on computers with two (a laptop's integrated and discrete "
                     "GPU, or two cards). Auto takes the fastest discrete one. It applies the next time SculptForge "
                     "starts, through the system's own setting (Windows' graphics preference; on Linux, Mesa's "
                     "DRI_PRIME or NVIDIA's render offload). If that card can't start, SculptForge falls back to the "
                     "system's choice and says so.",
                     kPath);
        const std::string autoId = autoGpuId(hw.gpus);
        std::string autoName = "the system's choice";
        for (const GpuAdapter& g : hw.gpus)
            if (g.id == autoId) autoName = g.name;
        std::vector<std::pair<std::string, std::string>> items = {{"", "Auto (" + autoName + ")"}};
        for (const GpuAdapter& g : hw.gpus) items.push_back({g.id, g.name + (g.discrete ? "" : " (integrated)")});
        int index = 0;
        for (int i = 1; i < int(items.size()); ++i)
            if (items[size_t(i)].first == p.overrides.gpu) index = i;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##perf.gpu", items[size_t(index)].second.c_str())) {
            for (int i = 0; i < int(items.size()); ++i)
                if (ImGui::Selectable(items[size_t(i)].second.c_str(), i == index) && i != index) {
                    ProfileOverrides o = p.overrides;
                    o.gpu = items[size_t(i)].first;
                    saveProfileOverrides(o);
                    status_ = "Graphics card: " + items[size_t(i)].second + ". Applies when SculptForge starts again";
                }
            ImGui::EndCombo();
        }
        dimWrapped("Drawing now on " + current + (hw.gpus.size() >= 2 ? ". A change applies after a restart." : ""));
    } else {
#if defined(__APPLE__)
        dimWrapped("Graphics card: " + current + " (macOS picks the graphics card)");
#else
        dimWrapped("Graphics card: " + current);
#endif
    }

    // Compute: Auto / CPU / GPU, and the path each task takes.
    ImGui::Spacing();
    ImGui::TextUnformatted(tr("Compute"));
    ui::HelpText("Compute", nullptr,
                 "Where the heavy work runs: on the processor (CPU) or the graphics card (GPU). Auto takes the GPU "
                 "only where it measured faster on this computer, and never on the Essential tier. If a GPU path "
                 "fails, that task switches to the CPU and says so once. Tasks without a GPU path yet stay on the "
                 "CPU.",
                 kPath);
    {
        ComputeChoice saved = ComputeChoice::Auto;
        computeChoiceFromName(p.overrides.compute, saved);
        int index = int(saved);
        const char* names[] = {tr("Auto"), tr("CPU"), tr("GPU")};
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##perf.compute", &index, names, 3) && index != int(saved)) {
            ProfileOverrides o = p.overrides;
            o.compute = index == 0 ? std::string() : std::string(computeChoiceName(ComputeChoice(index)));
            saveProfileOverrides(o);
        }
        if (tc.compute != saved) dimWrapped(std::string("SF_COMPUTE=") + computeChoiceName(tc.compute) + " (set for this run)");
        for (int i = 0; i < kComputeTaskCount; ++i) {
            const ComputeTaskInfo& info = computeTaskInfo(ComputeTask(i));
            const ComputePath path = resolveCompute(info.task, tc.compute, tc.hwTier, 0.0, 0.0);
            const std::string line = std::string(info.name) + ": " + (path == ComputePath::Gpu ? "GPU" : "CPU") +
                                     (!info.gpuPath ? "  (CPU only for now)" : gpuComputeFailed(info.task) ? "  (GPU failed: CPU)" : "");
            if (!info.gpuPath) ImGui::BeginDisabled();
            bullet(line);
            if (!info.gpuPath) ImGui::EndDisabled();
            ImGui::LogText("%s", line.c_str());
        }
    }
    ImGui::Separator();
    ImGui::Spacing();
}

void App::drawTierCard() {
    HwProfileUi& hw = *hwProfile_;
    const HardwareProfile& p = hw.profile;
    const float s = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Bottom right, over the canvas; it takes no focus and nothing waits for it.
    const float w = 400.0f * s;
    // (Inside the 3D view when there is one, so it never covers the trays and shelves.)
    const CanvasRect& cv = shell_.canvas;
    const ImVec2 corner = cv.w > 0.0f && cv.h > 0.0f ? ImVec2(cv.x + cv.w - 16.0f * s, cv.y + cv.h - 16.0f * s)
                                                     : ImVec2(vp->WorkPos.x + vp->WorkSize.x - 24.0f * s, vp->WorkPos.y + vp->WorkSize.y - 48.0f * s);
    ImGui::SetNextWindowPos(corner, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * s, 16.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::accent);
    const bool open = ImGui::Begin("##tiercard", nullptr,
                                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (open) {
        // The gauge, ours: a half dial and its needle in the accent.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = 11.0f * s;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 c(at.x + r, at.y + r * 1.25f);
        dl->PathArcTo(c, r, 3.14159f, 6.28318f, 20);
        dl->PathStroke(theme::u32(theme::accent), 0, 2.0f * s);
        const float angle = 3.14159f * (1.0f + (float(int(hw.tier.hwTier)) + 1.0f) / float(kHwTierCount + 1));
        dl->AddLine(c, ImVec2(c.x + std::cos(angle) * r * 0.8f, c.y + std::sin(angle) * r * 0.8f), theme::u32(theme::textBright), 2.0f * s);
        dl->AddCircleFilled(c, 2.0f * s, theme::u32(theme::textBright));
        ImGui::Dummy(ImVec2(2.0f * r + 8.0f * s, 2.0f * r));
        ImGui::SameLine();
        if (fontBold_) ImGui::PushFont(fontBold_);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Tuned for your machine"));
        if (fontBold_) ImGui::PopFont();
        // The close, top right: puts it away like Got it.
        const float cb = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - cb);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        const bool closed = ui::GlyphButton("##tiercard.close", ICON_LC_X, ImVec2(cb, cb));
        ImGui::PopStyleColor();
        hw.tierCardClose = {ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y};
        ui::HelpText("Close", "Esc", "Put this card away. It doesn't come back on this computer; the tier is in Preferences > Performance.");
        ImGui::TextWrapped("%s", ("Your machine: " + p.detected.machine + ".").c_str());
        if (fontBold_) ImGui::PushFont(fontBold_);
        ImGui::TextColored(theme::accent, "%s", (std::string("Tier: ") + hwTierDisplayName(hw.tier.hwTier)).c_str());
        if (fontBold_) ImGui::PopFont();
        ImGui::LogText("Your machine: %s. Tier: %s", p.detected.machine.c_str(), hwTierDisplayName(hw.tier.hwTier));
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("Turned on:"));
        for (const std::string& line : tierTurnsOn(hw.tier.hwTier, hw.tier.budgets)) bullet(line);
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("Change it any time in"));
        ImGui::SameLine(0.0f, 4.0f * s);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::accent);
        if (ImGui::Selectable(tr("Preferences > Performance"), false, 0, ImGui::CalcTextSize(tr("Preferences > Performance"))))
            openPerformancePrefs();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        const float bw = ImGui::CalcTextSize(tr("Got it")).x + 28.0f * s;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw);
        ImGui::PushStyleColor(ImGuiCol_Button, theme::accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::accentHover);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::textOnAccent);
        const bool gotIt = ImGui::Button(tr("Got it"), ImVec2(bw, 0.0f));
        ImGui::PopStyleColor(3);
        hw.tierCardGotIt = {ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y};
        if (gotIt || closed) {
            ImGui::End();
            dismissTierCard();
            return;
        }
    }
    ImGui::End();
}

void App::dismissTierCard() {
    if (!hwProfile_ || !hwProfile_->tierCard) return;
    HwProfileUi& hw = *hwProfile_;
    hw.tierCard = false;
    hw.tierCardPressed = 0;
    hw.tierCardGotIt = hw.tierCardClose = {};
    ProfileOverrides o = hw.profile.overrides;
    o.tierCardFor = rememberTierCard(o.tierCardFor, hw.profile.hardware); // not again on this machine
    saveProfileOverrides(o);
}

void App::tierCardFromDisk() {
    if (!hwProfile_ || hwProfile_->calibrator) return;
    HardwareProfile saved;
    hwProfile_->tierCard = !loadHardwareProfile(hardwareProfilePath(), saved) ||
                           !tierCardSeen(saved.overrides.tierCardFor, hwProfile_->profile.hardware);
}

// The card's buttons work on the first press, whatever ImGui's hover says: a pointer that jumps onto Got it and
// presses in the same frame (a pen landing, a trackpad tap, a press in the same batch of events as the move, the
// first frame the card is up) found ImGui's hover a frame behind, and the press went to the canvas instead (#161,
// on Linux). The press here is ImGui's only (no stroke under the card); the release on the same button puts the
// card away. Esc and Enter do too, while no text field or pop-up has the keys.
bool App::tierCardEvent(const SDL_Event& e) {
    if (!hwProfile_ || !hwProfile_->tierCard || hwProfile_->calibrator) return false;
    if (options_.benchmark || !options_.benchmarkScript.empty() || !options_.demo.empty()) return false; // (not shown)
    HwProfileUi& hw = *hwProfile_;
    const ImGuiIO& io = ImGui::GetIO();
    auto onCard = [](float x, float y) {
        const char* top = ui::FloatingWindowAt(ImVec2(x, y));
        return top && std::string(top).rfind("##tiercard", 0) == 0;
    };
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (!onCard(e.button.x, e.button.y)) return false;
        hw.tierCardPressed = e.button.button != SDL_BUTTON_LEFT       ? 0
                             : hw.tierCardGotIt.contains(e.button.x, e.button.y) ? 1
                             : hw.tierCardClose.contains(e.button.x, e.button.y) ? 2
                                                                                 : 0;
        SDL_Event m = e;
        ImGui_ImplSDL3_ProcessEvent(&m); // the card's own items (Preferences > Performance); nothing underneath
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const int pressed = hw.tierCardPressed;
        hw.tierCardPressed = 0;
        if (e.button.button != SDL_BUTTON_LEFT || !pressed) return false;
        const HwProfileUi::Box& b = pressed == 1 ? hw.tierCardGotIt : hw.tierCardClose;
        if (!b.contains(e.button.x, e.button.y)) return false;
        SDL_Event m = e;
        ImGui_ImplSDL3_ProcessEvent(&m); // (ImGui lets go of its button too)
        dismissTierCard();
        return true;
    }
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode k = e.key.key;
        if (e.key.repeat || (k != SDLK_ESCAPE && k != SDLK_RETURN && k != SDLK_KP_ENTER)) return false;
        if (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT | SDL_KMOD_GUI)) return false;
        if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return false;
        if (drag_ != Drag::None) return false; // (Esc cancels a stroke or a drag first)
        dismissTierCard();
        return true;
    }
    default:
        return false;
    }
}

} // namespace sf
