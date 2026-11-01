// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preferences > Performance (ZBrush: Preferences > Performance and Mem): how the 3D view draws (Antialiasing,
// Fast Navigation), Multithreading, what the last stroke cost per dab, and the memory the scene holds. The
// numbers come from the same instruments as the benchmark (core/Benchmark.h, docs/performance.md).
#include "app/App.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include "core/Benchmark.h"
#include "core/HardwareProfile.h"
#include "core/ThreadPool.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace sf {

namespace {

constexpr const char* kPath = "Preferences > Performance";
// Scenes this big draw without antialiasing while the view moves (with Fast Navigation on): the hardware profile's
// motion proxy threshold, which the frame governor adjusts (core/HardwareProfile.h), else 10 million. UI tests
// set it with the environment variable SF_FAST_NAVIGATION_TRIANGLES, so they needn't build a huge mesh.
size_t fastNavigationTriangles(bool governed) {
    const char* v = std::getenv("SF_FAST_NAVIGATION_TRIANGLES");
    if (v && *v) return size_t(std::strtoull(v, nullptr, 10));
    return governed ? size_t(motionProxyThreshold()) : size_t(10'000'000);
}

std::string megabytes(size_t bytes) {
    char buf[32];
    if (bytes >= size_t(1) << 30) std::snprintf(buf, sizeof buf, "%.2f GB", double(bytes) / double(size_t(1) << 30));
    else std::snprintf(buf, sizeof buf, "%.0f MB", double(bytes) / double(size_t(1) << 20));
    return buf;
}

void dimText(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

} // namespace

void App::registerPerformanceUiActions() {
    auto add = [this](std::string id, std::string label, std::function<void()> run, std::function<bool()> checked,
                      std::string description) {
        Action a;
        a.id = std::move(id);
        a.label = std::move(label);
        a.palette = kPath;
        a.run = std::move(run);
        a.checked = std::move(checked);
        a.description = std::move(description);
        actions_.add(std::move(a));
    };
    for (int samples : {1, 2, 4, 8}) {
        const std::string name = samples == 1 ? "Off" : std::to_string(samples) + "x";
        add("perf.aa_" + std::to_string(samples), "AA " + name,
            [this, samples, name] {
                uiPrefs_.antialiasing = samples;
                status_ = "Antialiasing " + name + (samples > 1 && uiPrefs_.fastNavigation ? " (off while big scenes move)" : "");
            },
            [this, samples] { return uiPrefs_.antialiasing == samples; },
            samples == 1 ? "Draw the 3D view without antialiasing: fastest, with jagged edges."
                         : "Smooth the 3D view's edges with " + std::to_string(samples) +
                               " samples per pixel (multisampling). More samples look smoother and cost more on huge "
                               "meshes. If the GPU can't make room for them, the view uses fewer.");
    }
    add("perf.fast_navigation", "Fast Navigation",
        [this] {
            uiPrefs_.fastNavigation = !uiPrefs_.fastNavigation;
            status_ = uiPrefs_.fastNavigation ? "Fast Navigation on" : "Fast Navigation off";
        },
        [this] { return uiPrefs_.fastNavigation; },
        "While you rotate, pan or zoom a big scene (over the hardware profile's motion proxy threshold, which the "
        "frame governor adjusts; 10 million triangles without a profile), draw a lighter version without "
        "antialiasing, so the view keeps up; it is full detail again as soon as the view stops.");
    add("perf.multithreading", "Multithreading",
        [this] {
            uiPrefs_.multithreading = ThreadPool::shared().serial();
            applyPerformancePrefs();
            status_ = uiPrefs_.multithreading ? "Multithreading on (" + std::to_string(ThreadPool::shared().threads()) + " threads)"
                                              : "Multithreading off (one thread)";
        },
        [] { return !ThreadPool::shared().serial(); },
        "Run brushes, normal and BVH updates and project saving on every core. Turn it off only to compare speeds "
        "or rule it out when troubleshooting: the results are the same either way.");
    add("perf.predictive_strokes", "Predictive Strokes",
        [this] {
            uiPrefs_.predictiveStrokes = !uiPrefs_.predictiveStrokes;
            status_ = uiPrefs_.predictiveStrokes ? "Predictive strokes on" : "Predictive strokes off";
        },
        [this] { return uiPrefs_.predictiveStrokes; },
        "Sculpt strokes work out the brush on their own thread while the view keeps drawing at full speed, and show "
        "the dabs still being worked out as a preview on the surface, with the cursor and the stroke's head drawn "
        "where the pen is about to be. On a heavy mesh the surface moves under the pen at once and lifting the pen "
        "is instant. The finished sculpt is exactly the same either way.");
    add("perf.draft_strokes", "Draft Strokes",
        [this] {
            uiPrefs_.draftStrokes = !uiPrefs_.draftStrokes;
            status_ = uiPrefs_.draftStrokes ? "Draft strokes on" : "Draft strokes off";
        },
        [this] { return uiPrefs_.draftStrokes; },
        "With Predictive Strokes: while you sculpt, the brush only moves the surface and shades the moved part from "
        "its facets; the smooth shading, the cavities and the rest are worked out once when you lift the pen. "
        "Lighter on laptops and on battery: the hardware profile turns it on there by itself (its Draft brushes "
        "setting); this turns it on everywhere. Brushes that follow the surface's direction (Inflate, the plane "
        "brushes) use its direction from the stroke's start, so the result can differ slightly from a full stroke.");
    registerPerformanceProfileUiActions(); // Re-calibrate, the frame governor (PanelPerformanceProfile.cpp)
}

void App::applyPerformancePrefs() { ThreadPool::shared().setSerial(!uiPrefs_.multithreading); }

int App::viewSamples() {
    const int aa = uiPrefs_.antialiasing;
    // The view moved in the last few frames (orbit, pan, zoom, snapping, framing), or is being dragged.
    const glm::mat4 view = camera_.view();
    if (view != perf_.lastView) {
        perf_.lastView = view;
        perf_.framesStill = 0;
    } else if (perf_.framesStill < 1000) {
        ++perf_.framesStill;
    }
    const bool moving = drag_ == Drag::Orbit || drag_ == Drag::Pan || drag_ == Drag::Zoom || perf_.framesStill < 8;
    size_t triangles = 0;
    for (int i = 0; i < doc_.subtoolCount(); ++i)
        if (doc_.subtools[size_t(i)].visible) triangles += doc_.subtoolMesh(i).triangleCount();
    perf_.movingNow = moving;
    perf_.sceneTriangles = triangles;
    perf_.proxyNow = uiPrefs_.fastNavigation && moving && triangles >= fastNavigationTriangles(hardwareProfileGoverns());
    perf_.fastNow = aa > 1 && perf_.proxyNow;
    perf_.samplesWanted = perf_.fastNow ? 1 : aa;
    return perf_.samplesWanted;
}

void App::notePerformanceFrame(double syncMs) {
    if (doc_.sculptor.strokeActive()) {
        perf_.strokeSyncMs += syncMs;
        ++perf_.strokeFrames;
    }
    // A SubTool too big for the GPU's memory isn't drawn: say so once.
    bool oom = false;
    for (const SubTool& s : doc_.subtools) oom = oom || (s.visible && renderer_.outOfMemory(s.id));
    if (oom && !perf_.gpuOutOfMemory)
        status_ = "Not enough GPU memory to show every Part: hide some, or step down a subdivision level "
                  "(Preferences > Performance)";
    perf_.gpuOutOfMemory = oom;
}

void App::drawPerformanceSection() {
    drawPerformanceProfileSection(); // what was detected and measured, the profile, overrides (PanelPerformanceProfile.cpp)
    // --- The 3D view ---
    ImGui::TextUnformatted(tr("Antialiasing"));
    ui::HelpText("Antialiasing", nullptr,
                 "Samples per pixel in the 3D view: more smooths edges, fewer is faster on huge meshes.", kPath);
    ui::Segmented(*this, {"perf.aa_1", "perf.aa_2", "perf.aa_4", "perf.aa_8"});
    bool fast = uiPrefs_.fastNavigation;
    if (ImGui::Checkbox(tr("Fast Navigation"), &fast)) actions_.find("perf.fast_navigation")->run();
    ui::HelpAction(*this, "perf.fast_navigation");
    if (perf_.samplesUsed > 0 && perf_.samplesUsed < uiPrefs_.antialiasing && !perf_.fastNow)
        dimText("The GPU had no room for " + std::to_string(uiPrefs_.antialiasing) + "x: using " +
                (perf_.samplesUsed == 1 ? std::string("none") : std::to_string(perf_.samplesUsed) + "x") + ".");

    bool predictive = uiPrefs_.predictiveStrokes;
    if (ImGui::Checkbox(tr("Predictive Strokes"), &predictive)) actions_.find("perf.predictive_strokes")->run();
    ui::HelpAction(*this, "perf.predictive_strokes");
    bool draft = uiPrefs_.draftStrokes;
    if (ImGui::Checkbox(tr("Draft Strokes"), &draft)) actions_.find("perf.draft_strokes")->run();
    ui::HelpAction(*this, "perf.draft_strokes");

    // --- Threads ---
    bool threads = !ThreadPool::shared().serial();
    if (ImGui::Checkbox(tr("Multithreading##perf.mt"), &threads)) actions_.find("perf.multithreading")->run();
    ui::HelpAction(*this, "perf.multithreading");
    ImGui::SameLine();
    ImGui::TextDisabled(tr("%d threads"), threads ? ThreadPool::shared().threads() : 1);

    // --- The last stroke ---
    ImGui::Spacing();
    ImGui::TextUnformatted(tr("Last stroke"));
    ui::HelpText("Last stroke", nullptr,
                 "What the last brush stroke cost, per dab (one step of the brush, all symmetry sides): finding and "
                 "moving the vertices, updating their normals and the BVH (the picking structure), and sending the "
                 "change to the GPU (per frame). Under 16 ms keeps up with a 60 Hz display.",
                 kPath);
    const SculptTimings& t = perf_.lastStroke;
    if (t.dabs == 0) {
        dimText("Sculpt a stroke to see what it costs.");
    } else {
        const double d = double(t.dabs);
        const double upload = perf_.lastStrokeFrames ? perf_.lastStrokeSyncMs / perf_.lastStrokeFrames : 0.0;
        char buf[256];
        std::snprintf(buf, sizeof buf, "%llu dabs, %.0fk vertices each\nBrush %.2f ms  Normals %.2f ms  BVH %.2f ms\nGPU upload %.2f ms per frame",
                      static_cast<unsigned long long>(t.dabs), double(t.vertices) / d / 1000.0, t.kernelMs / d,
                      t.normalsMs / d, t.bvhMs / d, upload);
        ImGui::TextUnformatted(buf);
        ImGui::LogText("%s", buf);
    }

    // --- Memory ---
    ImGui::Spacing();
    ImGui::TextUnformatted(tr("Memory"));
    ui::HelpText("Memory", nullptr,
                 "What the scene holds: the meshes with their topology, the BVHs, the subdivision levels kept for "
                 "stepping between them, the undo history (Edit keeps up to 1 GB, dropping the oldest steps), painted textures "
                 "with their layers and material channels (only painted 64x64 tiles of a channel use memory), and the "
                 "GPU buffers of the visible Parts.",
                 kPath);
    const MemoryUse m = documentMemory(doc_);
    const std::string mem = "Meshes " + megabytes(m.mesh) + "  BVH " + megabytes(m.bvh) + "\nSubdivision levels " +
                            megabytes(m.levels) + "\nUndo " + megabytes(m.undo) + " of " + megabytes(doc_.undo.byteBudget) +
                            "\nTextures " + megabytes(m.textures) + " (material channels " + megabytes(m.channels) + ")" +
                            (m.paintCaches ? "\nTexture painting caches " + megabytes(m.paintCaches) : std::string()) +
                            "\nGPU buffers about " + megabytes(m.gpu);
    ImGui::TextUnformatted(mem.c_str());
    ImGui::LogText("%s", mem.c_str());
    if (perf_.gpuOutOfMemory) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::color::danger));
        ImGui::TextWrapped("%s", tr("Not enough GPU memory to show every Part: hide some or step down a subdivision level."));
        ImGui::PopStyleColor();
    }
}

} // namespace sf
