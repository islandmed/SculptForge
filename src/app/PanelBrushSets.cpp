// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brush sets (#143): named bottom shelves with their keys, switched in one step from the Brush palette, the bottom
// shelf's right-click menu, the command palette or the brush_set command. The files are core/BrushSets.cpp's.
#include "app/App.h"

#include "app/I18n.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace sf {

namespace {

constexpr const char* kSavePopup = "Save Brush Set##brushset";
constexpr const char* kDeletePopup = "Delete Brush Set?##brushset";
constexpr const char* kRecipePrefix = "brush.recipe.";

std::string lowerText(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "Wood Carving" -> "wood_carving": a stable piece of an action id.
std::string slug(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) out += char(std::tolower(c));
        else if (c >= 0x80) out += char(c); // (names in other scripts stay apart)
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "set" : out;
}

// The shelf a set puts up: its items, with the [+] tile at the end when the file left it out.
std::vector<std::string> shelfOf(const BrushSet& set) {
    std::vector<std::string> shelf = set.shelf;
    if (std::find(shelf.begin(), shelf.end(), "widget.add_brush") == shelf.end()) shelf.push_back("widget.add_brush");
    return shelf;
}

// The action a shelf item runs when it is a brush (the one whose key a set keeps), or "".
std::string brushKeyAction(const std::string& item) {
    if (item == "brush.paint") return "draw.paint";
    if (item.rfind("brush.", 0) == 0 && item.rfind("brush.file.", 0) != 0) return item;
    return {};
}

int countBrushes(const std::vector<std::string>& shelf) {
    return int(std::count_if(shelf.begin(), shelf.end(), [](const std::string& i) { return !brushKeyAction(i).empty(); }));
}

int findSet(const std::vector<BrushSet>& list, const std::string& name) {
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == name) return int(i);
    for (size_t i = 0; i < list.size(); ++i) // then ignoring case ("wood" for Wood)
        if (lowerText(list[i].name) == lowerText(name)) return int(i);
    return -1;
}

// printf into a std::string (the messages below come in English and in the interface language).
template <class... A> std::string format(const char* f, A... args) {
    if constexpr (sizeof...(A) == 0) return f;
    char buf[512];
    std::snprintf(buf, sizeof buf, f, args...);
    return buf;
}

std::string countText(int n, const char* one, const char* many, bool translated) {
    const char* f = n == 1 ? one : many;
    return format(translated ? tr(f) : f, n);
}

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

} // namespace

void App::registerBrushSets() {
    const auto nameParam = [](const char* what) {
        return std::vector<ParamSpec>{{"name", ParamSpec::Type::String, what, true}};
    };
    commands_.add({"brush_set",
                   "Switch to a brush set (Brush > Brush set): the bottom shelf's brushes and their keys, in one step.",
                   nameParam("The set's name, as Save Shelf as Brush Set named it."),
                   [this](Document&, const Args& a) -> CommandResult {
                       std::string msg;
                       const bool ok = applyBrushSet(argString(a, "name"), msg);
                       return {ok, msg};
                   }});
    commands_.add({"brush_set_save",
                   "Save the bottom shelf and the keys of its brushes as a brush set (replaces a set of that name).",
                   nameParam("A name for the set (1 to 64 characters), e.g. Wood, Metal or Stone."),
                   [this](Document&, const Args& a) -> CommandResult {
                       std::string msg;
                       const bool ok = saveShelfAsBrushSet(argString(a, "name"), msg);
                       return {ok, msg};
                   }});
    commands_.add({"brush_set_delete", "Delete a brush set. The brushes and the shelf stay as they are.",
                   nameParam("The set's name."), [this](Document&, const Args& a) -> CommandResult {
                       std::string msg;
                       const bool ok = deleteBrushSet(argString(a, "name"), msg);
                       return {ok, msg};
                   }});
    Action save;
    save.id = "brushset.save";
    save.label = "Save Shelf as Brush Set...";
    save.icon = ICON_LC_BOOKMARK_PLUS;
    save.palette = "Brush";
    save.description = "Keep the bottom shelf's brushes and their keys as a named set (Wood, Metal, Stone...). Switch "
                       "between sets in one step from the Brush palette's Brush set menu, the bottom shelf's right-click menu or here.";
    save.run = [this] { openBrushSetSave(); };
    actions_.add(std::move(save));
    refreshBrushSets();
    restoreBrushSet();
    syncShelfBrushActions();
}

void App::refreshBrushSets() {
    actions_.removePrefix("brushset.use.");
    std::vector<std::string> messages;
    brushSets_.list = loadBrushSets(brushSetsDir(), &messages);
    for (const std::string& m : messages) SDL_Log("Brush sets: %s", m.c_str());
    for (const BrushSet& set : brushSets_.list) {
        Action a;
        a.id = "brushset.use." + slug(set.name);
        a.label = "Brush Set: " + set.name;
        a.icon = ICON_LC_LIBRARY_BIG;
        a.palette = "Brush";
        a.description = "Switch the bottom shelf to the brush set " + set.name + " (" +
                        std::to_string(countBrushes(set.shelf)) + " brushes) and give its brushes their keys.";
        // Next frame: running an action must not change the action list under it.
        a.run = [this, name = set.name] { brushSets_.pending = name; };
        a.checked = [this, name = set.name] {
            const int i = matchingBrushSet();
            return i >= 0 && brushSets_.list[size_t(i)].name == name;
        };
        actions_.add(std::move(a));
    }
}

int App::matchingBrushSet() const {
    // The set switched to last first: two sets may share a shelf and differ only in keys.
    const int last = findSet(brushSets_.list, uiPrefs_.brushSet);
    if (last >= 0 && shelfOf(brushSets_.list[size_t(last)]) == layout_.bottomShelf) return last;
    for (size_t i = 0; i < brushSets_.list.size(); ++i)
        if (shelfOf(brushSets_.list[i]) == layout_.bottomShelf) return int(i);
    return -1;
}

void App::restoreBrushSet() {
    if (uiPrefs_.brushSet.empty()) return;
    const int i = findSet(brushSets_.list, uiPrefs_.brushSet);
    if (i < 0) {
        uiPrefs_.brushSet.clear(); // deleted outside the app
        return;
    }
    // Its keys are in the saved shortcuts already; the shelf waits for Save Layout otherwise.
    layout_.bottomShelf = shelfOf(brushSets_.list[size_t(i)]);
}

void App::syncShelfBrushActions() {
    if (!brushSets_.pending.empty()) {
        std::string msg;
        applyBrushSet(std::exchange(brushSets_.pending, std::string()), msg);
        status_ = brushSets_.shown;
    }
    if (layout_.bottomShelf == brushSets_.shelfSeen) return;
    brushSets_.shelfSeen = layout_.bottomShelf;
    // A set switched to and then edited by hand is no longer the set: the next start keeps the saved layout.
    if (!uiPrefs_.brushSet.empty() && matchingBrushSet() < 0) uiPrefs_.brushSet.clear();
    // Custom brushes on the shelf get an action each, so they can hold a key like the built-in ones.
    std::vector<std::string> had;
    for (const Action& a : actions_.all())
        if (a.id.rfind(kRecipePrefix, 0) == 0) had.push_back(a.id);
    actions_.removePrefix(kRecipePrefix);
    for (const std::string& item : layout_.bottomShelf) {
        if (item.rfind(kRecipePrefix, 0) != 0 || actions_.find(item)) continue;
        const std::string name = item.substr(std::char_traits<char>::length(kRecipePrefix));
        Action a;
        a.id = item;
        a.label = name;
        a.icon = ICON_LC_BRUSH;
        a.palette = "Brush";
        a.description = "Sculpt with the " + name + " brush.";
        a.run = [this, name] {
            for (int i = 0; i < int(doc_.brushes.size()); ++i)
                if (doc_.brushes[size_t(i)].name == name) {
                    doc_.activeBrush = i;
                    paintMode_ = false;
                    guidesMode_ = false;
                    endInsertBrush();
                    maskBrush_.on = false;
                }
        };
        a.checked = [this, name] {
            return doc_.activeBrush >= 0 && doc_.activeBrush < int(doc_.brushes.size()) && !paintMode_ &&
                   doc_.brushes[size_t(doc_.activeBrush)].name == name;
        };
        actions_.add(std::move(a));
        // Back on the shelf after a while: a key it kept that another action took since goes to that action.
        if (std::find(had.begin(), had.end(), item) == had.end()) {
            std::string keep;
            for (const Chord& c : parseChords(actions_.shortcutText(item))) {
                const Action* other = actions_.actionForChord(c);
                if (!other || other->id == item) keep += (keep.empty() ? "" : " / ") + chordText(c);
            }
            if (keep != actions_.shortcutText(item)) actions_.setShortcut(item, keep);
        }
    }
}

// The result in English (the command's message: scripts, the console, the AI link) and in the interface language
// (brushSets_.shown, for the status bar when the step came from the interface).
template <class... A> void App::say(std::string& message, const char* english, A... args) {
    message = format(english, args...);
    brushSets_.shown = format(tr(english), args...);
}

void App::sayCounts(std::string& message, const char* english, const std::string& name, int brushes, int keys) {
    message = format(english, name.c_str(), countText(brushes, "%d brush", "%d brushes", false).c_str(),
                     countText(keys, "%d key", "%d keys", false).c_str());
    brushSets_.shown = format(tr(english), name.c_str(), countText(brushes, "%d brush", "%d brushes", true).c_str(),
                              countText(keys, "%d key", "%d keys", true).c_str());
}

void App::runBrushSetCommand(const std::string& command, const std::string& name) {
    brushSets_.shown.clear();
    runCommand(command, {{"name", name}});
    if (!brushSets_.shown.empty()) status_ = brushSets_.shown; // in the interface language
}

bool App::applyBrushSet(const std::string& name, std::string& message) {
    const int i = findSet(brushSets_.list, name);
    if (i < 0) {
        if (brushSets_.list.empty()) say(message, "No brush sets yet: Save Shelf as Brush Set makes one");
        else say(message, "No brush set called %s", name.c_str());
        return false;
    }
    const BrushSet set = brushSets_.list[size_t(i)];
    layout_.bottomShelf = shelfOf(set);
    syncShelfBrushActions(); // its custom brushes' actions first, so their keys can be taken from others
    int keys = 0;
    for (const auto& [id, text] : set.keys) {
        const std::vector<Chord> chords = parseChords(text);
        if (chords.empty()) {
            actions_.setShortcut(id, "");
            continue;
        }
        for (size_t c = 0; c < chords.size(); ++c) actions_.assignShortcut(id, chordText(chords[c]), c > 0);
        ++keys;
    }
    uiPrefs_.brushSet = set.name;
    sayCounts(message, "Brush set: %s (%s, %s)", set.name, countBrushes(set.shelf), keys);
    return true;
}

bool App::saveShelfAsBrushSet(const std::string& rawName, std::string& message) {
    const std::string name = trimmed(rawName);
    if (name.empty() || name.size() > kMaxBrushSetName) {
        say(message, "A brush set's name must be 1 to 64 characters");
        return false;
    }
    BrushSet set;
    set.name = name;
    set.shelf = layout_.bottomShelf;
    for (const std::string& item : set.shelf) {
        const std::string id = brushKeyAction(item);
        if (!id.empty() && actions_.find(id)) set.keys.emplace_back(id, actions_.shortcutText(id));
    }
    const bool replaced = std::any_of(brushSets_.list.begin(), brushSets_.list.end(), [&](const BrushSet& b) { return b.name == name; });
    std::string err;
    if (!saveBrushSet(brushSetsDir(), set, err)) {
        message = brushSets_.shown = err;
        return false;
    }
    refreshBrushSets();
    uiPrefs_.brushSet = name;
    const int keys = int(std::count_if(set.keys.begin(), set.keys.end(), [](const auto& k) { return !k.second.empty(); }));
    sayCounts(message, replaced ? "Brush set updated: %s (%s, %s)" : "Brush set saved: %s (%s, %s)", name, countBrushes(set.shelf),
              keys);
    return true;
}

bool App::deleteBrushSet(const std::string& name, std::string& message) {
    const int i = findSet(brushSets_.list, name);
    if (i < 0) {
        say(message, "No brush set called %s", name.c_str());
        return false;
    }
    const std::string gone = brushSets_.list[size_t(i)].name;
    std::string err;
    if (!deleteBrushSetFile(brushSets_.list[size_t(i)], err)) {
        message = brushSets_.shown = err;
        return false;
    }
    if (uiPrefs_.brushSet == gone) uiPrefs_.brushSet.clear();
    refreshBrushSets();
    say(message, "Brush set deleted: %s (the shelf stays as it is)", gone.c_str());
    return true;
}

void App::openBrushSetSave() {
    const int i = matchingBrushSet();
    std::snprintf(brushSets_.name, sizeof brushSets_.name, "%s", i >= 0 ? brushSets_.list[size_t(i)].name.c_str() : "");
    brushSets_.saveOpen = true;
}

void App::drawBrushSetMenu() {
    const int current = matchingBrushSet();
    if (brushSets_.list.empty()) ImGui::TextDisabled("%s", tr("No brush sets yet"));
    for (size_t i = 0; i < brushSets_.list.size(); ++i) {
        const BrushSet& set = brushSets_.list[i];
        const int n = countBrushes(set.shelf);
        char count[64];
        std::snprintf(count, sizeof count, n == 1 ? tr("%d brush") : tr("%d brushes"), n);
        const std::string label = set.name + "##brushset" + std::to_string(i);
        if (ImGui::MenuItem(label.c_str(), count, int(i) == current)) runBrushSetCommand("brush_set", set.name);
        ui::HelpText(set.name.c_str(), actions_.shortcutText("brushset.use." + slug(set.name)).c_str(),
                     "Switch the bottom shelf to this set's brushes and give them the set's keys.", "Brush > Brush set");
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Save Shelf as Brush Set...##brushsetmenu"))) openBrushSetSave();
    ui::HelpAction(*this, "brushset.save");
    if (current >= 0) {
        const std::string label = std::string(tr("Delete")) + " " + brushSets_.list[size_t(current)].name + "...##brushsetmenu";
        if (ImGui::MenuItem(label.c_str())) {
            brushSets_.deleting = brushSets_.list[size_t(current)].name;
            brushSets_.deleteOpen = true;
        }
        ui::HelpText("Delete brush set", nullptr, "Delete this brush set. The brushes and the shelf stay as they are.",
                     "Brush > Brush set");
    }
}

void App::drawBrushSetRow() {
    const float s = uiScale_;
    const int current = matchingBrushSet();
    const std::string preview = std::string(tr("Brush set:")) + " " +
                                (current >= 0           ? brushSets_.list[size_t(current)].name
                                 : brushSets_.list.empty() ? std::string(tr("none yet"))
                                                           : std::string(tr("this shelf (not saved)")));
    const float saveW = ImGui::CalcTextSize(tr("Save")).x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f * s;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - saveW - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::BeginCombo("##brushset", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        drawBrushSetMenu();
        ImGui::EndCombo();
    }
    ui::HelpText("Brush set", nullptr,
                 "Named bottom shelves with their keys (Wood, Metal, Stone...): pick one to swap the shelf's brushes and "
                 "their keys in one step. Save keeps the shelf as it is now, with its keys, as a set. Sets live in your "
                 "brush library; F3 finds them too.",
                 "Brush");
    ImGui::SameLine();
    if (ImGui::Button(tr("Save##brushsetsave"), ImVec2(saveW, 0)))
        openBrushSetSave();
    ui::HelpAction(*this, "brushset.save");
}

void App::drawBrushSetSave() {
    // (Translated titles: the popup's id follows the language, opened and drawn under the same one.)
    const char* savePopup = tr(kSavePopup);
    const char* deletePopup = tr(kDeletePopup);
    if (brushSets_.saveOpen) {
        ImGui::OpenPopup(savePopup);
        brushSets_.saveOpen = false;
        brushSets_.saveFocus = true;
    }
    if (brushSets_.deleteOpen) {
        ImGui::OpenPopup(deletePopup);
        brushSets_.deleteOpen = false;
    }
    const float s = uiScale_;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const ImVec2 button(110 * s, 0);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(savePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        std::string& error = brushSets_.error;
        if (brushSets_.saveFocus) {
            error.clear();
            ImGui::SetKeyboardFocusHere();
            brushSets_.saveFocus = false;
        }
        ImGui::SetNextItemWidth(320 * s);
        bool ok = ImGui::InputTextWithHint("##brushsetname", tr("Name, e.g. Wood, Metal or Stone"), brushSets_.name,
                                           sizeof brushSets_.name, ImGuiInputTextFlags_EnterReturnsTrue);
        const int brushes = countBrushes(layout_.bottomShelf);
        ImGui::PushTextWrapPos(320 * s);
        ImGui::TextDisabled(tr("The bottom shelf's %d brushes, in order, and their keys. Switching to the set later puts "
                               "both back."),
                            brushes);
        const std::string name = trimmed(brushSets_.name);
        const int existing = name.empty() ? -1 : findSet(brushSets_.list, name);
        if (existing >= 0 && brushSets_.list[size_t(existing)].name == name)
            ImGui::TextColored(ImVec4(0.94f, 0.53f, 0.18f, 1.0f), tr("Replaces the set %s."), name.c_str());
        ImGui::PopTextWrapPos();
        if (!error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", error.c_str());
        ImGui::BeginDisabled(name.empty());
        ok |= ImGui::Button(tr("Save##brushsetsave_ok"), button);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel##brushsetsave_cancel"), button) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        } else if (ok && !name.empty()) {
            runBrushSetCommand("brush_set_save", name);
            if (lastCommandOk_) ImGui::CloseCurrentPopup();
            else error = status_;
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(deletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text(tr("Delete the brush set %s?"), brushSets_.deleting.c_str());
        ImGui::TextDisabled("%s", tr("Only the set goes: the brushes and the shelf stay as they are."));
        bool close = false;
        if (ImGui::Button(tr("Delete##brushsetdelete_ok"), button)) {
            runBrushSetCommand("brush_set_delete", brushSets_.deleting);
            close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel##brushsetdelete_cancel"), button) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
        if (close) {
            brushSets_.deleting.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace sf
