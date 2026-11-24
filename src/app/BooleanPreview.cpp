// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Boolean preview in the view (App.h, boolPreview_). While two or more shown Parts have a Boolean, the view draws their
// result in their place. The result is worked out on another thread from copies of the Parts' shapes, whenever any of
// them changes (its history, its place, its Boolean, or its vertices this frame). While a stroke or a gizmo drag
// changes an operand, runs follow it back to back when they are quick (live); when the last one took longer than a
// frame budget, the Parts are drawn as they are until the edit ends and the exact result of the release lands.

#include "app/App.h"
#include "app/BooleanPreview.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace sf {

namespace {

constexpr double kLiveMs = 120.0; // runs this quick follow an edit live

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

} // namespace

void App::updateBooleanPreview() {
    if (!booleanPreview_) {
        if (boolPreview_ && !boolPreview_->job.valid()) boolPreview_.reset();
        return;
    }
    const std::vector<int> parts = booleanPartIndices(doc_);
    if (parts.size() < 2) {
        if (boolPreview_ && !boolPreview_->job.valid()) boolPreview_.reset();
        else if (boolPreview_) boolPreview_->shown = {};
        return;
    }
    if (!boolPreview_) boolPreview_ = std::make_unique<BooleanPreviewState>();
    BooleanPreviewState& p = *boolPreview_;
    // What the result depends on.
    const bool interacting = drag_ == Drag::Stroke || drag_ == Drag::Gizmo;
    bool editing = false;
    uint64_t sig = 0x51F0B001ull;
    for (int i : parts) {
        const Part& s = doc_.parts[size_t(i)];
        const Mesh& m = doc_.partMesh(i);
        sig = mix(sig, s.id);
        sig = mix(sig, uint64_t(s.boolean));
        sig = mix(sig, doc_.partUndo(i).revision());
        sig = mix(sig, m.vertexCount() * 31 + m.triangleCount());
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) {
                float f = s.transform[c][r];
                uint32_t bits;
                std::memcpy(&bits, &f, sizeof bits);
                sig = mix(sig, bits);
            }
        // (Only the active Part's vertices change under a stroke or a drag; the others' dirty marks wait for an
        // upload that doesn't come while the result stands in for them.)
        if (interacting && doc_.checkedOut(i) && (m.dirtyBegin < m.dirtyEnd || m.topologyDirty)) editing = true;
    }
    if (editing) ++p.edits;
    if (interacting || editing) sig = mix(sig, p.edits);
    // A finished run.
    if (p.job.valid() && p.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        BooleanPreviewResult r = p.job.get();
        p.lastMs = r.ms;
        p.shown = std::move(r);
    }
    const bool slow = p.lastMs > kLiveMs;
    p.raw = interacting && slow && p.shown.signature != sig;
    if (p.job.valid() || sig == p.started) return;
    if (interacting && slow) return; // (the release starts the exact run)
    p.started = sig;
    std::vector<BooleanOperand> ops = booleanOperands(doc_, true);
    BooleanSceneOptions o = booleanOptions_;
    o.weld = true;
    o.voxelResolution = 128; // (the preview's fallback: quick)
    std::vector<uint32_t> ids;
    for (int i : parts) ids.push_back(doc_.parts[size_t(i)].id);
    p.job = std::async(std::launch::async, [ops = std::move(ops), o, sig, ids = std::move(ids)]() mutable {
        BooleanPreviewResult r;
        const auto t0 = std::chrono::steady_clock::now();
        Mesh out;
        BooleanInfo info;
        if (combineOperands(std::move(ops), o, out, r.error, &info)) {
            out.topologyDirty = true;
            r.mesh = std::make_shared<Mesh>(std::move(out));
            r.voxel = info.voxel;
            if (info.voxel) r.error = info.exactError;
        }
        r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        r.signature = sig;
        r.ids = std::move(ids);
        return r;
    });
}

Mesh* App::booleanPreviewMesh() {
    if (!booleanPreview_ || !boolPreview_ || boolPreview_->raw || !boolPreview_->shown.mesh) return nullptr;
    // Only while the Parts it stands for still all take part (one hidden or set to None since: wait for the next run).
    const std::vector<int> parts = booleanPartIndices(doc_);
    if (parts.size() != boolPreview_->shown.ids.size()) return nullptr;
    for (size_t k = 0; k < parts.size(); ++k)
        if (doc_.parts[size_t(parts[k])].id != boolPreview_->shown.ids[k]) return nullptr;
    return boolPreview_->shown.mesh.get();
}

bool App::booleanPreviewDrawn(uint32_t partId) const {
    if (!booleanPreview_ || !boolPreview_ || boolPreview_->raw || !boolPreview_->shown.mesh) return false;
    const auto& ids = boolPreview_->shown.ids;
    return std::find(ids.begin(), ids.end(), partId) != ids.end();
}

} // namespace sf
