// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Demo.h"

#include "app/App.h"
#include "app/Ui.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <chrono>
#include <future>
#include <map>
#include <memory>

// ---------------------------------------------------------------------------
// Dear ImGui test-engine hooks: record where every labelled item was drawn.
// ---------------------------------------------------------------------------

namespace {

struct Placed {
    ImRect rect;
    ImRect clip; // the part of the screen the item's window shows
};

struct ItemTable {
    std::map<ImGuiID, Placed> rects;
    std::map<std::string, Placed> byLabel;
    // Every item drawn this frame with the ID-stack seed it was created under, so
    // items that never report a label (combo boxes) can be matched by hashing one.
    struct Seeded {
        ImGuiID id, seed;
        std::string window;
        Placed placed;
        std::vector<std::string> scopes;
    };
    std::vector<Seeded> frame;
    std::vector<std::string> scopes; // Demo::pushScope: panels drawn inside the shell's trays and drop-downs
};
ItemTable& items() {
    static ItemTable t;
    return t;
}

} // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*) {
    sf::ui::NoteItem(ctx, id, bb.Min, bb.Max); // the hover and focus rings (Ui.cpp)
    ImRect clip = bb;
    if (ctx && ctx->CurrentWindow) {
        clip = ctx->CurrentWindow->ClipRect;
        clip.ClipWith(ImRect(ImVec2(0, 0), ctx->IO.DisplaySize));
    }
    items().rects[id] = {bb, clip};
    if (id && ctx && ctx->CurrentWindow && !ctx->CurrentWindow->IDStack.empty()) {
        std::string window = ctx->CurrentWindow->RootWindow->Name; // a child (list, table) counts as its panel
        if (const size_t hash = window.find("##"); hash != std::string::npos) window.resize(hash);
        items().frame.push_back({id, ctx->CurrentWindow->IDStack.back(), std::move(window), {bb, clip}, items().scopes});
    }
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    sf::ui::NoteItemLabel(id);
    auto it = items().rects.find(id);
    if (!label || it == items().rects.end()) return;
    std::string text(label);
    std::string window = ctx && ctx->CurrentWindow ? ctx->CurrentWindow->RootWindow->Name : "";
    if (const size_t hash = window.find("##"); hash != std::string::npos) window.resize(hash);
    auto add = [&](const std::string& key) {
        items().byLabel[key] = it->second;
        if (!window.empty()) items().byLabel[window + "/" + key] = it->second;
        for (const auto& scope : items().scopes) items().byLabel[scope + "/" + key] = it->second;
    };
    add(text);
    if (const size_t hash = text.find("##"); hash != std::string::npos && hash > 0) add(text.substr(0, hash));
    // Labels that start with an icon (ICON_LC_*: UTF-8 private-use characters, U+E000-U+F8FF) are also found
    // by their words: "AI Brush" for "<sparkles> AI Brush".
    if (text.size() > 3 && static_cast<unsigned char>(text[0]) == 0xEE) {
        size_t start = 3;
        while (start < text.size() && text[start] == ' ') ++start;
        const std::string words = text.substr(start);
        if (!words.empty() && words.compare(0, 2, "##") != 0) {
            add(words);
            if (const size_t hash = words.find("##"); hash != std::string::npos && hash > 0) add(words.substr(0, hash));
        }
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

namespace sf {

namespace {

template <class T> bool ready(const std::future<T>& f) {
    return !f.valid() || f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

float smooth(float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

// Point on a sphere of radius `r` from latitude/longitude in degrees (front = +Z).
glm::vec3 onSphere(float latDeg, float lonDeg, float r = 1.0f) {
    const float lat = glm::radians(latDeg), lon = glm::radians(lonDeg);
    return r * glm::vec3(std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon));
}

} // namespace

void Demo::beginFrame() {
    items().byLabel.clear();
    items().frame.clear();
    items().scopes.clear();
}

void Demo::pushScope(const char* name) { items().scopes.emplace_back(name); }

void Demo::popScope() {
    if (!items().scopes.empty()) items().scopes.pop_back();
}

std::vector<std::string> Demo::similarItems(const std::string& label) {
    auto lower = [](std::string s) {
        for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::string want = lower(label);
    if (const size_t hash = want.find_first_not_of('#'); hash != std::string::npos) want = want.substr(hash);
    std::vector<std::string> out;
    for (const auto& [name, rect] : items().byLabel)
        if (lower(name).find(want) != std::string::npos && out.size() < 8) out.push_back(name);
    return out;
}

bool Demo::findItem(const std::string& label, ImVec2& center, bool* visible, ImVec4* clipOut) const {
    auto found = [&](const Placed& p) {
        center = p.rect.GetCenter();
        if (visible) *visible = p.clip.Contains(center);
        if (clipOut) *clipOut = ImVec4(p.clip.Min.x, p.clip.Min.y, p.clip.Max.x, p.clip.Max.y);
        return true;
    };
    auto it = items().byLabel.find(label);
    if (it != items().byLabel.end()) return found(it->second);
    // Unlabelled items: hash the label the way ImGui's GetID() would.
    std::string window, name = label;
    if (const size_t slash = label.find('/'); slash != std::string::npos && slash > 0 && label.compare(0, 2, "##") != 0) {
        window = label.substr(0, slash);
        name = label.substr(slash + 1);
    }
    for (auto s = items().frame.rbegin(); s != items().frame.rend(); ++s) {
        if (!window.empty() && s->window != window &&
            std::find(s->scopes.begin(), s->scopes.end(), window) == s->scopes.end())
            continue;
        if (ImHashStr(name.c_str(), 0, s->seed) == s->id) return found(s->placed);
    }
    return false;
}

std::vector<std::string> Demo::names() { return {"showcase", "sculpt", "image-to-3d", "brush-lab", "game-ready", "splats"}; }

bool Demo::load(const std::string& name, std::string& error) {
    if (name == "showcase") buildShowcase();
    else if (name == "sculpt") buildSculpt();
    else if (name == "image-to-3d") buildImageTo3D();
    else if (name == "brush-lab") buildBrushLab();
    else if (name == "game-ready") buildGameReady();
    else if (name == "splats") buildSplats();
    else {
        error = "Unknown demo '" + name + "'";
        return false;
    }
    wait(1.0f); // hold the last frame
    return true;
}

void Demo::update(float dt) {
    clock_ += dt;
    if (ImGuiWindow* w = ImGui::FindWindowByName("Brush Lab"); w && std::getenv("SF_DEMO_TRACE"))
        SDL_Log("t=%.2f step=%zu brushlab=(%.0f,%.0f) mouse=(%.0f,%.0f) down=%d", clock_, step_, w->Pos.x, w->Pos.y, mouse_.x,
                mouse_.y, int(buttonDown_));
    stride_ = 1;
    while (step_ < steps_.size()) {
        if (!steps_[step_](stepTime_)) {
            stepTime_ += dt;
            return;
        }
        ++step_;
        stepTime_ = 0.0f;
    }
}

// ---------------------------------------------------------------- input

void Demo::mouseMotion(float x, float y, SDL_MouseID which) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(app_.window_);
    e.motion.which = which;
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = x - mouse_.x;
    e.motion.yrel = y - mouse_.y;
    e.motion.state = buttonDown_ ? SDL_BUTTON_LMASK : 0;
    mouse_ = ImVec2(x, y);
    app_.handleEvent(e);
}

void Demo::mouseButton(bool down, int button, SDL_MouseID which, int clicks) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = SDL_GetWindowID(app_.window_);
    e.button.which = which;
    e.button.button = Uint8(button);
    e.button.down = down;
    e.button.clicks = Uint8(clicks);
    e.button.x = mouse_.x;
    e.button.y = mouse_.y;
    if (button == SDL_BUTTON_LEFT) buttonDown_ = down;
    app_.handleEvent(e);
}

ImVec2 Demo::project(const glm::vec3& p) const {
    const CanvasRect r = app_.canvasRect();
    const glm::vec4 c = app_.camera_.projection() * app_.camera_.view() * glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(c) / c.w;
    return ImVec2(r.x + (ndc.x * 0.5f + 0.5f) * r.w, r.y + (0.5f - ndc.y * 0.5f) * r.h);
}

ImVec2 Demo::onModel(float u, float v) const {
    const AABB b = app_.doc_.mesh.bounds();
    ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c((i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z);
        const ImVec2 p = project(c);
        lo = ImVec2(std::min(lo.x, p.x), std::min(lo.y, p.y));
        hi = ImVec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
    }
    ImVec2 p(lo.x + (hi.x - lo.x) * u, lo.y + (hi.y - lo.y) * v);
    const ImVec2 centre((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
    for (int i = 0; i < 20; ++i) { // walk towards the centre until the ray hits the sculpt
        RayHit hit;
        if (app_.pick(p.x, p.y, hit)) break;
        p = ImVec2(p.x + (centre.x - p.x) * 0.15f, p.y + (centre.y - p.y) * 0.15f);
    }
    return p;
}

// ---------------------------------------------------------------- virtual camera

namespace {
float smoother(float u) { // ease in and out with zero acceleration at both ends
    u = std::clamp(u, 0.0f, 1.0f);
    return u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
}
} // namespace

ImVec2 Demo::viewCenter(const View& v) const {
    const ImVec2 size(float(app_.options_.width), float(app_.options_.height));
    ImVec2 c = v.center.x < 0 ? ImVec2(size.x * 0.5f, size.y * 0.5f) : v.center;
    // Keep the shown rectangle inside the window.
    const float hw = size.x * 0.5f / v.zoom, hh = size.y * 0.5f / v.zoom;
    return ImVec2(std::clamp(c.x, hw, size.x - hw), std::clamp(c.y, hh, size.y - hh));
}

void Demo::view(ImVec2& center, float& zoom) const {
    const float u = view_.duration > 0.0f ? smoother((clock_ - view_.start) / view_.duration) : 1.0f;
    const float z0 = std::max(view_.from.zoom, 1.0f), z1 = std::max(view_.to.zoom, 1.0f);
    // Zoom in log space so pushing in and pulling out feel equally even; the centre follows the shown rectangle.
    zoom = z0 * std::pow(z1 / z0, u);
    const ImVec2 a = viewCenter(view_.from), b = viewCenter(view_.to);
    const float w = std::abs(1.0f / z1 - 1.0f / z0) > 1e-4f ? (1.0f / zoom - 1.0f / z0) / (1.0f / z1 - 1.0f / z0) : u;
    center = ImVec2(a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w);
}

void Demo::zoomTo(ImVec2 center, float zoom, float seconds) {
    zoomTo([center] { return center; }, zoom, seconds);
}

void Demo::zoomTo(std::function<ImVec2()> where, float zoom, float seconds) {
    steps_.push_back([this, where, zoom, seconds](float) {
        View now;
        view(now.center, now.zoom);
        view_ = {now, {where(), std::max(zoom, 1.0f)}, clock_, seconds};
        return true;
    });
}

void Demo::zoomToItem(const std::string& label, float zoom, float seconds, ImVec2 offset) {
    steps_.push_back([this, label, zoom, seconds, offset](float t) {
        ImVec2 c;
        if (!findItem(label, c)) {
            if (t > 3.0f) SDL_Log("Demo: no UI item \"%s\" to zoom to", label.c_str());
            return t > 3.0f;
        }
        const float k = float(app_.options_.height) / 1080.0f;
        View now;
        view(now.center, now.zoom);
        view_ = {now, {ImVec2(c.x + offset.x * k, c.y + offset.y * k), std::max(zoom, 1.0f)}, clock_, seconds};
        return true;
    });
}

void Demo::fadeTo(float black, float seconds) {
    steps_.push_back([this, black, seconds](float) {
        fadeFrom_ = fade();
        fadeTo_ = black;
        fadeStart_ = clock_;
        fadeDuration_ = seconds;
        return true;
    });
}

float Demo::fade() const {
    const float u = fadeDuration_ > 0.0f ? smooth((clock_ - fadeStart_) / fadeDuration_) : 1.0f;
    return fadeFrom_ + (fadeTo_ - fadeFrom_) * u;
}

// ---------------------------------------------------------------- building blocks

void Demo::wait(float seconds) {
    steps_.push_back([seconds](float t) { return t >= seconds; });
}

void Demo::caption(const std::string& title, const std::string& subtitle, float seconds, bool top) {
    steps_.push_back([this, title, subtitle, seconds, top](float) {
        captions_.push_back({title, subtitle, clock_, seconds, top});
        return true;
    });
}

void Demo::moveTo(float x, float y, float seconds) {
    auto from = std::make_shared<ImVec2>();
    steps_.push_back([this, x, y, seconds, from](float t) {
        if (t == 0.0f) *from = mouse_;
        const float u = smooth(t / seconds);
        mouseMotion(from->x + (x - from->x) * u, from->y + (y - from->y) * u);
        return t >= seconds;
    });
}

void Demo::moveToItem(const std::string& label, float seconds) {
    auto from = std::make_shared<ImVec2>();
    steps_.push_back([this, label, seconds, from](float t) {
        ImVec2 to;
        if (!findItem(label, to)) { // not on screen yet: give the UI a moment
            if (t > 3.0f) SDL_Log("Demo: no UI item \"%s\" to move to", label.c_str());
            return t > 3.0f;
        }
        if (t == 0.0f || (from->x == 0 && from->y == 0)) *from = mouse_;
        const float u = smooth(t / seconds);
        mouseMotion(from->x + (to.x - from->x) * u, from->y + (to.y - from->y) * u);
        return t >= seconds;
    });
}

void Demo::click(const std::string& label) {
    moveToItem(label);
    steps_.push_back([this](float t) {
        if (t == 0.0f) mouseButton(true, SDL_BUTTON_LEFT);
        if (t >= 0.08f) {
            mouseButton(false, SDL_BUTTON_LEFT);
            return true;
        }
        return false;
    });
    wait(0.25f);
}

void Demo::stroke(std::function<ImVec2(float u)> path, float seconds, bool invert) {
    // Glide to the start, press, follow the path, release.
    auto started = std::make_shared<bool>(false);
    auto from = std::make_shared<ImVec2>();
    steps_.push_back([this, path, from](float t) {
        const ImVec2 p = path(0.0f);
        const float u = smooth(t / 0.35f);
        if (t == 0.0f) *from = mouse_;
        mouseMotion(from->x + (p.x - from->x) * u, from->y + (p.y - from->y) * u);
        return t >= 0.35f;
    });
    steps_.push_back([this, path, seconds, invert, started](float t) {
        if (!*started) {
            app_.invertToggle_ = invert;
            mouseButton(true, SDL_BUTTON_LEFT);
            *started = true;
        }
        const ImVec2 p = path(std::min(t / seconds, 1.0f));
        mouseMotion(p.x, p.y);
        if (t >= seconds) {
            mouseButton(false, SDL_BUTTON_LEFT);
            app_.invertToggle_ = false;
            *started = false;
            return true;
        }
        return false;
    });
}

void Demo::orbit(float dx, float dy, float seconds) {
    auto last = std::make_shared<float>(0.0f); // eased progress at the previous frame (any frame rate)
    auto at = std::make_shared<ImVec2>();
    steps_.push_back([this, dx, dy, seconds, last, at](float t) {
        // Drag from the cursor, or from a corner of the viewport while the cursor is parked off screen.
        auto button = [this, at](bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.windowID = SDL_GetWindowID(app_.window_);
            e.button.button = SDL_BUTTON_RIGHT;
            e.button.down = down;
            e.button.clicks = 1;
            e.button.x = at->x;
            e.button.y = at->y;
            app_.handleEvent(e);
        };
        if (t == 0.0f) {
            const CanvasRect r = app_.canvasRect();
            const bool inside = mouse_.x >= r.x && mouse_.y >= r.y && mouse_.x < r.x + r.w && mouse_.y < r.y + r.h;
            *at = inside ? mouse_ : ImVec2(r.x + r.w * 0.04f, r.y + r.h * 0.5f);
            app_.hideBrushCursor_ = !inside;
            *last = 0.0f;
            SDL_Event move{}; // hover the viewport first, so the UI under the old position lets go
            move.type = SDL_EVENT_MOUSE_MOTION;
            move.motion.windowID = SDL_GetWindowID(app_.window_);
            move.motion.x = at->x;
            move.motion.y = at->y;
            app_.handleEvent(move);
            return false; // press on the next frame, once the UI has seen the hover
        }
        if (*last == 0.0f && !buttonHeld_) {
            buttonHeld_ = true;
            button(true);
        }
        const float u0 = *last, u1 = smooth(t / seconds);
        *last = u1;
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = SDL_GetWindowID(app_.window_);
        e.motion.x = at->x;
        e.motion.y = at->y;
        e.motion.xrel = dx * (u1 - u0);
        e.motion.yrel = dy * (u1 - u0);
        e.motion.state = SDL_BUTTON_RMASK;
        app_.handleEvent(e);
        if (t >= seconds) {
            button(false);
            buttonHeld_ = false;
            app_.hideBrushCursor_ = false;
            return true;
        }
        return false;
    });
}

void Demo::type(char* buffer, size_t size, const std::string& text, float seconds) {
    steps_.push_back([buffer, size, text, seconds](float t) {
        const size_t n = std::min(text.size(), size_t(float(text.size()) * std::min(t / seconds, 1.0f)));
        std::snprintf(buffer, size, "%s", text.substr(0, n).c_str());
        return t >= seconds;
    });
}

void Demo::run(std::function<void()> action) {
    steps_.push_back([action](float) {
        action();
        return true;
    });
}

void Demo::waitUntil(std::function<bool()> done, float timeout, int stride) {
    steps_.push_back([this, done, timeout, stride](float t) {
        stride_ = stride;
        // Real work (network, AI) runs in real time: pace the timeline to the clock so the
        // on-screen "N x speed" label is accurate.
        SDL_Delay(Uint32(1000 / std::max(app_.options_.fps, 1)));
        return done() || t >= timeout;
    });
}

// ---------------------------------------------------------------- overlay

void Demo::drawOverlay() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    // Captions and markers are laid out on the video frame; map them into the window through the virtual camera so
    // they keep their size while the view zooms.
    ImVec2 vc;
    float vz;
    view(vc, vz);
    const float iz = 1.0f / vz;
    auto P = [&](float x, float y) { return ImVec2(vc.x + (x - size.x * 0.5f) * iz, vc.y + (y - size.y * 0.5f) * iz); };

    // Lower-third captions (SF_DEMO_NO_CAPTIONS=1 records a clean take for posters).
    static const bool noCaptions = std::getenv("SF_DEMO_NO_CAPTIONS") != nullptr;
    for (const auto& c : captions_) {
        if (noCaptions) break;
        const float t = clock_ - c.start;
        if (t < 0.0f || t > c.duration) continue;
        const float a = std::min({1.0f, t / 0.35f, (c.duration - t) / 0.35f});
        const float rise = (1.0f - smooth(std::min(t, c.duration - t) / 0.45f)) * size.y * 0.012f; // drifts up in, down out
        ImFont* big = app_.fontCaption_ ? app_.fontCaption_ : ImGui::GetFont();
        ImFont* small = app_.fontCaptionSub_ ? app_.fontCaptionSub_ : ImGui::GetFont();
        const float bigSize = size.y * 0.042f, smallSize = size.y * 0.024f;
        const ImVec2 tsz = big->CalcTextSizeA(bigSize, FLT_MAX, 0.0f, c.title.c_str());
        const ImVec2 ssz = small->CalcTextSizeA(smallSize, FLT_MAX, 0.0f, c.subtitle.c_str());
        const float pad = size.y * 0.022f;
        const float x0 = size.x * 0.5f - std::max(tsz.x, ssz.x) * 0.5f - pad * 1.5f, y0 = size.y * (c.top ? 0.08f : 0.80f) + rise;
        const float x1 = size.x * 0.5f + std::max(tsz.x, ssz.x) * 0.5f + pad * 1.5f,
                    y1 = y0 + tsz.y + (c.subtitle.empty() ? 0.0f : ssz.y + pad * 0.3f) + pad * 2.0f;
        dl->AddRectFilled(P(x0, y0), P(x1, y1), IM_COL32(12, 12, 14, int(215 * a)), pad * 0.6f * iz);
        dl->AddRectFilled(P(x0, y1 - 4.0f), P(x1, y1), IM_COL32(242, 140, 60, int(255 * a)), pad * 0.6f * iz,
                          ImDrawFlags_RoundCornersBottom);
        dl->AddText(big, bigSize * iz, P(size.x * 0.5f - tsz.x * 0.5f, y0 + pad), IM_COL32(255, 255, 255, int(255 * a)),
                    c.title.c_str());
        if (!c.subtitle.empty())
            dl->AddText(small, smallSize * iz, P(size.x * 0.5f - ssz.x * 0.5f, y0 + pad + tsz.y + pad * 0.3f),
                        IM_COL32(205, 205, 210, int(255 * a)), c.subtitle.c_str());
    }

    // Honest time-lapse marker while waiting on a model.
    if (stride_ > 1) {
        char text[48];
        std::snprintf(text, sizeof text, "%dx speed - waiting for the model", stride_);
        ImFont* f = app_.fontCaptionSub_ ? app_.fontCaptionSub_ : ImGui::GetFont();
        const float fs = size.y * 0.022f;
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        const ImVec2 p(size.x - ts.x - size.y * 0.037f, size.y * 0.056f);
        dl->AddRectFilled(P(p.x - fs * 0.6f, p.y - fs * 0.35f), P(p.x + ts.x + fs * 0.6f, p.y + ts.y + fs * 0.35f),
                          IM_COL32(12, 12, 14, 200), fs * 0.35f * iz);
        dl->AddText(f, fs * iz, P(p.x, p.y), IM_COL32(255, 200, 150, 255), text);
    }

    // Cursor arrow (it zooms with the picture, but less, so it never looms).
    const float s = size.y / 1080.0f * 1.15f / std::sqrt(vz);
    const ImVec2 m = mouse_;
    const ImVec2 pts[] = {ImVec2(m.x, m.y), ImVec2(m.x, m.y + 22 * s), ImVec2(m.x + 5.5f * s, m.y + 16.5f * s),
                          ImVec2(m.x + 9.5f * s, m.y + 25 * s), ImVec2(m.x + 13 * s, m.y + 23.5f * s),
                          ImVec2(m.x + 9 * s, m.y + 15 * s), ImVec2(m.x + 16 * s, m.y + 15 * s)};
    dl->AddConcavePolyFilled(pts, 7, IM_COL32(255, 255, 255, 255));
    dl->AddPolyline(pts, 7, IM_COL32(0, 0, 0, 255), ImDrawFlags_Closed, 1.5f * s);
    if (buttonDown_) dl->AddCircle(m, 14.0f * s, IM_COL32(255, 160, 80, 220), 32, 2.5f * s);

    // Fade to and from black (seamless loops on the website).
    if (const float f = fade(); f > 0.001f)
        dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(9, 9, 11, int(255 * std::min(f, 1.0f))));
}

// ---------------------------------------------------------------- demos

void Demo::buildShowcase() {
    const float k = float(app_.options_.height) / 1080.0f;
    run([this] {
        app_.openPath(app_.options_.demoAsset);
        app_.runCommand("subdivide");
        app_.symmetry_.x = true;
        app_.showBrushLab_ = false;
        app_.hideUi_ = true;
        app_.camera_.frame(app_.doc_.mesh.bounds());
        app_.camera_.yaw = glm::radians(85.0f); // profile
        app_.camera_.pitch = glm::radians(-6.0f);
        app_.camera_.distance *= 0.86f;
        mouse_ = ImVec2(-50.0f, -50.0f); // cursor off screen for the beauty shot
    });
    caption("SculptForge", "The free, open-source alternative to ZBrush", 4.2f);
    // Orbiting turns 0.008 rad per pixel: 240 px glides from profile to a three-quarter front view.
    orbit(240.0f, 0.0f, 4.5f);
    run([this] {
        app_.hideUi_ = false;
    });
    caption("Sculpt the way you already do", "Brushes, custom brush recipes and symmetry", 4.0f);
    auto line = [this](float u0, float v0, float u1, float v1) {
        return [this, u0, v0, u1, v1](float u) {
            const ImVec2 a = onModel(u0, v0), b = onModel(u1, v1);
            return ImVec2(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
        };
    };
    // Weather the mane: Edge Wear only chips the ridges, so the forms stay intact.
    run([this, k] {
        app_.radiusPx_ = 70.0f * k;
        app_.strength_ = 0.9f;
        // Custom brushes sit on the bottom shelf next to the built-in ones.
        auto& shelf = app_.layout_.bottomShelf;
        shelf.insert(std::find(shelf.begin(), shelf.end(), "widget.add_brush"), "brush.recipe.Edge Wear");
    });
    click("Edge Wear");
    stroke(line(0.16f, 0.30f, 0.20f, 0.70f), 1.4f);
    stroke(line(0.30f, 0.06f, 0.18f, 0.30f), 1.1f);
    stroke(line(0.30f, 0.62f, 0.40f, 0.86f), 1.1f);
    // From clay to stone.
    auto from = std::make_shared<glm::vec3>();
    steps_.push_back([this, from](float t) {
        if (t == 0.0f) *from = app_.renderSettings_.baseColor;
        app_.renderSettings_.baseColor = glm::mix(*from, glm::vec3(0.62f, 0.60f, 0.56f), smooth(t / 1.6f));
        return t >= 1.6f;
    });
    run([this] {
        app_.hideUi_ = true;
        mouse_ = ImVec2(-50.0f, -50.0f);
    });
    // One continuous swing across the front (the bust has a flat back), with both captions on it.
    caption("Ready for games and film", "glTF, FBX and USD export with LODs and blend shapes", 4.0f);
    run([this] { captions_.push_back({"Free and open source", "Windows · macOS · Linux", clock_ + 4.2f, 4.6f}); });
    orbit(-160.0f, 0.0f, 8.8f);
}

void Demo::buildSculpt() {
    run([this] {
        app_.runCommand("new_sphere", {{"subdivisions", 6.0}});
        app_.symmetry_.x = true;
        app_.strength_ = 0.8f;
        app_.camera_.yaw = 0.0f;
        app_.camera_.pitch = glm::radians(-4.0f);
        app_.camera_.distance *= 0.78f;
        app_.showBrushLab_ = false;
    });
    caption("Sculpt the way you already do", "ZBrush-style brushes, symmetry, tablet pressure", 4.5f);
    wait(1.0f);
    auto arc = [this](float lat0, float lon0, float lat1, float lon1) {
        return [this, lat0, lon0, lat1, lon1](float u) {
            return project(onSphere(lat0 + (lat1 - lat0) * u, lon0 + (lon1 - lon0) * u));
        };
    };
    // Pull a point on the surface across the screen (Move brush).
    auto pull = [this](float lat, float lon, float dx, float dy) {
        return [this, lat, lon, dx, dy](float u) {
            const ImVec2 p = project(onSphere(lat, lon));
            const float e = smooth(u);
            return ImVec2(p.x + dx * e, p.y + dy * e);
        };
    };
    const float k = float(app_.options_.height) / 1080.0f; // screen offsets were authored at 1080p

    // Horns: drag the upper skull out with the Move brush (mirrored by symmetry).
    run([this, k] { app_.radiusPx_ = 110.0f * k; });
    click("Move##brush.move");
    stroke(pull(52, -38, -150 * k, -170 * k), 1.6f);
    run([this, k] { app_.radiusPx_ = 70.0f * k; });
    stroke(pull(70, -48, -140 * k, -150 * k), 1.3f);
    caption("Symmetry mirrors every stroke", "", 3.0f);

    // Heavy brow and cheekbones with Clay.
    run([this, k] { app_.radiusPx_ = 62.0f * k; });
    click("Clay##brush.clay");
    for (int pass = 0; pass < 3; ++pass) stroke(arc(24, -52, 30, -4), 0.9f);
    for (int pass = 0; pass < 2; ++pass) stroke(arc(-14, -58, -6, -24), 0.9f);

    // Eye sockets carved under the brow.
    run([this, k] {
        app_.radiusPx_ = 40.0f * k;
        app_.strength_ = 0.9f;
    });
    click("Standard##brush.standard");
    stroke(arc(10, -30, 10, -16), 0.7f, true);
    stroke(arc(6, -32, 6, -14), 0.7f, true);

    // Turn to the side and pull a snout forward.
    orbit(-330.0f * k, 0.0f, 1.6f);
    run([this, k] { app_.radiusPx_ = 120.0f * k; });
    click("Move##brush.move");
    stroke(pull(-14, 0, -190 * k, 30 * k), 1.6f);
    orbit(250.0f * k, 0.0f, 1.4f);

    // Nostrils and a mouth line.
    run([this, k] { app_.radiusPx_ = 30.0f * k; });
    click("Crease##brush.crease");
    stroke(arc(-36, -30, -33, 0), 1.0f, true);
    run([this, k] { app_.radiusPx_ = 34.0f * k; });
    click("Inflate##brush.inflate");
    stroke(arc(-6, -10, -4, -6), 0.5f);

    // Soften.
    run([this, k] {
        app_.radiusPx_ = 80.0f * k;
        app_.strength_ = 0.35f;
    });
    click("Smooth##brush.smooth");
    stroke(arc(-45, -60, 45, -60), 1.0f);
    caption("Smooth, fast and scriptable", "Unlimited undo, layers, voxel remesh, one-click LODs", 5.0f);
    orbit(-760.0f * k, 30.0f * k, 6.0f);
    wait(0.5f);
    run([this] {
        if (!app_.options_.demoOutput.empty()) app_.runCommand("export_glb", {{"path", app_.options_.demoOutput}});
    });
}

void Demo::buildImageTo3D() {
    const float k = float(app_.options_.height) / 1080.0f;
    // SF_DEMO_REHEARSE_MODEL=file.glb stands in for the generator (camera and timing rehearsals without spending
    // Hugging Face GPU time). Never publish a rehearsal: the real take generates the model.
    static const char* rehearse = std::getenv("SF_DEMO_REHEARSE_MODEL");
    run([this] {
        app_.runCommand("new_sphere", {{"subdivisions", 4.0}});
        app_.renderSettings_.showMesh = false; // an empty stage until the model arrives
        app_.showBrushLab_ = false;
        app_.aiSettings_.generatorService = "huggingface";
        app_.aiSettings_.hfModel = "hunyuan3d-2.1";
        app_.generatedRemesh_ = int(GeneratedRemesh::Quad);
        app_.remeshResolution_ = 256;
        app_.openPath(app_.options_.demoAsset); // opens the Generate 3D panel with the drawing
        fadeFrom_ = fadeTo_ = 1.0f;
        mouse_ = ImVec2(1000.0f * float(app_.options_.height) / 1080.0f, 760.0f * float(app_.options_.height) / 1080.0f);
    });
    wait(0.15f);
    // Open on the drawing in the panel, then take in the Generate button below it.
    zoomToItem("Generate 3D/Generate 3D model", 2.5f, 0.0f, ImVec2(10.0f, -470.0f));
    fadeTo(0.0f, 0.8f);
    caption("From a sketch to a 3D model", "Drop in a drawing, and free AI builds the whole thing", 3.0f);
    wait(2.3f);
    zoomToItem("Generate 3D/Generate 3D model", 1.75f, 1.3f, ImVec2(0.0f, -270.0f));
    if (rehearse) {
        moveToItem("Generate 3D/Generate 3D model"); // no click: a rehearsal never calls the service
        wait(2.0f);
        run([this] {
            app_.openPath(rehearse);
            app_.status_ = "Rehearsal model loaded";
        });
    } else {
        click("Generate 3D/Generate 3D model");
        waitUntil([this] { return ready(app_.pendingGenerator_); }, 600.0f, 10);
        run([this] {
            app_.pollGenerator();
            if (!app_.options_.demoOutput.empty()) { // keep a copy for rehearsals; the status bar keeps its message
                const std::string status = app_.status_;
                app_.runCommand("export_glb", {{"path", app_.options_.demoOutput}});
                app_.status_ = status;
            }
        });
    }
    run([this] {
        app_.renderSettings_.showMesh = true;
        app_.showImageTo3D_ = false;
        app_.camera_.frame(app_.doc_.mesh.bounds());
        app_.camera_.yaw = glm::radians(60.0f); // a three-quarter turn via the back, ending on the front
        app_.camera_.pitch = glm::radians(-8.0f);
        app_.camera_.distance *= 0.82f;
        mouse_ = ImVec2(-80.0f, -80.0f);
    });
    zoomOut(1.4f);
    caption("A full 3D model in about 20 seconds", "Hunyuan3D 2.1 on Hugging Face: free, no account", 3.8f);
    // Orbiting turns 0.008 rad per pixel: 3/4 of a turn, ending on a three-quarter front view.
    orbit(-590.0f, 0.0f, 6.0f);
    caption("Remeshed into one clean surface", "Watertight and ready to sculpt", 3.2f);
    zoomTo([this] { return onModel(0.5f, 0.45f); }, 1.5f, 1.2f);
    run([this, k] {
        app_.radiusPx_ = 22.0f * k;
        app_.strength_ = 0.8f;
        app_.symmetry_.x = true;
    });
    click("Crease##brush.crease");
    auto line = [this](float u0, float v0, float u1, float v1) {
        return [this, u0, v0, u1, v1](float t) {
            const ImVec2 a = onModel(u0, v0), b = onModel(u1, v1);
            return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        };
    };
    // Panel lines across the breastplate (mirrored by symmetry).
    stroke(line(0.36f, 0.385f, 0.50f, 0.385f), 0.8f);
    stroke(line(0.37f, 0.455f, 0.50f, 0.455f), 0.8f);
    wait(0.4f);
    zoomOut(1.3f);
    run([this] { mouse_ = ImVec2(-80.0f, -80.0f); });
    orbit(-60.0f, 0.0f, 2.4f);
    fadeTo(1.0f, 0.8f);
    wait(0.8f);
}

void Demo::buildBrushLab() {
    run([this] {
        app_.runCommand("new_plane", {{"resolution", 320.0}});
        app_.runCommand("subdivide");
        app_.showBrushLab_ = true;
        app_.camera_.yaw = glm::radians(-20.0f);
        app_.camera_.pitch = glm::radians(-50.0f);
        app_.camera_.distance *= 0.62f;
        app_.camera_.target = glm::vec3(-0.42f, 0.0f, 0.05f); // plane to the right of the Brush Lab panel
        app_.radiusPx_ = 70.0f * float(app_.options_.height) / 1080.0f;
        app_.strength_ = 0.6f;
    });
    caption("Brush Lab", "Describe the brush you want. AI builds it.", 4.0f);
    wait(1.0f);
    moveToItem("##prompt");
    type(app_.chatInput_, sizeof app_.chatInput_, "dry cracked mud", 1.4f);
    click("Create");
    waitUntil([this] { return ready(app_.pendingBrush_); }, 240.0f, 8);
    run([this] { app_.pollBrushRequest(); });
    caption("Your own AI, even fully local", "This brush came from a small open model running on this machine", 4.5f);
    auto line = [this](float z0, float z1) {
        return [this, z0, z1](float u) { return project(glm::vec3(-0.45f + 0.9f * u, 0.0f, z0 + (z1 - z0) * u)); };
    };
    stroke(line(-0.4f, -0.3f), 1.6f);
    stroke(line(-0.05f, 0.0f), 1.6f);
    stroke(line(0.3f, 0.25f), 1.6f);
    moveToItem("##prompt");
    type(app_.chatInput_, sizeof app_.chatInput_, "deeper cracks, sharper edges", 1.6f);
    click("Create");
    waitUntil([this] { return ready(app_.pendingBrush_); }, 240.0f, 8);
    run([this] { app_.pollBrushRequest(); });
    caption("Refine it in plain words", "Every setting is also a slider", 3.5f);
    stroke(line(-0.45f, 0.45f), 1.8f);
    stroke(line(0.45f, -0.45f), 1.8f);
    wait(1.5f);
}

void Demo::buildGameReady() {
    const float k = float(app_.options_.height) / 1080.0f; // offsets authored at 1080p
    run([this] {
        app_.openPath(app_.options_.demoAsset);
        app_.runCommand("subdivide"); // 190k triangles: smooth strokes and a deep LOD chain
        app_.symmetry_.x = true;
        app_.strength_ = 0.9f;
        app_.showBrushLab_ = false;
        app_.camera_.frame(app_.doc_.mesh.bounds());
        app_.camera_.yaw = glm::radians(-14.0f);
        app_.camera_.pitch = glm::radians(-4.0f);
        app_.camera_.distance *= 0.80f;
        app_.lodLevels_ = 4;
        app_.lodRatio_ = 0.3f; // 190k -> 57k -> 17k -> 5k triangles
        std::snprintf(app_.newLayerName_, sizeof app_.newLayerName_, "Snarl");
        app_.layout_.openSubpalettes = {"tool.layers", "tool.export"}; // the Tool palette's Layers and Export
        fadeFrom_ = fadeTo_ = 1.0f;
    });
    // Medium shot: the sculpt and the Tool tray together; the face for close-ups.
    const ImVec2 medium(1180.0f * k, 560.0f * k);
    auto face = [this] { return onModel(0.5f, 0.33f); };
    auto at = [this](float u, float v) { return [this, u, v] { return onModel(u, v); }; };

    fadeTo(0.0f, 0.9f);
    caption("Blend shapes", "Sculpt an expression on its own layer", 3.6f);
    wait(1.4f);
    zoomToItem("+ Add", 2.3f, 1.1f, ImVec2(-90.0f, -60.0f));
    wait(0.5f);
    click("+ Add");
    wait(0.3f);

    // The snarl: brows pulled down and in, wrinkles across the muzzle, puffed whisker pads (mirrored by symmetry).
    zoomTo(face, 2.1f, 1.3f);
    run([this, k] {
        app_.radiusPx_ = 46.0f * k;
        app_.strength_ = 1.0f;
    });
    click("Move##brush.move");
    auto pull = [this, k](float u, float v, float dx, float dy) {
        return [this, k, u, v, dx, dy](float t) {
            const ImVec2 p = onModel(u, v);
            const float e = smooth(t);
            return ImVec2(p.x + dx * k * e, p.y + dy * k * e);
        };
    };
    stroke(pull(0.405f, 0.272f, 16.0f, 40.0f), 1.0f);
    stroke(pull(0.452f, 0.284f, 10.0f, 26.0f), 0.8f);
    run([this, k] {
        app_.radiusPx_ = 18.0f * k;
        app_.strength_ = 0.7f;
    });
    click("Standard##brush.standard");
    auto line = [this](float u0, float v0, float u1, float v1) {
        return [this, u0, v0, u1, v1](float t) {
            const ImVec2 a = onModel(u0, v0), b = onModel(u1, v1);
            return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        };
    };
    for (int pass = 0; pass < 2; ++pass) {
        stroke(line(0.45f, 0.35f, 0.50f, 0.342f), 0.45f);
        stroke(line(0.445f, 0.372f, 0.50f, 0.365f), 0.45f);
        stroke(line(0.45f, 0.394f, 0.50f, 0.389f), 0.45f);
    }
    run([this, k] {
        app_.radiusPx_ = 40.0f * k;
        app_.strength_ = 0.7f;
    });
    click("Inflate##brush.inflate");
    stroke(line(0.41f, 0.47f, 0.46f, 0.455f), 0.6f);
    stroke(line(0.41f, 0.49f, 0.46f, 0.475f), 0.6f);

    // Play it back.
    zoomToItem("Play", 2.3f, 1.1f, ImVec2(-90.0f, 20.0f));
    wait(0.3f);
    click("Play");
    zoomTo(face, 1.8f, 1.2f);
    caption("Plays back as a morph target", "glTF morph targets, UsdSkel blend shapes, FBX for Unreal", 3.8f);
    wait(3.6f);

    // LODs.
    zoomToItem("Generate LOD chain", 2.0f, 1.1f, ImVec2(-40.0f, 70.0f));
    wait(0.3f);
    click("Generate LOD chain");
    wait(0.4f);
    click("LOD3");
    caption("One-click LOD chain", "5k triangles at LOD3, and it still snarls", 4.0f);
    zoomTo(face, 1.9f, 1.3f);
    wait(3.2f);
    zoomTo(medium, 1.25f, 1.1f);
    wait(0.6f);
    click("Game Ready/Sculpt");
    run([this] { app_.renderSettings_.wireframe = false; });

    // Unreal.
    zoomToItem("Game Ready/Send to Unreal", 2.1f, 1.1f, ImVec2(-150.0f, 0.0f));
    caption("Send to Unreal Engine", "A skeletal mesh with LODs and morph targets, straight into the editor", 3.8f, true);
    moveToItem("Game Ready/Send to Unreal", 0.9f);
    wait(2.6f);
    zoomOut(1.4f);
    run([this] { mouse_ = ImVec2(-80.0f, -80.0f); });
    orbit(260.0f * k, 0.0f, 3.2f);
    fadeTo(1.0f, 0.8f);
    wait(0.8f);
}

void Demo::buildSplats() {
    run([this] {
        app_.openPath(app_.options_.demoAsset);
        app_.showBrushLab_ = false;
        app_.layout_.rightTray = {"splats", "tool"}; // the Splats palette first in the tray
        app_.camera_.frame(app_.sceneBounds());
        app_.camera_.yaw = glm::radians(-30.0f);
    });
    caption("Gaussian splats", "Turn a sculpt into splats for web viewers and engines", 4.0f);
    wait(1.0f);
    click("Gaussian Splats/Sculpt to splats");
    click("Gaussian Splats/Show mesh");
    orbit(450.0f, 20.0f, 4.0f);
    caption("And captures back into meshes", "Splat -> watertight, sculptable mesh in under a second", 4.0f);
    click("Gaussian Splats/Convert to mesh");
    click("Gaussian Splats/Show mesh");
    click("Gaussian Splats/Show##splats");
    orbit(-450.0f, -20.0f, 4.0f);
    caption("Open standards", "PLY, .splat, glTF KHR_gaussian_splatting, OpenUSD ParticleField", 3.5f);
    wait(2.0f);
}

} // namespace sf
