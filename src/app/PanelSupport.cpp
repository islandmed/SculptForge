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

#include <IconsLucide.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace sf {
namespace featured_data { // assets/featured, compiled in (cmake/EmbedFeatured.cmake)
struct File {
    const char* path;
    const unsigned char* data;
    unsigned long long size;
};
extern const File kFiles[];
extern const int kFileCount;
} // namespace featured_data

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

namespace {
// The bundled creator videos (assets/featured), parsed once; their thumbnails are uploaded when first drawn.
struct FeaturedSlot {
    std::vector<FeaturedVideo> videos;
    std::vector<unsigned> textures;
};
FeaturedSlot& featuredSlot() {
    static FeaturedSlot slot = [] {
        FeaturedSlot f;
        const featured_data::File* json = nullptr;
        for (int i = 0; i < featured_data::kFileCount; ++i)
            if (std::string_view(featured_data::kFiles[i].path) == "featured.json") json = &featured_data::kFiles[i];
        if (json) f.videos = parseFeaturedVideos(std::string(reinterpret_cast<const char*>(json->data), size_t(json->size)));
        f.textures.assign(f.videos.size(), 0u);
        return f;
    }();
    return slot;
}
} // namespace

// "Featured by creators": up to three cards (thumbnail, title, creator); a click opens the video's page in the browser. Nothing
// is drawn, not even the heading, while the bundled list is empty.
void App::drawFeaturedVideos() {
    FeaturedSlot& slot = featuredSlot();
    if (slot.videos.empty()) return;
    ImGui::Spacing();
    ImGui::TextUnformatted(tr("Featured by creators"));
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - gap * float(kFeaturedMax - 1)) / float(kFeaturedMax), height = width * 9.0f / 16.0f;
    for (size_t i = 0; i < slot.videos.size(); ++i) {
        const FeaturedVideo& v = slot.videos[i];
        if (i > 0) ImGui::SameLine();
        ImGui::BeginGroup();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const std::string id = "##welcome.featured." + std::to_string(i);
        const bool clicked = ImGui::InvisibleButton(id.c_str(), ImVec2(width, height));
        if (slot.textures[i] == 0)
            for (int k = 0; k < featured_data::kFileCount; ++k)
                if (v.thumb == featured_data::kFiles[k].path)
                    slot.textures[i] = brand::imageTexture(featured_data::kFiles[k].data, featured_data::kFiles[k].size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(at, ImVec2(at.x + width, at.y + height), IM_COL32(24, 24, 28, 255), 3.0f * uiScale_);
        if (slot.textures[i])
            dl->AddImageRounded(ImTextureID(uintptr_t(slot.textures[i])), at, ImVec2(at.x + width, at.y + height), ImVec2(0, 0), ImVec2(1, 1),
                                ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(220, 220, 220, 255), 3.0f * uiScale_);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(v.title.c_str());
        ImGui::TextDisabled("%s", v.creator.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Opens in your browser."));
        if (clicked) openFeaturedVideo(int(i));
    }
}

void App::openFeaturedVideo(int index) {
    const FeaturedSlot& slot = featuredSlot();
    if (index < 0 || size_t(index) >= slot.videos.size()) return;
    ++featuredRequests_;
    const std::string& url = slot.videos[size_t(index)].url;
    if (!interactive()) { // UI tests, renders and demos never start a browser
        status_ = "Featured video: " + url;
        return;
    }
    status_ = sf::dialogs::openUrl(url.c_str()) ? std::string("Opened the video in your browser")
                                                : std::string("Couldn't open ") + url + ": " + SDL_GetError();
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

void App::drawSupportUi() {
    tickBigMeshTip(); // (here, not in tickProject: this runs every frame, UI tests included)
    const float s = uiScale_;
    const CanvasRect c = canvasRect();
    // The small membership card (PanelSupportCard.cpp): the sculpting moment, and the card once the frame is calm.
    // (Here, not in tickProject: this runs every frame, UI tests included.)
    tickMemberCard(ImGui::GetIO().DeltaTime);
    drawMemberCard();
    if (showWelcome_) {
        // Not modal: the app works around it, and it doesn't come back once closed.
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h * 0.3f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500.0f * s, 0.0f), ImGuiCond_Appearing);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
        if (ImGui::Begin(tr("Welcome to SculptForge##welcome"), &showWelcome_, flags)) {
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
            drawFeaturedVideos(); // creator reviews and tutorials: bundled, and nothing at all while there are none
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
            // The guide, the community (Discord, GitHub, X, Bluesky, Instagram) and, on the right, the memberships.
            const float rightW = ImGui::CalcTextSize(tr("Support SculptForge")).x + ImGui::CalcTextSize(tr("Close")).x +
                                 4.0f * ImGui::GetStyle().FramePadding.x + ImGui::GetStyle().ItemSpacing.x;
            if (ImGui::Button(tr("User Guide##welcome"))) {
                guide_.open();
                showWelcome_ = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Hold Ctrl over any control to see what it does. Shift+F1 opens the guide any time."));
            const float guideMid = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
            ImGui::SameLine(0.0f, 14.0f * s);
            drawSocialRow("welcome", ImVec2(ImGui::GetCursorScreenPos().x, guideMid)); // centred on the button
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - rightW));
            if (ImGui::Button(tr("Support SculptForge##welcome"))) showSupportCard(SupportCardReason::Manual); // the memberships
            ImGui::SameLine();
            if (ImGui::Button(tr("Close##welcome"))) showWelcome_ = false;
        }
        ImGui::End();
    }
    drawQuickTour(); // QuickTour.cpp
    drawLanguageBar(c);
    if (newLookCard_ && !showWelcome_ && !supportCardOpen() && !whatsNew_ && !showWhatsNew_) {
        // Once, after updating from before 0.10.7 (PanelProject.cpp): the new default look, never forced.
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h - 16.0f * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(420.0f * s, 0.0f), ImGuiCond_Always);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing;
        bool open = true;
        if (ImGui::Begin(tr("SculptForge has a new look##newlook"), &open, flags)) {
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
        if (ImGui::Begin(tr("Big meshes are faster now##bigmesh"), &open, flags)) {
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
