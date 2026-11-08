// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The membership cards (Support.h). The full card: a full-window card over the app on a calm stage (a warm glow and a
// faint grid, drawn here, no picture), the cube mark and the promise, the two memberships (Member and Studio) with
// their perks, in the current look's colours (Theme.h); on the first start only, and from Help > Support. The small
// card: one line along the bottom of the canvas at a good moment (memberMoment), at most once a week, until someone
// becomes a member. Never mid-stroke or during a job, never for a member, never in tests, renders, demos or CI.
// The top bar's quiet heart opens Help > Support SculptForge (a member sees a small badge instead).
// No countdowns, no guilt, nothing pre-selected; Close is always there. Every button only opens one of the fixed
// links in Support.h in the browser, except "I'm already a member", which is the member builds' GitHub sign-in.
#include "app/App.h"
#include "app/NativeDialog.h"
#include "app/CardPaint.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <utility>

namespace sf {

struct App::SupportCard {
    SupportCardReason reason = SupportCardReason::None;
    double openedAt = 0.0;          // seconds (ImGui clock)
    ImVec2 parallax{0.0f, 0.0f};    // follows the pointer, eased
    float hover[2] = {0.0f, 0.0f}; // tier cards lifted under the pointer, eased
    bool closing = false;
    double closingAt = 0.0;
};

void App::SupportCardDelete::operator()(SupportCard* c) const { delete c; }

using namespace card;

namespace {

// ImGui's clock: real time in the app, the scripted time in UI tests (so their screenshots show the card settled).
double nowSeconds() { return ImGui::GetTime(); }

// The stage behind the words: a soft warm light from `center` (`rx` x `ry`, brightest in the middle) and a faint
// drafting grid that fades out with it, kept inside the card's rounded corners. The same look as the membership page.
void stageLight(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImVec2 center, float rx, float ry, const ImVec4& tint,
                float alpha, const ImVec4& gridInk, float gridAlpha, float cell, float hair) {
    if (alpha <= 0.002f) return;
    auto falloff = [&](ImVec2 p) {
        const float dx = (p.x - center.x) / rx, dy = (p.y - center.y) / ry;
        const float d = clamp01(1.0f - std::sqrt(dx * dx + dy * dy));
        return d * d;
    };
    auto edgeFade = [&](ImVec2 p) {
        const float e = std::min({p.x - a.x, b.x - p.x, p.y - a.y, b.y - p.y});
        return clamp01((e - rounding * 0.3f) / (rounding * 1.5f));
    };
    dl->PushClipRect(a, b, true);
    { // the light: a fan of rings with the colour per vertex
        constexpr int kSeg = 64, kRings = 8;
        const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
        auto ring = [&](int r) { return float(r) / float(kRings); };
        dl->PrimReserve(kSeg * kRings * 6, kSeg * (kRings + 1));
        const ImDrawIdx base = ImDrawIdx(dl->_VtxCurrentIdx);
        for (int r = 0; r <= kRings; ++r)
            for (int i = 0; i < kSeg; ++i) {
                const float ang = float(i) / float(kSeg) * 6.28318531f, f = ring(r);
                const ImVec2 p(center.x + std::cos(ang) * rx * f, center.y + std::sin(ang) * ry * f);
                dl->PrimWriteVtx(p, uv, tc(tint, alpha * (1.0f - f) * (1.0f - f) * edgeFade(p)));
            }
        for (int r = 0; r < kRings; ++r)
            for (int i = 0; i < kSeg; ++i) {
                const int j = (i + 1) % kSeg;
                const ImDrawIdx v0 = ImDrawIdx(base + r * kSeg + i), v1 = ImDrawIdx(base + r * kSeg + j);
                const ImDrawIdx v2 = ImDrawIdx(v0 + kSeg), v3 = ImDrawIdx(v1 + kSeg);
                dl->PrimWriteIdx(v0), dl->PrimWriteIdx(v1), dl->PrimWriteIdx(v3);
                dl->PrimWriteIdx(v0), dl->PrimWriteIdx(v3), dl->PrimWriteIdx(v2);
            }
    }
    // The grid, in short pieces so each fades with the light.
    const float step = cell * 0.5f;
    for (float x = a.x + cell; x < b.x; x += cell)
        for (float y = a.y; y < b.y; y += step) {
            const ImVec2 p(x, y + step * 0.5f);
            const float w = gridAlpha * alpha * falloff(p) * edgeFade(p);
            if (w > 0.002f) dl->AddLine(ImVec2(x, y), ImVec2(x, std::min(y + step, b.y)), tc(gridInk, w), hair);
        }
    for (float y = a.y + cell; y < b.y; y += cell)
        for (float x = a.x; x < b.x; x += step) {
            const ImVec2 p(x + step * 0.5f, y);
            const float w = gridAlpha * alpha * falloff(p) * edgeFade(p);
            if (w > 0.002f) dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + step, b.x), y), tc(gridInk, w), hair);
        }
    dl->PopClipRect();
}

struct Tier {
    const char* name;
    const char* price;
    const char* per;
    const char* perks[5]; // (unused ones nullptr)
    const char* cta;
    const char* id; // the button's ImGui label (UI tests click it)
    const char* url;
    const char* note; // under the price (nullptr: none)
};

} // namespace

bool App::supportCardTestMode() const {
    return !interactive() || !options_.uiTest.empty() || !options_.demo.empty() || !options_.script.empty() ||
           options_.measureIdle > 0.0f || options_.measureStroke > 0.0f || options_.benchmark ||
           !options_.timelapsePath.empty() || std::getenv("SF_NO_SUPPORT_CARD") || std::getenv("CI");
}

bool App::supportCardOpen() const { return supportCard_ && !supportCard_->closing; }


void App::launchSupportCard(bool firstRun, int64_t now, bool testMode) {
    // A studio build is a Studio member's: no membership card in it (core/StudioProfile.h).
    const SupportCardReason reason =
        isStudioBuild() ? SupportCardReason::None : supportCardDue(supportCardPrefs_, firstRun, testMode);
    if (reason == SupportCardReason::None) return;
    supportCardPrefs_.shownAt = now; // (the week before a small card may show starts now)
    saveProjectPrefs(); // (recorded before it shows: a crash can't bring it back at once)
    if (showWelcome_) { // the first-run welcome follows the card
        showWelcome_ = false;
        welcomeAfterCard_ = true;
    }
    showSupportCard(reason);
}

void App::showSupportCard(SupportCardReason reason) {
    supportCard_.reset(new SupportCard);
    supportCard_->reason = reason;
    supportCard_->openedAt = nowSeconds();
    redrawIn(0.0);
}

void App::closeSupportCard() {
    if (!supportCard_ || supportCard_->closing) return;
    supportCard_->closing = true;
    supportCard_->closingAt = nowSeconds();
    if (welcomeAfterCard_) {
        welcomeAfterCard_ = false;
        showWelcome_ = true;
    }
    redrawIn(0.0);
}

void App::openSupportLink(const char* url) {
    if (!url || !supportLinkAllowed(url)) return;
    ++supportPageRequests_;
    if (!interactive()) { // UI tests, renders and demos never start a browser
        status_ = std::string("Support SculptForge: ") + url;
        return;
    }
    status_ = sf::dialogs::openUrl(url) ? std::string("Opened ") + url + " in your browser"
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

    // The current look's colours (Theme.h), so the card fits Classic and every other look.
    const ImVec4 bg = theme::shellBg, tierBg = theme::panelBg, raised = theme::header, accent = theme::accent;
    const ImVec4 ink = theme::textBright, body = theme::text, dim = theme::textDim, line = theme::text;

    const float s = uiScale_;
    const ImVec2 view = io.DisplaySize;
    // Logical size 1100 x 740, fitted to the window; scaled from 96 % while opening.
    const float fit = std::min({1.0f, (view.x - 40.0f * s) / (1100.0f * s), (view.y - 40.0f * s) / (740.0f * s)});
    const float zoom = lerp(0.96f, 1.0f, shown);
    const float k = s * std::max(fit, 0.45f) * zoom;
    const ImVec2 size(1100.0f * k, 740.0f * k);
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

    // The stage: a warm light at the top right that drifts a little with the pointer, and a faint grid under it.
    const ImVec2 mouse = io.MousePos;
    ImVec2 target(0.0f, 0.0f);
    if (ImGui::IsMousePosValid(&mouse)) {
        target.x = std::clamp((mouse.x - (a.x + b.x) * 0.5f) / (size.x * 0.5f), -1.0f, 1.0f);
        target.y = std::clamp((mouse.y - (a.y + b.y) * 0.5f) / (size.y * 0.5f), -1.0f, 1.0f);
    }
    const float follow = 1.0f - std::exp(-dt * 4.0f);
    card.parallax.x = lerp(card.parallax.x, target.x, follow);
    card.parallax.y = lerp(card.parallax.y, target.y, follow);
    {
        const ImVec2 c(a.x + size.x * 0.76f + card.parallax.x * 14.0f * k, a.y + size.y * 0.06f + card.parallax.y * 10.0f * k);
        stageLight(dl, a, b, radius, c, size.x * 0.46f, size.y * 0.62f, accent, 0.22f * A, line, 0.20f, 48.0f * k, hair);
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
    // Hero: the bust, the eyebrow and the promise.
    {
        const float m = 48.0f * k;
        const ImVec2 m0(a.x + pad, y), m1(m0.x + m, m0.y + m);
        markTile(dl, m0, m, A);
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
        if (ImGui::IsItemClicked()) closeSupportCard();
        const ImVec2 c(c0.x + d * 0.5f, c0.y + d * 0.5f);
        dl->AddCircleFilled(c, d * 0.5f, tc(hot ? raised : bg, 0.85f * A));
        dl->AddCircle(c, d * 0.5f, tc(line, (hot ? 0.40f : 0.20f) * A), 0, hair);
        const float x = 6.0f * k;
        dl->AddLine(ImVec2(c.x - x, c.y - x), ImVec2(c.x + x, c.y + x), tc(ink, (hot ? 1.0f : 0.8f) * A), 1.6f * k);
        dl->AddLine(ImVec2(c.x - x, c.y + x), ImVec2(c.x + x, c.y - x), tc(ink, (hot ? 1.0f : 0.8f) * A), 1.6f * k);
    }

    // The two memberships, with the perks worded as on website/support.html.
    const bool yearly = kSupportUrlMemberYearly[0] != '\0'; // (the yearly checkout, once it exists)
    const Tier kTiers[2] = {
        {"Member", "$5", "/ month",
         {"Betas and early builds of the next version", "The monthly feature vote: pick what we build next",
          "Member Requests: one a month, built first, credited to you", "The members' channel on Discord",
          "Your name in the credits, in the app and on the website"},
         "Become a member", "Become a member##card.member", kSupportUrlMember, yearly ? "or $48 a year" : nullptr},
        {"Studio", "$300", "/ month",
         {"A flat rate for the whole studio: any number of artists, each with everything in Member",
          "Your studio's own build: brushes, alphas, Lit Spheres, keymaps, logo",
          "Isolated: a private release channel, no outside connections",
          "First response within 2 business days; up to 3 custom items a month", nullptr},
         "Studio membership", "Studio membership##card.studio", kStudioPageUrl, "Founding Studio: $150, first 10 studios"},
    };
    const float gap = 22.0f * k, footer = 62.0f * k, tierH = 350.0f * k;
    const float tierW = (size.x - pad * 2.0f - gap) / 2.0f;
    const float tierTop = b.y - footer - tierH;
    const char* clicked = nullptr;
    bool discord = false; // the Discord pill in the footer (opens the invite, keeps the card)
    bool terms = false;   // Terms & refunds in the footer (opens the terms page, keeps the card)
    for (int i = 0; i < 2; ++i) {
        const Tier& tier = kTiers[i];
        const bool featured = i == 0;
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
        { // the tier card; the featured one warmer towards the top
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
        paint.text(headFont, 21.0f, ImVec2(ix, ty), tc(featured ? theme::accentHover : ink, appear), tier.name);
        ty += 32.0f * k;
        const ImVec2 ps = paint.text(titleFont, 44.0f, ImVec2(ix, ty), tc(ink, appear), tier.price);
        const ImVec2 pe = paint.measure(bodyFont, 16.5f, tier.per);
        paint.text(bodyFont, 16.5f, ImVec2(ix + ps.x + 6.0f * k, ty + ps.y - pe.y - 7.0f * k), tc(dim, appear), tier.per);
        ty += ps.y + 2.0f * k;
        if (tier.note) ty += paint.text(bodyFont, 14.0f, ImVec2(ix, ty), tc(dim, appear), tier.note).y;
        ty += 10.0f * k;
        dl->AddLine(ImVec2(ix, ty), ImVec2(ix + iw, ty), tc(featured ? accent : line, (featured ? 0.35f : 0.12f) * appear), hair);
        ty += 14.0f * k;
        // The perks, each after a check mark.
        for (const char* perk : tier.perks) {
            if (!perk) break;
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

    // Footer: Member yearly (once that checkout exists), the members' Discord, "I'm already a member" and "Not now".
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
        float x = a.x + pad;
        if (yearly) {
            const char* lead = "Prefer yearly?";
            const ImVec2 ls = paint.measure(bodyFont, 15.0f, lead);
            const float ly = std::floor(fy - ls.y * 0.5f);
            paint.text(bodyFont, 15.0f, ImVec2(x, ly), tc(dim, fa), lead);
            const char* label = "Member, $48 a year";
            if (link(label, "Member, $48 a year##card.member_yearly", ImVec2(x + ls.x + 8.0f * k, ly), accent, true))
                clicked = kSupportUrlMemberYearly;
            x = std::floor(x + ls.x + 8.0f * k + paint.measure(bodyFont, 15.0f, label).x + 28.0f * k);
        }
        // The members' Discord: a pill with a drawn speech-bubble glyph (no logo asset). It opens the invite and leaves
        // the card open.
        const float glyph = 18.0f * k;
        const ImVec2 c0(x, fy - 19.0f * k);
        const auto [hot, hit] = pill("Join the members on Discord", "Join the members on Discord##card.discord", c0, glyph + 10.0f * k);
        discord = hit;
        const ImVec2 g0(c0.x + 20.0f * k, fy - glyph * 0.42f), g1(g0.x + glyph, fy + glyph * 0.30f);
        dl->AddRectFilled(g0, g1, tc(hot ? ink : accent, fa), 5.0f * k);
        dl->AddTriangleFilled(ImVec2(g0.x + 4.0f * k, g1.y - 1.0f), ImVec2(g0.x + 10.0f * k, g1.y - 1.0f),
                              ImVec2(g0.x + 3.0f * k, g1.y + 5.0f * k), tc(hot ? ink : accent, fa));
        const float my = (g0.y + g1.y) * 0.5f, r = 1.6f * k;
        for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(ImVec2((g0.x + g1.x) * 0.5f + float(i) * 4.5f * k, my), r, tc(bg, fa));
        // The fine print: the membership terms (refunds, cancelling), after the pill.
        const char* label = tr("Terms & refunds");
        const float tx = std::floor(c0.x + paint.measure(boldUi, 15.0f, "Join the members on Discord").x + 44.0f * k + glyph +
                                    10.0f * k + 24.0f * k);
        terms = link(label, "##card.terms", ImVec2(tx, std::floor(fy - paint.measure(bodyFont, 15.0f, label).y * 0.5f)), dim, true);
    }
    bool later = false, supporter = false;
    {
        const char* label = "Not now";
        const ImVec2 ls = paint.measure(boldUi, 15.0f, label);
        const ImVec2 c0(b.x - pad - ls.x - 44.0f * k, fy - 19.0f * k);
        const auto [hot, hit] = pill(label, "Not now##card.later", c0, 0.0f);
        later = hit;
        if (hot) ImGui::SetTooltip("%s", tr("Nothing about memberships for a week."));
        const char* already = "I'm already a member";
        const ImVec2 as = paint.measure(bodyFont, 15.0f, already);
        supporter = link(already, "I'm already a member##card.supporter_already",
                         ImVec2(c0.x - 24.0f * k - as.x, std::floor(fy - as.y * 0.5f)), dim, false);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Sign in with GitHub (the member builds' sign-in): no more cards."));
    }

    // Esc, or a click outside the card: closed (the small card may follow in a week).
    const bool outside = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
                         !ImRect(a, b).Contains(io.MousePos) && t > 0.3f;
    if (!card.closing) {
        if (clicked) {
            openSupportLink(clicked);
            memberCardChoice(MemberCardChoice::Become);
            closeSupportCard();
        } else if (discord) {
            openDiscordPage(); // the card stays: a membership may still follow
        } else if (terms) {
            openSupportLink(kTermsUrl); // the card stays too
        } else if (supporter) {
            closeSupportCard();
            verifyMember(); // the small card shows the sign-in's steps
        } else if (later) {
            memberCardChoice(MemberCardChoice::NotNow);
            closeSupportCard();
        } else if (outside || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            closeSupportCard();
        }
    }
    ImGui::EndPopup();
}

void App::drawSupportPrefs() {
    if (supportCardPrefs_.supporter || memberSignedIn()) {
        ImGui::TextWrapped("%s", "Thank you for being a member! No membership cards show.");
    } else {
        ImGui::TextWrapped("%s", "A small membership card shows at most once a week, at a good moment (never mid-stroke), "
                                 "until you become a member. After a third Not now it waits a month; after a sixth it "
                                 "only comes with an update.");
        if (ImGui::Button(tr("I'm already a member##prefs.member_verify"), ImVec2(-1, 0))) verifyMember();
        ui::HelpText("I'm already a member", nullptr,
                     "Signs in with GitHub, as member builds do (Preferences > Updates): no more membership cards, "
                     "and the member builds are on.",
                     "Preferences > Support");
    }
    if (ImGui::Button(tr("Show the memberships"), ImVec2(-1, 0))) showSupportCard(SupportCardReason::Manual);
}

// --- The small card at good moments ---

int64_t App::memberClock() const { return int64_t(std::time(nullptr)) + memberClockOffset_; }

void App::memberMoment(MemberMoment m) {
    const bool testMode = supportCardTestMode() && !memberCardTestMode_;
    if (m == MemberMoment::None || memberCard_ != MemberMoment::None || memberVerifying_) return;
    if (studioMembership() || !memberMomentAllowed(supportCardPrefs_, memberSignedIn(), memberClock(), testMode, m)) return;
    memberMomentPending_ = m; // (the latest moment's words)
    redrawIn(0.0);
}

void App::memberMilestoneCheck() {
    if (memberCard_ != MemberMoment::None || memberMomentPending_ != MemberMoment::None || memberVerifying_) return;
    if (const MemberMoment m = milestoneDue(supportCardPrefs_); m != MemberMoment::None) memberMoment(m);
}

void App::countMemberSave() {
    if (supportCardTestMode() && !memberCardTestMode_) return; // (counted on this computer, never sent)
    supportCardPrefs_.saves = std::min(supportCardPrefs_.saves + 1, 100000000);
    memberMilestoneCheck();
}

void App::tickMemberCard(float dt) {
    const bool stroking = doc_.sculptor.strokeActive();
    if (stroking) {
        sessionSculptSeconds_ = addSculptTime(sessionSculptSeconds_, double(dt));
        if (!supportCardTestMode() || memberCardTestMode_)
            supportCardPrefs_.sculptSeconds = addSculptTime(supportCardPrefs_.sculptSeconds, double(dt));
    }
    if (sculptMomentDue(sessionSculptSeconds_, sculptMomentDone_)) {
        sculptMomentDone_ = true;
        memberMoment(MemberMoment::Sculpted);
    }
    if (!stroking && ImGui::GetTime() - milestoneCheckedAt_ >= 60.0) { // the sculpting hours: once a minute is plenty
        milestoneCheckedAt_ = ImGui::GetTime();
        memberMilestoneCheck();
    }
    if (memberMomentPending_ == MemberMoment::None) return;
    const bool otherCard = supportCard_ || showWelcome_ || quickTourOpen_ || whatsNew_ || showWhatsNew_ ||
                           ImGui::GetTopMostPopupModal() != nullptr;
    if (!memberCardMayOpen(stroking, backgroundBusy(), otherCard)) return; // (it waits)
    memberCard_ = std::exchange(memberMomentPending_, MemberMoment::None);
    supportCardPrefs_.shownAt = memberClock();
    // A milestone shows once, with its true number now; the feed's honest lines take turns, card by card.
    supportCardPrefs_.milestonesShown |= milestoneBit(memberCard_);
    memberCardMilestone_ = milestoneLine(memberCard_, supportCardPrefs_);
    memberCardFeedLine_ = supportFeedLine(supportCardPrefs_.feed, supportCardPrefs_.shownCount);
    supportCardPrefs_.shownCount = std::min(supportCardPrefs_.shownCount + 1, 1000000);
    saveProjectPrefs(); // (recorded as it shows: a crash can't bring it back at once)
    redrawIn(0.0);
}

void App::memberCardChoice(MemberCardChoice c) {
    applyMemberCardChoice(supportCardPrefs_, c, memberClock());
    if (c == MemberCardChoice::SignedIn) status_ = "Thank you for being a member! No more membership cards.";
    if (c == MemberCardChoice::NoSignIn)
        status_ = "No member sign-in: no membership cards for three months (Preferences > Support signs in later).";
    memberCard_ = MemberMoment::None;
    saveProjectPrefs();
}

void App::verifyMember() {
    if (memberSignedIn()) { // (already signed in for member builds)
        memberCardChoice(MemberCardChoice::SignedIn);
        return;
    }
    memberVerifying_ = true;
    memberMomentPending_ = MemberMoment::None;
    startMemberSignIn([this](bool member) { memberSignInFinished(member); }); // (PanelUpdate.cpp: the one sign-in)
    redrawIn(0.0);
}

void App::memberSignInFinished(bool member) {
    memberVerifying_ = false;
    // A member: gone for good (and member builds are on). Not signed in (cancelled, declined, no access): 3 months.
    memberCardChoice(member ? MemberCardChoice::SignedIn : MemberCardChoice::NoSignIn);
}

void App::drawMemberCard() {
    if (memberCard_ == MemberMoment::None && !memberVerifying_) return;
    const float s = uiScale_;
    const CanvasRect c = canvasRect();
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 16.0f * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(560.0f * s, c.w - 32.0f * s), 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * s, 14.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * s);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::accent);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    const bool began = ImGui::Begin("##member_card", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (began) {
        ImFont* bold = boldFont();
        if (memberVerifying_) {
            if (bold) ImGui::PushFont(bold);
            ImGui::TextWrapped("%s", "Sign in with the GitHub account that has the members' invitation.");
            if (bold) ImGui::PopFont();
            ImGui::TextDisabled("%s", "It can only read the member builds; Preferences > Updates signs out.");
            ImGui::Spacing();
            if (memberSignIn_) drawMemberSignInSteps(); // (PanelUpdate.cpp)
            else memberSignInFinished(memberSignedIn()); // (gone without an answer: Sign out in Preferences meanwhile)
        } else {
            std::string line;
            switch (memberCard_) {
            case MemberMoment::Sculpted: line = "Twenty minutes of sculpting. Members keep SculptForge free and moving."; break;
            case MemberMoment::SavedRender: line = "Nice render. Members keep SculptForge free and moving."; break;
            case MemberMoment::Updated:
                line = std::string("SculptForge ") + appVersionLabel() + " is here. Members get new versions early, as betas.";
                break;
            case MemberMoment::Challenge:
                line = challengeTitle_.empty() ? std::string("The monthly challenge results are in, on Discord.")
                                               : "The " + challengeTitle_ + " challenge results are in, on Discord.";
                break;
            case MemberMoment::Starts:
            case MemberMoment::SculptHours:
            case MemberMoment::Saves: line = formatCardLine(memberCardMilestone_, tr(memberCardMilestone_.format)); break;
            case MemberMoment::None: break;
            }
            if (bold) ImGui::PushFont(bold);
            ImGui::TextWrapped("%s", line.c_str());
            if (bold) ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::textDim);
            ImGui::TextWrapped("%s", "$5 a month or $48 a year: betas early, the feature vote, Member Requests and your "
                                     "name in the credits. Nothing in the app is ever paywalled.");
            ImGui::PopStyleColor();
            // The feed's honest line in turn (latest.json's "support"), else the Founding Members' offer.
            const FoundingMembers& founding = foundingMembers();
            if (memberCardFeedLine_.format) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::accentHover);
                ImGui::TextWrapped("%s", formatCardLine(memberCardFeedLine_, tr(memberCardFeedLine_.format)).c_str());
                ImGui::PopStyleColor();
            } else if (founding.open()) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::accentHover);
                ImGui::TextWrapped(tr("The first %d members are Founding Members: a badge, their name in the credits and "
                                   "their price locked for good."), founding.limit);
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, theme::accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::accentHover);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::textOnAccent);
            const bool become = ImGui::Button(tr("Become a member##member_card.become"));
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            const bool notNow = ImGui::Button(tr("Not now##member_card.not_now"));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Nothing about memberships for a week."));
            ImGui::SameLine();
            const bool already = ImGui::Button(tr("I'm already a member##member_card.already"));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Sign in with GitHub (the member builds' sign-in): no more cards."));
            { // the fine print: the membership terms (refunds, cancelling), a quiet link after the buttons (or under them)
                const char* terms = tr("Terms & refunds##member_card.terms");
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(terms, nullptr, true).x) ImGui::NewLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, theme::textDim);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                if (ImGui::SmallButton(terms)) openSupportLink(kTermsUrl);
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
            }
            if (become) {
                openSupportPage(); // both choices (monthly, yearly) on the membership page
                memberCardChoice(MemberCardChoice::Become);
            } else if (notNow) {
                memberCardChoice(MemberCardChoice::NotNow);
            } else if (already) {
                memberCard_ = MemberMoment::None;
                verifyMember();
            }
        }
    }
    ImGui::End();
}

// --- The top bar's heart ---

bool App::drawSupportHeart(ImVec2 at, float size, ImDrawList* dl) {
    if (studioMembership()) return false; // (a Studio member's own build)
    const bool member = supportCardPrefs_.supporter || memberSignedIn();
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton(member ? "Member##topbar.member" : "Support SculptForge##topbar.heart",
                                                ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 b(at.x + size, at.y + size);
    if (hovered && !member) dl->AddRectFilled(at, b, theme::u32(theme::frameHover), theme::rounding(3.0f * uiScale_));
    // Quiet: the dim text colour like the window buttons; the accent only under the pointer (a member's badge: the
    // accent, small and still). No animation, no count.
    const ImU32 col = theme::u32(member ? theme::accent : hovered ? theme::accentHover : theme::textDim, member ? 0.85f : 1.0f);
    ui::CentredGlyph(dl, nullptr, at, b, col, member ? ICON_LC_BADGE_CHECK : ICON_LC_HEART);
    if (member) {
        ui::HelpText("Member", nullptr, "Thank you for being a member: you keep SculptForge free and growing.", "Top bar");
    } else {
        ui::HelpText("Support SculptForge", nullptr,
                     "SculptForge is free and open source. Members keep it free and growing (Help > Support SculptForge).",
                     "Top bar");
        if (clicked) {
            if (const Action* a = actions_.find("help.support"); a && a->run) a->run();
        }
    }
    return true;
}

} // namespace sf
