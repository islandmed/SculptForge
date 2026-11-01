// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The persistent brush library in the app (core/BrushLibrary.h): the Custom
// brushes list with where each brush comes from and its right-click menu
// (copy code, save, rename, delete, reveal), pasting brushes from the
// clipboard, and saving Brush Lab edits to the user library automatically.
#include "app/App.h"

#include "core/BrushLibrary.h"

#include <imgui.h>

#include <cctype>
#include <cstring>
#include <filesystem>

namespace sf {

namespace {

namespace fs = std::filesystem;

constexpr double kAutosaveDelay = 0.75; // seconds after the last edit (sliders save once released)
constexpr const char* kRenamePopup = "Rename brush";
constexpr const char* kDeletePopup = "Delete brush?";

fs::path fsPath(const std::string& p) { return fs::path(std::u8string(p.begin(), p.end())); }
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// The subtle tag after a brush's name: where it comes from, when that isn't the artist's own or a built-in.
const char* brushTag(const BrushRecipe& b) {
    switch (b.origin.source) {
    case BrushSource::Studio: return "studio";
    case BrushSource::Pack: return b.origin.pack.c_str(); // the pack's name
    case BrushSource::Gallery: return "gallery";
    case BrushSource::Project: return "project";
    case BrushSource::Session: return "unsaved";
    case BrushSource::User: return b.origin.overrides != BrushSource::Session ? "yours" : nullptr;
    case BrushSource::Builtin: return nullptr;
    }
    return nullptr;
}


// `text` shortened with "..." to fit `width` pixels.
std::string fitText(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    std::string s = text;
    while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > width) {
        s.pop_back();
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back(); // whole UTF-8 characters
    }
    return s + "...";
}

std::string fileUrl(const std::string& path) {
    std::string p = path;
    for (char& c : p)
        if (c == '\\') c = '/';
    std::string url = p.rfind('/', 0) == 0 ? "file://" : "file:///";
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : p) {
        if (std::isalnum(c) || std::strchr("/:-_.~", c)) url += char(c);
        else url += std::string("%") + hex[c >> 4] + hex[c & 15];
    }
    return url;
}

} // namespace

// Where a brush comes from, in words (list tooltips, picker Quick Info).
std::string brushOriginText(const BrushRecipe& b) {
    switch (b.origin.source) {
    case BrushSource::Builtin: return "Built-in brush";
    case BrushSource::User:
        return b.origin.overrides != BrushSource::Session
                   ? std::string("Your version - replaces the ") + brushSourceName(b.origin.overrides) +
                         " brush of the same name (delete it to go back)"
                   : "In your brush library";
    case BrushSource::Gallery: return "From the community gallery, in your brush library";
    case BrushSource::Studio: return "Studio brush (shared folder, read-only)";
    case BrushSource::Pack: return "From the pack " + b.origin.pack + " (read-only; Library > Packs)";
    case BrushSource::Project: return "From the open project, not in your brush library";
    case BrushSource::Session: return "Not saved - refine it in Brush Lab to keep it";
    }
    return "";
}

void App::initBrushLibrary() {
    BrushLibraryPaths& paths = brushLibraryPaths();
    if (!options_.uiTest.empty()) {
        // UI tests get an empty library of their own, never the artist's.
        const fs::path base = options_.uiOutDir.empty() ? fs::temp_directory_path() / "sculptforge-ui-test" : fsPath(options_.uiOutDir);
        paths = {utf8(base / "brush-library"), {}, {}};
        std::error_code ec;
        fs::remove_all(fsPath(paths.userDir), ec);
        // ... and no packs or macros but their own (core/Pack.h, core/Macro.h).
        setPacksRoot(utf8(base / "packs"));
        setUserMacroDir(utf8(base / "macros"));
        fs::remove_all(base / "packs", ec);
        fs::remove_all(base / "macros", ec);
    } else if (!options_.recordPath.empty() || !options_.renderPath.empty()) {
        paths = {}; // demo videos and renders: built-in brushes only, nothing saved
        setPacksSafeMode(true);
    }
    applyPacksToBrushLibrary(paths); // the enabled packs' brushes and VDMs (none in safe mode)
    const auto messages = loadBrushLibrary(doc_, paths);
    for (const auto& m : messages) SDL_Log("Brush library: %s", m.c_str());
    if (!messages.empty())
        status_ = std::to_string(messages.size()) + (messages.size() == 1 ? " brush file" : " brush files") +
                  " couldn't be loaded (details in the log)";
    watchActiveBrush();
}

void App::watchActiveBrush() {
    const BrushRecipe* r = activeRecipe();
    brushWatch_ = {r ? doc_.activeBrush : -1, r ? r->name : "", r ? r->toJson().dump() : "", false, ImGui::GetTime()};
}

void App::saveBrushEdits(int index) {
    if (brushLibraryPaths().userDir.empty() || index < 0 || index >= int(doc_.brushes.size())) return;
    const bool wasSaved = isUserLibraryBrush(doc_.brushes[size_t(index)]);
    std::string err;
    if (!saveBrushToLibrary(doc_, index, err)) {
        status_ = "Couldn't save brush '" + doc_.brushes[size_t(index)].name + "': " + err;
        SDL_Log("%s", status_.c_str());
        return;
    }
    const BrushRecipe& b = doc_.brushes[size_t(index)];
    if (!wasSaved)
        status_ = b.origin.overrides != BrushSource::Session
                      ? "Saved your version of '" + b.name + "' (the " + brushSourceName(b.origin.overrides) + " one stays available)"
                      : "Saved '" + b.name + "' to your brush library";
}

void App::flushBrushEdits() {
    if (!brushWatch_.pending) return;
    brushWatch_.pending = false;
    // By name: the list may have changed since the edit.
    for (int i = 0; i < int(doc_.brushes.size()); ++i)
        if (doc_.brushes[size_t(i)].name == brushWatch_.name) saveBrushEdits(i);
}

void App::autosaveBrush() {
    const BrushRecipe* r = activeRecipe();
    if (!r || doc_.activeBrush != brushWatch_.index || r->name != brushWatch_.name) {
        flushBrushEdits(); // another brush was picked: the last one's edits first
        watchActiveBrush();
        return;
    }
    const double now = ImGui::GetTime();
    std::string json = r->toJson().dump();
    if (json != brushWatch_.json) {
        brushWatch_.json = std::move(json);
        brushWatch_.pending = true;
        brushWatch_.changedAt = now;
    }
    if (brushWatch_.pending && now - brushWatch_.changedAt >= kAutosaveDelay && !ImGui::IsAnyItemActive()) {
        brushWatch_.pending = false;
        saveBrushEdits(doc_.activeBrush);
    }
}

void App::copyBrushCode(int index) {
    if (index < 0 || index >= int(doc_.brushes.size())) return;
    const BrushRecipe& b = doc_.brushes[size_t(index)];
    if (!SDL_SetClipboardText(encodeBrushCode(b).c_str())) {
        status_ = std::string("Couldn't copy to the clipboard: ") + SDL_GetError();
        return;
    }
    status_ = "Copied the brush code for '" + b.name + "' - share it anywhere; Ctrl+V in SculptForge installs it";
}

void App::pasteBrush() {
    char* clip = SDL_GetClipboardText();
    const std::string text = clip ? clip : "";
    SDL_free(clip);
    BrushRecipe r;
    std::string err;
    if (!parseBrushText(text, r, err)) {
        status_ = "Paste brush: " + err;
        return;
    }
    runCommand("brush_install", {{"json", r.toJson().dump()}});
    if (!lastCommandOk_ || !activeRecipe()) return; // status says why
    paintMode_ = false;
    const BrushRecipe& b = *activeRecipe();
    status_ = "Pasted brush '" + b.name + "'" + (isUserLibraryBrush(b) ? " - it's in your brush library" : "");
}

void App::installBrushFile(const std::string& path) {
    runCommand("brush_install", {{"path", path}});
    if (!lastCommandOk_ || !activeRecipe()) return; // status says why
    paintMode_ = false;
    const BrushRecipe& b = *activeRecipe();
    status_ = "Installed brush '" + b.name + "'" + (isUserLibraryBrush(b) ? " in your brush library" : "");
}

void App::revealBrushFile(int index) {
    if (index < 0 || index >= int(doc_.brushes.size()) || doc_.brushes[size_t(index)].origin.path.empty()) return;
    const std::string dir = utf8(fsPath(doc_.brushes[size_t(index)].origin.path).parent_path());
    if (!SDL_OpenURL(fileUrl(dir).c_str())) status_ = "Couldn't open " + dir + ": " + SDL_GetError();
}

void App::drawCustomBrushes() {
    ImGui::SeparatorText("Custom brushes");
    enum class Action { None, Copy, Save, Rename, Delete, Reveal } action = Action::None;
    int actionIndex = -1;
    if (ImGui::BeginListBox("##custom", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 5.5f))) {
        const ImGuiStyle& style = ImGui::GetStyle();
        for (int i = 0; i < int(doc_.brushes.size()); ++i) {
            const BrushRecipe& b = doc_.brushes[size_t(i)];
            const char* tag = brushTag(b);
            const float tagWidth = tag ? ImGui::CalcTextSize(tag).x + style.ItemSpacing.x : 0.0f;
            const std::string label = fitText(b.name, ImGui::GetContentRegionAvail().x - tagWidth) + "##brush" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), doc_.activeBrush == i, ImGuiSelectableFlags_AllowOverlap)) {
                doc_.activeBrush = i;
                paintMode_ = false;
            }
            // A brush picked elsewhere (pasted, installed, made in Brush Lab) is scrolled into view.
            if (i == doc_.activeBrush && i != listedBrush_) {
                const float top = ImGui::GetWindowPos().y, bottom = top + ImGui::GetWindowHeight();
                if (ImGui::GetItemRectMin().y < top || ImGui::GetItemRectMax().y > bottom) ImGui::SetScrollHereY(0.5f);
            }
            if (tag) {
                const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax(), size = ImGui::CalcTextSize(tag);
                ImGui::GetWindowDrawList()->AddText(ImVec2(max.x - size.x - style.FramePadding.x * 0.5f, min.y + (max.y - min.y - size.y) * 0.5f),
                                                    ImGui::GetColorU32(doc_.activeBrush == i ? ImGuiCol_Text : ImGuiCol_TextDisabled,
                                                                       doc_.activeBrush == i ? 0.7f : 1.0f),
                                                    tag);
                ImGui::LogText(" [%s]", tag); // drawn directly: tell the log (UI tests read it) too
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
                if (!b.description.empty()) ImGui::TextUnformatted(b.description.c_str());
                ImGui::TextDisabled("%s", brushOriginText(b).c_str());
                if (!b.origin.path.empty()) ImGui::TextDisabled("%s", b.origin.path.c_str());
                ImGui::TextDisabled("Right-click to share, rename or delete");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::IsWindowAppearing()) { // right-clicking picks the brush too
                    doc_.activeBrush = i;
                    paintMode_ = false;
                }
                ImGui::TextDisabled("%s", b.name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Copy brush code")) action = Action::Copy;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("One line of text to share in chat, email or a forum");
                if (ImGui::MenuItem("Save .sfbrush...")) action = Action::Save;
                if (ImGui::MenuItem("Rename...##brush")) action = Action::Rename;
                if (ImGui::MenuItem("Delete...##brush", nullptr, false, isUserLibraryBrush(b))) action = Action::Delete;
                if (!isUserLibraryBrush(b) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Only brushes in your own library can be deleted");
                if (ImGui::MenuItem("Reveal in folder", nullptr, false, !b.origin.path.empty())) action = Action::Reveal;
                if (action != Action::None) actionIndex = i;
                ImGui::EndPopup();
            }
        }
        ImGui::EndListBox();
        listedBrush_ = doc_.activeBrush;
    }
    // After the list: these may change it.
    if (actionIndex < 0) return;
    const std::string name = doc_.brushes[size_t(actionIndex)].name;
    switch (action) {
    case Action::Copy: copyBrushCode(actionIndex); break;
    case Action::Save:
        doc_.activeBrush = actionIndex;
        saveFileDialog(FileAction::SaveBrush);
        break;
    case Action::Rename:
        brushPopup_ = BrushPopup::Rename;
        brushPopupName_ = name;
        std::snprintf(brushRenameBuf_, sizeof brushRenameBuf_, "%s", name.c_str());
        break;
    case Action::Delete:
        brushPopup_ = BrushPopup::Delete;
        brushPopupName_ = name;
        break;
    case Action::Reveal: revealBrushFile(actionIndex); break;
    case Action::None: break;
    }
}

void App::drawBrushLibraryPopups() {
    if (brushPopup_ == BrushPopup::Rename) ImGui::OpenPopup(kRenamePopup);
    if (brushPopup_ == BrushPopup::Delete) ImGui::OpenPopup(kDeletePopup);
    brushPopup_ = BrushPopup::None;
    int index = -1;
    for (int i = 0; i < int(doc_.brushes.size()); ++i)
        if (doc_.brushes[size_t(i)].name == brushPopupName_) index = i;
    const float scale = uiScale_;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const ImVec2 buttonSize(110 * scale, 0);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kRenamePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string& error = brushPopupError_;
        if (ImGui::IsWindowAppearing()) {
            error.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(300 * scale);
        bool ok = ImGui::InputText("##newname", brushRenameBuf_, sizeof brushRenameBuf_, ImGuiInputTextFlags_EnterReturnsTrue);
        if (index >= 0 && !isUserLibraryBrush(doc_.brushes[size_t(index)]) &&
            doc_.brushes[size_t(index)].origin.source != BrushSource::Session &&
            doc_.brushes[size_t(index)].origin.source != BrushSource::Project) {
            ImGui::PushTextWrapPos(300 * scale);
            ImGui::TextDisabled("The %s brush stays as it is; the renamed copy goes into your library.",
                                brushSourceName(doc_.brushes[size_t(index)].origin.source));
            ImGui::PopTextWrapPos();
        }
        if (!error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", error.c_str());
        ok |= ImGui::Button("Rename", buttonSize);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape) || index < 0) ImGui::CloseCurrentPopup();
        else if (ok) {
            runCommand("brush_rename", {{"name", brushPopupName_}, {"to", std::string(brushRenameBuf_)}});
            if (lastCommandOk_) ImGui::CloseCurrentPopup();
            else error = status_;
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kDeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (index >= 0) {
            const BrushRecipe& b = doc_.brushes[size_t(index)];
            ImGui::Text("Delete '%s' from your brush library?", b.name.c_str());
            ImGui::PushTextWrapPos(340 * scale);
            if (b.origin.overrides != BrushSource::Session)
                ImGui::TextDisabled("The %s brush of the same name is used again.", brushSourceName(b.origin.overrides));
            else
                ImGui::TextDisabled("Its .sfbrush file is removed. Copy its brush code first if you might want it back.");
            ImGui::PopTextWrapPos();
        }
        if (ImGui::Button("Delete", buttonSize) && index >= 0) {
            runCommand("brush_delete", {{"name", brushPopupName_}});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape) || index < 0) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace sf
