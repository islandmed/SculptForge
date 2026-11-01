// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GPU brushes on OpenGL 4.3 compute shaders (Engine v2 phase 1, docs/design/engine-v2.md section 5): the
// BrushCompute (core/BrushCompute.h) the app installs when its context has compute shaders.
//
// Each dab: the points under the brush go up to shader storage buffers (mesh levels: the vertices' positions and
// normals; micro levels: the base corners and the height grids of the base triangles in reach), one pass finds the
// area plane (per-workgroup partial sums, added on the CPU in a fixed order), the kernel pass computes every
// point's change, and the integer deltas come back for the CPU to commit. Micro levels evaluate the brush on the
// coarse points of the dab's stride and spread it over every grid point on the GPU, which is where the CPU spent
// its time on big brushes.
//
// Runs on the thread that created it (the GL thread), with that thread's context current; any other thread is
// refused (the dab runs on the CPU). Leaves the context as the renderer expects: no program bound, no shader
// storage buffers bound. Nothing here touches the renderer's buffers.
#pragma once

#include "core/BrushCompute.h"
#include "render/GL.h"

#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sf {

class GpuSculpt final : public BrushCompute {
public:
    // With the current context: the GPU path, or null and `reason` (e.g. "OpenGL 4.1 has no compute shaders").
    // `loader` resolves GL functions (SDL_GL_GetProcAddress).
    static std::unique_ptr<GpuSculpt> create(gl::ProcLoader loader, std::string& reason);
    ~GpuSculpt() override;

    std::string name() const override { return "GPU (OpenGL 4.3 compute)"; }
    bool meshDab(const MeshDabJob& job, MeshDabResult& out) override;
    bool microDab(const MicroDabJob& job, MicroDabResult& out) override;
    bool microGrab(const MicroGrabJob& job, std::vector<int16_t>& delta) override;
    size_t gpuBytes() const;

private:
    GpuSculpt() = default;
    bool init(std::string& reason);
    bool onOwnerThread() const { return std::this_thread::get_id() == owner_; }
    // A storage buffer of at least `bytes` (grown, never shrunk) bound at `binding`.
    void bind(int index, size_t bytes, int binding);
    void upload(int index, const void* data, size_t bytes, int binding);
    void dispatch(size_t invocations, int localSize);
    void finish();

    std::thread::id owner_;
    GLuint meshSums_ = 0, meshDeltas_ = 0, microPlane_ = 0, microCoarse_ = 0, microFine_ = 0;
    enum Buffer { Pos, Nrm, Partial, Out, Corners, Slots, Heights, Mask, Coords, CoarseCoords, Coarse, BufferCount };
    GLuint buffers_[BufferCount] = {};
    size_t sizes_[BufferCount] = {};
    std::vector<float> stageA_, stageB_, partials_;
    std::vector<int16_t> stageHeights_;
    std::vector<uint8_t> stageMask_;
    std::vector<uint32_t> stageSlots_;
    std::vector<glm::vec4> stageCorners_;
    uint32_t coordsN_ = 0, coarseN_ = 0;
};

} // namespace sf
