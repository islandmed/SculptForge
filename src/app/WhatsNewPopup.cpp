// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What's New: a centred card over the app, shown once after an update and from Help > What's New. A hero band (the bust in
// forge light, "WHAT'S NEW · X", the version's theme and tagline), then its feature cards in one row, each with a drawn
// picture (an icon on the forge background: never an app capture, no text in it; tools/whats_new_cards.py), a measured
// win if it has one, a title, one line and a Try it button that opens the feature; the footer has the full release notes
// (the guide chapter), the Discord and Close. The data is the version's ```features block in docs/guide/whats-new.md
// (core/WhatsNew.h); the colours are the current look's (Theme.h). It fades and scales in, follows the UI scale, and
// works from the keyboard: Esc closes, Tab and the arrow keys move between the cards, Enter tries the focused one.
#include "app/App.h"
#include "app/CardPaint.h"
#include "app/I18n.h"
#include "app/Theme.h"
#include "core/WhatsNew.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace sf {

using namespace card;

struct App::WhatsNewPopup {
    whatsnew::Version version;
    double openedAt = 0.0;
    bool closing = false;
    double closingAt = 0.0;
    std::vector<float> hover; // per card, eased
    int focus = -1;           // the card the keyboard is on
};

void App::WhatsNewDelete::operator()(WhatsNewPopup* p) const { delete p; }

namespace {

double nowSeconds() { return ImGui::GetTime(); }

bool isGuideAction(const std::string& a) { return a.rfind("guide:", 0) == 0; }

// The picture's part that covers a box of aspect BOX (centred), as UVs.
void coverUv(float image, float box, ImVec2& uv0, ImVec2& uv1) {
    uv0 = ImVec2(0, 0), uv1 = ImVec2(1, 1);
    if (image > box) {
        const float u = box / image;
        uv0.x = 0.5f - u * 0.5f, uv1.x = 0.5f + u * 0.5f;
    } else if (image < box) {
        const float v = image / box;
        uv0.y = 0.5f - v * 0.5f, uv1.y = 0.5f + v * 0.5f;
    }
}

} // namespace

bool App::whatsNewOpen() const { return whatsNew_ && !whatsNew_->closing; }

const whatsnew::Version* App::whatsNewVersion() const { return whatsNew_ ? &whatsNew_->version : nullptr; }

void App::showWhatsNew() {
    const guide::Document* doc = guide_.document("whats-new.md");
    // SF_WHATS_NEW_VERSION shows the window as of another version (UI tests keep checking 0.10.7's cards).
    const char* as = std::getenv("SF_WHATS_NEW_VERSION");
    const whatsnew::Page page = doc ? whatsnew::build(*doc, as && *as ? as : SCULPTFORGE_VERSION) : whatsnew::Page{};
    if (page.versions.empty() || page.versions[0].cards.empty()) { // nothing to show as cards: the chapter
        guide_.open("whats-new.md");
        return;
    }
    whatsNew_.reset(new WhatsNewPopup);
    whatsNew_->version = page.versions[0];
    whatsNew_->openedAt = nowSeconds();
    whatsNew_->hover.assign(whatsNew_->version.cards.size(), 0.0f);
    redrawIn(0.0);
}

void App::closeWhatsNew() {
    if (!whatsNew_ || whatsNew_->closing) return;
    whatsNew_->closing = true;
    whatsNew_->closingAt = nowSeconds();
    redrawIn(0.0);
}

bool App::runWhatsNewAction(const std::string& action) {
    ++whatsNewTries_;
    whatsNewLastAction_ = action;
    auto after = [&](const char* prefix) { return action.substr(std::string_view(prefix).size()); };
    if (action == "look:new") {
        chooseNewLook(true);
    } else if (action == "card:membership") {
        showSupportCard(SupportCardReason::Manual);
    } else if (action.rfind("brush:", 0) == 0) {
        const std::string name = after("brush:");
        for (int i = 0; i < int(doc_.brushes.size()); ++i)
            if (doc_.brushes[size_t(i)].name == name) {
                doc_.activeBrush = i;
                paintMode_ = false;
                status_ = "Brush: " + name;
                return true;
            }
        status_ = "No brush named " + name;
        return false;
    } else if (isGuideAction(action)) {
        guide_.open(after("guide:"));
    } else if (const Action* a = actions_.find(action); a && a->run && (!a->enabled || a->enabled())) {
        actions_.touch(action);
        a->run();
    } else {
        status_ = "What's New: nothing to open for " + action;
        return false;
    }
    return true;
}

bool App::whatsNewEvent(const SDL_Event& event) {
    if (!whatsNew_ || supportCard_) return false; // (a membership card opened from it takes the input first)
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

void App::drawWhatsNew() {
    if (!whatsNew_) return;
    WhatsNewPopup& pop = *whatsNew_;
    const double now = nowSeconds();
    const float t = float(now - pop.openedAt);
    const ImGuiIO& io = ImGui::GetIO();
    const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);

    constexpr const char* kPopup = "##whats_new";
    float shown = easeOut(t / 0.18f);
    if (pop.closing) {
        const float c = float(now - pop.closingAt) / 0.15f;
        if (c >= 1.0f) {
            if (ImGui::IsPopupOpen(kPopup)) {
                ImGui::SetNextWindowSize(ImVec2(1, 1));
                if (ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground)) {
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            whatsNew_.reset();
            redrawIn(0.0);
            return;
        }
        shown *= 1.0f - easeInOut(c);
    }
    if (supportCard_) return; // the membership card (Try it) is on top; this one waits behind it, closing
    if (!ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);
    if (t < 1.0f || pop.closing) redrawIn(0.0); // animated while it opens and closes (then while a card's hover eases)

    const ImVec4 bg = theme::shellBg, cardBg = theme::panelBg, raised = theme::header, accent = theme::accent;
    const ImVec4 ink = theme::textBright, body = theme::text, dim = theme::textDim, line = theme::text;
    ImFont* titleFont = fontCardTitle_ ? fontCardTitle_ : ImGui::GetFont();
    ImFont* headFont = fontCardHead_ ? fontCardHead_ : boldFont();
    ImFont* bodyFont = fontCardBody_ ? fontCardBody_ : ImGui::GetFont();
    ImFont* boldUi = boldFont() ? boldFont() : ImGui::GetFont();
    const whatsnew::Version& v = pop.version;
    const int n = int(v.cards.size());

    // The layout in logical pixels: the hero band (the bust in forge light, the version's theme and tagline over it),
    // then the cards in one row (picture, a measured win, title, line, Try it), as tall as the longest card's words:
    // never clipped. Logical 1000 wide, fitted to the window and the UI scale; scaled from 96 % while opening.
    constexpr float W = 1000.0f, kPad = 32.0f, kHeroH = 230.0f, kGap = 16.0f, kFooterH = 72.0f;
    const float cardW = n ? (W - kPad * 2.0f - kGap * float(n - 1)) / float(n) : 0.0f, picH = std::round(cardW * 340.0f / 600.0f);
    const float textW = cardW - 32.0f;
    auto tall = [](ImFont* f, float px, const std::string& s, float wrap) { return f->CalcTextSizeA(px, FLT_MAX, wrap, s.c_str()).y; };
    float wordsH = 0.0f;
    for (const auto& c : v.cards)
        wordsH = std::max(wordsH, (c.stat.empty() ? 0.0f : tall(titleFont, 28.0f, c.stat, 0.0f) + 4.0f) + tall(headFont, 16.5f, c.title, textW) +
                                      6.0f + tall(bodyFont, 13.5f, c.line, textW));
    const float cardH = picH + 16.0f + wordsH + 18.0f + 30.0f + 16.0f;
    const float H = kPad + kHeroH + 20.0f + cardH + kFooterH;

    const float s = uiScale_;
    const ImVec2 view = io.DisplaySize;
    const float fit = std::min({1.0f, (view.x - 32.0f * s) / (W * s), (view.y - 32.0f * s) / (H * s)});
    const float k = s * std::max(fit, 0.4f) * lerp(0.96f, 1.0f, shown);
    const ImVec2 size(W * k, H * k);
    const ImVec2 a(std::floor((view.x - size.x) * 0.5f), std::floor((view.y - size.y) * 0.5f));
    const ImVec2 b(a.x + size.x, a.y + size.y);
    const float radius = 18.0f * k, hair = std::max(1.0f, k);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(view);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0)); // (dimmed below, with the fade)
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav;
    const bool began = ImGui::BeginPopupModal(kPopup, nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (!began) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Painter paint{dl, k};
    const float A = shown;

    // The app dimmed behind it; the window with a soft shadow and a hairline edge.
    dl->AddRectFilled(ImVec2(0, 0), view, col(0x060608, 0.72f * A));
    glow(dl, ImVec2(a.x, a.y + 16.0f * k), ImVec2(b.x, b.y + 16.0f * k), radius, 40.0f * k, ImVec4(0, 0, 0, 1), 0.9f * A, 10);
    glow(dl, a, b, radius, 90.0f * k, accent, 0.035f * A, 14);
    {
        const int v0 = dl->VtxBuffer.Size;
        dl->AddRectFilled(a, b, col(0xffffff, A), radius);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, a, ImVec2(a.x, b.y), tc(mix(bg, accent, 0.03f), 1.0f),
                                                      tc(bg, 1.0f));
    }
    dl->AddRect(a, b, tc(line, 0.10f * A), radius, 0, hair);

    const float pad = kPad * k;
    // The hero band: its picture is always dark (the brand's forge light), so its words are light in every look.
    const ImVec2 h0(a.x + pad, a.y + pad), h1(b.x - pad, a.y + pad + kHeroH * k);
    {
        const float r = 16.0f * k;
        ImVec2 texSize;
        const ImTextureID tex = v.hero.empty() ? ImTextureID(0) : guide_.image(v.hero, &texSize);
        if (tex) {
            ImVec2 uv0, uv1;
            coverUv(texSize.x / std::max(1.0f, texSize.y), (h1.x - h0.x) / std::max(1.0f, h1.y - h0.y), uv0, uv1);
            dl->AddImageRounded(tex, h0, h1, uv0, uv1, col(0xffffff, A), r);
        } else { // no picture (an older version): the forge colours and the bust
            const int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(h0, h1, col(0xffffff, A), r);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, h0, ImVec2(h1.x, h0.y), col(0x0f0f12, 1.0f),
                                                          col(0x2a1a10, 1.0f));
            const float m = (h1.y - h0.y) * 0.72f;
            markTile(dl, ImVec2(h1.x - m - 48.0f * k, (h0.y + h1.y - m) * 0.5f), m, A);
        }
        dl->AddRect(h0, h1, col(0xffffff, 0.10f * A), r, 0, hair);
        // The eyebrow ("WHAT'S NEW · 0.12.7"), the theme (or the tagline) large, the tagline under it.
        std::string eyebrow = tr("What's new");
        bool ascii = true;
        for (char& ch : eyebrow) {
            if (static_cast<unsigned char>(ch) >= 0x80) ascii = false;
            else ch = char(std::toupper(static_cast<unsigned char>(ch)));
        }
        eyebrow += "  \xc2\xb7  " + v.version;
        if (!v.date.empty() && v.date != "unreleased") eyebrow += "  \xc2\xb7  " + v.date;
        const std::string& headline = v.theme.empty() ? v.tagline : v.theme;
        const std::string& under = v.theme.empty() ? std::string() : v.tagline;
        const float x = h0.x + 40.0f * k, w = (h1.x - h0.x) * 0.56f;
        const float eh = paint.measure(boldUi, 12.0f, "W").y + 14.0f * k;
        const float th = paint.measure(titleFont, 36.0f, headline.c_str(), w).y;
        const float uh = under.empty() ? 0.0f : 10.0f * k + paint.measure(bodyFont, 16.0f, under.c_str(), w * 0.92f).y;
        float y = std::floor((h0.y + h1.y - (eh + th + uh)) * 0.5f);
        if (ascii) paint.spaced(boldUi, 12.0f, ImVec2(x, y), col(kBrandOrange, A), eyebrow.c_str(), 1.8f);
        else paint.text(boldUi, 12.0f, ImVec2(x, y), col(kBrandOrange, A), eyebrow.c_str());
        y += eh;
        y += paint.text(titleFont, 36.0f, ImVec2(x, y), col(0xfafafc, A), headline.c_str(), w).y;
        if (!under.empty()) paint.text(bodyFont, 16.0f, ImVec2(x, y + 10.0f * k), col(0xb4b4be, A), under.c_str(), w * 0.92f);
    }

    // Close (the hero band's top right).
    bool close = false;
    {
        const float d = 32.0f * k;
        const ImVec2 c0(h1.x - 16.0f * k - d, h0.y + 16.0f * k);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton("Close##whats_new.x", ImVec2(d, d));
        const bool hot = ImGui::IsItemHovered();
        close |= ImGui::IsItemClicked();
        const ImVec2 c(c0.x + d * 0.5f, c0.y + d * 0.5f);
        dl->AddCircleFilled(c, d * 0.5f, col(hot ? 0x2a2a31 : 0x0f0f12, 0.75f * A));
        dl->AddCircle(c, d * 0.5f, col(0xffffff, (hot ? 0.40f : 0.18f) * A), 0, hair);
        const float x = 5.5f * k;
        dl->AddLine(ImVec2(c.x - x, c.y - x), ImVec2(c.x + x, c.y + x), col(0xfafafc, (hot ? 1.0f : 0.75f) * A), 1.6f * k);
        dl->AddLine(ImVec2(c.x - x, c.y + x), ImVec2(c.x + x, c.y - x), col(0xfafafc, (hot ? 1.0f : 0.75f) * A), 1.6f * k);
    }

    // Keyboard: Tab / arrows move between the cards, Enter or Space tries the focused one, Esc closes.
    if (!pop.closing && n) {
        const bool back = ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                          (ImGui::IsKeyPressed(ImGuiKey_Tab) && io.KeyShift);
        const bool next = ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
                          (ImGui::IsKeyPressed(ImGuiKey_Tab) && !io.KeyShift);
        if (next) pop.focus = pop.focus < 0 ? 0 : (pop.focus + 1) % n;
        if (back) pop.focus = pop.focus < 0 ? n - 1 : (pop.focus + n - 1) % n;
    }
    int tried = -1;
    if (!pop.closing && pop.focus >= 0 &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Space, false)))
        tried = pop.focus;

    const float footerH = kFooterH * k, top = h1.y + 20.0f * k, gap = kGap * k;
    auto tryButton = [&](const whatsnew::Card& c, int i, ImVec2 at, bool primary, float appear) {
        const bool guideLink = isGuideAction(c.action);
        const char* label = guideLink ? tr("Read more") : tr("Try it");
        const std::string id = std::string(label) + "##whats_new.try." + std::to_string(i);
        const ImVec2 ls = paint.measure(boldUi, 14.0f, label);
        const float arrow = 14.0f * k;
        const ImVec2 c1(at.x + ls.x + arrow + 34.0f * k, at.y + 32.0f * k);
        ImGui::SetCursorScreenPos(at);
        ImGui::InvisibleButton(id.c_str(), ImVec2(c1.x - at.x, c1.y - at.y));
        const bool hot = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        if (ImGui::IsItemClicked()) tried = i;
        if (primary) {
            glow(dl, at, c1, 16.0f * k, 10.0f * k, accent, (hot ? 0.8f : 0.45f) * appear, 6);
            dl->AddRectFilled(at, c1, tc(held ? accent : hot ? theme::accentHover : accent, appear), 16.0f * k);
        } else {
            dl->AddRectFilled(at, c1, tc(held ? theme::buttonPressed : hot ? theme::buttonHover : theme::button, appear), 16.0f * k);
            dl->AddRect(at, c1, tc(hot ? accent : line, (hot ? 0.8f : 0.16f) * appear), 16.0f * k, 0, hair);
        }
        const ImU32 fg = tc(primary ? theme::textOnAccent : ink, appear);
        const float ty = std::floor((at.y + c1.y - ls.y) * 0.5f);
        paint.text(boldUi, 14.0f, ImVec2(at.x + 15.0f * k, ty), fg, label);
        const float ax = at.x + 15.0f * k + ls.x + 8.0f * k + (hot ? 2.0f * k : 0.0f), ay = (at.y + c1.y) * 0.5f;
        dl->AddLine(ImVec2(ax, ay), ImVec2(ax + arrow * 0.7f, ay), fg, 1.6f * k);
        dl->AddLine(ImVec2(ax + arrow * 0.7f - 4.0f * k, ay - 4.0f * k), ImVec2(ax + arrow * 0.7f, ay), fg, 1.6f * k);
        dl->AddLine(ImVec2(ax + arrow * 0.7f - 4.0f * k, ay + 4.0f * k), ImVec2(ax + arrow * 0.7f, ay), fg, 1.6f * k);
    };
    auto picture = [&](const whatsnew::Card& c, ImVec2 p0, ImVec2 p1, ImDrawFlags corners, float appear) {
        ImVec2 texSize;
        // A clip (frames:): one picture of COLS x ROWS frames, played in a loop while the window is open.
        ImTextureID tex = c.frames.empty() ? ImTextureID(0) : guide_.image(c.frames, &texSize);
        if (tex) {
            const int count = c.frameCols * c.frameRows;
            const int frame = int((nowSeconds() - pop.openedAt) * c.frameFps) % count;
            const ImVec2 cell(1.0f / float(c.frameCols), 1.0f / float(c.frameRows));
            const ImVec2 f0(float(frame % c.frameCols) * cell.x, float(frame / c.frameCols) * cell.y);
            const ImVec2 f1(f0.x + cell.x, f0.y + cell.y);
            // Fitted, not cropped (a clip's labels sit at its edges): letterboxed on the clips' own dark grey.
            const float aspect = texSize.x * cell.x / std::max(1.0f, texSize.y * cell.y);
            const float bw = p1.x - p0.x, bh = p1.y - p0.y;
            const float w = std::min(bw, bh * aspect), h = w / aspect;
            const ImVec2 q0(std::floor(p0.x + (bw - w) * 0.5f), std::floor(p0.y + (bh - h) * 0.5f));
            dl->AddRectFilled(p0, p1, col(0x2e2e2e, appear), 12.0f * k, corners);
            if (w >= bw - 1.0f && h >= bh - 1.0f)
                dl->AddImageRounded(tex, p0, p1, f0, f1, col(0xffffff, appear), 12.0f * k, corners);
            else
                dl->AddImage(tex, q0, ImVec2(q0.x + w, q0.y + h), f0, f1, col(0xffffff, appear));
            redrawIn(1.0 / double(c.frameFps));
            return;
        }
        tex = c.image.empty() ? ImTextureID(0) : guide_.image(c.image, &texSize);
        if (!tex) { // no picture: a quiet tile with the bust
            dl->AddRectFilled(p0, p1, tc(raised, appear), 12.0f * k, corners);
            const float m = std::min(p1.x - p0.x, p1.y - p0.y) * 0.3f;
            brand::bust(dl, ImVec2((p0.x + p1.x - m) * 0.5f, (p0.y + p1.y - m) * 0.5f), m, 0.6f * appear);
            return;
        }
        ImVec2 uv0, uv1;
        coverUv(texSize.x / std::max(1.0f, texSize.y), (p1.x - p0.x) / std::max(1.0f, p1.y - p0.y), uv0, uv1);
        dl->AddImageRounded(tex, p0, p1, uv0, uv1, col(0xffffff, appear), 12.0f * k, corners);
    };

    for (int i = 0; i < n; ++i) {
        const whatsnew::Card& c = v.cards[size_t(i)];
        const float appear = easeOut((t - 0.05f - 0.05f * float(i)) / 0.22f) * A;
        const float rise = (1.0f - appear) * 14.0f * k - pop.hover[size_t(i)] * 3.0f * k;
        const ImVec2 p0(a.x + pad + float(i) * (cardW * k + gap), top + rise), p1(p0.x + cardW * k, p0.y + cardH * k);
        const float r = 14.0f * k;
        const bool focused = pop.focus == i;
        const bool cardHot = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(p0, p1);
        if (cardHot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) pop.focus = i;
        const float target = cardHot || focused ? 1.0f : 0.0f;
        pop.hover[size_t(i)] = lerp(pop.hover[size_t(i)], target, 1.0f - std::exp(-dt * 12.0f));
        if (std::abs(pop.hover[size_t(i)] - target) > 0.01f) redrawIn(0.0);
        glow(dl, ImVec2(p0.x, p0.y + 6.0f * k), ImVec2(p1.x, p1.y + 6.0f * k), r, 16.0f * k, ImVec4(0, 0, 0, 1),
             (0.45f + 0.2f * pop.hover[size_t(i)]) * appear, 6);
        {
            const int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(p0, p1, col(0xffffff, appear), r);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, p0, ImVec2(p0.x, p1.y), tc(raised, 1.0f),
                                                          tc(cardBg, 1.0f));
        }
        picture(c, p0, ImVec2(p1.x, p0.y + picH * k), ImDrawFlags_RoundCornersTop, appear);
        const float x = p0.x + 16.0f * k, w = textW * k;
        float y = p0.y + picH * k + 16.0f * k;
        if (!c.stat.empty()) // a measured win, large in the accent
            y += paint.text(titleFont, 28.0f, ImVec2(x, y - 2.0f * k), tc(accent, appear), c.stat.c_str()).y + 4.0f * k;
        y += paint.text(headFont, 16.5f, ImVec2(x, y), tc(ink, appear), c.title.c_str(), w).y + 6.0f * k;
        paint.text(bodyFont, 13.5f, ImVec2(x, y), tc(dim, appear), c.line.c_str(), w);
        tryButton(c, i, ImVec2(x, p1.y - 16.0f * k - 30.0f * k), i == 0, appear);
        dl->AddRect(p0, p1, focused ? tc(accent, appear) : tc(cardHot ? accent : line, (cardHot ? 0.45f : 0.10f) * appear), r, 0,
                    (focused ? 2.0f : 1.0f) * k);
    }

    // Footer: the full release notes and the Discord on the left, Close on the right.
    const float fy = b.y - footerH * 0.5f;
    const float fa = easeOut((t - 0.12f) / 0.2f) * A;
    dl->AddLine(ImVec2(a.x + pad, b.y - footerH), ImVec2(b.x - pad, b.y - footerH), tc(line, 0.08f * fa), hair);
    auto pill = [&](const char* label, const char* id, float x, bool primary) {
        const ImVec2 ls = paint.measure(boldUi, 14.0f, label);
        const ImVec2 c0(x, fy - 17.0f * k), c1(x + ls.x + 36.0f * k, fy + 17.0f * k);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton(id, ImVec2(c1.x - c0.x, c1.y - c0.y));
        const bool hot = ImGui::IsItemHovered();
        if (primary) {
            dl->AddRectFilled(c0, c1, tc(hot ? theme::buttonHover : theme::button, fa), 17.0f * k);
            dl->AddRect(c0, c1, tc(hot ? accent : line, (hot ? 0.7f : 0.22f) * fa), 17.0f * k, 0, hair);
        } else if (hot) {
            dl->AddRectFilled(c0, c1, tc(theme::button, 0.6f * fa), 17.0f * k);
        }
        paint.text(boldUi, 14.0f, ImVec2(std::floor(c0.x + 18.0f * k), std::floor(fy - ls.y * 0.5f)), tc(primary || hot ? ink : body, fa),
                   label);
        return std::pair<float, bool>(c1.x - c0.x, ImGui::IsItemClicked());
    };
    bool notes = false, discord = false;
    {
        float x = a.x + pad - 18.0f * k;
        const auto [w0, n0] = pill(tr("Full release notes"), "Full release notes##whats_new", x, false);
        notes = n0;
        x += w0 + 4.0f * k;
        const auto [w1, d1] = pill(tr("Join the Discord"), "Join the Discord##whats_new", x, false);
        discord = d1;
        const ImVec2 cs = paint.measure(boldUi, 14.0f, tr("Close"));
        close |= pill(tr("Close"), "Close##whats_new", b.x - pad - cs.x - 36.0f * k, true).second;
    }

    if (!pop.closing) {
        if (tried >= 0) {
            const std::string action = v.cards[size_t(tried)].action;
            closeWhatsNew();
            runWhatsNewAction(action);
        } else if (notes) {
            guide_.open("whats-new.md");
            closeWhatsNew();
        } else if (discord) {
            openDiscordPage(); // stays open
        } else if (close || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            closeWhatsNew();
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImRect(a, b).Contains(io.MousePos) && t > 0.3f) {
            closeWhatsNew(); // a click outside the card
        }
    }
    ImGui::EndPopup();
}

} // namespace sf
