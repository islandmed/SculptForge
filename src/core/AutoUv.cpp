// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AutoUv.h"

#include "core/Mesh.h"
#include "core/Texture.h"

#include <xatlas.h>

#include <algorithm>
#include <atomic>
#include <memory>

namespace sf {

namespace {

struct ProgressState {
    const AutoUvProgress* fn = nullptr;
    std::atomic<bool> cancelled{false};
};

bool onProgress(xatlas::ProgressCategory category, int percent, void* user) {
    auto* s = static_cast<ProgressState*>(user);
    if (!s->fn || !*s->fn) return true;
    // Rough share of the time each stage takes.
    float from = 0.0f, span = 0.05f;
    switch (category) {
    case xatlas::ProgressCategory::AddMesh: from = 0.0f, span = 0.05f; break;
    case xatlas::ProgressCategory::ComputeCharts: from = 0.05f, span = 0.6f; break;
    case xatlas::ProgressCategory::PackCharts: from = 0.65f, span = 0.3f; break;
    case xatlas::ProgressCategory::BuildOutputMeshes: from = 0.95f, span = 0.05f; break;
    }
    if (!(*s->fn)(from + span * float(std::clamp(percent, 0, 100)) / 100.0f)) s->cancelled = true;
    return !s->cancelled;
}

} // namespace

bool computeAutoUv(const std::vector<glm::vec3>& positions, const std::vector<glm::uvec3>& triangles, int resolution,
                   AutoUvResult& out, std::string& error, const AutoUvProgress& progress) {
    out = AutoUvResult();
    if (positions.empty() || triangles.empty()) {
        error = "There is no mesh to unwrap";
        return false;
    }
    std::unique_ptr<xatlas::Atlas, void (*)(xatlas::Atlas*)> atlas(xatlas::Create(), xatlas::Destroy);
    ProgressState state;
    state.fn = &progress;
    xatlas::SetProgressCallback(atlas.get(), onProgress, &state);
    xatlas::MeshDecl decl;
    decl.vertexPositionData = positions.data();
    decl.vertexPositionStride = sizeof(glm::vec3);
    decl.vertexCount = uint32_t(positions.size());
    decl.indexData = triangles.data();
    decl.indexCount = uint32_t(triangles.size() * 3);
    decl.indexFormat = xatlas::IndexFormat::UInt32;
    const xatlas::AddMeshError added = xatlas::AddMesh(atlas.get(), decl, 1);
    if (added != xatlas::AddMeshError::Success) {
        error = std::string("Auto UV failed: ") + xatlas::StringForEnum(added);
        return false;
    }
    xatlas::AddMeshJoin(atlas.get());
    if (state.cancelled) {
        error = "Auto UV was cancelled";
        return false;
    }
    xatlas::ChartOptions charts;
    // Charts of at most about kChartFaces triangles (by area): flattening a chart costs more than linearly in its
    // size, so a dense, smooth sculpt cut into a few huge charts took minutes (a 1.3M-triangle sphere: about
    // 8 minutes; 16 s with the cap). Detailed sculpts get smaller charts than that anyway.
    constexpr double kChartFaces = 4000.0;
    if (double(triangles.size()) > kChartFaces) {
        double area = 0.0;
        for (const glm::uvec3& t : triangles)
            area += 0.5 * double(glm::length(glm::cross(positions[t.y] - positions[t.x], positions[t.z] - positions[t.x])));
        charts.maxChartArea = float(area * kChartFaces / double(triangles.size()));
    }
    xatlas::PackOptions pack;
    pack.resolution = uint32_t(std::clamp(resolution, 256, 8192));
    pack.padding = uint32_t(kTextureGutter); // the painter pads islands by this much
    pack.bilinear = true;
    pack.blockAlign = true; // faster packing on big meshes
    xatlas::Generate(atlas.get(), charts, pack);
    if (state.cancelled) {
        error = "Auto UV was cancelled";
        return false;
    }
    if (atlas->meshCount != 1 || atlas->width == 0 || atlas->height == 0) {
        error = "Auto UV produced no charts";
        return false;
    }
    const xatlas::Mesh& m = atlas->meshes[0];
    if (m.indexCount != triangles.size() * 3) {
        error = "Auto UV changed the triangle count";
        return false;
    }
    const float sx = 1.0f / float(atlas->width), sy = 1.0f / float(atlas->height);
    out.uvs.resize(m.indexCount);
    for (uint32_t i = 0; i < m.indexCount; ++i) {
        const xatlas::Vertex& v = m.vertexArray[m.indexArray[i]];
        out.uvs[i] = glm::clamp(glm::vec2(v.uv[0] * sx, v.uv[1] * sy), glm::vec2(0.0f), glm::vec2(1.0f));
    }
    out.charts = int(m.chartCount);
    return true;
}

bool computeAutoCharts(const std::vector<glm::vec3>& positions, const std::vector<glm::uvec3>& triangles,
                       std::vector<uint32_t>& chartOfTriangle, int& charts, std::string& error) {
    chartOfTriangle.assign(triangles.size(), 0);
    charts = 0;
    if (positions.empty() || triangles.empty()) {
        error = "There is no mesh to cut into charts";
        return false;
    }
    std::unique_ptr<xatlas::Atlas, void (*)(xatlas::Atlas*)> atlas(xatlas::Create(), xatlas::Destroy);
    xatlas::MeshDecl decl;
    decl.vertexPositionData = positions.data();
    decl.vertexPositionStride = sizeof(glm::vec3);
    decl.vertexCount = uint32_t(positions.size());
    decl.indexData = triangles.data();
    decl.indexCount = uint32_t(triangles.size() * 3);
    decl.indexFormat = xatlas::IndexFormat::UInt32;
    if (xatlas::AddMesh(atlas.get(), decl, 1) != xatlas::AddMeshError::Success) {
        error = "Couldn't cut the piece into charts";
        return false;
    }
    xatlas::AddMeshJoin(atlas.get());
    xatlas::ChartOptions options;
    xatlas::PackOptions pack;
    pack.resolution = 256; // (only the charts matter)
    pack.padding = 0;
    pack.blockAlign = true;
    xatlas::Generate(atlas.get(), options, pack);
    if (atlas->meshCount != 1) {
        error = "Couldn't cut the piece into charts";
        return false;
    }
    const xatlas::Mesh& m = atlas->meshes[0];
    for (uint32_t c = 0; c < m.chartCount; ++c)
        for (uint32_t k = 0; k < m.chartArray[c].faceCount; ++k) {
            const uint32_t f = m.chartArray[c].faceArray[k];
            if (f < chartOfTriangle.size()) chartOfTriangle[f] = c;
        }
    charts = int(m.chartCount);
    return true;
}

bool computeAutoUv(const Mesh& mesh, AutoUvResult& out, std::string& error, const AutoUvProgress& progress) {
    const int res = mesh.texture ? std::max(mesh.texture->width, mesh.texture->height) : kDefaultTextureSize;
    return computeAutoUv(mesh.positions, mesh.triangles, res, out, error, progress);
}

} // namespace sf
