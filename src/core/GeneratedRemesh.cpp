// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/GeneratedRemesh.h"

#include "core/Multires.h"
#include "core/Projection.h"
#include "core/QRemesh.h"
#include "core/Voxel.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace sf {

bool generatedRemeshFromName(std::string_view name, GeneratedRemesh& mode) {
    if (name == "quad" || name == "true" || name == "1") mode = GeneratedRemesh::Quad;
    else if (name == "voxel") mode = GeneratedRemesh::Voxel;
    else if (name == "false" || name == "off" || name == "none" || name == "0") mode = GeneratedRemesh::Off;
    else return false;
    return true;
}

const char* generatedRemeshName(GeneratedRemesh mode) {
    return mode == GeneratedRemesh::Quad ? "quad" : mode == GeneratedRemesh::Voxel ? "voxel" : "false";
}

int generatedQuadTarget(int resolution) {
    const int r = std::clamp(resolution, 64, 512);
    return std::clamp(r * r / 2, 2000, 40000);
}

int generatedCleanupResolution(int resolution) {
    return std::clamp(int(float(std::clamp(resolution, 64, 512)) * 0.7f + 0.5f), 64, 200);
}

bool remeshGenerated(const Mesh& raw, GeneratedRemesh mode, int resolution, int levels, Mesh& out, std::string& error,
                     GeneratedRemeshStats* stats, const std::function<bool(float)>& progress) {
    const auto start = std::chrono::steady_clock::now();
    GeneratedRemeshStats st;
    st.rawTriangles = raw.triangleCount();
    if (mode == GeneratedRemesh::Off) {
        out = raw;
    } else {
        // The cleanup pass: one watertight, manifold surface whatever the model came as.
        Mesh voxel = voxelRemesh(raw, std::clamp(resolution, 32, 512), &error);
        if (voxel.triangleCount() == 0) return false;
        if (mode == GeneratedRemesh::Voxel) {
            out = std::move(voxel);
        } else {
            QRemeshOptions o;
            o.targetQuads = generatedQuadTarget(resolution);
            o.measureQuality = false; // the report is for Tool > Remesh; here it only costs time
            const int cleanupRes = generatedCleanupResolution(resolution);
            Mesh cleanup = cleanupRes < std::clamp(resolution, 32, 512) ? voxelRemesh(raw, cleanupRes, &error) : voxel;
            if (cleanup.triangleCount() == 0) return false;
            Mesh quads;
            if (!qremesh(cleanup, o, quads, error, progress)) return false;
            for (int i = 0; i < std::clamp(levels, 0, 2); ++i) {
                if (!subdivDivide(quads, true, error)) return false;
                ++st.levels;
            }
            if (st.levels > 0) { // Divide smooths: put the top level back on the surface
                std::vector<glm::vec3> positions;
                ProjectStats ps;
                ProjectOptions po;
                po.polypaint = false;
                if (!computeProjection(quads, {&voxel}, po, positions, nullptr, ps, error)) return false;
                quads.layerTarget(-1) = positions;
                quads.recomputeAllNormals();
                subdivCommit(quads);
            }
            out = std::move(quads);
            st.quads = out.quadCount() + (out.triangleCount() - 2 * out.quadCount());
        }
    }
    st.triangles = out.triangleCount();
    st.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (stats) *stats = st;
    return true;
}

std::string generatedRemeshNote(const GeneratedRemeshStats& st, GeneratedRemesh mode) {
    if (mode == GeneratedRemesh::Quad)
        return "remeshed to " + std::to_string(st.quads) + " quads" +
               (st.levels ? " (" + std::to_string(st.levels + 1) + " subdivision levels)" : std::string());
    if (mode == GeneratedRemesh::Voxel) return "remeshed to " + std::to_string(st.triangles) + " triangles";
    return {};
}

} // namespace sf
