// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hardware profile (core/HardwareProfile.h) and the adaptive frame governor (core/FrameGovernor.h): profile
// mapping from synthetic measurements, cache invalidation, the JSON schema, governor convergence and hysteresis
// on synthetic frame-time traces. Timing checks of the real CPU benchmarks run only with SF_BENCH_LIMITS=1.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/FrameGovernor.h"
#include "core/HardwareProfile.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace sf;

namespace {

HardwareInfo machine(const std::string& vendor, const std::string& renderer, uint64_t vramMB, uint64_t ramMB, int threads = 12) {
    HardwareInfo h;
    h.glVendor = vendor;
    h.glRenderer = renderer;
    h.glVersion = "4.6.0 test";
    h.gpuKind = classifyGpu(vendor, renderer);
    h.vramMB = vramMB;
    h.ramMB = ramMB;
    h.cpuThreads = threads;
    h.maxTextureSize = 16384;
    h.appVersion = "9.9.9";
    h.os = "TestOS";
    return h;
}

Calibration measured(double triM, double uploadGBps = 10.0, double dabMs = 0.5, double memGBps = 20.0) {
    Calibration c;
    c.trianglesPerSecondM = triM;
    c.uploadGBps = uploadGBps;
    c.dabMs = dabMs;
    c.dabVertices = 4000;
    c.memoryGBps = memGBps;
    c.runs = 5;
    c.durationMs = 2000;
    return c;
}

bool sameSettings(const PerformanceSettings& a, const PerformanceSettings& b) {
    return a.renderTriangleBudget == b.renderTriangleBudget && a.motionProxyThreshold == b.motionProxyThreshold &&
           a.vramBudgetMB == b.vramBudgetMB && a.uploadBudgetMBPerFrame == b.uploadBudgetMBPerFrame &&
           a.maxTextureSize == b.maxTextureSize && a.maxSubdivisionTriangles == b.maxSubdivisionTriangles &&
           a.microTriangleLimit == b.microTriangleLimit && a.threads == b.threads && a.targetFps == b.targetFps;
}

// Points the per-user data folder (appDataDir) at a fresh temporary one while a test runs.
struct IsolatedDataDir {
    std::string dir, oldXdg, oldHome, oldAppData;
    explicit IsolatedDataDir(const char* name) {
        dir = tempPath(name);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        auto get = [](const char* k) { const char* v = std::getenv(k); return v ? std::string(v) : std::string(); };
        oldXdg = get("XDG_DATA_HOME");
        oldHome = get("HOME");
        oldAppData = get("APPDATA");
#if defined(_WIN32)
        _putenv_s("APPDATA", dir.c_str());
#else
        setenv("XDG_DATA_HOME", dir.c_str(), 1);
#if defined(__APPLE__)
        setenv("HOME", dir.c_str(), 1);
#endif
#endif
    }
    ~IsolatedDataDir() {
#if defined(_WIN32)
        _putenv_s("APPDATA", oldAppData.c_str());
#else
        if (oldXdg.empty()) unsetenv("XDG_DATA_HOME");
        else setenv("XDG_DATA_HOME", oldXdg.c_str(), 1);
#if defined(__APPLE__)
        setenv("HOME", oldHome.c_str(), 1);
#endif
#endif
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

// A tiny deterministic noise source for frame-time traces.
struct Lcg {
    uint32_t s = 12345;
    double next() { // [-1, 1)
        s = s * 1664525u + 1013904223u;
        return double(s >> 8) / double(1u << 23) - 1.0;
    }
};

} // namespace

TEST(hw_classify_gpu) {
    CHECK(classifyGpu("Mesa", "llvmpipe (LLVM 19.1.0, 256 bits)") == GpuKind::Software);
    CHECK(classifyGpu("Microsoft Corporation", "GDI Generic") == GpuKind::Software);
    CHECK(classifyGpu("Google Inc.", "ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device))") == GpuKind::Software);
    CHECK(classifyGpu("Apple", "Apple M1 Pro") == GpuKind::Unified);
    CHECK(classifyGpu("Mesa", "Apple M1 Pro (G13S C0)") == GpuKind::Unified); // Asahi Linux
    CHECK(classifyGpu("NVIDIA Corporation", "NVIDIA GeForce RTX 4090/PCIe/SSE2") == GpuKind::Discrete);
    CHECK(classifyGpu("Intel", "Mesa Intel(R) UHD Graphics 620 (KBL GT2)") == GpuKind::Integrated);
    CHECK(classifyGpu("Intel", "Mesa Intel(R) Arc(tm) A770 Graphics (DG2)") == GpuKind::Discrete);
    CHECK(classifyGpu("AMD", "AMD Radeon RX 7900 XTX (radeonsi, navi31, LLVM 18.1.8, DRM 3.57)") == GpuKind::Discrete);
    CHECK(classifyGpu("AMD", "AMD Radeon 780M (radeonsi, gfx1103_r1, LLVM 18.1.8, DRM 3.57)") == GpuKind::Integrated);
    CHECK(classifyGpu("ATI Technologies Inc.", "AMD Radeon(TM) Graphics") == GpuKind::Integrated);
    CHECK(classifyGpu("", "") == GpuKind::Unknown);
}

TEST(hw_sysfs_vram) {
    const std::string root = tempPath("sf_hw_sysfs");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    auto card = [&](const char* name, const char* vendor, const char* vram) {
        const auto dev = std::filesystem::path(root) / name / "device";
        std::filesystem::create_directories(dev, ec);
        std::ofstream(dev / "vendor") << vendor << "\n";
        if (vram) std::ofstream(dev / "mem_info_vram_total") << vram << "\n";
    };
    card("card0", "0x8086", nullptr);                  // Intel iGPU: no VRAM file
    card("card1", "0x1002", "17163091968");            // amdgpu, 16 GiB
    card("card1-DP-1", "0x1002", "1");                 // a connector, not a card
    CHECK(sysfsVramMB("AMD", root) == 16368);
    CHECK(sysfsVramMB("Intel", root) == 0);
    CHECK(sysfsVramMB("", root) == 16368); // unknown vendor: any card that says
    CHECK(sysfsVramMB("AMD", root + "/missing") == 0);
    std::filesystem::remove_all(root, ec);
}

TEST(hw_profile_mapping) {
    // Software renderers are safe mode whatever they measure, with half the threads (llvmpipe draws on them too).
    {
        const HardwareProfile p = mapProfile(machine("Mesa", "llvmpipe (LLVM 19.1.0, 256 bits)", 0, 16384, 8), measured(40.0, 2.0));
        CHECK(p.tier == ProfileTier::Software);
        CHECK(p.settings.threads == 4);
        CHECK(p.settings.renderTriangleBudget <= 4'000'000);
        CHECK(p.settings.maxTextureSize <= 4096);
    }
    // A big discrete card on a big machine.
    {
        const HardwareProfile p = mapProfile(machine("NVIDIA Corporation", "NVIDIA GeForce RTX 4090/PCIe/SSE2", 24564, 65536, 32),
                                             measured(15000.0, 25.0, 0.2, 40.0));
        CHECK(p.tier == ProfileTier::Monster);
        CHECK(p.settings.renderTriangleBudget >= 100'000'000);
        CHECK(p.settings.vramBudgetMB == 24564 * 4 / 5);
        CHECK(p.settings.threads == 16); // the thread pool's limit
        CHECK(p.reason.find("Monster") != std::string::npos);
        CHECK(p.reason.find("RTX 4090") != std::string::npos);
    }
    // A mid card: measured as a workstation, its 8 GB keep it there.
    {
        const HardwareProfile p = mapProfile(machine("NVIDIA Corporation", "NVIDIA GeForce RTX 3070/PCIe/SSE2", 8192, 32768),
                                             measured(8000.0));
        CHECK(p.tier == ProfileTier::Workstation);
        CHECK(p.reason.find("capped by GPU memory") != std::string::npos);
    }
    // An integrated GPU on a 16 GB laptop.
    {
        const HardwareProfile p = mapProfile(machine("Intel", "Mesa Intel(R) Xe Graphics (TGL GT2)", 0, 16384, 8), measured(400.0, 4.0));
        CHECK(p.tier == ProfileTier::Laptop);
        CHECK(p.settings.vramBudgetMB == 8192); // shares half the RAM
        CHECK(p.settings.renderTriangleBudget >= 2'000'000 && p.settings.renderTriangleBudget <= 40'000'000);
    }
    // A fast GPU with little memory is capped.
    {
        const HardwareProfile p = mapProfile(machine("AMD", "AMD Radeon RX 6500 XT", 2048, 32768), measured(3000.0));
        CHECK(p.tier == ProfileTier::Laptop);
        CHECK(p.settings.maxTextureSize <= 8192);
    }
    // Unified memory: two thirds of the RAM for the GPU.
    {
        const HardwareProfile p = mapProfile(machine("Mesa", "Apple M1 Pro (G13S C0)", 0, 32768, 10), measured(2000.0));
        CHECK(p.hardware.gpuKind == GpuKind::Unified);
        CHECK(p.settings.vramBudgetMB == 32768 * 2 / 3);
        CHECK(p.tier == ProfileTier::Workstation);
    }
    // Measurements decide, not names: an unknown GPU that measures like software is safe mode.
    CHECK(mapProfile(machine("Weird", "Mystery GPU", 4096, 16384), measured(20.0)).tier == ProfileTier::Software);
    // Same measurements, a different (same-kind) name: the same profile.
    {
        const HardwareProfile a = mapProfile(machine("NVIDIA Corporation", "NVIDIA RTX A4000", 16384, 65536), measured(4000.0));
        const HardwareProfile b = mapProfile(machine("NVIDIA Corporation", "NVIDIA GeForce RTX 4070 Ti SUPER", 16384, 65536), measured(4000.0));
        CHECK(a.tier == b.tier);
        CHECK(sameSettings(a.settings, b.settings));
    }
    // Nothing measured (skipped): a guess from memory sizes, never above Workstation.
    {
        Calibration skipped;
        skipped.skipped = true;
        CHECK(mapProfile(machine("NVIDIA Corporation", "NVIDIA GeForce RTX 4090", 24564, 65536), skipped).tier == ProfileTier::Workstation);
        CHECK(mapProfile(machine("Intel", "Intel(R) UHD Graphics", 0, 8192), skipped).tier == ProfileTier::Laptop);
    }
    // Slow brushes or memory cap the tier.
    CHECK(mapProfile(machine("NVIDIA Corporation", "NVIDIA GeForce RTX 4090", 24564, 65536), measured(15000.0, 25.0, 5.0)).tier ==
          ProfileTier::Laptop);
    // Deterministic, and sane everywhere.
    for (double tri : {10.0, 100.0, 900.0, 1500.0, 4000.0, 6000.0, 30000.0}) {
        const HardwareInfo h = machine("NVIDIA Corporation", "NVIDIA GeForce RTX 4080", 16384, 65536);
        const HardwareProfile a = mapProfile(h, measured(tri)), b = mapProfile(h, measured(tri));
        CHECK(a.tier == b.tier && sameSettings(a.settings, b.settings));
        CHECK(a.settings.motionProxyThreshold >= 262'144);
        CHECK(a.settings.motionProxyThreshold >= a.settings.renderTriangleBudget);
        CHECK(a.settings.maxTextureSize <= h.maxTextureSize);
        CHECK(a.settings.maxSubdivisionTriangles >= 2'000'000);
        CHECK(a.settings.threads >= 1);
        CHECK(a.settings.targetFps == 60);
    }
    // Faster measurements never give a lower tier or budget.
    {
        const HardwareInfo h = machine("NVIDIA Corporation", "NVIDIA GeForce RTX 4090", 24564, 131072, 32);
        HardwareProfile prev = mapProfile(h, measured(30.0));
        for (double tri = 60.0; tri < 40000.0; tri *= 1.5) {
            const HardwareProfile p = mapProfile(h, measured(tri));
            CHECK(int(p.tier) >= int(prev.tier));
            CHECK(p.tier != prev.tier || p.settings.renderTriangleBudget >= prev.settings.renderTriangleBudget);
            prev = p;
        }
    }
}

TEST(hw_profile_overrides) {
    const HardwareInfo h = machine("NVIDIA Corporation", "NVIDIA GeForce RTX 3060", 12288, 32768);
    ProfileOverrides o;
    o.tier = "Laptop";
    o.renderTriangleBudget = 5'000'000;
    o.threads = 3;
    const HardwareProfile p = mapProfile(h, measured(3000.0), o);
    CHECK(p.tier == ProfileTier::Laptop);
    CHECK(p.reason.find("by hand") != std::string::npos);
    CHECK(p.settings.renderTriangleBudget == 5'000'000);
    CHECK(p.settings.motionProxyThreshold == 5'000'000); // follows the budget
    CHECK(p.settings.threads == 3);
    CHECK(p.automatic.renderTriangleBudget != 5'000'000 || p.automatic.threads != 3);
    CHECK(o.any());
    CHECK(!ProfileOverrides{}.any());
    // A tier name in any case, and the safe-mode alias.
    ProfileTier t;
    CHECK(tierFromName("monster", t) && t == ProfileTier::Monster);
    CHECK(tierFromName("llvmpipe", t) && t == ProfileTier::Software);
    CHECK(!tierFromName("fast", t));
}

TEST(hw_profile_cache_invalidation) {
    const std::string path = tempPath("sf_hw_profile_cache.json");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    HardwareProfile missing;
    CHECK(!loadHardwareProfile(path, missing));

    const HardwareInfo h = machine("AMD", "AMD Radeon RX 7800 XT (radeonsi, navi32)", 16384, 32768);
    ProfileOverrides o;
    o.maxTextureSize = 4096;
    o.governor = false;
    const HardwareProfile p = mapProfile(h, measured(5000.0), o);
    std::string error;
    CHECK(saveHardwareProfile(path, p, error));
    HardwareProfile back;
    CHECK(loadHardwareProfile(path, back));
    CHECK(back.hardware.glRenderer == h.glRenderer);
    CHECK(back.tier == p.tier);
    CHECK(sameSettings(back.settings, p.settings));
    CHECK(back.overrides.maxTextureSize == 4096 && !back.overrides.governor); // overrides survive
    CHECK(back.calibration.trianglesPerSecondM == 5000.0);

    // Same GPU, driver and version: the cache holds.
    HardwareInfo now = h;
    CHECK(!needsRecalibration(&back, now));
    CHECK(recalibrationReason(&back, now).empty());
    // A different renderer string (a new GPU, or the same one on another driver stack) recalibrates.
    now.glRenderer = "AMD Radeon RX 7900 XTX (radeonsi, navi31)";
    CHECK(needsRecalibration(&back, now));
    CHECK(recalibrationReason(&back, now) == "GPU changed");
    now = h;
    now.glVendor = "NVIDIA Corporation";
    CHECK(recalibrationReason(&back, now) == "GPU changed");
    // A driver update changes GL_VERSION.
    now = h;
    now.glVersion = "4.6 (Core Profile) Mesa 26.0.1";
    CHECK(recalibrationReason(&back, now) == "driver changed");
    // A new app version.
    now = h;
    now.appVersion = "10.0.0";
    CHECK(recalibrationReason(&back, now) == "new version");
    // Nothing cached.
    CHECK(needsRecalibration(nullptr, h));
    CHECK(recalibrationReason(nullptr, h) == "first launch");
    // A CPU-only calibration (sculptforge-cli) is measured again once the app has a GPU.
    HardwareProfile cpuOnly = back;
    cpuOnly.calibration.cpuOnly = true;
    CHECK(recalibrationReason(&cpuOnly, h) == "GPU not measured yet");
    HardwareInfo headless = h;
    headless.glRenderer.clear();
    headless.glVendor.clear();
    headless.glVersion.clear();
    cpuOnly.hardware.glRenderer.clear();
    cpuOnly.hardware.glVendor.clear();
    cpuOnly.hardware.glVersion.clear();
    CHECK(!needsRecalibration(&cpuOnly, headless));

    // Another cache version, or a broken file, is not used.
    {
        Json j = hardwareProfileToJson(p);
        j.set("cache_version", kHardwareProfileCacheVersion + 1);
        std::ofstream(path, std::ios::trunc) << j.dump();
        CHECK(!loadHardwareProfile(path, back));
        std::ofstream(path, std::ios::trunc) << "{ not json";
        CHECK(!loadHardwareProfile(path, back));
    }
    std::filesystem::remove(path, ec);
}

TEST(hw_calibration_json_schema) {
    HardwareInfo h = machine("Mesa", "Apple M1 Pro (G13S C0)", 0, 32768, 10);
    h.simd = {"neon"};
    h.simdBuild = "neon";
    h.vramSource = "unified (system RAM)";
    const HardwareProfile p = mapProfile(h, measured(2500.0));
    const Json j = calibrationJson(p);
    std::string error;
    CHECK(validateCalibrationJson(j, error));
    CHECK(error.empty());
    // Round trip through text, as `sculptforge --calibrate` prints it.
    Json parsed;
    CHECK(Json::parse(j.dump(true), parsed, error));
    CHECK(validateCalibrationJson(parsed, error));
    CHECK(parsed["schema"].asString() == kCalibrationSchema);
    CHECK(parsed["profile"].asString() == "Workstation");
    CHECK(parsed["hardware"]["gpu_kind"].asString() == "unified");
    CHECK(parsed["settings"]["render_triangle_budget"].asNumber() > 0);
    CHECK(!parsed["network"].asBool());
    // Broken documents are caught, naming the field.
    {
        Json bad = Json::object();
        for (const auto& [k, v] : parsed.members())
            if (k != "settings") bad.set(k, v);
        CHECK(!validateCalibrationJson(bad, error));
        CHECK(error.find("settings") != std::string::npos);
    }
    {
        Json bad = parsed;
        bad.set("network", true);
        CHECK(!validateCalibrationJson(bad, error));
    }
    {
        Json bad = parsed;
        bad.set("profile", "Turbo");
        CHECK(!validateCalibrationJson(bad, error));
    }
    {
        Json bad = parsed;
        Json m = parsed["measurements"];
        m.set("dab_ms", "fast");
        bad.set("measurements", m);
        CHECK(!validateCalibrationJson(bad, error));
        CHECK(error.find("dab_ms") != std::string::npos);
    }
    {
        Json bad = parsed;
        bad.set("schema", "sculptforge.calibration/0");
        CHECK(!validateCalibrationJson(bad, error));
    }
    CHECK(!validateCalibrationJson(Json(1.0), error));
}

TEST(hw_profile_commands) {
    IsolatedDataDir data("sf_hw_profile_commands");
    CommandRegistry registry;
    Document doc;
    // Before any calibration: the probe alone, as JSON.
    CommandResult r = registry.execute(doc, "performance_profile");
    CHECK(r.ok);
    Json j;
    std::string error;
    CHECK(Json::parse(r.message, j, error));
    CHECK(validateCalibrationJson(j, error));
    CHECK(j["hardware"]["cpu_threads"].asNumber() >= 1);
    CHECK(j["hardware"]["ram_mb"].asNumber() > 0);
    // Overrides are saved and come back.
    r = registry.execute(doc, "set_performance_override", {{"name", std::string("render_triangle_budget")}, {"value", std::string("12000000")}});
    CHECK(r.ok);
    CHECK(currentPerformanceSettings().renderTriangleBudget == 12'000'000);
    CHECK(renderTriangleBudget() == 12'000'000);
    r = registry.execute(doc, "set_performance_override", {{"name", std::string("tier")}, {"value", std::string("Monster")}});
    CHECK(r.ok);
    r = registry.execute(doc, "performance_profile");
    CHECK(Json::parse(r.message, j, error));
    CHECK(j["profile"].asString() == "Monster");
    CHECK(j["settings"]["render_triangle_budget"].asNumber() == 12'000'000);
    CHECK(j["overrides"]["tier"].asString() == "Monster");
    r = registry.execute(doc, "set_performance_override", {{"name", std::string("tier")}, {"value", std::string("auto")}});
    CHECK(r.ok);
    r = registry.execute(doc, "set_performance_override", {{"name", std::string("warp_drive")}, {"value", std::string("1")}});
    CHECK(!r.ok);
    r = registry.execute(doc, "set_performance_override", {{"name", std::string("tier")}, {"value", std::string("Turbo")}});
    CHECK(!r.ok);
    // The file lives in the (isolated) settings folder.
    CHECK(std::filesystem::exists(hardwareProfilePath()));
    CHECK(hardwareProfilePath().find(data.dir) == 0);
    // Put the process-wide settings back for the other tests.
    setCurrentPerformanceSettings(settingsForTier(ProfileTier::Laptop, HardwareInfo{}, Calibration{}));
}

TEST(hw_cpu_benchmarks) {
    // The CPU half of calibration runs and reports something plausible (quick: about a second).
    Calibration c;
    calibrateCpu(c, true);
    CHECK(c.memoryGBps > 0.0);
    CHECK(c.dabMs > 0.0);
    CHECK(c.dabVertices > 100.0);
    HardwareInfo h;
    probeSystem(h);
    CHECK(h.cpuThreads >= 1);
    CHECK(h.ramMB > 0);
    CHECK(!h.os.empty());
    CHECK(!h.simdBuild.empty());
    CHECK(!h.appVersion.empty());
    // Timing limits only on request (machines and CI runners differ): SF_BENCH_LIMITS=1.
    if (const char* v = std::getenv("SF_BENCH_LIMITS"); v && std::string(v) == "1") {
        std::printf("  memory %.1f GB/s, dab %.3f ms (%.0f vertices)\n", c.memoryGBps, c.dabMs, c.dabVertices);
        CHECK(c.memoryGBps > 2.0);
        CHECK(c.dabMs < 5.0);
    }
}

// --- Frame governor ---

namespace {

// A renderer whose frame cost grows with what it draws: overhead + drawn / rate (+ noise).
struct SyntheticView {
    uint64_t scene = 0;
    double overheadMs = 2.0, trianglesPerMs = 2'000'000.0;
    bool proxyStep = false; // today's renderer: over the threshold it draws the proxy (a quarter); else continuous LOD
    double noise = 0.0;     // relative, uniform
    Lcg rng;
    FrameSample frame(const FrameGovernor& g) {
        FrameSample f;
        f.sceneTriangles = scene;
        if (proxyStep) {
            f.limited = scene >= g.proxyThreshold();
            f.drawnTriangles = f.limited ? scene / 4 : scene;
        } else {
            f.drawnTriangles = std::min(scene, g.budget());
            f.limited = f.drawnTriangles < scene;
        }
        const double cost = (overheadMs + double(f.drawnTriangles) / trianglesPerMs) * (1.0 + noise * rng.next());
        f.cpuMs = cost * 0.4;
        f.gpuMs = cost;
        f.moving = true;
        return f;
    }
    double cost(const FrameGovernor& g) {
        const double saved = noise;
        noise = 0.0;
        const FrameSample f = frame(g);
        noise = saved;
        return std::max(f.cpuMs, f.gpuMs);
    }
};

} // namespace

TEST(governor_converges_from_above) {
    GovernorConfig cfg; // 60 fps
    FrameGovernor g(cfg, 400'000'000);
    SyntheticView v;
    v.scene = 2'000'000'000; // more than any budget: continuous level of detail
    for (int i = 0; i < 2000; ++i) g.addFrame(v.frame(g));
    const double T = g.targetMs();
    CHECK(v.cost(g) <= T * cfg.highFraction);
    CHECK(v.cost(g) >= T * cfg.lowFraction * 0.8);
    // Settled: no change over another long run.
    const int before = g.adjustments();
    for (int i = 0; i < 1000; ++i) g.addFrame(v.frame(g));
    CHECK(g.adjustments() == before);
    CHECK(g.proxyThreshold() == g.budget());
}

TEST(governor_converges_from_below) {
    GovernorConfig cfg;
    FrameGovernor g(cfg, 1'000'000);
    SyntheticView v;
    v.scene = 2'000'000'000;
    v.trianglesPerMs = 5'000'000.0;
    for (int i = 0; i < 4000; ++i) g.addFrame(v.frame(g));
    const double T = g.targetMs();
    CHECK(v.cost(g) <= T * cfg.highFraction);
    CHECK(v.cost(g) >= T * cfg.lowFraction);
    CHECK(g.lastAction() == FrameGovernor::Action::Hold);
}

TEST(governor_hysteresis_with_noise) {
    GovernorConfig cfg;
    FrameGovernor g(cfg, 30'000'000);
    SyntheticView v;
    v.scene = 2'000'000'000;
    v.noise = 0.15;
    for (int i = 0; i < 2000; ++i) g.addFrame(v.frame(g));
    const int settled = g.adjustments();
    for (int i = 0; i < 5000; ++i) g.addFrame(v.frame(g));
    CHECK(g.adjustments() - settled <= 1); // noise inside the dead band changes nothing
    CHECK(v.cost(g) <= g.targetMs());
    // Dead band: a frame cost between 60% and 100% of the target holds the budget.
    FrameGovernor h(cfg, 10'000'000);
    for (double fraction : {0.62, 0.8, 0.98}) {
        for (int i = 0; i < 200; ++i) {
            FrameSample f;
            f.gpuMs = h.targetMs() * fraction;
            f.drawnTriangles = 10'000'000;
            f.sceneTriangles = 50'000'000;
            f.limited = true;
            h.addFrame(f);
        }
    }
    CHECK(h.budget() == 10'000'000);
    CHECK(h.adjustments() == 0);
    // Just over: it lowers; well under: it raises.
    for (int i = 0; i < 25; ++i) {
        FrameSample f;
        f.gpuMs = h.targetMs() * 1.1;
        f.drawnTriangles = 10'000'000;
        h.addFrame(f);
    }
    CHECK(h.budget() < 10'000'000);
    CHECK(h.lastAction() == FrameGovernor::Action::Lowered);
}

TEST(governor_motion_proxy_no_flapping) {
    // Today's renderer: the motion proxy draws a quarter of the scene. A scene too heavy to move in full at 60 fps
    // must settle on the proxy, not flip between proxy and full detail.
    GovernorConfig cfg;
    FrameGovernor g(cfg, 200'000'000);
    SyntheticView v;
    v.proxyStep = true;
    v.scene = 80'000'000;
    v.trianglesPerMs = 4'000'000.0; // full: 22 ms (too slow); proxy: 7 ms
    int flips = 0;
    bool lastLimited = false;
    for (int i = 0; i < 6000; ++i) {
        const FrameSample f = v.frame(g);
        if (i > 500 && f.limited != lastLimited) ++flips;
        lastLimited = f.limited;
        g.addFrame(f);
    }
    CHECK(lastLimited);       // on the proxy
    CHECK(flips == 0);        // and stays there
    CHECK(g.budget() < v.scene);
    // A scene light enough to move in full: the budget rises past it and the proxy switches off.
    FrameGovernor h(cfg, 2'000'000);
    v.scene = 20'000'000; // full: 7 ms
    for (int i = 0; i < 6000; ++i) h.addFrame(v.frame(h));
    CHECK(h.proxyThreshold() > v.scene);
}

TEST(governor_ignores_still_frames_and_hitches) {
    GovernorConfig cfg;
    FrameGovernor g(cfg, 10'000'000);
    for (int i = 0; i < 500; ++i) {
        FrameSample still;
        still.gpuMs = 100.0; // slow, but the view isn't moving: full detail regardless
        still.moving = false;
        still.drawnTriangles = 10'000'000;
        g.addFrame(still);
        FrameSample hitch;
        hitch.cpuMs = 900.0; // a file dialog
        hitch.drawnTriangles = 10'000'000;
        g.addFrame(hitch);
    }
    CHECK(g.budget() == 10'000'000);
    CHECK(g.decisions() == 0);
    // CPU time alone (no GPU timer queries) still drives it.
    for (int i = 0; i < 25; ++i) {
        FrameSample f;
        f.cpuMs = 40.0;
        f.gpuMs = -1.0;
        f.drawnTriangles = 10'000'000;
        g.addFrame(f);
    }
    CHECK(g.budget() < 10'000'000);
    // Bounds hold.
    GovernorConfig tight;
    tight.minBudget = 5'000'000;
    tight.maxBudget = 6'000'000;
    FrameGovernor b(tight, 100'000'000);
    CHECK(b.budget() == 6'000'000);
    for (int i = 0; i < 500; ++i) {
        FrameSample f;
        f.gpuMs = 200.0;
        f.drawnTriangles = 6'000'000;
        b.addFrame(f);
    }
    CHECK(b.budget() == 5'000'000);
}

TEST(governor_target_120) {
    GovernorConfig cfg;
    cfg.targetFps = 120;
    FrameGovernor g(cfg, 100'000'000);
    SyntheticView v;
    v.scene = 2'000'000'000;
    for (int i = 0; i < 3000; ++i) g.addFrame(v.frame(g));
    CHECK(v.cost(g) <= 1000.0 / 120.0);
    FrameGovernor g60(GovernorConfig{}, 100'000'000);
    for (int i = 0; i < 3000; ++i) g60.addFrame(v.frame(g60));
    CHECK(g.budget() < g60.budget()); // half the frame time, fewer triangles
    g60.setTargetFps(120);
    CHECK(std::abs(g60.targetMs() - 1000.0 / 120.0) < 1e-9);
}
