// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BrushCompute.h"

#include "core/Micro.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>

namespace sf {

bool gpuMeshBrush(BrushType type, GpuBrush& out) {
    switch (type) {
    case BrushType::Standard: out = GpuBrush::Standard; return true;
    case BrushType::Clay: out = GpuBrush::Clay; return true;
    case BrushType::Inflate: out = GpuBrush::Inflate; return true;
    case BrushType::Flatten: out = GpuBrush::Flatten; return true;
    case BrushType::Pinch: out = GpuBrush::Pinch; return true;
    case BrushType::Crease: out = GpuBrush::Crease; return true;
    default: return false;
    }
}

bool gpuMicroBrush(BrushType type, GpuBrush& out) {
    if (type == BrushType::Smooth) {
        out = GpuBrush::Smooth;
        return true;
    }
    return gpuMeshBrush(type, out);
}

void brushPlaneFrame(const glm::vec3& areaNormal, const glm::vec3& alphaUp, glm::vec3& up, glm::vec3& right) {
    auto projectOnPlane = [](const glm::vec3& v, const glm::vec3& n) { return v - n * glm::dot(v, n); };
    up = projectOnPlane(alphaUp, areaNormal);
    if (glm::length(up) < 1e-6f)
        up = projectOnPlane(std::abs(areaNormal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0), areaNormal);
    up = glm::normalize(up);
    right = glm::cross(up, areaNormal);
}

float meshDeltaUnit(float radius) {
    int e = 0;
    std::frexp(std::max(radius, 1e-30f), &e); // radius < 2^e
    return std::ldexp(1.0f, e - 24);
}

namespace {

BrushCompute* gCompute = nullptr;
std::atomic<int> gEnabled{-1}; // -1: not decided yet (the environment)

} // namespace

BrushCompute* brushCompute() { return gCompute; }
void setBrushCompute(BrushCompute* compute) { gCompute = compute; }

bool gpuBrushesEnabled() {
    int v = gEnabled.load(std::memory_order_relaxed);
    if (v < 0) {
        const char* env = std::getenv("SF_GPU_BRUSHES");
        v = env && *env == '0' ? 0 : 1;
        gEnabled.store(v, std::memory_order_relaxed);
    }
    return v == 1;
}

void setGpuBrushesEnabled(bool on) { gEnabled.store(on ? 1 : 0, std::memory_order_relaxed); }

namespace {
std::atomic<long long> gMinPoints{-1};
} // namespace

size_t gpuMinPoints() {
    long long v = gMinPoints.load(std::memory_order_relaxed);
    if (v < 0) {
        const char* env = std::getenv("SF_GPU_MIN_POINTS");
        v = env && *env ? std::max(0LL, std::atoll(env)) : 16384;
        gMinPoints.store(v, std::memory_order_relaxed);
    }
    return size_t(v);
}

void setGpuMinPoints(size_t points) { gMinPoints.store((long long)points, std::memory_order_relaxed); }

std::string brushPathName() {
    BrushCompute* c = activeBrushCompute();
    return c ? c->name() : std::string("CPU");
}

const std::vector<uint32_t>& microGridCoords(uint32_t n) {
    static std::mutex mutex;
    static std::map<uint32_t, std::vector<uint32_t>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<uint32_t>& v = cache[n];
    if (v.empty()) {
        v.reserve(size_t(n + 1) * (n + 2) / 2);
        for (uint32_t j = 0; j <= n; ++j)
            for (uint32_t i = 0; i + j <= n; ++i) v.push_back(i | j << 16);
    }
    return v;
}

} // namespace sf
