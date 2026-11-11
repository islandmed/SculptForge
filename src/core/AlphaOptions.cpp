// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AlphaOptions.h"

#include "core/Image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace sf {

void AlphaOptions::clamp() {
    angle = std::isfinite(angle) ? std::clamp(angle, -180.0f, 180.0f) : 0.0f;
    jitter = std::isfinite(jitter) ? std::clamp(jitter, 0.0f, 180.0f) : 0.0f;
    scaleJitter = std::isfinite(scaleJitter) ? std::clamp(scaleJitter, 0.0f, 90.0f) : 0.0f;
    positionJitter = std::isfinite(positionJitter) ? std::clamp(positionJitter, 0.0f, 100.0f) : 0.0f;
    tileX = std::clamp(tileX, 1, 8);
    tileY = std::clamp(tileY, 1, 8);
}

void AlphaRake::reset(glm::vec2 at) {
    last_ = at;
    angle_ = 0.0f;
    known_ = false;
}

bool AlphaRake::update(glm::vec2 at) {
    const glm::vec2 d = at - last_;
    if (glm::dot(d, d) >= kRakeStep * kRakeStep) {
        // The alpha's top, (-sin a, cos a) with y up, along the motion (d.x, -d.y): a = atan2(-d.x, -d.y).
        angle_ = std::atan2(-d.x, -d.y);
        last_ = at;
        known_ = true;
    }
    return known_;
}

float alphaJitterAngle(float jitterDeg, uint32_t& seed) {
    if (!(jitterDeg > 0.0f)) return 0.0f;
    if (seed == 0) seed = 1;
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    const float r = float(seed & 0xffff) / 65535.0f * 2.0f - 1.0f;
    return glm::radians(jitterDeg) * r;
}

namespace {
float nextUnit(uint32_t& seed) { // -1..1
    if (seed == 0) seed = 1;
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return float(seed & 0xffff) / 65535.0f * 2.0f - 1.0f;
}
} // namespace

AlphaDabJitter alphaDabJitter(const AlphaOptions& o, uint32_t& seed) {
    AlphaDabJitter j;
    if (o.scaleJitter > 0.0f) j.scale = 1.0f + o.scaleJitter * 0.01f * nextUnit(seed);
    if (o.positionJitter > 0.0f) {
        // A point in the disc (rejection-free: a random angle and the square root of a random radius).
        const float a = nextUnit(seed) * 3.14159265f, r = std::sqrt(0.5f + 0.5f * nextUnit(seed));
        j.offset = glm::vec2(std::cos(a), std::sin(a)) * (r * o.positionJitter * 0.01f);
    }
    return j;
}

float alphaDabAngle(const AlphaOptions& o, const AlphaRake& rake, uint32_t& seed) {
    float a = glm::radians(o.angle);
    if (o.followStroke) a += rake.angle();
    return a + alphaJitterAngle(o.jitter, seed);
}

float alphaStampAngle(const AlphaOptions& o, bool followDrag, glm::vec2 drag, float jitter) {
    float a = glm::radians(o.angle) + jitter;
    if (followDrag && glm::dot(drag, drag) > 0.0f) a += std::atan2(-drag.x, -drag.y); // (as AlphaRake::update)
    return a;
}

const std::shared_ptr<const BrushAlpha>& AlphaPool::pick(uint32_t& seed) const {
    if (items_.size() < 2) return items_.front();
    return items_[size_t(0.5f * (nextUnit(seed) + 1.0f) * float(items_.size())) % items_.size()];
}

bool AlphaPool::contains(const std::string& key) const {
    for (const auto& a : items_)
        if (a->path.empty() ? a->name == key : a->path == key) return true;
    return false;
}

std::shared_ptr<const AlphaPool> AlphaPool::with(BrushAlpha a) const {
    auto next = std::make_shared<AlphaPool>(*this);
    if (next->items_.size() >= kMax || next->contains(a.path.empty() ? a.name : a.path)) return next;
    next->items_.push_back(std::make_shared<const BrushAlpha>(std::move(a)));
    return next;
}

std::shared_ptr<const AlphaPool> AlphaPool::fromFolder(const std::string& dir, std::string& error) {
    namespace fs = std::filesystem;
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(std::u8string(dir.begin(), dir.end())), ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().u8string();
        if (name.empty() || name[0] == u8'.' || !it->is_regular_file(ec)) continue;
        const std::u8string ext = it->path().extension().u8string();
        std::string e(ext.begin(), ext.end());
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga")
            files.push_back(std::string(name.begin(), name.end()));
    }
    if (ec) {
        error = "Cannot read the folder: " + dir;
        return nullptr;
    }
    std::sort(files.begin(), files.end()); // A-Z: the same folder gives the same pool, so a stroke repeats
    auto pool = std::make_shared<AlphaPool>();
    for (const std::string& f : files) {
        if (pool->items_.size() >= kMax) break;
        const auto full = (fs::path(std::u8string(dir.begin(), dir.end())) / fs::path(std::u8string(f.begin(), f.end()))).u8string();
        const std::string path(full.begin(), full.end());
        Image img;
        std::string err;
        if (!Image::load(path, img, err)) continue;
        auto stem = fs::path(std::u8string(f.begin(), f.end())).stem().u8string();
        BrushAlpha a = BrushAlpha::fromImage(img, std::string(stem.begin(), stem.end()));
        if (a.empty()) continue;
        a.path = path;
        pool->items_.push_back(std::make_shared<const BrushAlpha>(std::move(a)));
    }
    if (pool->empty()) {
        error = "No images in " + dir + " (png, jpg, bmp or tga)";
        return nullptr;
    }
    return pool;
}

} // namespace sf
