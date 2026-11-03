// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Thumbnails.h"

#include "app/AssetLibrary.h"
#include "app/BrushPreview.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Matcap.h"
#include "core/PathStroke.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Sculpt.h"
#include "render/Camera.h"
#include "render/GL.h"
#include "render/Renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <system_error>

namespace sf {

namespace {

constexpr size_t kMaxJobs = 3;       // worker threads at a time
constexpr int kRendersPerFrame = 2;  // GL renders per frame
constexpr double kStatSeconds = 2.0; // how long a file's size / time stamp is trusted
// Alpha previews: the colour of no effect (the tile's charcoal) and of full effect (Soft Clay's lit grey), sRGB.
constexpr float kAlphaNone[3] = {30.0f, 30.0f, 33.0f};
constexpr float kAlphaFull[3] = {214.0f, 208.0f, 198.0f};

std::filesystem::path fsPath(const std::string& p) { return std::filesystem::path(std::u8string(p.begin(), p.end())); }

template <class T> bool ready(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

ImTextureID texId(unsigned tex) { return ImTextureID(uintptr_t(tex)); }

constexpr float kBallPitch = -8.0f; // degrees; a touch from above
constexpr float kBallFill = 0.97f;  // the ball's share of the tile's height: framed tight, so the mark reads at shelf size

// Brush previews: the camera distance at which the ball fills kBallFill of the tile.
float ballDistance() {
    const float fovY = Camera().fovY;
    return kBallRadius / (kBallFill * std::tan(fovY * 0.5f));
}

} // namespace

ThumbnailCache::ThumbnailCache() = default;

ThumbnailCache::~ThumbnailCache() {
    // Textures die with the GL context (App::shutdown calls clear() while it is current).
    for (auto& j : jobs_)
        if (j.result.valid()) j.result.wait();
    for (auto& w : writes_)
        if (w.valid()) w.wait();
}

void ThumbnailCache::clear() {
    for (auto& [key, e] : entries_)
        if (e.tex) gl::DeleteTextures(1, &e.tex);
    entries_.clear();
    queue_.clear();
    renders_.clear();
}

void ThumbnailCache::setCacheDir(std::string dir) {
    cacheDir_ = std::move(dir);
    if (cacheDir_.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(fsPath(cacheDir_), ec);
    if (ec) {
        cacheDir_.clear();
        return;
    }
    // Keep the disk cache bounded: the least recently written thumbnails go first.
    writes_.push_back(std::async(std::launch::async, [dir = cacheDir_] {
        constexpr size_t kKeep = 2000;
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
        std::error_code e;
        for (std::filesystem::directory_iterator it(fsPath(dir), e), end; !e && it != end; it.increment(e))
            if (it->path().extension() == ".png") files.emplace_back(it->last_write_time(e), it->path());
        if (files.size() <= kKeep) return;
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = kKeep; i < files.size(); ++i)
            std::filesystem::remove(files[i].second, e);
    }));
}

bool ThumbnailCache::busy() const { return !queue_.empty() || !jobs_.empty() || !renders_.empty(); }

ImTextureID ThumbnailCache::request(const std::string& key, Producer produce, bool diskCache) {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        it->second.used = frame_;
        return it->second.state == State::Ready ? texId(it->second.tex) : ImTextureID(0);
    }
    Entry& e = entries_[key];
    e.used = frame_;
    if (diskCache && !cacheDir_.empty()) {
        // Try the disk cache first; fall back to making it.
        const std::string cached = thumbnailCachePath(cacheDir_, key);
        produce = [cached, make = std::move(produce)]() {
            Result r;
            std::string err;
            std::error_code ec;
            if (std::filesystem::is_regular_file(fsPath(cached), ec) && Image::load(cached, r.image, err)) {
                r.store = false;
                return r;
            }
            return make();
        };
    } else if (!diskCache) {
        produce = [make = std::move(produce)]() {
            Result r = make();
            r.store = false;
            return r;
        };
    }
    queue_.emplace_front(key, std::move(produce));
    return ImTextureID(0);
}

ImTextureID ThumbnailCache::brush(int brushType) {
    const std::string key = "brush|" + std::to_string(brushType) + "|" + std::to_string(kThumbnailVersion);
    return request(
        hex64(fnv1a64(key)),
        [brushType] {
            Result r;
            r.mesh = sculptBrushPreview(nullptr, brushType);
            r.label = "brush-" + std::to_string(brushType);
            r.yaw = 0.0f;
            r.pitch = kBallPitch;
            r.distance = ballDistance();
            r.polypaint = brushType == kPaintBrush;
            return r;
        },
        true);
}

ImTextureID ThumbnailCache::recipe(const BrushRecipe& recipe) {
    const std::string key = "recipe|" + recipe.toJson().dump() + "|" + std::to_string(kThumbnailVersion);
    return request(
        hex64(fnv1a64(key)),
        [recipe] {
            Result r;
            r.mesh = sculptBrushPreview(&recipe, 0);
            r.label = "recipe-" + recipe.name;
            r.yaw = 0.0f;
            r.pitch = kBallPitch;
            r.distance = ballDistance();
            return r;
        },
        true);
}

ImTextureID ThumbnailCache::vdm(const std::shared_ptr<const VectorDisplacement>& vdm) {
    if (!vdm || vdm->empty()) return ImTextureID(0);
    // Name, size and a sparse sample of the vectors (as alpha() does): cheap enough every frame.
    uint64_t h = fnv1a64("vdm|" + vdm->name + "|" + std::to_string(vdm->width) + "x" + std::to_string(vdm->height));
    const size_t n = vdm->vectors.size();
    for (size_t i = 0; i < 97; ++i) {
        const glm::vec3 v = vdm->vectors[i * n / 97];
        h = fnv1a64(std::string_view(reinterpret_cast<const char*>(&v), sizeof v), h);
    }
    return request(
        "vdm|" + hex64(h) + "|" + std::to_string(kThumbnailVersion),
        [vdm] {
            // One stamp on a fine plane, seen from above at an angle (the stamp's top is away from the camera).
            Result r;
            Document d;
            d.setMesh(makePlane(200, 2.6f));
            d.brushVdm = vdm;
            PathStroke st;
            st.points = {glm::vec3(0.0f, 1.0f, 0.0f)};
            st.view = glm::vec3(0.0f, -1.0f, 0.0f);
            st.radius = 0.62f;
            st.strength = 0.5f;
            st.curve = false;
            std::string err;
            strokePath(d, nullptr, BrushType::Vdm, st, err);
            r.mesh = sceneMesh(d);
            r.pitch = -40.0f;
            r.yaw = 20.0f;
            r.distance = 2.3f;
            r.label = "vdm " + vdm->name;
            return r;
        },
        false);
}

ImTextureID ThumbnailCache::alpha(const BrushAlpha& alpha) {
    // Name, size and a sparse sample of the values: cheap enough to compute every frame.
    uint64_t h = fnv1a64(alpha.name + "|" + std::to_string(alpha.width) + "x" + std::to_string(alpha.height));
    const size_t n = alpha.values.size();
    for (size_t i = 0; i < 64 && n; ++i) {
        const float v = alpha.values[i * n / 64];
        h = fnv1a64(std::string_view(reinterpret_cast<const char*>(&v), sizeof v), h);
    }
    return request(
        "alpha|" + hex64(h),
        [alpha] {
            // The stamp as a height map: full effect light, none dark. Clay grey on the tile's charcoal rather than
            // pure white on black, so it doesn't outshine the model and the rest of the tray.
            Result r;
            constexpr int S = 128;
            r.image.width = r.image.height = S;
            r.image.rgba.resize(size_t(S) * S * 4);
            const BrushAlpha shown = alpha.empty() ? BrushAlpha::round() : alpha;
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x) {
                    const float u = (float(x) + 0.5f) / S * 2.0f - 1.0f, v = 1.0f - (float(y) + 0.5f) / S * 2.0f;
                    float g = shown.empty() ? brushFalloff(std::sqrt(u * u + v * v), 1.0f) : shown.sample(u, v);
                    const float t = std::clamp(g, 0.0f, 1.0f);
                    uint8_t* p = &r.image.rgba[(size_t(y) * S + size_t(x)) * 4];
                    for (int k = 0; k < 3; ++k) p[k] = uint8_t(kAlphaNone[k] + (kAlphaFull[k] - kAlphaNone[k]) * t + 0.5f);
                    p[3] = 255;
                }
            return r;
        },
        false);
}

ImTextureID ThumbnailCache::material(int matcap) {
    // By name: the built-in list can grow or change order between versions.
    const std::string key =
        "material|" + (matcap < 0 ? std::string("Studio") : matcapKey(matcap)) + "|" + std::to_string(kThumbnailVersion);
    return request(
        hex64(fnv1a64(key)),
        [matcap] {
            Result r;
            r.mesh = std::make_unique<Mesh>(makeIcosphere(5, kBallRadius));
            r.matcap = matcap;
            r.yaw = 0.0f;
            r.pitch = 0.0f;
            r.distance = ballDistance(); // the same ball, framed as the brush previews are
            return r;
        },
        true);
}

ImTextureID ThumbnailCache::primitive(int primitive) {
    const std::string key = "primitive|" + std::to_string(primitive) + "|" + std::to_string(kThumbnailVersion);
    return request(
        hex64(fnv1a64(key)),
        [primitive] {
            Result r;
            switch (Primitive(primitive)) {
            case Primitive::Cube:
                r.mesh = std::make_unique<Mesh>(makeCube(24, 1.4f));
                break;
            case Primitive::Plane:
                r.mesh = std::make_unique<Mesh>(makePlane(32, 2.0f));
                r.pitch = -35.0f;
                break;
            default:
                r.mesh = std::make_unique<Mesh>(makeIcosphere(5, 1.0f));
                break;
            }
            return r;
        },
        true);
}

ImTextureID ThumbnailCache::file(const std::string& path) {
    Stat& s = stats_[path];
    const double now = ImGui::GetTime();
    if (now - s.at > kStatSeconds) {
        std::error_code ec;
        s.at = now;
        s.exists = std::filesystem::is_regular_file(fsPath(path), ec);
        s.size = s.exists ? uint64_t(std::filesystem::file_size(fsPath(path), ec)) : 0;
        s.mtime = modifiedTime(path);
    }
    if (!s.exists) return ImTextureID(0);
    const bool image = isImageFile(path), project = isProjectFile(path);
    const std::string kind = project ? "project" : image ? "image" : "mesh";
    const std::string key = fileThumbnailKey(kind, path, s.size, s.mtime, kPixels);
    return request(
        key,
        [path, image, project] {
            Result r;
            std::string err;
            if (image) {
                Image full;
                if (Image::load(path, full, err)) r.image = full.downscaled(kPixels);
                return r;
            }
            if (project && loadProjectThumbnail(path, r.image, err)) return r;
            // Meshes (and projects saved without a thumbnail): load on this worker, render on the main thread.
            Document scratch;
            if (CommandRegistry().execute(scratch, "open", {{"path", path}}).ok) r.mesh = sceneMesh(scratch);
            r.polypaint = true;
            return r;
        },
        true);
}

void ThumbnailCache::refresh(const std::string& path) {
    auto s = stats_.find(path);
    if (s == stats_.end()) return;
    const std::string kind = isProjectFile(path) ? "project" : isImageFile(path) ? "image" : "mesh";
    const std::string key = fileThumbnailKey(kind, path, s->second.size, s->second.mtime, kPixels);
    if (!cacheDir_.empty()) {
        std::error_code ec;
        std::filesystem::remove(fsPath(thumbnailCachePath(cacheDir_, key)), ec);
    }
    if (auto e = entries_.find(key); e != entries_.end() && e->second.state != State::Running) {
        if (e->second.tex) gl::DeleteTextures(1, &e->second.tex);
        entries_.erase(e);
    }
    stats_.erase(s);
}

void ThumbnailCache::upload(const std::string& key, const Image& image) {
    auto it = entries_.find(key);
    if (it == entries_.end()) return; // evicted meanwhile
    Entry& e = it->second;
    if (image.empty()) {
        e.state = State::Failed;
        return;
    }
    if (!e.tex) gl::GenTextures(1, &e.tex);
    gl::BindTexture(GL_TEXTURE_2D, e.tex);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   image.rgba.data());
    gl::BindTexture(GL_TEXTURE_2D, 0);
    e.state = State::Ready;
}

void ThumbnailCache::evict() {
    size_t live = 0;
    for (const auto& [k, e] : entries_)
        live += e.tex != 0;
    if (live <= kMaxTextures) return;
    std::vector<std::pair<uint64_t, std::string>> byAge;
    for (const auto& [k, e] : entries_)
        if (e.tex && e.used + 2 < frame_) byAge.emplace_back(e.used, k);
    std::sort(byAge.begin(), byAge.end());
    for (size_t i = 0; i < byAge.size() && live > kMaxTextures; ++i, --live) {
        auto it = entries_.find(byAge[i].second);
        gl::DeleteTextures(1, &it->second.tex);
        entries_.erase(it); // made again (from the disk cache) when shown again
    }
}

void ThumbnailCache::pump(Renderer& renderer) {
    ++frame_;
    // Requests not seen for a while (scrolled away, picker closed) leave the queue.
    for (auto it = queue_.begin(); it != queue_.end();) {
        auto e = entries_.find(it->first);
        if (e == entries_.end() || e->second.used + 30 < frame_) {
            if (e != entries_.end()) entries_.erase(e);
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }
    while (jobs_.size() < kMaxJobs && !queue_.empty()) {
        auto [key, produce] = std::move(queue_.front());
        queue_.pop_front();
        entries_[key].state = State::Running;
        jobs_.push_back({key, std::async(std::launch::async, std::move(produce))});
    }
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (!ready(it->result)) {
            ++it;
            continue;
        }
        Result r = it->result.get();
        if (r.mesh) {
            if (auto e = entries_.find(it->key); e != entries_.end()) e->second.state = State::Rendering;
            renders_.emplace_back(it->key, std::move(r));
        } else {
            upload(it->key, r.image);
            if (r.store && !r.image.empty() && !cacheDir_.empty())
                writes_.push_back(
                    std::async(std::launch::async, [path = thumbnailCachePath(cacheDir_, it->key), img = r.image] {
                        std::string err;
                        img.savePng(path, err);
                    }));
        }
        it = jobs_.erase(it);
    }
    for (int n = 0; n < kRendersPerFrame && !renders_.empty(); ++n) {
        auto [key, r] = std::move(renders_.front());
        renders_.pop_front();
        Camera cam;
        cam.aspect = 1.0f;
        cam.yaw = glm::radians(r.yaw);
        cam.pitch = glm::radians(r.pitch);
        cam.frame(r.mesh->bounds());
        cam.distance *= r.zoom;
        if (r.distance > 0.0f) {
            cam.target = glm::vec3(0.0f);
            cam.distance = r.distance;
        }
        RenderSettings settings;
        settings.matcap = r.matcap;
        settings.showPolypaint = r.polypaint;
        settings.showMask = settings.showPolygroups = false;
        settings.dimInactive = false;
        Image img;
        img.width = img.height = kPixels;
        std::string err;
        if (!renderer.renderMeshThumbnail(*r.mesh, cam, kPixels, settings, img.rgba, err)) img = Image();
        upload(key, img);
        if (!r.label.empty() && !img.empty() && SDL_getenv("SCULPTFORGE_THUMBNAIL_CACHE") && !cacheDir_.empty()) {
            std::string e;
            img.savePng(cacheDir_ + "/" + r.label + ".png", e);
        }
        if (r.store && !img.empty() && !cacheDir_.empty())
            writes_.push_back(std::async(std::launch::async, [path = thumbnailCachePath(cacheDir_, key), img] {
                std::string e;
                img.savePng(path, e);
            }));
    }
    writes_.erase(std::remove_if(writes_.begin(), writes_.end(), [](const std::future<void>& f) { return ready(f); }),
                  writes_.end());
    evict();
}

} // namespace sf
