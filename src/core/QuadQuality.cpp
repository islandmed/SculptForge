// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The quality report shown after QRemesh (core/QRemesh.h, quadQuality): poles and the valence histogram, how far
// quad corners are from right angles, how even the edges are, and the distance to the source sculpt.
#include "core/QRemesh.h"

#include "core/SurfaceQuery.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sf {

QuadQuality quadQuality(const Mesh& mesh, const Mesh* source) {
    QuadQuality q;
    q.vertices = mesh.vertexCount();
    std::vector<std::vector<uint32_t>> faces;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        glm::uvec4 f;
        const int n = mesh.exportFace(t, f);
        if (n == 4) faces.push_back({f.x, f.y, f.z, f.w}), ++q.quads;
        else if (n == 3) faces.push_back({f.x, f.y, f.z}), ++q.triangles;
    }
    std::unordered_map<uint64_t, int> edges;
    edges.reserve(faces.size() * 3);
    for (const auto& f : faces)
        for (size_t k = 0; k < f.size(); ++k) {
            const uint32_t a = f[k], b = f[(k + 1) % f.size()];
            ++edges[a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a];
        }
    std::vector<int> valence(mesh.vertexCount(), 0);
    std::vector<uint8_t> open(mesh.vertexCount(), 0);
    double lsum = 0.0, lsum2 = 0.0;
    for (auto [key, uses] : edges) {
        const uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
        ++valence[a], ++valence[b];
        if (uses == 1) open[a] = open[b] = 1;
        const double l = double(glm::length(mesh.positions[a] - mesh.positions[b]));
        lsum += l, lsum2 += l * l;
    }
    if (!edges.empty()) {
        const double mean = lsum / double(edges.size());
        const double var = std::max(0.0, lsum2 / double(edges.size()) - mean * mean);
        q.edgeLengthCv = mean > 0.0 ? float(std::sqrt(var) / mean) : 0.0f;
    }
    for (uint32_t v = 0; v < mesh.vertexCount(); ++v) {
        if (valence[v] == 0) continue;
        if (open[v]) {
            if (valence[v] != 3) ++q.boundaryPoles;
            continue;
        }
        ++q.valence[std::clamp(valence[v], 0, 7)];
        if (valence[v] != 4) ++q.poles;
    }
    std::vector<float> dev;
    dev.reserve(q.quads * 4);
    for (const auto& f : faces) {
        if (f.size() != 4) continue;
        for (int k = 0; k < 4; ++k) {
            const glm::vec3 p = mesh.positions[f[size_t(k)]];
            const glm::vec3 a = mesh.positions[f[size_t((k + 1) % 4)]] - p, b = mesh.positions[f[size_t((k + 3) % 4)]] - p;
            const float la = glm::length(a), lb = glm::length(b);
            if (la <= 0.0f || lb <= 0.0f) continue;
            const float ang = glm::degrees(std::acos(std::clamp(glm::dot(a, b) / (la * lb), -1.0f, 1.0f)));
            dev.push_back(std::abs(ang - 90.0f));
        }
    }
    if (!dev.empty()) {
        double s = 0.0;
        for (float d : dev) s += d;
        q.angleDeviation = float(s / double(dev.size()));
        const size_t k = std::min(dev.size() - 1, dev.size() * 99 / 100);
        std::nth_element(dev.begin(), dev.begin() + long(k), dev.end());
        q.angleDeviationMax = dev[k];
    }
    if (source && source->triangleCount() > 0 && mesh.triangleCount() > 0) {
        const float diag = glm::length(source->bounds().extent());
        const SurfaceDeviation d = measureDeviation(mesh, *source, true);
        if (diag > 0.0f) {
            q.hausdorff = 100.0f * d.max / diag;
            q.meanDistance = 100.0f * d.mean / diag;
        }
        q.measured = true;
    }
    return q;
}

} // namespace sf
