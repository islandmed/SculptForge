// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Free memory checks (core/MemoryGuard.h) with a mocked amount of free memory: a benchmark scene that wouldn't fit
// is skipped with a clear message, and Subdivide / Divide / micro / remesh know what they'd need.
#include "TestHarness.h"

#include "core/MemoryGuard.h"
#include "core/Primitives.h"
#include "core/Voxel.h"

#include <cstdio>

using namespace sf;

// CHECK with what went wrong.
#define CHECK_MSG(cond, msg)                                           \
    do {                                                               \
        const bool ok_ = (cond);                                       \
        CHECK(ok_);                                                    \
        if (!ok_) std::printf("    %s\n", std::string(msg).c_str());  \
    } while (0)

namespace {

constexpr size_t kGB = 1'000'000'000;

// The standard scenes' need (ProofMode: millions x 1e6 x 150 bytes a triangle).
size_t sceneNeed(double millions) { return size_t(millions * 1e6 * 150.0); }

} // namespace

TEST(memory_check_margin) {
    CHECK(memoryMargin(100) == size_t(512) << 20); // at least 512 MB
    CHECK(memoryMargin(10 * kGB) == kGB);          // else a tenth
    CHECK(checkMemory(kGB, 0).enough);             // unknown free memory: go on
    CHECK(checkMemory(0, 1).enough);               // nothing needed
    CHECK(checkMemory(kGB, 2 * kGB).enough);
    CHECK(!checkMemory(kGB, kGB).enough);          // fits only without the margin
}

TEST(memory_benchmark_scene_skipped) {
    setAvailableMemoryOverride(size_t(1.3 * double(kGB))); // a real report: 1.3 GB free
    CHECK(availableMemoryBytes() == size_t(1.3 * double(kGB)));
    const MemoryCheck c33 = checkMemory(sceneNeed(33));
    CHECK(!c33.enough);
    const std::string msg = benchmarkSkippedMessage("33M", c33);
    CHECK(msg == "33M skipped: needs ~5 GB free, 1.3 GB available. Close other apps and run again");
    CHECK(!checkMemory(sceneNeed(15)).enough); // (15M: ~2.3 GB)

    setAvailableMemoryOverride(4 * kGB); // enough for 15M, not 33M
    CHECK(checkMemory(sceneNeed(15)).enough);
    CHECK(!checkMemory(sceneNeed(33)).enough);
    setAvailableMemoryOverride(8 * kGB);
    CHECK(checkMemory(sceneNeed(33)).enough);
    setAvailableMemoryOverride(0); // (back to the system's own number)
    CHECK(availableMemoryBytes() != size_t(8 * kGB));
}

TEST(memory_gigabytes_text) {
    CHECK(formatGigabytes(size_t(4.95 * double(kGB))) == "5 GB");
    CHECK(formatGigabytes(size_t(2.26 * double(kGB))) == "2.3 GB");
    CHECK(formatGigabytes(size_t(0.4 * double(kGB))) == "0.4 GB");
    CHECK(formatGigabytes(size_t(12.3 * double(kGB))) == "12 GB");
}

TEST(memory_command_need) {
    const Mesh m = makeIcosphere(5); // 20480 triangles
    const size_t nt = m.triangleCount();
    CHECK(commandMemoryNeed(m, "subdivide", {}) == size_t(double(nt) * 4 * 150));
    CHECK(commandMemoryNeed(m, "subdivide", {{"levels", 2.0}}) == size_t(double(nt) * 16 * 150));
    CHECK(commandMemoryNeed(m, "subdiv_divide", {}) > 0);
    CHECK(commandMemoryNeed(m, "micro_divide", {{"level", 3.0}}) == size_t(double(nt) * 64 * 6));
    CHECK(commandMemoryNeed(m, "remesh", {{"resolution", 512.0}}) == 0); // (the voxel remesh has its own tiers: one estimate, core/Voxel.h)
    CHECK(commandMemoryNeed(m, "smooth", {}) == 0); // not one of the big ones
    CHECK(commandMemoryNeed(Mesh(), "subdivide", {}) == 0);

    // Four levels of Subdivide on this sphere need ~0.8 GB: refused with 0.5 GB free (a dialog asks in the app).
    setAvailableMemoryOverride(kGB / 2);
    const MemoryCheck c = checkMemory(commandMemoryNeed(m, "subdivide", {{"levels", 4.0}}));
    CHECK(!c.enough);
    CHECK(commandShortMessage("Subdivide", c) == "Subdivide needs ~0.8 GB free, 0.5 GB available.");
    setAvailableMemoryOverride(0);
}

TEST(memory_tiers_with_injected_numbers) {
    // 16 GB computer, 4 GB swap free: the ceiling is (16 + 4) * 0.9 = 18 GB.
    MemoryLimits l;
    l.available = 6 * kGB;
    l.total = 16 * kGB;
    l.swapMax = 4 * kGB;
    CHECK(memoryCeiling(l) == 18 * kGB);
    CHECK(classifyMemory(0, l) == MemoryTier::Fits);
    CHECK(classifyMemory(4 * kGB, l) == MemoryTier::Fits);     // 4 GB + its 512 MB margin fits in 6 GB free
    CHECK(classifyMemory(6 * kGB, l) == MemoryTier::NeedsSwap); // more than free (the margin counts)
    CHECK(classifyMemory(18 * kGB, l) == MemoryTier::NeedsSwap); // up to the ceiling itself
    CHECK(classifyMemory(18 * kGB + 1, l) == MemoryTier::TooBig);
    l.swapMax = 0; // no swap: 14.4 GB
    CHECK(classifyMemory(14 * kGB, l) == MemoryTier::NeedsSwap);
    CHECK(classifyMemory(15 * kGB, l) == MemoryTier::TooBig);
    l.total = 0; // total unknown: refuses what doesn't fit free, as before
    CHECK(classifyMemory(7 * kGB, l) == MemoryTier::TooBig);
    CHECK(classifyMemory(kGB, l) == MemoryTier::Fits);
    l.available = 0; // free memory unknown: go on
    CHECK(classifyMemory(100 * kGB, l) == MemoryTier::Fits);
}

TEST(memory_limits_override_and_messages) {
    const MemoryLimits l{kGB, 8 * kGB, 0};
    setMemoryLimitsOverride(l);
    CHECK(memoryLimits().total == 8 * kGB && memoryLimits().available == kGB);
    CHECK(classifyMemory(3 * kGB) == MemoryTier::NeedsSwap);
    setMemoryLimitsOverride({}, false);
    CHECK(memoryLimits().total != 8 * kGB || memoryLimits().available != kGB); // (the system's own again)
    CHECK(memorySwapMessage(6 * kGB, 2 * kGB) == "This needs about 5.6 GB; 1.9 GB is free. It may use swap and be slow.");
}

TEST(memory_remesh_too_big_says_what_fits) {
    const Mesh m = makeIcosphere(5);
    MemoryLimits l;
    l.available = 600u << 20; // margin alone is 512 MB: only a small remesh fits
    l.total = 700u << 20;
    const int fit = voxelResolutionThatFits(m, 4096, l);
    CHECK(fit >= kVoxelResolutionMin && fit < 4096);
    CHECK(checkMemory(estimateVoxelRemesh(m, fit).bytes, l.available).enough);
    if (fit < 4095) CHECK(!checkMemory(estimateVoxelRemesh(m, fit + 1).bytes, l.available).enough); // the highest that does
    CHECK(voxelResolutionThatFits(m, kVoxelResolutionMin, l) == 0); // nothing below the lowest
    l.available = 1u << 20;
    CHECK(voxelResolutionThatFits(m, 4096, l) == 0); // not even 32 with a 512 MB margin
    l.available = 600u << 20;
    const std::string msg = voxelRemeshTooBigMessage(m, 4096, l);
    CHECK_MSG(msg.find("Voxel Remesh at 4096") == 0 && msg.find("needs ~") != std::string::npos &&
                  msg.find("can't hold it even with swap") != std::string::npos &&
                  msg.find("Lower the Resolution to " + std::to_string(fit)) != std::string::npos,
              msg);
}

TEST(memory_ceiling_linux_fixed_swap) {
    const SwapCeiling c = linuxSwapCeiling(8 * kGB); // SwapTotal, not what is free
    CHECK(c.bytes == 8 * kGB && c.kind == SwapKind::Fixed);
    const MemoryLimits l{kGB, 16 * kGB, c.bytes, c.kind};
    CHECK(memoryCeiling(l) == size_t(double(24 * kGB) * 0.9));
    CHECK(memoryCeilingText(l) == "RAM 15 GB + swap 7.5 GB, 90%: 20 GB");
}

TEST(memory_ceiling_macos_dynamic_swap) {
    // Swap files are made as needed: RAM + the startup volume's free space less 10 GB kept.
    CHECK(macSwapCeiling(100 * kGB).bytes == 100 * kGB - kMacDiskReserve);
    CHECK(macSwapCeiling(100 * kGB).kind == SwapKind::DiskDynamic);
    CHECK(macSwapCeiling(5 * kGB).bytes == 0); // less than the reserve
    CHECK(macSwapCeiling(0).bytes == 0);
    const MemoryLimits l{kGB, 16 * kGB, macSwapCeiling(110 * kGB).bytes, SwapKind::DiskDynamic};
    CHECK(memoryCeiling(l) == size_t(double(16 * kGB + 110 * kGB - kMacDiskReserve) * 0.9));
    CHECK(memoryCeilingText(l).find("free disk space less") != std::string::npos);
}

TEST(memory_pagefile_settings_parse) {
    const auto f = parsePagingFiles({"C:\\pagefile.sys 16384 86016", "D:\\Swap Files\\pagefile.sys 0 0", "?:\\pagefile.sys", ""});
    CHECK(f.size() == 3);
    CHECK(f[0].path == "C:\\pagefile.sys" && f[0].minMB == 16384 && f[0].maxMB == 86016);
    CHECK(f[1].path == "D:\\Swap Files\\pagefile.sys" && f[1].maxMB == 0); // (a space in the path)
    CHECK(f[2].path == "?:\\pagefile.sys" && f[2].maxMB == 0);               // (no sizes: system managed)
}

TEST(memory_ceiling_windows_pagefile_maximum) {
    // The #182 case: the page file is 52 GB now and grows 60 -> 72 -> 84 GB up to the maximum the user set. The ceiling
    // is the maximum, not the current size.
    PageFileSetting c;
    c.path = "C:\\pagefile.sys";
    c.minMB = 52 * 1024;
    c.maxMB = 84 * 1024;
    c.volumeFreeBytes = 200 * kGB;
    c.currentBytes = 52ull << 30;
    const SwapCeiling set = windowsSwapCeiling({c}, 32 * kGB);
    CHECK(set.kind == SwapKind::PageFileSet);
    CHECK(set.bytes == size_t(84) << 30);
    const MemoryLimits l{kGB, 32 * kGB, set.bytes, set.kind};
    CHECK(memoryCeiling(l) == size_t(double(32 * kGB + (size_t(84) << 30)) * 0.9));
    CHECK(memoryCeilingText(l).find("page file maximum 84 GB") != std::string::npos);
    // The maximum can't be reached when the volume has no room for it: free space plus the file's own size.
    c.volumeFreeBytes = 10ull << 30;
    CHECK(windowsSwapCeiling({c}, 32 * kGB).bytes == (size_t(62) << 30));
    // Two files on two drives add up.
    PageFileSetting d = c;
    d.path = "D:\\pagefile.sys";
    d.maxMB = 16 * 1024;
    d.minMB = 1024;
    d.volumeFreeBytes = 500 * kGB;
    d.currentBytes = 0;
    c.volumeFreeBytes = 200 * kGB;
    CHECK(windowsSwapCeiling({c, d}, 32 * kGB).bytes == (size_t(100) << 30));
}

TEST(memory_ceiling_windows_system_managed) {
    // Maximum 0: Windows grows it as needed: the volume's free space, at most 3x RAM.
    PageFileSetting c;
    c.path = "C:\\pagefile.sys";
    c.volumeFreeBytes = 500 * kGB;
    c.currentBytes = 20 * kGB;
    SwapCeiling m = windowsSwapCeiling({c}, 32 * kGB);
    CHECK(m.kind == SwapKind::PageFileManaged);
    CHECK(m.bytes == 96 * kGB); // 3 x 32 GB RAM
    c.volumeFreeBytes = 30 * kGB;
    CHECK(windowsSwapCeiling({c}, 32 * kGB).bytes == 50 * kGB); // the disk is the limit (30 free + the file's 20)
    // Two entries on the same drive count its room once.
    PageFileSetting c2 = c;
    c2.path = "c:\\other.sys";
    CHECK(windowsSwapCeiling({c, c2}, 32 * kGB).bytes == 50 * kGB);
    CHECK(windowsSwapCeiling({}, 32 * kGB).bytes == 0); // no page file at all
}

TEST(memory_ceiling_tier_uses_the_maximum) {
    // 32 GB RAM, page file now 52 GB but allowed to grow to 84 GB: a 100 GB need is allowed to try (limit 104 GB).
    const MemoryLimits l{8 * kGB, 32 * kGB, size_t(84) << 30, SwapKind::PageFileSet};
    CHECK(classifyMemory(100 * kGB, l) == MemoryTier::NeedsSwap);
    CHECK(classifyMemory(115 * kGB, l) == MemoryTier::TooBig);
}
