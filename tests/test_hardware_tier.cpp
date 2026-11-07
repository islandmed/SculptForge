// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hardware tiers (core/HardwareTier.h): detection from synthetic machines, the budgets (Floor = the values before
// tiers), names, SF_HW_SIMULATE / SF_TIER_* / SF_COMPUTE, the memory guard, compute choice, the GPU list parsers.
#include "TestHarness.h"

#include "core/HardwareProfile.h"
#include "core/HardwareTier.h"
#include "core/TierConfig.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace sf;

namespace {

HardwareInfo box(const std::string& vendor, const std::string& renderer, uint64_t vramMB, uint64_t ramMB, int threads) {
    HardwareInfo h;
    h.glVendor = vendor;
    h.glRenderer = renderer;
    h.glVersion = "4.6.0 test";
    h.gpuKind = classifyGpu(vendor, renderer);
    h.vramMB = vramMB;
    h.ramMB = ramMB;
    h.cpuThreads = threads;
    h.maxTextureSize = 32768;
    h.appVersion = "9.9.9";
    return h;
}

Calibration fast(double triM = 3000.0, double dabMs = 0.5) {
    Calibration c;
    c.trianglesPerSecondM = triM;
    c.uploadGBps = 10.0;
    c.dabMs = dabMs;
    c.memoryGBps = 20.0;
    c.runs = 5;
    return c;
}

HwTier tierOf(const HardwareInfo& h, const Calibration& c = fast()) { return mapProfile(h, c).hwTier; }

void setEnv(const char* k, const char* v) {
#if defined(_WIN32)
    _putenv_s(k, v ? v : "");
#else
    if (v) setenv(k, v, 1);
    else unsetenv(k);
#endif
}

void writeFile(const std::filesystem::path& p, const std::string& text) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

} // namespace

TEST(hw_tier_names) {
    CHECK(std::string(hwTierName(HwTier::Floor)) == "Floor");
    CHECK(std::string(hwTierDisplayName(HwTier::Floor)) == "Essential"); // never "low"
    CHECK(std::string(hwTierDisplayName(HwTier::High)) == "High");
    HwTier t;
    CHECK(hwTierFromName("floor", t) && t == HwTier::Floor);
    CHECK(hwTierFromName("Essential", t) && t == HwTier::Floor);
    CHECK(hwTierFromName(" EXTREME ", t) && t == HwTier::Extreme);
    CHECK(hwTierFromName("standard", t) && t == HwTier::Standard);
    CHECK(!hwTierFromName("monster", t));
}

TEST(hw_tier_short_gpu_name) {
    CHECK(shortGpuName("NVIDIA GeForce RTX 3060/PCIe/SSE2") == "RTX 3060");
    CHECK(shortGpuName("Mesa Intel(R) UHD Graphics 620 (KBL GT2)") == "Intel UHD Graphics 620");
    CHECK(shortGpuName("Apple M1 Pro (G13S C0)") == "Apple M1 Pro");
    CHECK(shortGpuName("AMD Radeon RX 7900 XTX (radeonsi, navi31, LLVM 18.1.8, DRM 3.57)") == "Radeon RX 7900 XTX");
    CHECK(shortGpuName("NVIDIA RTX A4000/PCIe/SSE2") == "RTX A4000");
    CHECK(shortGpuName("llvmpipe (LLVM 19.1.0, 256 bits)") == "llvmpipe");
    CHECK(shortGpuName("") == "");
    // Mesa's layered drivers (WSL's D3D12, zink) name the real card in brackets.
    CHECK(shortGpuName("D3D12 (NVIDIA GeForce RTX 3060)") == "RTX 3060");
    CHECK(shortGpuName("zink Vulkan 1.3(NVIDIA GeForce RTX 3060 (NVIDIA_PROPRIETARY))") == "RTX 3060");
}

TEST(hw_tier_gl_features) {
    GlFeatures f = glFeatures("4.6.0 NVIDIA 550.54");
    CHECK(f.major == 4 && f.minor == 6 && f.compute && f.bufferStorage);
    f = glFeatures("4.1 Metal - 88.1");
    CHECK(f.major == 4 && f.minor == 1 && !f.compute && !f.bufferStorage);
    f = glFeatures("3.3 (Core Profile) Mesa 24.1.0");
    CHECK(f.major == 3 && f.minor == 3 && !f.compute);
    f = glFeatures("OpenGL ES 3.2 Mesa");
    CHECK(f.major == 3 && f.minor == 2);
    f = glFeatures("");
    CHECK(f.major == 0 && !f.compute);
}

TEST(hw_tier_detection) {
    setEnv("SF_HW_SIMULATE", nullptr);
    // The brief's example: RTX 3060 12 GB, 32 GB RAM, 12 threads -> High; Extreme needs 16 GB VRAM and 64 GB RAM.
    const HardwareProfile pc = mapProfile(box("NVIDIA Corporation", "NVIDIA GeForce RTX 3060/PCIe/SSE2", 12288, 32020, 12), fast());
    CHECK(pc.hwTier == HwTier::High);
    CHECK(pc.detected.machine == "RTX 3060, 12 GB VRAM, 31 GB RAM, 12 threads" ||
          pc.detected.machine == "RTX 3060, 12 GB VRAM, 32 GB RAM, 12 threads");
    CHECK(pc.detected.limit.find("Extreme needs") == 0);
    CHECK(tierSummary(pc.detected, pc.hwTier).find(": High") != std::string::npos);
    // A 4090 workstation: Extreme.
    CHECK(tierOf(box("NVIDIA Corporation", "NVIDIA GeForce RTX 4090/PCIe/SSE2", 24564, 128000, 32)) == HwTier::Extreme);
    // ... but 8 threads keep it at High.
    CHECK(tierOf(box("NVIDIA Corporation", "NVIDIA GeForce RTX 4090/PCIe/SSE2", 24564, 128000, 8)) == HwTier::High);
    // A GTX 1650 4 GB in a 16 GB box: Standard. A 2 GB card: Floor (video memory).
    CHECK(tierOf(box("NVIDIA Corporation", "NVIDIA GeForce GTX 1650/PCIe/SSE2", 3911, 15800, 8)) == HwTier::Standard);
    const HardwareProfile small = mapProfile(box("NVIDIA Corporation", "NVIDIA GeForce MX250/PCIe/SSE2", 2048, 15800, 8), fast());
    CHECK(small.hwTier == HwTier::Floor);
    CHECK(small.detected.limit.find("video memory") != std::string::npos);
    CHECK(small.detected.limit.find("this computer has less video memory") != std::string::npos);
    // An 8 GB laptop with Intel graphics: Floor (RAM). 16 GB: Standard. 32 GB: still Standard (integrated).
    CHECK(tierOf(box("Intel", "Mesa Intel(R) UHD Graphics 620 (KBL GT2)", 0, 7800, 8)) == HwTier::Floor);
    CHECK(tierOf(box("Intel", "Mesa Intel(R) Iris(R) Xe Graphics (TGL GT2)", 0, 15700, 8)) == HwTier::Standard);
    CHECK(tierOf(box("Intel", "Mesa Intel(R) Iris(R) Xe Graphics (TGL GT2)", 0, 31700, 8)) == HwTier::Standard);
    // A big APU with 128 GB (Strix Halo): High.
    CHECK(tierOf(box("AMD", "AMD Radeon 8060S (radeonsi, gfx1151)", 0, 126000, 32)) == HwTier::High);
    // Apple: 16 GB M2 Standard, 32 GB M1 Pro High (10 threads), 64 GB M3 Max with 16 threads Extreme.
    CHECK(tierOf(box("Apple", "Apple M2", 0, 16384, 8)) == HwTier::Standard);
    CHECK(tierOf(box("Apple", "Apple M1 Pro", 0, 32768, 10)) == HwTier::High);
    CHECK(tierOf(box("Apple", "Apple M3 Max", 0, 65536, 16)) == HwTier::Extreme);
    // 4 threads: Floor whatever the GPU.
    CHECK(tierOf(box("NVIDIA Corporation", "NVIDIA GeForce RTX 3060/PCIe/SSE2", 12288, 32020, 4)) == HwTier::Floor);
    // A software renderer: Floor.
    const HardwareProfile sw = mapProfile(box("Mesa", "llvmpipe (LLVM 19.1.0, 256 bits)", 0, 65536, 32), fast(40.0));
    CHECK(sw.hwTier == HwTier::Floor);
    // Measurements cap: a slow GPU (200M/s) or slow brushes stop at Standard.
    const HardwareInfo big = box("NVIDIA Corporation", "NVIDIA GeForce RTX 4090/PCIe/SSE2", 24564, 128000, 32);
    CHECK(tierOf(big, fast(200.0)) == HwTier::Standard);
    CHECK(tierOf(big, fast(3000.0, 3.0)) == HwTier::Standard);
    // Not measured (skipped): the memory sizes alone.
    Calibration skipped;
    skipped.skipped = true;
    CHECK(tierOf(big, skipped) == HwTier::Extreme);
    // Unknown GPU type (nothing probed): the base tier.
    CHECK(tierOf(box("Vendor", "Some GPU", 0, 65536, 16)) == HwTier::Floor);
    // Deterministic.
    CHECK(tierOf(big) == tierOf(big));
}

TEST(hw_tier_budgets) {
    // Floor = the values every machine had before tiers (focusdetail, core200m, microlod, billion-ideas read them).
    const TierBudgets f = budgetsForTier(HwTier::Floor);
    CHECK(f.focusTriangles == 300'000'000);
    CHECK(f.focusResidentMB == 512);
    CHECK(f.focusGpuPoolMB == 384);
    CHECK(f.focusUpdateMs == 3.0);
    CHECK(f.focusLookaheadSeconds == 0.25);
    CHECK(f.strokeBacklogMs == 50.0);
    CHECK(f.draftDabVertices == 0);
    CHECK(f.normalsCatchUpMs == 0.0);
    CHECK(f.stillFrameMs == 100.0);
    CHECK(f.stillDelaySeconds == 0.15);
    CHECK(f.motionBudgetFraction == 0.5);
    CHECK(f.coarseLevels);
    CHECK(!f.billionScenes);
    CHECK(f.maxTextureSize == 8192);
    CHECK(f.gpuDabs == 0);
    // Each tier holds at least as much as the one below.
    for (int i = 1; i < kHwTierCount; ++i) {
        const TierBudgets a = budgetsForTier(HwTier(i - 1)), b = budgetsForTier(HwTier(i));
        CHECK(b.residentTriangles >= a.residentTriangles);
        CHECK(b.maxMicroTriangles >= a.maxMicroTriangles);
        CHECK(b.focusResidentMB >= a.focusResidentMB);
        CHECK(b.focusGpuPoolMB >= a.focusGpuPoolMB);
        CHECK(b.microResidentMB >= a.microResidentMB);
        CHECK(b.maxTextureSize >= a.maxTextureSize);
    }
    CHECK(budgetsForTier(HwTier::High).billionScenes);
    CHECK(budgetsForTier(HwTier::Extreme).focusTriangles == 1'000'000'000);
    // Names: snake case, unique; the JSON has every one.
    const Json j = tierBudgetsJson(f);
    bool foundResident = false;
    for (const TierBudgetField& x : tierBudgetFields()) {
        CHECK(j[x.name].type() != Json::Type::Null);
        foundResident = foundResident || std::string(x.name) == "focus_resident_mb";
    }
    CHECK(foundResident);
    CHECK(j["focus_resident_mb"].asNumber() == 512);
    CHECK(j["billion_scenes"].type() == Json::Type::Bool);
    // "What this turns on": something on every tier, positive wording on the base one.
    for (int i = 0; i < kHwTierCount; ++i) CHECK(!tierTurnsOn(HwTier(i), budgetsForTier(HwTier(i))).empty());
    for (const std::string& line : tierTurnsOn(HwTier::Floor, f)) {
        CHECK(line.find("low") == std::string::npos);
        CHECK(line.find("Low") == std::string::npos);
    }
}

TEST(hw_tier_env_pins_and_simulate) {
    setEnv("SF_HW_SIMULATE", nullptr);
    const HardwareInfo pcHw = box("NVIDIA Corporation", "NVIDIA GeForce RTX 3060/PCIe/SSE2", 12288, 32020, 12);
    // SF_HW_SIMULATE=<tier> forces each tier (floor keeps its old render-budget meaning in the app).
    for (const char* name : {"floor", "standard", "high", "extreme", "Essential"}) {
        setEnv("SF_HW_SIMULATE", name);
        HwTier want;
        CHECK(hwTierFromName(name, want));
        CHECK(mapProfile(pcHw, fast()).hwTier == want);
        CHECK(tierConfigFor(mapProfile(pcHw, fast()), PowerSource::AC, 60).hwTier == want);
    }
    setEnv("SF_HW_SIMULATE", nullptr);
    // A tier set by hand (ProfileOverrides::level), kept through JSON.
    ProfileOverrides o;
    o.level = "Standard";
    o.compute = "cpu";
    o.gpu = "pci-0000_01_00_0";
    o.turbo = true;
    o.tierCardFor = "x";
    const HardwareProfile byHand = mapProfile(pcHw, fast(), o);
    CHECK(byHand.hwTier == HwTier::Standard);
    CHECK(byHand.detected.tier == HwTier::High);
    HardwareProfile back;
    CHECK(hardwareProfileFromJson(hardwareProfileToJson(byHand), back));
    CHECK(back.hwTier == HwTier::Standard);
    CHECK(back.overrides.compute == "cpu" && back.overrides.gpu == "pci-0000_01_00_0" && back.overrides.turbo &&
          back.overrides.tierCardFor == "x");
    // The calibration JSON carries the tier and still validates.
    std::string error;
    const Json cj = calibrationJson(byHand);
    CHECK(validateCalibrationJson(cj, error));
    CHECK(cj["hardware_tier"]["tier"].asString() == "Standard");
    CHECK(cj["hardware_tier"]["detected"].asString() == "High");
    // SF_TIER_<NAME> pins one budget over the tier.
    setEnv("SF_TIER_FOCUS_RESIDENT_MB", "777");
    setEnv("SF_TIER_BILLION_SCENES", "on");
    TierConfig c = tierConfigFor(mapProfile(pcHw, fast()), PowerSource::AC, 60);
    CHECK(c.budgets.focusResidentMB == 777);
    CHECK(c.budgets.billionScenes);
    setEnv("SF_TIER_FOCUS_RESIDENT_MB", nullptr);
    setEnv("SF_TIER_BILLION_SCENES", nullptr);
    // SF_COMPUTE beats the saved choice.
    setEnv("SF_COMPUTE", "gpu");
    CHECK(tierConfigFor(byHand, PowerSource::AC, 60).compute == ComputeChoice::Gpu);
    setEnv("SF_COMPUTE", nullptr);
    CHECK(tierConfigFor(byHand, PowerSource::AC, 60).compute == ComputeChoice::Cpu);
    CHECK(tierConfigFor(byHand, PowerSource::AC, 60).turbo);
    // The memory guard's steps: High - 1 = Standard; never below Floor.
    c = tierConfigFor(mapProfile(pcHw, fast()), PowerSource::AC, 60, 1);
    CHECK(c.hwTier == HwTier::Standard && c.chosenHwTier == HwTier::High && c.memoryStepDown == 1);
    CHECK(c.budgets.focusResidentMB == budgetsForTier(HwTier::Standard).focusResidentMB);
    c = tierConfigFor(mapProfile(pcHw, fast()), PowerSource::AC, 60, 9);
    CHECK(c.hwTier == HwTier::Floor && c.memoryStepDown == 2);
    // 16K textures on High where the card has the memory.
    CHECK(mapProfile(pcHw, fast()).settings.maxTextureSize == 16384);
    CHECK(mapProfile(pcHw, fast(), o).settings.maxTextureSize <= 8192 || mapProfile(pcHw, fast(), o).tier == ProfileTier::Workstation);
    // Back to automatic keeps the choices (any() counts the tier, not compute / gpu / turbo).
    ProfileOverrides choices;
    choices.compute = "gpu";
    choices.turbo = true;
    choices.gpu = "x";
    CHECK(!choices.any());
    choices.level = "High";
    CHECK(choices.any());
}

TEST(hw_tier_memory_guard) {
    TierMemoryGuard g;
    const uint64_t total = 32768;
    // Roomy: nothing happens.
    CHECK(g.update(0.0, 20000, total, 0, 2) == 0);
    // Tight (under 3.2 GB free) for under 2 s: holds; past 2 s: one step down.
    CHECK(g.update(10.0, 2000, total, 0, 2) == 0);
    CHECK(g.update(11.0, 2000, total, 0, 2) == 0);
    CHECK(g.update(12.1, 2000, total, 0, 2) == 1);
    CHECK(g.stepDown == 1);
    // Still tight: no second step within 10 s.
    CHECK(g.update(15.0, 2000, total, 0, 2) == 0);
    CHECK(g.update(22.5, 2000, total, 0, 2) == 1);
    CHECK(g.stepDown == 2);
    // At the floor: no more steps.
    CHECK(g.update(40.0, 2000, total, 0, 2) == 0);
    CHECK(g.stepDown == 2);
    // Roomy again for 60 s: one step back up, then another after 60 more.
    CHECK(g.update(50.0, 20000, total, 0, 2) == 0);
    CHECK(g.update(100.0, 20000, total, 0, 2) == 0);
    CHECK(g.update(110.5, 20000, total, 0, 2) == -1);
    CHECK(g.stepDown == 1);
    CHECK(g.update(171.0, 20000, total, 0, 2) == -1);
    CHECK(g.stepDown == 0);
    // GPU memory alone can be tight.
    TierMemoryGuard gpu;
    CHECK(gpu.update(0.0, 20000, total, 100, 3) == 0);
    CHECK(gpu.update(2.5, 20000, total, 100, 3) == 1);
    // Unknown memory: nothing.
    TierMemoryGuard none;
    CHECK(none.update(100.0, 0, 0, 0, 3) == 0);
}

TEST(hw_tier_compute) {
    resetComputeFailures();
    // No GPU paths in this build yet: every task is CPU, whatever the choice.
    for (int i = 0; i < kComputeTaskCount; ++i) {
        const ComputeTask t = ComputeTask(i);
        CHECK(computeTaskInfo(t).task == t);
        CHECK(resolveCompute(t, ComputeChoice::Gpu, HwTier::Extreme, 10.0, 1.0) ==
              (computeTaskInfo(t).gpuPath ? ComputePath::Gpu : ComputePath::Cpu));
        CHECK(resolveCompute(t, ComputeChoice::Cpu, HwTier::Extreme, 10.0, 1.0) == ComputePath::Cpu);
        CHECK(resolveCompute(t, ComputeChoice::Auto, HwTier::Floor, 10.0, 1.0) == ComputePath::Cpu); // Floor: as before
    }
    ComputeChoice c;
    CHECK(computeChoiceFromName("GPU", c) && c == ComputeChoice::Gpu);
    CHECK(computeChoiceFromName("", c) && c == ComputeChoice::Auto);
    CHECK(!computeChoiceFromName("tpu", c));
    // A failure is reported once and sticks for the session.
    std::string msg;
    CHECK(!takeComputeFailureMessage(msg));
    reportGpuComputeFailure(ComputeTask::Normals, "shader compile");
    reportGpuComputeFailure(ComputeTask::Normals, "again");
    CHECK(gpuComputeFailed(ComputeTask::Normals));
    CHECK(takeComputeFailureMessage(msg));
    CHECK(msg.find("Normals switched to the CPU") == 0);
    CHECK(msg.find("shader compile") != std::string::npos);
    CHECK(!takeComputeFailureMessage(msg));
    CHECK(resolveCompute(ComputeTask::Normals, ComputeChoice::Gpu, HwTier::Extreme, 10.0, 1.0) == ComputePath::Cpu);
    resetComputeFailures();
}

TEST(hw_tier_gpu_list) {
    const std::string ids =
        "# comment\n"
        "10de  NVIDIA Corporation\n"
        "\t2504  GA106 [GeForce RTX 3060 Lite Hash Rate]\n"
        "\t\t1043 87f3  subsystem line\n"
        "\t25a2  GA107M [GeForce RTX 3050 Mobile]\n"
        "8086  Intel Corporation\n"
        "\t9a49  TigerLake-LP GT2 [Iris Xe Graphics]\n"
        "1002  Advanced Micro Devices, Inc. [AMD/ATI]\n"
        "\t73ff  Navi 23 [Radeon RX 6600/6600 XT/6600M]\n";
    CHECK(pciIdsName(ids, 0x10de, 0x2504) == "NVIDIA GeForce RTX 3060 Lite Hash Rate");
    CHECK(pciIdsName(ids, 0x8086, 0x9a49) == "Intel Iris Xe Graphics");
    CHECK(pciIdsName(ids, 0x1002, 0x73ff) == "AMD Radeon RX 6600/6600 XT/6600M");
    CHECK(pciIdsName(ids, 0x10de, 0xffff).empty());
    // A hybrid laptop's sysfs: Intel iGPU (boot display) + NVIDIA dGPU.
    const std::filesystem::path root = tempPath("sf_hw_tier_drm");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    auto card = [&](const char* name, const char* vendor, const char* device, const char* boot, const char* slot) {
        const auto dev = root / name / "device";
        writeFile(dev / "class", "0x030000\n");
        writeFile(dev / "vendor", std::string(vendor) + "\n");
        writeFile(dev / "device", std::string(device) + "\n");
        writeFile(dev / "boot_vga", std::string(boot) + "\n");
        writeFile(dev / "uevent", std::string("DRIVER=x\nPCI_SLOT_NAME=") + slot + "\n");
    };
    card("card0", "0x8086", "0x9a49", "1", "0000:00:02.0");
    card("card1", "0x10de", "0x25a2", "0", "0000:01:00.0");
    std::filesystem::create_directories(root / "card1-HDMI-A-1"); // a connector, not a card
    writeFile(root / "pci.ids", ids);
    const std::vector<GpuAdapter> gpus = listGpusSysfs(root.string(), (root / "pci.ids").string());
    CHECK(gpus.size() == 2);
    if (gpus.size() == 2) {
        CHECK(gpus[0].name == "Intel Iris Xe Graphics");
        CHECK(!gpus[0].discrete && gpus[0].bootDisplay);
        CHECK(gpus[0].id == "pci-0000_00_02_0");
        CHECK(gpus[1].name == "NVIDIA GeForce RTX 3050 Mobile");
        CHECK(gpus[1].discrete && !gpus[1].bootDisplay);
        CHECK(gpus[1].id == "pci-0000_01_00_0");
        CHECK(autoGpuId(gpus) == "pci-0000_01_00_0");
    }
    // No pci.ids: a readable fallback name.
    const std::vector<GpuAdapter> bare = listGpusSysfs(root.string(), "");
    CHECK(bare.size() == 2 && bare[1].name == "NVIDIA GPU 10de:25a2");
    CHECK(listGpusSysfs((root / "missing").string(), "").empty());
    CHECK(autoGpuId({}).empty());
    std::filesystem::remove_all(root, ec);
}
