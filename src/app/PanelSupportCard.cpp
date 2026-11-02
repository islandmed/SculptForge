// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The support card at launch: a full-window card over the app with a bronze sculpt render (rendered by SculptForge
// itself, assets/branding/support-card.jpg), the three memberships and a one-time gift. When it may show is decided
// once while starting (supportCardDue in Support.h): the first start, the first start of each new version, then at
// most once a week; never mid-session, never for a supporter, never in tests, renders, demos or CI. Every button only
// opens one of the fixed links in Support.h in the browser. The picture is decoded on a worker thread the first time
// the card shows, so starting is no slower; the card fades in over a plain backdrop and the picture follows.
#include "app/App.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "render/GL.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>

namespace sf {

namespace assets {
extern const unsigned char supportCard[]; // assets/branding/support-card.jpg (CMakeLists.txt)
extern const unsigned long long supportCard_size;
} // namespace assets

struct App::SupportCard {
    SupportCardReason reason = SupportCardReason::None;
    double openedAt = 0.0;          // seconds (ImGui clock)
    std::future<Image> picture;     // decoded and graded on a worker thread
    GLuint texture = 0;
    float pictureAspect = 16.0f / 9.0f;
    double pictureAt = -1.0;        // when the texture became ready (it fades in)
    ImVec2 parallax{0.0f, 0.0f};    // follows the pointer, eased
    float hover[3] = {0.0f, 0.0f, 0.0f}; // tier cards lifted under the pointer, eased
    bool closing = false;
    double closingAt = 0.0;
    bool supporterOnClose = false;

    ~SupportCard() {
        if (texture) gl::DeleteTextures(1, &texture);
        if (picture.valid()) picture.wait();
    }
};

void App::SupportCardDelete::operator()(SupportCard* c) const { delete c; }

namespace {

// ImGui's clock: real time in the app, the scripted time in UI tests (so their screenshots show the card settled).
double nowSeconds() { return ImGui::GetTime(); }

float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float easeOut(float x) { // cubic
    x = 1.0f - clamp01(x);
    return 1.0f - x * x * x;
}
float easeInOut(float x) {
    x = clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}
float lerp(float a, float b, float t) { return a + (b - a) * t; }

ImU32 col(unsigned hex, float alpha) {
    return IM_COL32((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff, int(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f));
}

// The picture with the darkening for the text baked in (left third and the lower part, under the tier cards), so the
// card draws it as one rounded image.
Image loadPicture() {
    Image img;
    std::string error;
    if (!Image::decode(assets::supportCard, size_t(assets::supportCard_size), img, error)) return {};
    const int w = img.width, h = img.height;
    for (int y = 0; y < h; ++y) {
        const float v = (float(y) + 0.5f) / float(h);
        const float bottom = 0.92f * easeInOut((v - 0.42f) / 0.5f);
        for (int x = 0; x < w; ++x) {
            const float u = (float(x) + 0.5f) / float(w);
            const float left = 0.86f * std::pow(1.0f - easeInOut(u / 0.62f), 1.3f);
            const float dark = 1.0f - (1.0f - left) * (1.0f - bottom);
            uint8_t* p = &img.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
            static constexpr float kInk[3] = {12.0f, 12.0f, 15.0f};
            for (int c = 0; c < 3; ++c) p[c] = uint8_t(lerp(float(p[c]), kInk[c], dark) + 0.5f);
        }
    }
    return img;
}

GLuint uploadPicture(const Image& img) {
    GLuint tex = 0;
    gl::GenTextures(1, &tex);
    gl::BindTexture(GL_TEXTURE_2D, tex);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    gl::BindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Text drawn straight to the draw list, also logged so UI tests can find it (expect_text).
struct Painter {
    ImDrawList* dl;
    float k; // logical pixels to screen pixels (UI scale x fit x the opening zoom)

    ImVec2 measure(ImFont* f, float px, const char* s, float wrap = 0.0f) const {
        return f->CalcTextSizeA(px * k, FLT_MAX, wrap, s);
    }
    ImVec2 text(ImFont* f, float px, ImVec2 at, ImU32 c, const char* s, float wrap = 0.0f) const {
        dl->AddText(f, px * k, at, c, s, nullptr, wrap);
        ImGui::LogText("%s\n", s);
        return measure(f, px, s, wrap);
    }
    // Small capitals with letter spacing (the eyebrow and the badge).
    float spaced(ImFont* f, float px, ImVec2 at, ImU32 c, const char* s, float spacing, bool draw = true) const {
        float x = at.x;
        char one[2] = {0, 0};
        for (const char* p = s; *p; ++p) {
            one[0] = *p;
            if (draw) dl->AddText(f, px * k, ImVec2(x, at.y), c, one);
            x += f->CalcTextSizeA(px * k, FLT_MAX, 0.0f, one).x + spacing * k;
        }
        if (draw) ImGui::LogText("%s\n", s);
        return x - at.x - spacing * k;
    }
};

// A soft shadow / glow: rounded rectangles growing outwards, fading.
void glow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float spread, unsigned hex, float alpha, int layers = 10) {
    for (int i = layers; i >= 1; --i) {
        const float t = float(i) / float(layers), g = spread * t;
        dl->AddRectFilled(ImVec2(a.x - g, a.y - g), ImVec2(b.x + g, b.y + g), col(hex, alpha * (1.0f - t) * (1.0f - t) * 0.5f),
                          rounding + g);
    }
}

// A light sweeping across the picture: a soft diagonal band, faded towards the card's edges so it stays inside the
// rounded corners.
void lightSweep(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float x, float width, float alpha) {
    if (alpha <= 0.002f) return;
    const ImVec2 dir = ImVec2(0.34f, 1.0f); // leaning right going down
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    const ImVec2 d(dir.x / len, dir.y / len), n(d.y, -d.x);
    const float h = (b.y - a.y) / d.y; // the band's length across the card
    const ImVec2 top(x - d.x * h * 0.5f, a.y + (b.y - a.y) * 0.5f - h * 0.5f);
    constexpr int kSteps = 28;
    auto edgeFade = [&](ImVec2 p) {
        const float e = std::min({p.x - a.x, b.x - p.x, p.y - a.y, b.y - p.y});
        return clamp01((e - rounding * 0.4f) / (rounding * 2.0f));
    };
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve(kSteps * 12, (kSteps + 1) * 3);
    const ImDrawIdx base = ImDrawIdx(dl->_VtxCurrentIdx);
    for (int i = 0; i <= kSteps; ++i) {
        const float t = float(i) / float(kSteps);
        const ImVec2 c(top.x + d.x * h * t, top.y + d.y * h * t);
        const float fall = std::sin(t * 3.14159265f); // brightest in the middle of the card
        const ImVec2 pts[3] = {ImVec2(c.x - n.x * width, c.y - n.y * width), c, ImVec2(c.x + n.x * width, c.y + n.y * width)};
        for (int j = 0; j < 3; ++j) {
            const float aj = j == 1 ? alpha * fall * edgeFade(pts[j]) : 0.0f;
            dl->PrimWriteVtx(pts[j], uv, col(0xfff3e2, aj));
        }
    }
    for (int i = 0; i < kSteps; ++i)
        for (int j = 0; j < 2; ++j) {
            const ImDrawIdx v0 = ImDrawIdx(base + i * 3 + j), v1 = ImDrawIdx(v0 + 1), v2 = ImDrawIdx(v0 + 3), v3 = ImDrawIdx(v0 + 4);
            dl->PrimWriteIdx(v0), dl->PrimWriteIdx(v1), dl->PrimWriteIdx(v3);
            dl->PrimWriteIdx(v0), dl->PrimWriteIdx(v3), dl->PrimWriteIdx(v2);
        }
}

struct Tier {
    const char* name;
    const char* price;
    const char* per;
    const char* perks;
    const char* cta;
    const char* id; // the button's ImGui label (UI tests click it)
    const char* url;
};

} // namespace

bool App::supportCardTestMode() const {
    return !interactive() || !options_.uiTest.empty() || !options_.demo.empty() || !options_.script.empty() ||
           options_.measureIdle > 0.0f || options_.measureStroke > 0.0f || options_.benchmark ||
           !options_.timelapsePath.empty() || std::getenv("SF_NO_SUPPORT_CARD") || std::getenv("CI");
}

bool App::supportCardOpen() const { return supportCard_ && !supportCard_->closing; }

bool App::supportCardLoading() const { return supportCard_ && !supportCard_->texture && supportCard_->picture.valid(); }

void App::launchSupportCard(bool firstRun, int64_t now, bool testMode) {
    supportCardReason_ = supportCardDue(supportCardPrefs_, firstRun, SCULPTFORGE_VERSION, now, testMode);
    if (supportCardReason_ == SupportCardReason::None) return;
    supportCardPrefs_.shownAt = now;
    supportCardPrefs_.shownVersion = SCULPTFORGE_VERSION;
    saveProjectPrefs(); // (recorded before it shows: a crash can't bring it back at once)
    if (showWelcome_) { // the first-run welcome follows the card
        showWelcome_ = false;
        welcomeAfterCard_ = true;
    }
    showSupportCard(supportCardReason_);
}

void App::showSupportCard(SupportCardReason reason) {
    supportCard_.reset(new SupportCard);
    supportCard_->reason = reason;
    supportCard_->openedAt = nowSeconds();
    supportCard_->picture = std::async(std::launch::async, loadPicture);
    redrawIn(0.0);
}

void App::closeSupportCard(bool supporter) {
    if (!supportCard_ || supportCard_->closing) return;
    supportCard_->closing = true;
    supportCard_->closingAt = nowSeconds();
    if (supporter) {
        supportCardPrefs_.supporter = true;
        saveProjectPrefs();
        status_ = "Thank you for supporting SculptForge! The support card won't show again (Preferences > Support).";
    }
    if (welcomeAfterCard_) {
        welcomeAfterCard_ = false;
        showWelcome_ = true;
    }
    redrawIn(0.0);
}

void App::openSupportLink(const char* url) {
    ++supportPageRequests_;
    if (!interactive()) { // UI tests, renders and demos never start a browser
        status_ = std::string("Support SculptForge: ") + url;
        return;
    }
    status_ = SDL_OpenURL(url) ? std::string("Opened the Stripe checkout in your browser")
                               : std::string("Couldn't open ") + url + ": " + SDL_GetError();
}

bool App::supportCardEvent(const SDL_Event& event) {
    if (!supportCard_) return false;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_FINGER_DOWN: {
        SDL_Event e = event;
        ImGui_ImplSDL3_ProcessEvent(&e); // the card's buttons only: no stroke, shortcut or camera move underneath
        return true;
    }
    default:
        return false;
    }
}

void App::drawSupportCard() {
    if (!supportCard_) return;
    SupportCard& card = *supportCard_;
    const double now = nowSeconds();
    const float t = float(now - card.openedAt);
    const ImGuiIO& io = ImGui::GetIO();
    const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);

    // Fade in (and out when closing). A modal pop-up: above every panel and pop-up, and nothing under it takes input.
    constexpr const char* kPopup = "##support_card";
    float shown = easeOut(t / 0.45f);
    bool finished = false;
    if (card.closing) {
        const float c = float(now - card.closingAt) / 0.22f;
        finished = c >= 1.0f;
        shown *= 1.0f - easeInOut(c);
    }
    if (finished) {
        if (ImGui::IsPopupOpen(kPopup)) {
            ImGui::SetNextWindowSize(ImVec2(1, 1));
            if (ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground)) {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        supportCard_.reset();
        redrawIn(0.0);
        return;
    }
    if (!ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);
    redrawIn(0.0); // animated while open (60 fps with vsync); nothing is drawn for it once closed

    // The picture: decoded off the main thread, uploaded here once ready.
    if (!card.texture && card.picture.valid() && card.picture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const Image img = card.picture.get();
        if (!img.empty()) {
            card.texture = uploadPicture(img);
            card.pictureAspect = img.aspect();
            card.pictureAt = now;
        }
    }

    const float s = uiScale_;
    const ImVec2 view = io.DisplaySize;
    // Logical size 1100 x 680, fitted to the window; a slight zoom while opening.
    const float fit = std::min({1.0f, (view.x - 40.0f * s) / (1100.0f * s), (view.y - 40.0f * s) / (680.0f * s)});
    const float zoom = card.closing ? lerp(0.985f, 1.0f, shown) : lerp(0.955f, 1.0f, easeOut(t / 0.6f));
    const float k = s * std::max(fit, 0.45f) * zoom;
    const ImVec2 size(1100.0f * k, 680.0f * k);
    const float rise = (1.0f - easeOut(t / 0.6f)) * 16.0f * s;
    const ImVec2 a(std::floor((view.x - size.x) * 0.5f), std::floor((view.y - size.y) * 0.5f + rise));
    const ImVec2 b(a.x + size.x, a.y + size.y);
    const float radius = 20.0f * k;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(view);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0)); // (dimmed below, with the fade)
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
    const bool began = ImGui::BeginPopupModal(kPopup, nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (!began) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Painter paint{dl, k};
    ImFont* titleFont = fontCardTitle_ ? fontCardTitle_ : ImGui::GetFont();
    ImFont* headFont = fontCardHead_ ? fontCardHead_ : boldFont();
    ImFont* bodyFont = fontCardBody_ ? fontCardBody_ : ImGui::GetFont();
    ImFont* boldUi = boldFont() ? boldFont() : ImGui::GetFont();
    const float A = shown; // overall opacity

    // The app dimmed behind it, with a warm light from the card.
    dl->AddRectFilled(ImVec2(0, 0), view, col(0x060608, 0.84f * A));
    glow(dl, a, b, radius, 120.0f * k, 0xf0882e, 0.035f * A, 16);
    glow(dl, ImVec2(a.x, a.y + 18.0f * k), ImVec2(b.x, b.y + 18.0f * k), radius, 40.0f * k, 0x000000, 0.9f * A, 10);
    dl->AddRectFilled(a, b, col(0x0e0e11, A), radius);

    // The picture: covers the card, a little larger so the pointer can move it (parallax), fading in once decoded.
    const ImVec2 mouse = io.MousePos;
    ImVec2 target(0.0f, 0.0f);
    if (ImGui::IsMousePosValid(&mouse)) {
        target.x = std::clamp((mouse.x - (a.x + b.x) * 0.5f) / (size.x * 0.5f), -1.0f, 1.0f);
        target.y = std::clamp((mouse.y - (a.y + b.y) * 0.5f) / (size.y * 0.5f), -1.0f, 1.0f);
    }
    const float follow = 1.0f - std::exp(-dt * 4.0f);
    card.parallax.x = lerp(card.parallax.x, target.x, follow);
    card.parallax.y = lerp(card.parallax.y, target.y, follow);
    if (card.texture) {
        const float fade = easeOut(float(now - card.pictureAt) / 0.7f) * A;
        const float cardAspect = size.x / size.y;
        float uw = 1.0f, vh = 1.0f; // the part of the picture that covers the card
        if (cardAspect > card.pictureAspect) vh = card.pictureAspect / cardAspect;
        else uw = cardAspect / card.pictureAspect;
        const float margin = 0.94f, drift = 0.025f; // 6 % to spare for the parallax; a slow drift as it opens
        const float settle = 1.0f + drift * (1.0f - easeOut(float(now - card.pictureAt) / 2.4f));
        uw *= margin / settle;
        vh *= margin / settle;
        const float cu = 0.5f - card.parallax.x * (1.0f - margin) * 0.45f + (1.0f - uw) * 0.06f; // a little right
        const float cv = 0.5f - card.parallax.y * (1.0f - margin) * 0.45f - (1.0f - vh) * 0.2f;   // a little up
        dl->AddImageRounded(ImTextureID(uintptr_t(card.texture)), a, b, ImVec2(cu - uw * 0.5f, cv - vh * 0.5f),
                            ImVec2(cu + uw * 0.5f, cv + vh * 0.5f), col(0xffffff, fade), radius);
    }
    // A light passing over the sculpt now and then (every 7 s, the first soon after opening).
    {
        const float period = 7.0f, run = 1.9f;
        const float since = t - 0.7f;
        const float phase = since >= 0.0f ? std::fmod(since, period) : -1.0f;
        if (phase >= 0.0f && phase < run) {
            const float u = easeInOut(phase / run);
            lightSweep(dl, a, b, radius, lerp(a.x + size.x * 0.25f, b.x + size.x * 0.1f, u), 150.0f * k,
                       0.10f * A * std::sin(u * 3.14159265f));
        }
    }
    // A hairline edge, brighter along the top.
    dl->AddRect(a, b, col(0xffffff, 0.08f * A), radius, 0, std::max(1.0f, k));
    dl->AddLine(ImVec2(a.x + radius, a.y + 0.5f), ImVec2(b.x - radius, a.y + 0.5f), col(0xffffff, 0.10f * A), std::max(1.0f, k));

    const float pad = 48.0f * k;
    float y = a.y + 46.0f * k;
    const float colW = size.x * 0.50f;
    // Updated: a chip that leads to What's New (open behind the card).
    if (supportCard_->reason == SupportCardReason::Updated) {
        const std::string chip = std::string("SculptForge ") + SCULPTFORGE_VERSION + "  " + "\xc2\xb7" + "  What's new";
        const ImVec2 ts = paint.measure(boldUi, 13.0f, chip.c_str());
        const ImVec2 c0(a.x + pad, y), c1(c0.x + ts.x + 26.0f * k, y + 28.0f * k);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton("What's new##card.whatsnew", ImVec2(c1.x - c0.x, c1.y - c0.y));
        const bool hot = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            guide_.open("whats-new.md");
            closeSupportCard(false);
        }
        dl->AddRectFilled(c0, c1, col(0xffffff, (hot ? 0.12f : 0.07f) * A), 14.0f * k);
        dl->AddRect(c0, c1, col(0xffffff, 0.12f * A), 14.0f * k);
        paint.text(boldUi, 13.0f, ImVec2(c0.x + 13.0f * k, c0.y + (28.0f * k - ts.y) * 0.5f), col(0xf2f2f2, A), chip.c_str());
        y += 44.0f * k;
    }
    paint.spaced(boldUi, 12.5f, ImVec2(a.x + pad, y), col(0xffb06a, A), "SUPPORT SCULPTFORGE", 2.2f);
    y += 30.0f * k;
    y += paint.text(titleFont, 40.0f, ImVec2(a.x + pad, y), col(0xffffff, A), "Free for everyone,").y;
    // The second line in the accent, with a soft glow.
    {
        const char* line = "kept going by you.";
        for (int i = 0; i < 6; ++i) {
            const float ang = float(i) * 1.0472f, r = 3.0f * k;
            dl->AddText(titleFont, 40.0f * k, ImVec2(a.x + pad + std::cos(ang) * r, y + std::sin(ang) * r),
                        col(0xf0882e, 0.07f * A), line);
        }
        y += paint.text(titleFont, 40.0f, ImVec2(a.x + pad, y), col(0xff9a45, A), line).y;
    }
    y += 16.0f * k;
    y += paint.text(bodyFont, 16.5f, ImVec2(a.x + pad, y), col(0xc9c9d0, A),
                    "SculptForge has no ads, no accounts and no telemetry, and every feature stays free for "
                    "everyone. Memberships pay for code signing, test hardware and development time.",
                    colW - pad)
             .y;
    y += 14.0f * k;
    // The Insider promise, worded exactly as on the website.
    {
        const float dot = 4.0f * k;
        const ImVec2 at(a.x + pad, y);
        const ImVec2 ts = paint.measure(bodyFont, 16.5f, kInsiderNote, colW - pad - 18.0f * k);
        dl->AddCircleFilled(ImVec2(at.x + dot, at.y + 11.0f * k), dot, col(0xff9a45, A));
        dl->AddCircle(ImVec2(at.x + dot, at.y + 11.0f * k), dot * 2.2f, col(0xff9a45, 0.35f * A), 0, std::max(1.0f, k));
        paint.text(bodyFont, 16.5f, ImVec2(at.x + 18.0f * k, at.y), col(0xf2e6da, A), kInsiderNote,
                   colW - pad - 18.0f * k);
        (void)ts;
    }

    // Close (top right).
    {
        const float d = 34.0f * k;
        const ImVec2 c0(b.x - 18.0f * k - d, a.y + 18.0f * k);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton("Close##card.close", ImVec2(d, d));
        const bool hot = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) closeSupportCard(false);
        const ImVec2 c(c0.x + d * 0.5f, c0.y + d * 0.5f);
        dl->AddCircleFilled(c, d * 0.5f, col(0x000000, (hot ? 0.55f : 0.35f) * A));
        dl->AddCircle(c, d * 0.5f, col(0xffffff, (hot ? 0.30f : 0.14f) * A), 0, std::max(1.0f, k));
        const float x = 6.0f * k;
        dl->AddLine(ImVec2(c.x - x, c.y - x), ImVec2(c.x + x, c.y + x), col(0xffffff, (hot ? 1.0f : 0.75f) * A), 1.6f * k);
        dl->AddLine(ImVec2(c.x - x, c.y + x), ImVec2(c.x + x, c.y - x), col(0xffffff, (hot ? 1.0f : 0.75f) * A), 1.6f * k);
    }

    // The three memberships.
    static const Tier kTiers[3] = {
        {"Supporter", "$5", "/ month", "Your name in the app's Supporters list and on the website, if you like.",
         "Support monthly", "Support monthly##card.supporter", kSupportUrlSupporter},
        {"Insider", "$20", "/ month",
         "Each release about two weeks early, starting with SculptForge 1.0. A monthly vote on what gets built "
         "next, and the monthly Supporter Pack of brushes, alphas and VDMs.",
         "Become an Insider", "Become an Insider##card.insider", kSupportUrlInsider},
        {"Studio", "$150", "/ month",
         "Insider access for up to 10 artists ($15 each), priority triage of your pipeline bugs, monthly "
         "invoices and your logo on the website and README.",
         "Studio membership", "Studio membership##card.studio", kSupportUrlStudio},
    };
    const float gap = 18.0f * k, footer = 58.0f * k, tierH = 262.0f * k;
    const float tierW = (size.x - pad * 2.0f - gap * 2.0f) / 3.0f;
    const float tierTop = b.y - footer - tierH;
    const char* clicked = nullptr;
    bool discord = false; // the Discord pill in the footer (opens the invite, keeps the card)
    for (int i = 0; i < 3; ++i) {
        const Tier& tier = kTiers[i];
        const bool featured = i == 1;
        const float appear = easeOut((t - 0.16f - 0.08f * float(i)) / 0.55f) * A;
        const float lift = card.hover[i] * 4.0f * k + (featured ? 10.0f * k : 0.0f);
        const ImVec2 p0(a.x + pad + float(i) * (tierW + gap), tierTop - lift + (1.0f - appear) * 22.0f * k);
        const ImVec2 p1(p0.x + tierW, p0.y + tierH);
        const float r = 16.0f * k;
        if (featured) { // a slow breathing glow
            const float breathe = 0.75f + 0.25f * std::sin(t * 2.0f);
            glow(dl, p0, p1, r, 26.0f * k, 0xf0882e, 0.55f * breathe * appear, 12);
        } else {
            glow(dl, ImVec2(p0.x, p0.y + 8.0f * k), ImVec2(p1.x, p1.y + 8.0f * k), r, 18.0f * k, 0x000000, 0.6f * appear, 6);
        }
        { // glass over the picture; the featured card warmer towards the top
            const int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(p0, p1, col(0xffffff, 0.92f * appear), r);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, p0, ImVec2(p0.x, p1.y),
                                                          featured ? col(0x2a1d12, 1.0f) : col(0x1a1a1f, 1.0f),
                                                          featured ? col(0x141215, 1.0f) : col(0x111114, 1.0f));
        }
        const float edge = featured ? 0.95f : 0.10f + 0.10f * card.hover[i];
        dl->AddRect(p0, p1, featured ? col(0xf6994d, edge * appear) : col(0xffffff, edge * appear), r, 0,
                    (featured ? 1.6f : 1.0f) * k);

        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton((std::string("##card.tier") + char('0' + i)).c_str(), ImVec2(tierW, tierH - 64.0f * k));
        const bool cardHot = ImGui::IsItemHovered();

        const float ix = p0.x + 24.0f * k;
        float ty = p0.y + 24.0f * k;
        if (featured) { // the badge, on the card's top edge
            const float bw = paint.spaced(boldUi, 11.0f, ImVec2(0, 0), 0, "RECOMMENDED", 1.6f, false) + 24.0f * k;
            const ImVec2 b0(p0.x + (tierW - bw) * 0.5f, p0.y - 12.0f * k), b1(b0.x + bw, b0.y + 24.0f * k);
            glow(dl, b0, b1, 12.0f * k, 10.0f * k, 0xf0882e, 0.6f * appear, 6);
            dl->AddRectFilled(b0, b1, col(0xf6994d, appear), 12.0f * k);
            const float th = 11.0f * k;
            paint.spaced(boldUi, 11.0f, ImVec2(b0.x + 12.0f * k, b0.y + (24.0f * k - th) * 0.5f - 0.5f * k),
                         col(0x1a0d04, appear), "RECOMMENDED", 1.6f);
        }
        paint.text(headFont, 21.0f, ImVec2(ix, ty), col(0xffffff, appear), tier.name);
        ty += 34.0f * k;
        const ImVec2 ps = paint.text(titleFont, 40.0f, ImVec2(ix, ty), col(0xffffff, appear), tier.price);
        const ImVec2 pe = paint.measure(bodyFont, 16.5f, tier.per);
        paint.text(bodyFont, 16.5f, ImVec2(ix + ps.x + 6.0f * k, ty + ps.y - pe.y - 6.0f * k), col(0x9a9aa3, appear), tier.per);
        ty += ps.y + 8.0f * k;
        paint.text(ImGui::GetFont(), 14.5f, ImVec2(ix, ty), col(featured ? 0xd9d0c6 : 0xa9a9b2, appear), tier.perks,
                   tierW - 48.0f * k);

        // The one button, along the bottom.
        const ImVec2 c0(ix, p1.y - 24.0f * k - 44.0f * k), c1(p1.x - 24.0f * k, p1.y - 24.0f * k);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton(tier.id, ImVec2(c1.x - c0.x, c1.y - c0.y));
        const bool hot = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        if (ImGui::IsItemClicked()) clicked = tier.url;
        const float target = (cardHot || hot) ? 1.0f : 0.0f;
        card.hover[i] = lerp(card.hover[i], target, 1.0f - std::exp(-dt * 12.0f));
        if (featured) {
            glow(dl, c0, c1, 22.0f * k, 12.0f * k, 0xf0882e, (hot ? 0.9f : 0.5f) * appear, 6);
            const int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(c0, c1, col(0xffffff, appear), 22.0f * k);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, c0, ImVec2(c0.x, c1.y),
                                                          col(held ? 0xf08a3a : hot ? 0xffb877 : 0xffa95e, 1.0f),
                                                          col(held ? 0xd0661f : hot ? 0xf08632 : 0xe8782c, 1.0f));
        } else {
            dl->AddRectFilled(c0, c1, col(0xffffff, (held ? 0.16f : hot ? 0.11f : 0.05f) * appear), 22.0f * k);
            dl->AddRect(c0, c1, col(0xffffff, (hot ? 0.40f : 0.20f) * appear), 22.0f * k, 0, std::max(1.0f, k));
        }
        const ImVec2 ls = paint.measure(boldUi, 15.0f, tier.cta);
        paint.text(boldUi, 15.0f, ImVec2(std::floor((c0.x + c1.x - ls.x) * 0.5f), std::floor((c0.y + c1.y - ls.y) * 0.5f)),
                   col(featured ? 0x1a0d04 : 0xf2f2f2, appear), tier.cta);
    }

    // Footer: a one-time gift, the community Discord, "I'm already a supporter" and "Maybe later".
    const float fy = b.y - footer * 0.5f;
    const float fa = easeOut((t - 0.45f) / 0.5f) * A;
    auto link = [&](const char* label, const char* id, ImVec2 at, unsigned hex, bool underline) {
        const ImVec2 ts = paint.measure(bodyFont, 15.0f, label);
        ImGui::SetCursorScreenPos(ImVec2(at.x - 4.0f * k, at.y - 6.0f * k));
        ImGui::InvisibleButton(id, ImVec2(ts.x + 8.0f * k, ts.y + 12.0f * k));
        const bool hot = ImGui::IsItemHovered();
        paint.text(bodyFont, 15.0f, at, col(hot ? 0xffffff : hex, fa), label);
        if (underline || hot)
            dl->AddLine(ImVec2(at.x, at.y + ts.y), ImVec2(at.x + ts.x, at.y + ts.y), col(hot ? 0xffffff : hex, 0.55f * fa), std::max(1.0f, k));
        return ImGui::IsItemClicked();
    };
    {
        const char* lead = "Prefer a one-time gift?";
        const ImVec2 ls = paint.measure(bodyFont, 15.0f, lead);
        const float ly = std::floor(fy - ls.y * 0.5f);
        paint.text(bodyFont, 15.0f, ImVec2(a.x + pad, ly), col(0x9a9aa3, fa), lead);
        if (link("Give once", "Give once##card.once", ImVec2(a.x + pad + ls.x + 8.0f * k, ly), 0xffb06a, true))
            clicked = kSupportUrlOnce;
        // The community Discord: a secondary pill the weight of "Maybe later", with a drawn speech-bubble glyph (no
        // logo asset). It opens the invite and leaves the card open.
        const char* label = "Join the community on Discord";
        const ImVec2 ds = paint.measure(boldUi, 15.0f, label);
        const ImVec2 gs = paint.measure(bodyFont, 15.0f, "Give once");
        const float glyph = 18.0f * k;
        const ImVec2 c0(std::floor(a.x + pad + ls.x + 8.0f * k + gs.x + 28.0f * k), fy - 19.0f * k);
        const ImVec2 c1(c0.x + 18.0f * k + glyph + 10.0f * k + ds.x + 22.0f * k, fy + 19.0f * k);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton("Join the community on Discord##card.discord", ImVec2(c1.x - c0.x, c1.y - c0.y));
        const bool hot = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) discord = true;
        dl->AddRectFilled(c0, c1, col(0xffffff, (hot ? 0.12f : 0.06f) * fa), 19.0f * k);
        dl->AddRect(c0, c1, col(0xffffff, (hot ? 0.35f : 0.18f) * fa), 19.0f * k, 0, std::max(1.0f, k));
        {
            const unsigned ink = hot ? 0xffffff : 0xb9bcff;
            const ImVec2 g0(c0.x + 18.0f * k, fy - glyph * 0.42f), g1(g0.x + glyph, fy + glyph * 0.30f);
            dl->AddRectFilled(g0, g1, col(ink, fa), 5.0f * k);
            dl->AddTriangleFilled(ImVec2(g0.x + 4.0f * k, g1.y - 1.0f), ImVec2(g0.x + 10.0f * k, g1.y - 1.0f),
                                  ImVec2(g0.x + 3.0f * k, g1.y + 5.0f * k), col(ink, fa));
            const float my = (g0.y + g1.y) * 0.5f, r = 1.6f * k;
            for (int i = -1; i <= 1; ++i)
                dl->AddCircleFilled(ImVec2((g0.x + g1.x) * 0.5f + float(i) * 4.5f * k, my), r, col(0x0e0e11, fa));
        }
        paint.text(boldUi, 15.0f, ImVec2(std::floor(c0.x + 18.0f * k + glyph + 10.0f * k), std::floor(fy - ds.y * 0.5f)),
                   col(0xf2f2f2, fa), label);
    }
    bool later = false, supporter = false;
    {
        const char* label = "Maybe later";
        const ImVec2 ls = paint.measure(boldUi, 15.0f, label);
        const ImVec2 c1(b.x - pad, fy + 19.0f * k), c0(c1.x - ls.x - 44.0f * k, fy - 19.0f * k);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton("Maybe later##card.later", ImVec2(c1.x - c0.x, c1.y - c0.y));
        const bool hot = ImGui::IsItemHovered();
        later = ImGui::IsItemClicked();
        dl->AddRectFilled(c0, c1, col(0xffffff, (hot ? 0.12f : 0.06f) * fa), 19.0f * k);
        dl->AddRect(c0, c1, col(0xffffff, (hot ? 0.35f : 0.18f) * fa), 19.0f * k, 0, std::max(1.0f, k));
        paint.text(boldUi, 15.0f, ImVec2(std::floor(c0.x + 22.0f * k), std::floor(fy - ls.y * 0.5f)), col(0xf2f2f2, fa), label);
        const char* already = "I'm already a supporter";
        const ImVec2 as = paint.measure(bodyFont, 15.0f, already);
        supporter = link(already, "I'm already a supporter##card.supporter_already",
                         ImVec2(c0.x - 24.0f * k - as.x, std::floor(fy - as.y * 0.5f)), 0x9a9aa3, false);
    }

    // Esc, or a click outside the card: Maybe later.
    const bool outside = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
                         !ImRect(a, b).Contains(io.MousePos) && t > 0.3f;
    if (!card.closing) {
        if (clicked) {
            openSupportLink(clicked);
            closeSupportCard(false);
        } else if (discord) {
            openDiscordPage(); // the card stays: a membership may still follow
        } else if (supporter) {
            closeSupportCard(true);
        } else if (later || outside || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            closeSupportCard(false);
        }
    }
    ImGui::EndPopup();
}

void App::drawSupportPrefs() {
    if (ImGui::Checkbox("Show the support card at launch", &supportCardPrefs_.enabled)) saveProjectPrefs();
    ui::HelpText("Show the support card at launch", nullptr,
                 "A card with the memberships that support SculptForge, shown while it starts: the first time, after "
                 "each update, and at most once a week otherwise. Never while you work.",
                 "Preferences > Support");
    if (ImGui::Checkbox("I'm a supporter", &supportCardPrefs_.supporter)) saveProjectPrefs();
    ui::HelpText("I'm a supporter", nullptr,
                 "Thank you! The support card never shows. Kept on this computer only: SculptForge has no accounts.",
                 "Preferences > Support");
    if (ImGui::Button("Show the support card", ImVec2(-1, 0))) showSupportCard(SupportCardReason::Weekly);
}

} // namespace sf
