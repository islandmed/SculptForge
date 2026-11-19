// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Free memory checks (core/MemoryGuard.h) with a mocked amount of free memory: a benchmark scene that wouldn't fit
// is skipped with a clear message, and Subdivide / Divide / micro / remesh know what they'd need.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/MemoryGuard.h"
#include "core/Primitives.h"

using namespace sf;

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
    CHECK(commandMemoryNeed(m, "remesh", {{"resolution", 512.0}}) > commandMemoryNeed(m, "remesh", {{"resolution", 128.0}}));
    CHECK(commandMemoryNeed(m, "smooth", {}) == 0); // not one of the big ones
    CHECK(commandMemoryNeed(Mesh(), "subdivide", {}) == 0);

    // Four levels of Subdivide on this sphere need ~0.8 GB: refused with 0.5 GB free (a dialog asks in the app).
    setAvailableMemoryOverride(kGB / 2);
    const MemoryCheck c = checkMemory(commandMemoryNeed(m, "subdivide", {{"levels", 4.0}}));
    CHECK(!c.enough);
    CHECK(commandShortMessage("Subdivide", c) == "Subdivide needs ~0.8 GB free, 0.5 GB available.");
    setAvailableMemoryOverride(0);
}

TEST(memory_core_scene_need) {
    // core-800m, measured on the floor tier: 805M micro triangles, 1.66 GB resident + 2.56 GB graphics buffers, a 0.6 GB scene file.
    const CoreSceneNeed n = coreSceneNeed(800);
    CHECK(n.ramBytes == size_t(6.4e9));
    CHECK(n.diskBytes == size_t(0.64e9));
    CHECK(coreSceneShortMessage("core-800m", 800, 16 * kGB, 100 * kGB, true).empty());
    CHECK(coreSceneShortMessage("core-800m", 800, 0, 0, true).empty()); // unknown: go on
    // a small machine: the plain message, with the figures, never an out-of-memory
    CHECK(coreSceneShortMessage("core-800m", 800, size_t(1.3 * double(kGB)), 100 * kGB, true) ==
          "core-800m skipped: needs ~6.4 GB free, 1.3 GB available. Close other apps and run again");
    CHECK(coreSceneShortMessage("core-800m", 800, 8 * kGB, 100 * kGB, true).empty());  // 6.4 GB + the margin fits
    CHECK(!coreSceneShortMessage("core-800m", 800, 7 * kGB, 100 * kGB, true).empty()); // 7 GB does not
    // a full disk: the message names the file's need; an existing scene file needs none
    const std::string disk = coreSceneShortMessage("core-800m", 800, 16 * kGB, kGB / 2, true);
    CHECK(disk == "core-800m skipped: needs ~0.9 GB of free disk space for its scene file (written once), 0.5 GB free. Free some space and run again");
    CHECK(coreSceneShortMessage("core-800m", 800, 16 * kGB, kGB / 2, false).empty());
}
