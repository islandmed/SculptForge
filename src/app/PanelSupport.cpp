// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supporting SculptForge: Help > Support SculptForge..., the first-run welcome and the supporters and Founding
// Members in Help > About. Each only opens kSupportUrl in the browser (Support.h); the app is the same for everyone,
// so nothing here checks, sends or stores anything else.
#include "app/App.h"
#include "app/BrandMark.h"
#include "app/NativeDialog.h"
#include "app/Config.h"
#include "app/LookSchemes.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace sf {
namespace assets {
extern const unsigned char supporters[]; // docs/supporters.txt (CMakeLists.txt)
extern const unsigned long long supporters_size;
extern const unsigned char foundingMembers[]; // website/founding.json (CMakeLists.txt)
extern const unsigned long long foundingMembers_size;
} // namespace assets

namespace {

const std::vector<std::string>& supporterNames() {
    static const std::vector<std::string> names =
        parseSupporters(std::string_view(reinterpret_cast<const char*>(assets::supporters), size_t(assets::supporters_size)));
    return names;
}

} // namespace

const FoundingMembers& foundingMembers() {
    static const FoundingMembers f = parseFoundingMembers(
        std::string(reinterpret_cast<const char*>(assets::foundingMembers), size_t(assets::foundingMembers_size)));
    return f;
}

void App::openSupportPage() {
    ++supportPageRequests_;
    if (!interactive()) { // UI tests, renders and demos never start a browser
        status_ = std::string("Support SculptForge: ") + kSupportUrl;
        return;
    }
    status_ = sf::dialogs::openUrl(kSupportUrl) ? std::string("Opened sculptforge.app/support.html in your browser")
                                       : std::string("Couldn't open ") + kSupportUrl + ": " + SDL_GetError();
}

void App::openDiscordPage() {
    ++discordRequests_;
    if (!interactive()) { // UI tests, renders and demos never start a browser
        status_ = std::string("Join the community on Discord: ") + kDiscordUrl;
        return;
    }
    status_ = sf::dialogs::openUrl(kDiscordUrl) ? std::string("Opened the SculptForge Discord invite in your browser")
                                       : std::string("Couldn't open ") + kDiscordUrl + ": " + SDL_GetError();
}

void App::openSocialLink(int index) {
    if (index < 0 || index >= kSocialCount) return;
    ++socialRequests_;
    lastSocial_ = index;
    const SocialLink& link = kSocialLinks[index];
    if (!interactive()) { // UI tests, renders and demos never start a browser
        status_ = std::string(link.name) + ": " + link.url;
        return;
    }
    status_ = sf::dialogs::openUrl(link.url) ? std::string("Opened SculptForge on ") + link.name + " in your browser"
                                             : std::string("Couldn't open ") + link.url + ": " + SDL_GetError();
}

// Small grey icons in a row at the cursor, one line high, each lighter under the pointer; a click opens that page in the browser.
float App::socialRowWidth() const {
    const float size = ImGui::GetFrameHeight() * 0.72f, gap = 8.0f * uiScale_;
    return kSocialCount * size + (kSocialCount - 1) * gap;
}

void App::drawSocialRow(const char* where, ImVec2 at) {
    const float size = ImGui::GetFrameHeight() * 0.72f, gap = 8.0f * uiScale_, frame = ImGui::GetFrameHeight();
    const ImVec2 topLeft(at.x, at.y - frame * 0.5f);
    ImGui::BeginGroup(); // one frame-high item: what follows (the Support button) keeps its place
    for (int i = 0; i < kSocialCount; ++i) {
        const ImVec2 icon(at.x + float(i) * (size + gap), at.y - size * 0.5f);
        ImGui::SetCursorScreenPos(icon);
        const std::string id = std::string(kSocialLinks[i].name) + "##" + where + ".social";
        const bool clicked = ImGui::InvisibleButton(id.c_str(), ImVec2(size, size));
        const bool hot = ImGui::IsItemHovered();
        const ImVec4 tint = ImGui::GetStyleColorVec4(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        brand::socialIcon(ImGui::GetWindowDrawList(), i, icon, size, ImGui::GetColorU32(tint));
        if (hot) ImGui::SetTooltip("%s", kSocialLinks[i].name);
        if (clicked) openSocialLink(i);
    }
    ImGui::SetCursorScreenPos(topLeft);
    ImGui::Dummy(ImVec2(socialRowWidth(), frame));
    ImGui::EndGroup();
}

void App::drawSupportersSection() {
    const FoundingMembers& founding = foundingMembers();
    if (!founding.names.empty()) {
        ImGui::SeparatorText(tr("Founding Members"));
        ImGui::TextWrapped(tr("The first %d members, with our thanks for good:"), founding.limit);
        for (const std::string& name : founding.names) ImGui::BulletText("%s", name.c_str()); // (plain text, sanitised)
    }
    ImGui::SeparatorText(tr("Supporters"));
    const std::vector<std::string>& names = supporterNames();
    if (names.empty()) {
        ImGui::TextDisabled("%s", tr("Be the first: sculptforge.app/support.html"));
    } else {
        ImGui::TextWrapped("%s", tr("Thank you to everyone who supports SculptForge:"));
        // Plain text only (TextUnformatted: no format strings, markup or links), sanitised by parseSupporters.
        const float rows = float(std::min<size_t>(names.size(), 8));
        if (ImGui::BeginChild("##supporters", ImVec2(0, rows * ImGui::GetTextLineHeightWithSpacing() + 4.0f * uiScale_),
                              ImGuiChildFlags_Borders)) {
            ImGuiListClipper clipper;
            clipper.Begin(int(names.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) ImGui::TextUnformatted(names[size_t(i)].c_str());
        }
        ImGui::EndChild();
    }
    ImGui::TextWrapped("%s", kFreeReleaseNote);
    if (ImGui::Button(tr("Support SculptForge##about"))) openSupportPage();
}

void App::chooseNewLook(bool tryIt) {
    newLookCard_ = false;
    newLookCardDone_ = true;
    if (tryIt) {
        uiPrefs_.look.look = int(looks::LookId::ForgeTerminal);
        uiPrefs_.look.scheme = "neutral";
        uiPrefs_.look.matchOmarchy = false;
        setShellLayout(1);
        lookDirty_ = true;
        status_ = "The new look: Neutral, SculptForge layout (Preferences > Interface > Look changes it)";
    } else {
        status_ = "Kept your layout (Preferences > Interface > Look has the new look)";
    }
    saveProjectPrefs();
}

// The first-run look picker (the welcome): Neutral (the default), Forge Terminal with Tokyo Night (0.10.7's
// default), or the Classic look with the Classic layout. A click applies it at once and remembers it.
void App::drawLookPicker() {
    struct Choice {
        const char* label;
        const char* name;
        int look;
        const char* scheme;
        int layout;
        const char* help;
    };
    static const Choice kChoices[] = {
        {"##welcome.look.neutral", "Neutral", int(looks::LookId::ForgeTerminal), "neutral", 1,
         "Neutral grey panels, so your model's colours read true, with one orange accent. The SculptForge layout."},
        {"##welcome.look.forge", "Forge Terminal", int(looks::LookId::ForgeTerminal), "tokyo_night", 1,
         "Terminal-style tiles in the Tokyo Night colours (blue-grey). Preferences > Interface > Look has more schemes."},
        {"##welcome.look.classic", "Classic layout", int(looks::LookId::Classic), "neutral", 0,
         "The Classic grey look with the alphabetical palette bar and the two-row top shelf, for experienced sculptors."},
    };
    const float s = uiScale_, spacing = ImGui::GetStyle().ItemSpacing.x;
    const float w = std::floor((ImGui::GetContentRegionAvail().x - 2.0f * spacing) / 3.0f), h = 92.0f * s;
    for (int i = 0; i < 3; ++i) {
        const Choice& c = kChoices[i];
        if (i) ImGui::SameLine();
        const bool current = uiPrefs_.look.look == c.look && uiPrefs_.shellLayout == c.layout &&
                             (c.look != int(looks::LookId::ForgeTerminal) || uiPrefs_.look.scheme == c.scheme);
        const bool clicked = ImGui::InvisibleButton(c.label, ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const looks::SchemeInfo* scheme = looks::findScheme(c.scheme);
        const looks::Palette pal = c.look == int(looks::LookId::Classic) || !scheme ? config::classicPalette() : scheme->palette;
        config::drawLookMiniature(dl, a, b, pal, c.name, c.look == int(looks::LookId::Classic) ? fontInterSmall_ : fontMonoSmall_, s,
                                  c.layout);
        if (hovered && !current) dl->AddRectFilled(a, b, theme::u32(theme::textHover, 0.05f)); // (a faint lift, no border)
        if (current) dl->AddRect(a, b, theme::u32(theme::accent), 0.0f, 0, 2.0f * s);
        ImGui::LogText("%s", c.name);
        if (hovered) ImGui::SetTooltip("%s", c.help);
        if (clicked && !current) {
            uiPrefs_.look.look = c.look;
            uiPrefs_.look.scheme = c.scheme;
            uiPrefs_.look.matchOmarchy = false;
            setShellLayout(c.layout);
            lookDirty_ = true;
            saveProjectPrefs(); // remembered for the next start
            status_ = std::string("Look: ") + c.name + " (Preferences > Interface > Look changes it)";
        }
    }
}

void App::setInterfaceLanguage(Language lang) {
    uiPrefs_.language = languageCode(lang);
    setLanguage(lang);
    requestUiScale(); // the fonts again, with or without the Chinese glyphs
}

void App::offerFirstRunLanguage(std::string_view osTag) {
    const Language lang = languageFromOsLocale(osTag);
    if (lang == Language::English) return; // not shipped (or English): the welcome's language row is the offer
    setInterfaceLanguage(lang);
    languageBar_ = true;
    languageBarFor_ = lang;
    languageUnsaved_ = true; // (the first autosave look would take it for what was on disk)
}

// Each language in its own script, so it can be found from any other; the current one lit.
void App::drawLanguageRow() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Language:"));
    for (int i = 0; i < kLanguageCount; ++i) {
        const Language lang = Language(i);
        const std::string label = std::string(languageName(lang)) + "##welcome.language." + languageCode(lang);
        ImGui::SameLine();
        const float width = ImGui::CalcTextSize(languageName(lang)).x + 2.0f * ImGui::GetStyle().FramePadding.x;
        if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine(); // a narrow window: the rest on the next row
        const bool on = language() == lang;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label.c_str()) && !on) setInterfaceLanguage(lang);
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tr("The language of menus, palettes, shelves and messages. Brush and tool names (Quad Remesh, "
                                       "Forge Spheres, Clay Block...) stay as they are."));
    }
}

void App::openPreferencesSection(const char* subpalette) {
    auto& open = layout_.openSubpalettes;
    if (uiPrefs_.oneSubpalette) // as a click on its header would: the other Preferences sections close
        open.erase(std::remove_if(open.begin(), open.end(), [](const std::string& o) { return o.rfind("preferences.", 0) == 0; }),
                   open.end());
    if (std::find(open.begin(), open.end(), subpalette) == open.end()) open.push_back(subpalette);
    flashSubpalette_ = subpalette;
    flashSubpaletteStart_ = ImGui::GetTime();
    if (const Action* a = actions_.find("ui.palette.preferences"); a && a->run) a->run();
}

// After the first start switched the language by itself: a note along the bottom, in that language and in English.
void App::drawLanguageBar(const CanvasRect& c) {
    if (languageBar_ && language() != languageBarFor_) languageBar_ = false; // changed another way: nothing to offer
    if (!languageBar_ || supportCardOpen()) return;
    const float s = uiScale_;
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 12.0f * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(500.0f * s, c.w - 32.0f * s), 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoTitleBar;
    bool open = true;
    if (ImGui::Begin("##languagebar", &open, flags)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped(tr("SculptForge is in %s."), languageName(languageBarFor_));
        ImGui::SameLine();
        if (ImGui::Button(tr("Change##languagebar"))) {
            openPreferencesSection("preferences.interface");
            open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Got it##languagebar"))) open = false;
        // (English on purpose, whatever the language: someone who can't read the line above can read this one)
        const std::string english = std::string("SculptForge is in ") + languageName(languageBarFor_) +
                                    ". Preferences > Interface > Language changes it.";
        ImGui::TextDisabled("%s", english.c_str());
    }
    ImGui::End();
    if (!open) languageBar_ = false;
}

// The current window moved inside `c` (a window placed by its centre can have its top or right edge off a small
// screen, where its close button can't be reached). Call after Begin; a window larger than `c` keeps its top left.
void App::keepWindowInView(const CanvasRect& c) {
    const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const ImVec2 to(std::clamp(pos.x, c.x, std::max(c.x, c.x + c.w - size.x)),
                    std::clamp(pos.y, c.y, std::max(c.y, c.y + c.h - size.y)));
    if (to.x != pos.x || to.y != pos.y) ImGui::SetWindowPos(to);
}

// Android's Back (Hameron, Pixel, 11-13: the welcome could only be closed by the small X): the topmost of the cards and
// windows that open by themselves closes; with none open it is the system's Back (the app goes to the background).
bool App::androidBackWanted() const {
    return supportCardOpen() || whatsNewOpen() || quickTourOpen_ || showWelcome_ || languageBar_;
}

bool App::androidBack() {
    if (supportCardOpen()) closeSupportCard();
    else if (whatsNewOpen()) closeWhatsNew();
    else if (quickTourOpen_) closeQuickTour();
    else if (showWelcome_) showWelcome_ = false;
    else if (languageBar_) languageBar_ = false;
    else return false;
    redrawIn(0.0);
    return true;
}

void App::drawSupportUi() {
    tickBigMeshTip(); // (here, not in tickProject: this runs every frame, UI tests included)
#ifdef __ANDROID__
    { // Back reaches the app as a key only while something is open to close; otherwise it is the system's (leave the app)
        static int trapping = -1;
        const int want = androidBackWanted() ? 1 : 0;
        if (want != trapping) {
            SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, want ? "1" : "0");
            trapping = want;
        }
    }
#endif
    const float s = uiScale_;
    const CanvasRect c = canvasRect();
    // The small membership card (PanelSupportCard.cpp): the sculpting moment, and the card once the frame is calm.
    // (Here, not in tickProject: this runs every frame, UI tests included.)
    tickMemberCard(ImGui::GetIO().DeltaTime);
    drawMemberCard();
    if (showWelcome_) {
        // Not modal: the app works around it, and it doesn't come back once closed.
        // Never wider or taller than the room: on a phone (Android, Discord 11-12) a fixed 500 dp-ish window ran off the
        // screen, its X and Close with it. Between a phone's two shelves the 3D view is too narrow, so there it takes the
        // whole screen below the status bar, and the text and rows shrink to fit (`fit`); a window taller than that scrolls.
        // A phone (a touch screen, short side under 600 dp, either way up) gets a sheet: it never moves, its Close row stays at the bottom and
        // its content scrolls with a drag (a finger can't scroll a window that is only dragged to move: landscape at 300 %,
        // 594 px of it, Close with it, were below the screen). It stays inside the safe area (cut-out, bars, gesture area).
        const CanvasRect safe = safeRect();
        const float margin = 16.0f * uiScale_;
        // (Only where a finger can be the pointer: Android, a touch screen, a UI test. A short desktop window with a mouse
        // keeps its movable window.)
        bool touchScreen = !options_.uiTest.empty();
#ifdef __ANDROID__
        touchScreen = true;
#else
        if (!touchScreen) {
            int touchDevices = 0;
            if (SDL_TouchID* ids = SDL_GetTouchDevices(&touchDevices)) {
                for (int i = 0; i < touchDevices; ++i)
                    touchScreen |= SDL_GetTouchDeviceType(ids[i]) == SDL_TOUCH_DEVICE_DIRECT;
                SDL_free(ids);
            }
        }
#endif
        const bool sheet = touchScreen && std::min(safe.w, safe.h) < 600.0f * uiScale_;
        CanvasRect room = safe;
        if (!sheet && c.w >= 500.0f * uiScale_ + margin) { // (the 3D view between the shelves, inside the safe area)
            const float x0 = std::max(c.x, safe.x), y0 = std::max(c.y, safe.y);
            room = {x0, y0, std::max(std::min(c.x + c.w, safe.x + safe.w) - x0, 1.0f), std::max(std::min(c.y + c.h, safe.y + safe.h) - y0, 1.0f)};
        }
        const bool narrow = room.w - margin < 500.0f * uiScale_;
        // (Paddings and spacing don't shrink with the text, so the rows need a tenth more room than the ratio.)
        float fit = narrow ? std::clamp((room.w - margin) / (500.0f * uiScale_) * 0.9f, 0.35f, 1.0f) : 1.0f;
        if (sheet) fit = std::min(fit, std::clamp((room.h - margin) / (420.0f * uiScale_), 0.6f, 1.0f)); // (landscape: shorter text)
        const float s = uiScale_ * fit; // (this window's own scale, below the outer one)
        const ImGuiStyle& st = ImGui::GetStyle();
        const float btnH = (sheet ? 44.0f : 0.0f) * s; // (a finger's size for the row at the bottom)
        const float footerH = (btnH > 0.0f ? btnH : ImGui::GetFrameHeight() * fit) + st.ItemSpacing.y * 5.0f + 1.0f;
        const float width = narrow ? room.w - margin : std::min(room.w - margin, 500.0f * uiScale_ * (fit < 1.0f ? fit / 0.9f : 1.0f));
        if (sheet) ImGui::SetNextWindowPos(ImVec2(room.x + room.w * 0.5f, room.y + room.h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        else ImGui::SetNextWindowPos(ImVec2(room.x + room.w * 0.5f, room.y + room.h * 0.3f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        // Height 0 every frame: the window fits its content, so the featured creator card arriving after the first frame
        // (it loads from the kept feed) doesn't push the Support button and the icon row below the fold. (A sheet is as tall
        // as its content last frame, or the room.)
        const float sheetH = welcomeBodyH_ > 0.0f ? std::min(room.h - margin, ImGui::GetFontSize() + 4.0f * st.FramePadding.y + 2.0f * st.WindowPadding.y + welcomeBodyH_ + footerH + 2.0f)
                                                  : room.h - margin;
        ImGui::SetNextWindowSize(ImVec2(width, sheet ? std::max(sheetH, 100.0f) : 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, std::max(room.h - margin, 100.0f)));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
        if (sheet) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ui::PushRole(theme::Role::Learn);
        const bool welcomeOpen = ImGui::Begin(tr("Welcome to SculptForge##welcome"), &showWelcome_, flags);
        ui::PopRole();
        ui::RoleStrip(theme::Role::Learn);
        keepWindowInView(room); // (title bar and X inside the view, however the window was placed)
        {
            const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
            const float pad = ImGui::GetStyle().FramePadding.y, fs = ImGui::GetFontSize();
            welcomeXRect_ = ImVec4(wp.x + ws.x - pad - fs, wp.y + pad, wp.x + ws.x - pad, wp.y + pad + fs); // (ImGui's close button)
        }
        if (fit < 1.0f) ImGui::SetWindowFontScale(fit);
        if (welcomeOpen) {
            if (sheet) ImGui::BeginChild("##welcome.body", ImVec2(0.0f, -footerH), ImGuiChildFlags_None, ImGuiWindowFlags_None);
            // The interface language: one click, live (a hook for the mission headline above it, queue item 44).
            drawLanguageRow();
            ImGui::Spacing();
            // New to sculpting: the lesson first, big (docs/guide/first-sculpt.md); beside it, for those who have sculpted
            // before, the quick tour (QuickTour.cpp), where everything is in ten slides.
            const float tourW = ImGui::CalcTextSize(tr("Quick tour")).x + ImGui::GetFontSize() * 2.2f + 2.0f * ImGui::GetStyle().FramePadding.x;
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(tr(ICON_LC_GRADUATION_CAP "  Start the 10-minute lesson##welcome.lesson"),
                              ImVec2(ImGui::GetContentRegionAvail().x - tourW - ImGui::GetStyle().ItemSpacing.x, 40.0f * s))) {
                guide_.open("first-sculpt.md");
                showWelcome_ = false;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Your first sculpt: a cute owl head from this sphere, step by step, in about ten minutes."));
            ImGui::SameLine();
            if (ImGui::Button(tr(ICON_LC_COMPASS "  Quick tour##welcome.tour"), ImVec2(-1, 40.0f * s))) showQuickTour(0);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Ten short slides: remeshing, dynamic topology, retopology, levels, masking, cut, "
                                  "brushes and baking, each with a Show me button."));
            drawFeaturedCard(); // PanelFeatured.cpp: nothing when there is no featured creator
            // The look (0.10.9, #U6): three cards, the first the default. Existing installs never see this window
            // unless they open Help > Welcome, so nobody's look changes by itself.
            ImGui::Spacing();
            ImGui::TextUnformatted(tr("Pick a look:"));
            drawLookPicker();
            // Where you come from sets the keys and the layout together (Preferences > Input and > Interface > Look
            // change them later; both are remembered on its own).
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tr("Coming from:"));
            struct Origin {
                const char* label;
                int style, layout;
                const char* help;
            };
            static const Origin kOrigins[] = {
                {"Classic sculpting keys##welcome.from", 0, 0, "The classic sculpting keys and gestures, and the Classic layout."},
                {"Blender##welcome.from", 2, 1, "Blender's navigation and sculpt-mode keys, the SculptForge layout."},
                {"Maya##welcome.from", 3, 1, "Maya's Alt+drag view controls, the SculptForge layout."},
                {"New to sculpting##welcome.from", 1, 1, "SculptForge's own keys and layout (the default)."},
            };
            for (const Origin& o : kOrigins) {
                ImGui::SameLine();
                const bool on = uiPrefs_.inputStyle == o.style && uiPrefs_.shellLayout == o.layout;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(o.label)) {
                    setInputStyle(o.style);
                    startInputStyle_ = o.style; // kept for the next start
                    setShellLayout(o.layout);
                    lookDirty_ = true;
                    saveProjectPrefs();
                    status_ = std::string("Keys: ") + (o.style == 0 ? "Classic sculpting keys" : o.style == 2 ? "Blender" : o.style == 3 ? "Maya" : "SculptForge") +
                              (o.layout == 0 ? ", Classic layout" : ", SculptForge layout") + " (Preferences changes them)";
                }
                if (on) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", o.help);
            }
            // A base mesh to start from (core/StarterMeshes.h), instead of the sphere.
            ImGui::Spacing();
            if (ImGui::Button(tr(ICON_LC_USER_ROUND "  Start from a head##welcome"))) {
                confirmDiscard([this] { runCommand("new_starter_mesh", {{"name", std::string("human-head")}}); });
                showWelcome_ = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("A neutral human head with clean quads and eye and mouth loops, ready for a portrait."));
            ImGui::SameLine();
            if (ImGui::Button(tr("More starter meshes##welcome"))) {
                showStarterMeshes();
                showWelcome_ = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Bust, full body, hand, foot, ear, eye, stylised and creature heads, torso and "
                                  "bevelled primitives: Library > Tool > Starter meshes."));
            // AI (Preferences > AI changes it later; saved at once).
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tr("AI features:"));
            for (const bool choice : {true, false}) {
                ImGui::SameLine();
                const bool on = ai::aiEnabled() == choice;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                ImGui::BeginDisabled(ai::aiPolicyLocked());
                if (ImGui::Button(tr(choice ? "On##welcome.ai" : "Off##welcome.ai"))) setAiFeatures(choice);
                ImGui::EndDisabled();
                if (on) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", choice ? "AI Brush (describe a brush) and Generate 3D (image or text to a model)."
                                                   : "Hides them; SculptForge makes no AI network calls.");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr("Your art is never uploaded or used for training."));
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Details##welcome.ai"))) // the full wording: ai::kPrivacyLine, also in Help > About
                sf::dialogs::openUrl("https://github.com/islandmed/SculptForge/blob/main/docs/ai-services.md");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(ai::kPrivacyLine));
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("%s", tr("Free for everyone. Members get betas and a vote on what's next."));
            if (sheet) {
                welcomeBodyH_ = ImGui::GetCursorPosY();
                ImGuiWindow* body = ImGui::GetCurrentWindow();
                welcomeScrollMax_ = body->ScrollMax.y;
                // A finger drags the content: the press that began on a button turns into a scroll, not a click.
                ImGuiIO& io = ImGui::GetIO();
                if (body->ScrollMax.y > 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && io.MouseSource == ImGuiMouseSource_TouchScreen &&
                    io.MouseDown[0] && ImGui::IsMouseDragging(0, 10.0f * uiScale_)) {
                    ImGui::ClearActiveID();
                    ImGui::SetScrollY(body, std::clamp(body->Scroll.y - io.MouseDelta.y, 0.0f, body->ScrollMax.y));
                }
                welcomeScrollY_ = body->Scroll.y;
                ImGui::EndChild();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
            } else {
                welcomeScrollMax_ = welcomeScrollY_ = 0.0f;
            }
            // The guide, the community (Discord, GitHub, X, Bluesky, Instagram, Facebook) and, on the right, the memberships.
            const float rightW = ImGui::CalcTextSize(tr("Support SculptForge")).x + ImGui::CalcTextSize(tr("Close")).x +
                                 4.0f * ImGui::GetStyle().FramePadding.x + ImGui::GetStyle().ItemSpacing.x;
            if (ImGui::Button(tr("User Guide##welcome"), ImVec2(0.0f, btnH))) {
                guide_.open();
                showWelcome_ = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Hold Ctrl over any control to see what it does. Shift+F1 opens the guide any time."));
            const float guideMid = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
            ImGui::SameLine(0.0f, 14.0f * s);
            drawSocialRow("welcome", ImVec2(ImGui::GetCursorScreenPos().x, guideMid)); // centred on the button
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - rightW));
            if (ImGui::Button(tr("Support SculptForge##welcome"), ImVec2(0.0f, btnH))) showSupportCard(SupportCardReason::Manual); // the memberships
            ImGui::SameLine();
            if (ImGui::Button(tr("Close##welcome"), ImVec2(0.0f, btnH))) showWelcome_ = false;
            welcomeCloseRect_ = ImVec4(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y);
        }
        ImGui::End();
    }
    drawQuickTour(); // QuickTour.cpp
    drawLanguageBar(c);
    if (newLookCard_ && !showWelcome_ && !supportCardOpen() && !whatsNew_ && !showWhatsNew_) {
        // Once, after updating from before 0.10.7 (PanelProject.cpp): the new default look, never forced.
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 16.0f * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(std::min(420.0f * s, c.w - 16.0f * s), 0.0f), ImGuiCond_Always);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing;
        bool open = true;
        ui::PushRole(theme::Role::New);
        const bool began = ImGui::Begin(tr("SculptForge has a new look##newlook"), &open, flags);
        ui::PopRole();
        if (began) {
            ui::RoleStrip(theme::Role::New);
            ImGui::TextWrapped("%s", tr("Neutral grey panels with one orange accent, and the SculptForge layout: menus by task, a "
                               "status bar and a one-row tool shelf. Your layout stays as it is unless you try it."));
            ImGui::Spacing();
            if (ImGui::Button(tr("Try it##newlook"))) chooseNewLook(true);
            ImGui::SameLine();
            if (ImGui::Button(tr("Keep my layout##newlook"))) chooseNewLook(false);
            ImGui::TextDisabled("%s", tr("Either way: Preferences > Interface > Look changes it later."));
        }
        ImGui::End();
        if (!open) chooseNewLook(false);
    }
    if (bigMeshTipOpen_ && !showWelcome_ && !supportCardOpen() && !whatsNew_ && !quickTourOpen_) {
        // The one big-mesh tip (BigMeshTip.h): a note along the bottom of the canvas, no focus taken, shown once.
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 12.0f * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(std::min(500.0f * s, c.w - 32.0f * s), 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.96f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        bool open = true;
        ui::PushRole(theme::Role::Info);
        const bool began = ImGui::Begin(tr("Big meshes are faster now##bigmesh"), &open, flags);
        ui::PopRole();
        if (began) {
            ui::RoleStrip(theme::Role::Info);
            ImGui::TextWrapped("%s", tr("On a dense scan with micro levels, big brushes now keep up with the pen and the view turns "
                               "smoothly right after a stroke."));
            ImGui::Spacing();
            ImGui::TextWrapped("%s", tr("Two settings in Preferences > Performance help most on big meshes:"));
            ImGui::Bullet();
            ImGui::TextWrapped("%s", tr("Background Preview (experimental): a big brush shows under the pen while it's still being worked out."));
            ImGui::Bullet();
            ImGui::TextWrapped("%s", tr("View resolution 75%: draws about half the pixels, for integrated graphics and big screens."));
            ImGui::Spacing();
            if (ImGui::Button(tr("Performance settings##bigmesh"))) {
                openPerformancePrefs();
                open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Got it##bigmesh"))) open = false;
        }
        ImGui::End();
        if (!open) bigMeshTipOpen_ = false;
    }
}

} // namespace sf
