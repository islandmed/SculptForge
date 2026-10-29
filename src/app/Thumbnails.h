// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thumbnails for brushes, alphas, materials and files, rendered offscreen and
// cached in memory and on disk. Calls never block: they return 0 until the
// image is ready, and pump() does a little work each frame.
//
// Workers sculpt the brush previews (one S-stroke with the real brush code on the
// front of a fresh clay ball, every brush framed and lit alike), load meshes and
// decode images; the main thread then renders at most two meshes per frame. Finished thumbnails are written to
// <cache dir>/<fnv1a64 key>.png, and at most 256 textures stay in GPU memory.
#pragma once

#include "core/BrushAlpha.h"
#include "core/BrushRecipe.h"
#include "core/Image.h"
#include "core/Matcap.h"
#include "core/Mesh.h"
#include "core/Vdm.h"

#include <imgui.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sf {

class Renderer;

class ThumbnailCache {
public:
    static constexpr int kPixels = 192; // rendered size (shown at 96, crisp on HiDPI)
    static constexpr size_t kMaxTextures = 256;

    ThumbnailCache();
    ~ThumbnailCache();
    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    ImTextureID brush(int brushType); // BrushType, or kPaintBrush (AssetLibrary.h)
    ImTextureID recipe(const BrushRecipe& recipe);
    ImTextureID alpha(const BrushAlpha& alpha);
    ImTextureID vdm(const std::shared_ptr<const VectorDisplacement>& vdm); // one stamp of the map on a plane
    ImTextureID material(int matcap);     // -1 = Studio
    ImTextureID primitive(int primitive); // Primitive (AssetLibrary.h)
    // Projects (their embedded thumbnail.png, else rendered once), meshes and images.
    ImTextureID file(const std::string& path);
    // Every frame on the main thread with the GL context current.
    void pump(Renderer& renderer);

    void setCacheDir(std::string dir);     // "" = memory only (UI tests, renders)
    void refresh(const std::string& path); // forget a file's thumbnail (memory and disk) and make it again
    bool busy() const;                     // work queued or running (UI tests: wait_idle)
    void clear();                          // frees every texture (needs the GL context)

private:
    struct Result {
        Image image;                // ready to upload (cache hit, image file, alpha, project thumbnail)
        std::unique_ptr<Mesh> mesh; // needs a render
        int matcap = defaultMatcap(); // -1 = Studio
        float yaw = 30.0f, pitch = -15.0f; // degrees
        float zoom = 0.92f;                // camera distance relative to framing the whole mesh
        float distance = 0.0f;             // > 0: this camera distance from the origin instead
        std::string label;                 // for SCULPTFORGE_THUMBNAIL_CACHE debugging
        bool polypaint = false;
        bool store = true; // write to the disk cache
    };
    using Producer = std::function<Result()>;
    enum class State { Queued, Running, Rendering, Ready, Failed };
    struct Entry {
        State state = State::Queued;
        unsigned tex = 0;
        uint64_t used = 0;
    };
    ImTextureID request(const std::string& key, Producer produce, bool diskCache);
    void upload(const std::string& key, const Image& image);
    void evict();

    std::unordered_map<std::string, Entry> entries_;
    std::deque<std::pair<std::string, Producer>> queue_; // newest first: what is on screen now
    struct Job {
        std::string key;
        std::future<Result> result;
    };
    std::vector<Job> jobs_;
    std::deque<std::pair<std::string, Result>> renders_;
    std::vector<std::future<void>> writes_;
    std::string cacheDir_;
    uint64_t frame_ = 0;
    struct Stat {
        uint64_t size = 0;
        int64_t mtime = 0;
        double at = -1e9;
        bool exists = false;
    };
    std::unordered_map<std::string, Stat> stats_; // file stats, refreshed every 2 s
};

} // namespace sf
