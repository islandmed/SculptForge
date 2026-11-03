// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The support card at launch: a full-window card over the app with a bronze sculpt render (rendered by SculptForge
// itself, assets/branding/support-card.jpg), the cube mark and the promise, the three memberships with their perks and
// the one-time Supporter, in the current look's colours (Theme.h). When it may show is decided once while starting
// (supportCardDue in Support.h): the first start, the first start of each new version, then at most once a week, and
// not for 30 days after "Maybe later"; never mid-session, never for a supporter, never in tests, renders, demos or CI.
// No countdowns, no guilt, nothing pre-selected; Close is always there. Every button only
// opens one of the fixed links in Support.h in the browser. The picture is decoded on a worker thread the first time
// the card shows, so starting is no slower; the card fades in over a plain backdrop and the picture follows.
#include "app/App.h"
#include "app/CardPaint.h"
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

using namespace card;

namespace {

// ImGui's clock: real time in the app, the scripted time in UI tests (so their screenshots show the card settled).
double nowSeconds() { return ImGui::GetTime(); }

// The picture with the shading for the text baked in (left half and the lower part, under the tier cards), towards the
// look's background `ink`, so the card draws it as one rounded image.
Image loadPicture(ImVec4 ink) {
    Image img;
    std::string error;
    if (!Image::decode(assets::supportCard, size_t(assets::supportCard_size), img, error)) return {};
    const int w = img.width, h = img.height;
    for (int y = 0; y < h; ++y) {
        const float v = (float(y) + 0.5f) / float(h);
        const float bottom = 0.92f * easeInOut((v - 0.42f) / 0.5f);
        for (int x = 0; x < w; ++x) {
            const float u = (float(x) + 0.5f) / float(w);
            const float left = 0.9f * std::pow(1.0f - easeInOut(u / 0.66f), 1.3f);
            const float dark = 1.0f - (1.0f - left) * (1.0f - bottom);
            uint8_t* p = &img.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
            const float kInk[3] = {ink.x * 255.0f, ink.y * 255.0f, ink.z * 255.0f};
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
    const char* perks[4];
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
    supportCardNow_ = now;
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
    supportCard_->picture = std::async(std::launch::async, loadPicture, theme::shellBg);
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

    // Fade and scale in over 150 ms (out when closing). A modal pop-up: above every panel and pop-up, and nothing under
    // it takes input.
    constexpr const char* kPopup = "##support_card";
    float shown = easeOut(t / 0.15f);
    bool finished = false;
    if (card.closing) {
        const float c = float(now - card.closingAt) / 0.15f;
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

    // The current look's colours (Theme.h), so the card fits Classic and every other look.
    const ImVec4 bg = theme::shellBg, tierBg = theme::panelBg, raised = theme::header, accent = theme::accent;
    const ImVec4 ink = theme::textBright, body = theme::text, dim = theme::textDim, line = theme::text;

    const float s = uiScale_;
    const ImVec2 view = io.DisplaySize;
    // Logical size 1100 x 720, fitted to the window; scaled from 96 % while opening.
    const float fit = std::min({1.0f, (view.x - 40.0f * s) / (1100.0f * s), (view.y - 40.0f * s) / (720.0f * s)});
    const float zoom = lerp(0.96f, 1.0f, shown);
    const float k = s * std::max(fit, 0.45f) * zoom;
    const ImVec2 size(1100.0f * k, 720.0f * k);
    const ImVec2 a(std::floor((view.x - size.x) * 0.5f), std::floor((view.y - size.y) * 0.5f));
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
    const float hair = std::max(1.0f, k);

    // The app dimmed behind it, with a warm light from the card.
    dl->AddRectFilled(ImVec2(0, 0), view, col(0x060608, 0.80f * A));
    glow(dl, a, b, radius, 120.0f * k, accent, 0.04f * A, 16);
    glow(dl, ImVec2(a.x, a.y + 18.0f * k), ImVec2(b.x, b.y + 18.0f * k), radius, 40.0f * k, ImVec4(0, 0, 0, 1), 0.9f * A, 10);
    dl->AddRectFilled(a, b, tc(bg, A), radius);

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
        const float fade = easeOut(float(now - card.pictureAt) / 0.35f) * A;
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
        const float since = t - 0.5f;
        const float phase = since >= 0.0f ? std::fmod(since, period) : -1.0f;
        if (phase >= 0.0f && phase < run) {
            const float u = easeInOut(phase / run);
            lightSweep(dl, a, b, radius, lerp(a.x + size.x * 0.25f, b.x + size.x * 0.1f, u), 150.0f * k,
                       0.10f * A * std::sin(u * 3.14159265f));
        }
    }
    // A hairline edge, brighter along the top, and a thin accent line across the top.
    dl->AddRect(a, b, tc(line, 0.10f * A), radius, 0, hair);
    {
        const float w = size.x * 0.36f, cx = a.x + size.x * 0.5f;
        dl->AddRectFilledMultiColor(ImVec2(cx - w, a.y), ImVec2(cx, a.y + 2.0f * k), tc(accent, 0.0f), tc(accent, 0.9f * A),
                                    tc(accent, 0.9f * A), tc(accent, 0.0f));
        dl->AddRectFilledMultiColor(ImVec2(cx, a.y), ImVec2(cx + w, a.y + 2.0f * k), tc(accent, 0.9f * A), tc(accent, 0.0f),
                                    tc(accent, 0.0f), tc(accent, 0.9f * A));
    }

    const float pad = 48.0f * k;
    float y = a.y + 40.0f * k;
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
            closeSupportCard(false);
            showWhatsNew_ = true; // the cards, once the membership card has gone (PanelProject.cpp)
        }
        dl->AddRectFilled(c0, c1, tc(raised, (hot ? 1.0f : 0.8f) * A), 14.0f * k);
        dl->AddRect(c0, c1, tc(line, (hot ? 0.3f : 0.14f) * A), 14.0f * k, 0, hair);
        paint.text(boldUi, 13.0f, ImVec2(c0.x + 13.0f * k, c0.y + (28.0f * k - ts.y) * 0.5f), tc(ink, A), chip.c_str());
        y += 42.0f * k;
    }
    // Hero: the cube mark on its tile, the eyebrow and the promise.
    {
        const float m = 48.0f * k;
        const ImVec2 m0(a.x + pad, y), m1(m0.x + m, m0.y + m);
        cubeTile(dl, m0, m, k, A);
        const float ex = m1.x + 16.0f * k;
        paint.spaced(boldUi, 12.5f, ImVec2(ex, y + 6.0f * k), tc(accent, A), "SCULPTFORGE MEMBERSHIP", 2.2f);
        paint.text(bodyFont, 15.0f, ImVec2(ex, y + 25.0f * k), tc(dim, A), "Thank you for sculpting with SculptForge");
        y += m + 22.0f * k;
    }
    y += paint.text(titleFont, 40.0f, ImVec2(a.x + pad, y), tc(ink, A), "Free and open source.").y;
    { // the second line in the accent, with a soft glow
        const char* promise = "Members keep it moving.";
        for (int i = 0; i < 6; ++i) {
            const float ang = float(i) * 1.0472f, r = 3.0f * k;
            dl->AddText(titleFont, 40.0f * k, ImVec2(a.x + pad + std::cos(ang) * r, y + std::sin(ang) * r), tc(accent, 0.08f * A),
                        promise);
        }
        y += paint.text(titleFont, 40.0f, ImVec2(a.x + pad, y), tc(theme::accentHover, A), promise).y;
    }
    y += 14.0f * k;
    paint.text(bodyFont, 16.5f, ImVec2(a.x + pad, y), tc(body, A),
               "No ads, no accounts, no telemetry, and nothing is ever paywalled: members pay for time and influence. "
               "Every release stays free for everyone.",
               colW - pad);

    // Close (top right), always visible.
    {
        const float d = 34.0f * k;
        const ImVec2 c0(b.x - 18.0f * k - d, a.y + 18.0f * k);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton("Close##card.close", ImVec2(d, d));
        const bool hot = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) closeSupportCard(false);
        const ImVec2 c(c0.x + d * 0.5f, c0.y + d * 0.5f);
        dl->AddCircleFilled(c, d * 0.5f, tc(hot ? raised : bg, 0.85f * A));
        dl->AddCircle(c, d * 0.5f, tc(line, (hot ? 0.40f : 0.20f) * A), 0, hair);
        const float x = 6.0f * k;
        dl->AddLine(ImVec2(c.x - x, c.y - x), ImVec2(c.x + x, c.y + x), tc(ink, (hot ? 1.0f : 0.8f) * A), 1.6f * k);
        dl->AddLine(ImVec2(c.x - x, c.y + x), ImVec2(c.x + x, c.y - x), tc(ink, (hot ? 1.0f : 0.8f) * A), 1.6f * k);
    }

    // The three memberships, with the perks worded as on website/support.html (the full list is there).
    static const Tier kTiers[3] = {
        {"Supporter", "$5", "/ month",
         {"Your name in the credits, in the app and on the website", "The members' channel on Discord",
          "A vote in the monthly feature vote", "The monthly pack at month end"},
         "Join as a Supporter", "Join as a Supporter##card.supporter", kSupportUrlSupporter},
        {"Insider", "$20", "/ month",
         {"Everything in Supporter, and you propose the vote's options", "A daily drop: a brush, alpha or VDM",
          "Preview builds of the next release, about a week early", "The monthly dev Q&A"},
         "Become an Insider", "Become an Insider##card.insider", kSupportUrlInsider},
        {"Studio", kFoundingStudioOpen ? "$150" : "$300", "/ month",
         {"Insider for every artist in your company", "Your own build: presets, keymaps, brushes and packs baked in",
          "Up to 5 custom items a month; your requests go first", "Priority triage, a team channel, your logo"},
         "Studio membership", "Studio membership##card.studio", kSupportUrlStudio},
    };
    const float gap = 18.0f * k, footer = 62.0f * k, tierH = 318.0f * k;
    const float tierW = (size.x - pad * 2.0f - gap * 2.0f) / 3.0f;
    const float tierTop = b.y - footer - tierH;
    const char* clicked = nullptr;
    bool discord = false; // the Discord pill in the footer (opens the invite, keeps the card)
    for (int i = 0; i < 3; ++i) {
        const Tier& tier = kTiers[i];
        const bool featured = i == 1;
        const float appear = easeOut((t - 0.04f - 0.04f * float(i)) / 0.2f) * A;
        const float lift = card.hover[i] * 4.0f * k + (featured ? 12.0f * k : 0.0f);
        const ImVec2 p0(a.x + pad + float(i) * (tierW + gap), tierTop - lift + (1.0f - appear) * 14.0f * k);
        const ImVec2 p1(p0.x + tierW, p0.y + tierH + (featured ? 12.0f * k : 0.0f));
        const float r = 16.0f * k;
        if (featured) { // a soft breathing glow
            const float breathe = 0.8f + 0.2f * std::sin(t * 2.0f);
            glow(dl, p0, p1, r, 30.0f * k, accent, 0.6f * breathe * appear, 12);
        } else {
            glow(dl, ImVec2(p0.x, p0.y + 8.0f * k), ImVec2(p1.x, p1.y + 8.0f * k), r, 18.0f * k, ImVec4(0, 0, 0, 1), 0.5f * appear, 6);
        }
        { // glass over the picture; the featured card warmer towards the top
            const int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(p0, p1, col(0xffffff, 0.95f * appear), r);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, p0, ImVec2(p0.x, p1.y),
                                                          tc(featured ? mix(tierBg, accent, 0.16f) : raised, 1.0f),
                                                          tc(tierBg, 1.0f));
        }
        dl->AddRect(p0, p1, featured ? tc(accent, 0.95f * appear) : tc(line, (0.12f + 0.12f * card.hover[i]) * appear), r, 0,
                    (featured ? 2.0f : 1.0f) * k);

        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton((std::string("##card.tier") + char('0' + i)).c_str(), ImVec2(tierW, p1.y - p0.y - 70.0f * k));
        const bool cardHot = ImGui::IsItemHovered();

        const float ix = p0.x + 24.0f * k, iw = tierW - 48.0f * k;
        float ty = p0.y + 22.0f * k;
        if (featured) { // the badge, on the card's top edge
            const char* badge = "RECOMMENDED";
            const float bw = paint.spaced(boldUi, 11.0f, ImVec2(0, 0), 0, badge, 1.6f, false) + 24.0f * k;
            const ImVec2 b0(p0.x + (tierW - bw) * 0.5f, p0.y - 12.0f * k), b1(b0.x + bw, b0.y + 24.0f * k);
            glow(dl, b0, b1, 12.0f * k, 10.0f * k, accent, 0.6f * appear, 6);
            dl->AddRectFilled(b0, b1, tc(accent, appear), 12.0f * k);
            paint.spaced(boldUi, 11.0f, ImVec2(b0.x + 12.0f * k, b0.y + (24.0f * k - 11.0f * k) * 0.5f - 0.5f * k),
                         tc(theme::textOnAccent, appear), badge, 1.6f);
            ty += 4.0f * k;
        }
        const bool founding = i == 2 && kFoundingStudioOpen; // the launch offer: a quieter outlined badge
        if (founding) {
            const char* badge = "FOUNDING STUDIO";
            const float bw = paint.spaced(boldUi, 11.0f, ImVec2(0, 0), 0, badge, 1.6f, false) + 24.0f * k;
            const ImVec2 b0(p0.x + (tierW - bw) * 0.5f, p0.y - 12.0f * k), b1(b0.x + bw, b0.y + 24.0f * k);
            dl->AddRectFilled(b0, b1, tc(tierBg, appear), 12.0f * k);
            dl->AddRect(b0, b1, tc(accent, appear), 12.0f * k, 0, 1.5f * k);
            paint.spaced(boldUi, 11.0f, ImVec2(b0.x + 12.0f * k, b0.y + (24.0f * k - 11.0f * k) * 0.5f - 0.5f * k),
                         tc(theme::accentHover, appear), badge, 1.6f);
        }
        paint.text(headFont, 21.0f, ImVec2(ix, ty), tc(featured ? theme::accentHover : ink, appear), tier.name);
        ty += 32.0f * k;
        const ImVec2 ps = paint.text(titleFont, 44.0f, ImVec2(ix, ty), tc(ink, appear), tier.price);
        const ImVec2 pe = paint.measure(bodyFont, 16.5f, tier.per);
        float px = ix + ps.x + 6.0f * k;
        if (founding) { // the regular price, struck through
            const ImVec2 at(px, ty + ps.y - pe.y - 7.0f * k);
            const ImVec2 ws = paint.text(bodyFont, 16.5f, at, tc(dim, appear), "$300");
            dl->AddLine(ImVec2(at.x - 1.0f * k, at.y + ws.y * 0.55f), ImVec2(at.x + ws.x + 1.0f * k, at.y + ws.y * 0.55f),
                        tc(dim, appear), 1.5f * k);
            px += ws.x + 6.0f * k;
        }
        paint.text(bodyFont, 16.5f, ImVec2(px, ty + ps.y - pe.y - 7.0f * k), tc(dim, appear), tier.per);
        ty += ps.y + (founding ? 2.0f : 10.0f) * k;
        if (founding)
            ty += paint.text(boldUi, 12.5f, ImVec2(ix, ty), tc(theme::accentHover, appear),
                             "First 10 studios lock in $150/month for good", iw).y + 6.0f * k;
        dl->AddLine(ImVec2(ix, ty), ImVec2(ix + iw, ty), tc(featured ? accent : line, (featured ? 0.35f : 0.12f) * appear), hair);
        ty += 14.0f * k;
        // The perks, each after a check mark.
        for (const char* perk : tier.perks) {
            const float cy = ty + 9.5f * k, cr = 8.0f * k;
            const ImVec2 cc(ix + cr, cy);
            dl->AddCircleFilled(cc, cr, tc(accent, (featured ? 0.28f : 0.16f) * appear));
            const ImVec2 tick[3] = {ImVec2(cc.x - 3.6f * k, cc.y + 0.2f * k), ImVec2(cc.x - 1.0f * k, cc.y + 2.8f * k),
                                    ImVec2(cc.x + 3.8f * k, cc.y - 2.6f * k)};
            dl->AddPolyline(tick, 3, tc(theme::accentHover, appear), 0, 1.7f * k);
            ty += paint.text(ImGui::GetFont(), 14.5f, ImVec2(ix + 26.0f * k, ty), tc(featured ? ink : body, appear), perk,
                             iw - 26.0f * k)
                      .y +
                  7.0f * k;
        }

        // The one button, along the bottom.
        const ImVec2 c0(ix, p1.y - 22.0f * k - 46.0f * k), c1(p1.x - 24.0f * k, p1.y - 22.0f * k);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton(tier.id, ImVec2(c1.x - c0.x, c1.y - c0.y));
        const bool hot = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        if (ImGui::IsItemClicked()) clicked = tier.url;
        const float target = (cardHot || hot) ? 1.0f : 0.0f;
        card.hover[i] = lerp(card.hover[i], target, 1.0f - std::exp(-dt * 12.0f));
        if (featured) {
            glow(dl, c0, c1, 23.0f * k, 12.0f * k, accent, (hot ? 0.9f : 0.5f) * appear, 6);
            const ImVec4 top = held ? accent : hot ? mix(theme::accentHover, ImVec4(1, 1, 1, 1), 0.12f) : theme::accentHover;
            const int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(c0, c1, col(0xffffff, appear), 23.0f * k);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, c0, ImVec2(c0.x, c1.y), tc(top, 1.0f),
                                                          tc(held ? mix(accent, ImVec4(0, 0, 0, 1), 0.15f) : accent, 1.0f));
        } else {
            dl->AddRectFilled(c0, c1, tc(held ? theme::buttonPressed : hot ? theme::buttonHover : theme::button, appear), 23.0f * k);
            dl->AddRect(c0, c1, tc(hot ? accent : line, (hot ? 0.8f : 0.16f) * appear), 23.0f * k, 0, hair);
        }
        const ImVec2 ls = paint.measure(boldUi, 15.5f, tier.cta);
        paint.text(boldUi, 15.5f, ImVec2(std::floor((c0.x + c1.x - ls.x) * 0.5f), std::floor((c0.y + c1.y - ls.y) * 0.5f)),
                   tc(featured ? theme::textOnAccent : ink, appear), tier.cta);
    }

    // Footer: the one-time Supporter (pay what you want), the members' Discord, "I'm already a supporter" and "Maybe later".
    const float fy = b.y - footer * 0.5f;
    const float fa = easeOut((t - 0.1f) / 0.2f) * A;
    auto link = [&](const char* label, const char* id, ImVec2 at, const ImVec4& c, bool underline) {
        const ImVec2 ts = paint.measure(bodyFont, 15.0f, label);
        ImGui::SetCursorScreenPos(ImVec2(at.x - 4.0f * k, at.y - 6.0f * k));
        ImGui::InvisibleButton(id, ImVec2(ts.x + 8.0f * k, ts.y + 12.0f * k));
        const bool hot = ImGui::IsItemHovered();
        paint.text(bodyFont, 15.0f, at, tc(hot ? ink : c, fa), label);
        if (underline || hot) dl->AddLine(ImVec2(at.x, at.y + ts.y), ImVec2(at.x + ts.x, at.y + ts.y), tc(hot ? ink : c, 0.55f * fa), hair);
        return ImGui::IsItemClicked();
    };
    auto pill = [&](const char* label, const char* id, ImVec2 c0, float extra) { // a secondary button; returns hovered, clicked
        const ImVec2 ls = paint.measure(boldUi, 15.0f, label);
        const ImVec2 c1(c0.x + ls.x + 44.0f * k + extra, fy + 19.0f * k);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton(id, ImVec2(c1.x - c0.x, c1.y - c0.y));
        const bool hot = ImGui::IsItemHovered();
        dl->AddRectFilled(c0, c1, tc(hot ? theme::buttonHover : theme::button, 0.9f * fa), 19.0f * k);
        dl->AddRect(c0, c1, tc(line, (hot ? 0.40f : 0.18f) * fa), 19.0f * k, 0, hair);
        paint.text(boldUi, 15.0f, ImVec2(std::floor(c0.x + 22.0f * k + extra), std::floor(fy - ls.y * 0.5f)), tc(ink, fa), label);
        return std::pair<bool, bool>(hot, ImGui::IsItemClicked());
    };
    {
        const char* lead = "No subscription?";
        const ImVec2 ls = paint.measure(bodyFont, 15.0f, lead);
        const float ly = std::floor(fy - ls.y * 0.5f);
        paint.text(bodyFont, 15.0f, ImVec2(a.x + pad, ly), tc(dim, fa), lead);
        if (link("One-time Supporter", "One-time Supporter##card.once", ImVec2(a.x + pad + ls.x + 8.0f * k, ly), accent, true))
            clicked = kSupportUrlOnce;
        // The members' Discord: a pill with a drawn speech-bubble glyph (no logo asset). It opens the invite and leaves
        // the card open.
        const ImVec2 gs = paint.measure(bodyFont, 15.0f, "One-time Supporter");
        const float glyph = 18.0f * k;
        const ImVec2 c0(std::floor(a.x + pad + ls.x + 8.0f * k + gs.x + 28.0f * k), fy - 19.0f * k);
        const auto [hot, hit] = pill("Join the members on Discord", "Join the members on Discord##card.discord", c0, glyph + 10.0f * k);
        discord = hit;
        const ImVec2 g0(c0.x + 20.0f * k, fy - glyph * 0.42f), g1(g0.x + glyph, fy + glyph * 0.30f);
        dl->AddRectFilled(g0, g1, tc(hot ? ink : accent, fa), 5.0f * k);
        dl->AddTriangleFilled(ImVec2(g0.x + 4.0f * k, g1.y - 1.0f), ImVec2(g0.x + 10.0f * k, g1.y - 1.0f),
                              ImVec2(g0.x + 3.0f * k, g1.y + 5.0f * k), tc(hot ? ink : accent, fa));
        const float my = (g0.y + g1.y) * 0.5f, r = 1.6f * k;
        for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(ImVec2((g0.x + g1.x) * 0.5f + float(i) * 4.5f * k, my), r, tc(bg, fa));
    }
    bool later = false, supporter = false;
    {
        const char* label = "Maybe later";
        const ImVec2 ls = paint.measure(boldUi, 15.0f, label);
        const ImVec2 c0(b.x - pad - ls.x - 44.0f * k, fy - 19.0f * k);
        const auto [hot, hit] = pill(label, "Maybe later##card.later", c0, 0.0f);
        later = hit;
        if (hot) ImGui::SetTooltip("Hides this card for 30 days.");
        const char* already = "I'm already a supporter";
        const ImVec2 as = paint.measure(bodyFont, 15.0f, already);
        supporter = link(already, "I'm already a supporter##card.supporter_already",
                         ImVec2(c0.x - 24.0f * k - as.x, std::floor(fy - as.y * 0.5f)), dim, false);
    }

    // Esc, or a click outside the card: closed until the next start it's due (Maybe later: 30 days).
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
        } else if (later) {
            supportCardPrefs_.laterAt = supportCardNow_;
            saveProjectPrefs();
            closeSupportCard(false);
        } else if (outside || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            closeSupportCard(false);
        }
    }
    ImGui::EndPopup();
}

void App::drawSupportPrefs() {
    if (ImGui::Checkbox("Show the support card at launch", &supportCardPrefs_.enabled)) saveProjectPrefs();
    ui::HelpText("Show the support card at launch", nullptr,
                 "A card with the SculptForge memberships, shown while it starts: the first time, after "
                 "each update, and at most once a week otherwise; Maybe later hides it for 30 days. Never while you work.",
                 "Preferences > Support");
    if (ImGui::Checkbox("I'm a supporter", &supportCardPrefs_.supporter)) saveProjectPrefs();
    ui::HelpText("I'm a supporter", nullptr,
                 "Thank you! The support card never shows. Kept on this computer only: SculptForge has no accounts.",
                 "Preferences > Support");
    if (ImGui::Button("Show the support card", ImVec2(-1, 0))) showSupportCard(SupportCardReason::Weekly);
}

} // namespace sf
