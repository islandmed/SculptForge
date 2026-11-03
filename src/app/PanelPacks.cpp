// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Library > Packs and macros (#20): installing a pack after a review screen, the installed packs (on / off,
// remove, their keymaps, pie menus and macros), New Pack, recording and editing macros, the confirmation a macro
// shows before it first runs, and its file dialogs. The checks themselves are in core/Pack.cpp and core/Macro.cpp.
#include "app/App.h"

#include "app/I18n.h"
#include "app/Ui.h"
#include "core/BrushLibrary.h"
#include "core/Matcap.h"
#include "core/MaterialRecipe.h"
#include "core/Project.h"
#include "core/Texture.h"
#include "core/Vdm.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace sf {

namespace {

namespace fs = std::filesystem;

fs::path fsPath(const std::string& p) { return fs::path(std::u8string(p.begin(), p.end())); }
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string lowerText(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// "Clean Up Scan" -> "clean_up_scan": a stable piece of an action id.
std::string slug(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) out += char(std::tolower(c));
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "macro" : out.substr(0, 48);
}

constexpr ImU32 kAccent = IM_COL32(240, 136, 46, 255);
constexpr ImU32 kInsider = IM_COL32(236, 190, 90, 255);

void SDLCALL onPackDialog(void* userdata, const char* const* files, int) {
    wakeMainLoop();
    auto* app = static_cast<App*>(userdata);
    if (!files) app->packDialogAnswered(""); // an error
    else app->packDialogAnswered(files[0] ? files[0] : "");
}

void centerNextWindow(float w, float h) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(w, vp->WorkSize.x - 40.0f), std::min(h, vp->WorkSize.y - 40.0f)),
                             ImGuiCond_Appearing);
}

std::vector<std::string> filesIn(const std::string& dir, const std::function<bool(const std::string&)>& keep) {
    std::vector<std::string> out;
    if (dir.empty()) return out;
    std::error_code ec;
    for (fs::directory_iterator it(fsPath(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_symlink(e2) && it->is_regular_file(e2) && keep(utf8(it->path().filename())))
            out.push_back(utf8(it->path()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Actions and refreshing
// ---------------------------------------------------------------------------

void App::registerPackActions() {
    auto add = [this](const char* id, const char* label, const char* icon, const char* palette, const char* description,
                      std::function<void()> run, std::function<bool()> checked = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = palette;
        a.description = description;
        a.run = std::move(run);
        a.checked = std::move(checked);
        actions_.add(std::move(a));
    };
    add("pack.install", "Install Pack...", ICON_LC_PACKAGE_PLUS, "File",
        "Install a SculptForge pack (.sfpack): brushes, alphas, Lit Spheres, Material Recipes, VDMs, keymaps, pie "
        "menus and macros. A review shows exactly what's inside, its license and its author before anything is "
        "installed. Packs hold data only: nothing in one ever runs.",
        [this] { openPackDialog(kPackDialogInstall); });
    add("pack.new", "New Pack...", ICON_LC_PACKAGE, "Library > Packs",
        "Make a pack from your own brushes, alphas, Lit Spheres, Material Recipe, VDMs, pie menus, shortcuts and "
        "macros, to share as one .sfpack file.",
        [this] { openNewPack(); });
    add("pack.library", "Packs", ICON_LC_PACKAGE, "Library",
        "Library > Packs: the installed packs. Switch one off or remove it, apply its keymap, add its pie menus.",
        [this] { lightbox_.show(LibraryTab::Packs); });
    add("macro.record", "Record Macro", ICON_LC_CIRCLE_DOT, "Library > Packs",
        "Start recording: the commands you run (menus, buttons, the console) are kept as a macro until you press it "
        "again. Files inside the project folder are kept as relative paths, any other file is asked for when the "
        "macro runs.",
        [this] { toggleMacroRecording(); }, [this] { return macros_.recorder.active(); });
    add("macro.editor", "Macro Editor", ICON_LC_SQUARE_CODE, "Library > Packs",
        "Write or change a macro: SculptForge commands, one per line, checked as you type. Only SculptForge's own "
        "commands, nothing that reaches the network, and files only inside the project folder or picked when it runs.",
        [this] { openMacroEditor("My Macro", "", ""); });
    packs_.installed = listInstalledPacks();
    packs_.alphas = enabledPackItems(PackItemType::Alpha);
    packs_.materials = enabledPackItems(PackItemType::Material);
    packs_.keymaps = enabledPackItems(PackItemType::Keymap);
    packs_.pieMenus = enabledPackItems(PackItemType::PieMenu);
    loadPackMatcaps();
    refreshMacroActions();
}

void App::refreshMacroActions() {
    actions_.removePrefix("macro.run.");
    std::vector<std::string> messages;
    macros_.list = listMacros(&commands_, &messages);
    for (const std::string& m : messages) SDL_Log("Macros: %s", m.c_str());
    for (const Macro& m : macros_.list) {
        Action a;
        a.id = "macro.run." + (m.pack.empty() ? std::string("my") : slug(m.pack)) + "." + slug(m.name);
        a.label = m.pack.empty() ? m.name : m.name + " (" + m.pack + ")";
        a.icon = ICON_LC_LIST_VIDEO;
        a.palette = "Macros";
        std::string first;
        for (size_t i = 0; i < m.statements.size() && i < 3; ++i)
            first += (i ? "; " : "") + formatStatement(m.statements[i].command, m.statements[i].args);
        a.description = (m.description.empty() ? std::string("Macro") : m.description) + " - " +
                        std::to_string(m.statements.size()) + " commands: " + first +
                        (m.statements.size() > 3 ? "; ..." : "");
        a.run = [this, m] { requestMacroRun(m); };
        actions_.add(std::move(a));
    }
}

void App::loadPackMatcaps() {
    for (int i : packs_.matcaps) removeCustomMatcap(i);
    packs_.matcaps.clear();
    for (const PackContentItem& item : enabledPackItems(PackItemType::Matcap)) {
        CustomMatcap m;
        std::string err;
        if (!readMatcapImage(item.file, m, err)) {
            SDL_Log("Lit Spheres: %s (pack %s): %s", item.file.c_str(), item.packName.c_str(), err.c_str());
            continue;
        }
        m.name = item.label(); // "Chrome (Member pack)": never mistaken for (or replacing) another
        packs_.matcaps.push_back(addCustomMatcap(std::move(m)));
    }
}

void App::refreshPacks() {
    packs_.installed = listInstalledPacks();
    applyPacksToBrushLibrary(brushLibraryPaths());
    for (const auto& m : loadBrushLibrary(doc_)) SDL_Log("Brush library: %s", m.c_str());
    watchActiveBrush();
    packs_.alphas = enabledPackItems(PackItemType::Alpha);
    packs_.materials = enabledPackItems(PackItemType::Material);
    packs_.keymaps = enabledPackItems(PackItemType::Keymap);
    packs_.pieMenus = enabledPackItems(PackItemType::PieMenu);
    loadPackMatcaps();
    refreshMacroActions();
}

std::string App::macroProjectDir() const {
    if (doc_.path.empty() || !isProjectPath(doc_.path)) return "";
    return utf8(fsPath(doc_.path).parent_path());
}

// ---------------------------------------------------------------------------
// File dialogs (answered on another thread; UI tests answer with answer_dialog)
// ---------------------------------------------------------------------------

void App::openPackDialog(int kind, int pathKind) {
    {
        std::lock_guard lock(packs_.mutex);
        packs_.dialog = kind;
        packs_.answered = false;
    }
    if (!options_.uiTest.empty()) return; // the script answers (answer_dialog)
    static const SDL_DialogFileFilter kPackFilters[] = {{"SculptForge packs", "sfpack"}, {"All files", "*"}};
    static const SDL_DialogFileFilter kAll[] = {{"All files", "*"}};
    const std::string project = macroProjectDir();
    const char* where = project.empty() ? nullptr : project.c_str();
    switch (kind) {
    case kPackDialogInstall: SDL_ShowOpenFileDialog(onPackDialog, this, window_, kPackFilters, 2, nullptr, false); break;
    case kPackDialogSave: {
        const std::string name = std::string(packs_.name[0] ? packs_.name : "My Pack") + kPackExtension;
        SDL_ShowSaveFileDialog(onPackDialog, this, window_, kPackFilters, 2, name.c_str());
        break;
    }
    case kPackDialogMacro:
        if (pathKind == int(MacroPathKind::Folder)) SDL_ShowOpenFolderDialog(onPackDialog, this, window_, where, false);
        else if (pathKind == int(MacroPathKind::Save)) SDL_ShowSaveFileDialog(onPackDialog, this, window_, kAll, 1, where);
        else SDL_ShowOpenFileDialog(onPackDialog, this, window_, kAll, 1, where, false);
        break;
    default: break;
    }
}

void App::packDialogAnswered(std::string path) {
    std::lock_guard lock(packs_.mutex);
    packs_.answer = std::move(path);
    packs_.answerCancelled = packs_.answer.empty();
    packs_.answered = true;
}

void App::pollPackDialogs() {
    std::string answer;
    int kind;
    {
        std::lock_guard lock(packs_.mutex);
        if (!packs_.answered) return;
        packs_.answered = false;
        answer = std::move(packs_.answer);
        kind = packs_.dialog;
        packs_.dialog = kPackDialogNone;
    }
    switch (kind) {
    case kPackDialogInstall:
        if (!answer.empty()) reviewPackFile(answer);
        break;
    case kPackDialogSave:
        if (!answer.empty()) {
            if (fsPath(answer).extension().empty()) answer += kPackExtension;
            writeNewPack(answer);
        }
        break;
    case kPackDialogMacro:
        if (!macros_.waiting) break;
        if (answer.empty()) {
            macros_.waiting = false;
            status_ = "Macro " + macros_.running.name + " stopped: no file picked";
            break;
        }
        macros_.answers.push_back(answer);
        if (macros_.answers.size() < macros_.asks.size())
            openPackDialog(kPackDialogMacro, int(macros_.asks[macros_.answers.size()].kind));
        else runMacroNow();
        break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Install review
// ---------------------------------------------------------------------------

void App::reviewPackFile(const std::string& path) {
    packs_.review = std::make_unique<PackReview>();
    packs_.reviewPath = path;
    packs_.reviewError.clear();
    std::string err;
    if (!reviewPack(path, *packs_.review, err, &commands_)) {
        packs_.review.reset();
        packs_.reviewError = err;
        status_ = "Pack refused: " + err;
        SDL_Log("Pack %s refused: %s", path.c_str(), err.c_str());
    }
    packs_.reviewOpen = true;
}

void App::drawPackReview() {
    if (!packs_.reviewOpen) return;
    const float s = uiScale_;
    centerNextWindow(560 * s, 520 * s);
    bool open = true;
    if (!ImGui::Begin(tr("Install Pack"), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        packs_.reviewOpen = open;
        return;
    }
    PackReview* r = packs_.review.get();
    bool close = !open;
    if (!r) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 110, 100, 255));
        ImGui::TextUnformatted(tr("This pack can't be installed"));
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", utf8(fsPath(packs_.reviewPath).filename()).c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("%s", packs_.reviewError.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("%s", tr("Nothing was installed. SculptForge only installs packs that pass every check: "
                                    "plain data files of the kinds a pack may hold, listed in its pack.json, within "
                                    "safe sizes, with nothing that could leave the pack's folder."));
        ImGui::Spacing();
        if (ImGui::Button(tr("Close##packreview"), ImVec2(120 * s, 0))) close = true;
    } else {
        const PackManifest& m = r->manifest;
        if (fontBold_) ImGui::PushFont(fontBold_);
        ImGui::TextUnformatted((m.name + "  " + m.version).c_str());
        if (fontBold_) ImGui::PopFont();
        if (m.channel == "insider") {
            ImGui::SameLine();
            ImGui::TextColored(ImColor(kInsider), "%s", tr("Member pack"));
        } else if (!m.channel.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", m.channel.c_str());
        }
        ImGui::Text("%s %s", tr("Author:"), m.author.c_str());
        ImGui::Text("%s %s", tr("License:"), m.license.c_str());
        ImGui::TextDisabled("%s  -  %s unpacked", utf8(fsPath(packs_.reviewPath).filename()).c_str(),
                            r->bytes >= 1024 * 1024 ? (std::to_string(r->bytes / (1024 * 1024)) + " MB").c_str()
                                                    : (std::to_string(std::max<uint64_t>(1, r->bytes / 1024)) + " KB").c_str());
        if (!m.description.empty()) ImGui::TextWrapped("%s", m.description.c_str());
        if (!r->installedVersion.empty())
            ImGui::TextColored(ImColor(kAccent), "%s %s", tr("Replaces the installed version"), r->installedVersion.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s %s", tr("Contains:"), packCountsText(r->counts).c_str());
        const float listH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 4.2f;
        ImGui::BeginChild("##packitems", ImVec2(ImGui::GetContentRegionAvail().x * 0.48f, std::max(60.0f, listH)),
                          ImGuiChildFlags_Borders);
        for (const std::string& n : r->itemNames) ImGui::TextUnformatted(n.c_str());
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##packlicense", ImVec2(0, std::max(60.0f, listH)), ImGuiChildFlags_Borders);
        if (r->licenseText.empty()) ImGui::TextDisabled("%s", tr("No license text in the pack (see License above)."));
        else ImGui::TextWrapped("%s", r->licenseText.c_str());
        ImGui::EndChild();
        ImGui::TextWrapped("%s", tr("Packs hold data only: nothing in this one runs on install or when a project "
                                    "opens. Its macros list their commands and ask before they first run."));
        if (r->counts[size_t(PackItemType::Keymap)] || r->counts[size_t(PackItemType::PieMenu)])
            ImGui::TextDisabled("%s", tr("Keymaps and pie menus are only applied when you choose to (Library > Packs)."));
        if (ImGui::Button(tr("Install##packreview"), ImVec2(120 * s, 0))) {
            std::string err;
            if (installPack(*r, err)) {
                refreshPacks();
                status_ = "Installed " + m.name + " " + m.version + ": " + packCountsText(r->counts) +
                          (packsSafeMode() ? " (packs are off this session: --no-packs)" : "");
                lightbox_.show(LibraryTab::Packs);
                close = true;
            } else {
                status_ = "Could not install " + m.name + ": " + err;
            }
        }
        ui::HelpText("Install", nullptr, "Copy the pack into SculptForge's packs folder and add its items to the lists.",
                     "Library > Packs");
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel##packreview"), ImVec2(120 * s, 0))) close = true;
    }
    ImGui::End();
    if (close) {
        packs_.reviewOpen = false;
        packs_.review.reset();
    }
}

// ---------------------------------------------------------------------------
// Library > Packs
// ---------------------------------------------------------------------------

void App::drawPacksTab(float height) {
    const float s = uiScale_;
    ImGui::BeginChild("##packs", ImVec2(0, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ui::ActionButton(*this, "pack.install", ImVec2(0, 0), ui::Look::IconLabel);
    ImGui::SameLine();
    ui::ActionButton(*this, "pack.new", ImVec2(0, 0), ui::Look::IconLabel);
    ImGui::SameLine(0, 18 * s);
    if (macros_.recorder.active()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 90, 80, 255));
        if (ImGui::Button((std::string(ICON_LC_SQUARE "  ") + tr("Stop Recording") + " (" +
                           std::to_string(macros_.recorder.lines().size()) + ")###macro.stop").c_str()))
            toggleMacroRecording();
        ImGui::PopStyleColor();
    } else {
        ui::ActionButton(*this, "macro.record", ImVec2(0, 0), ui::Look::IconLabel);
    }
    ImGui::SameLine();
    ui::ActionButton(*this, "macro.editor", ImVec2(0, 0), ui::Look::IconLabel);
    if (packsSafeMode()) {
        ImGui::SameLine(0, 18 * s);
        ImGui::TextColored(ImColor(kAccent), "%s", tr("Safe mode (--no-packs): no pack is loaded this session"));
    }

    if (packs_.installed.empty())
        ImGui::TextDisabled("%s", tr("No packs installed. Drop a .sfpack on the window or use Install Pack..."));
    std::string toggle, remove;
    bool toggleOn = false;
    for (const InstalledPack& p : packs_.installed) {
        const PackManifest& m = p.manifest;
        ImGui::PushID(m.id.c_str());
        bool on = p.enabled;
        if (ui::Checkbox(("##on" + m.id).c_str(), &on)) {
            toggle = m.id;
            toggleOn = on;
        }
        ui::HelpText("Pack on", nullptr, "Off: its items leave the lists until you switch it on again (nothing is deleted).",
                     "Library > Packs");
        ImGui::SameLine();
        ImGui::TextUnformatted((m.name + " " + m.version).c_str());
        if (m.channel == "insider") {
            ImGui::SameLine();
            ImGui::TextColored(ImColor(kInsider), "%s", tr("Member pack"));
        }
        std::array<int, kPackItemTypes> counts{};
        for (const PackItem& it : m.contents) ++counts[size_t(it.type)];
        ImGui::SameLine();
        ImGui::TextDisabled("by %s  -  %s  -  %s", m.author.c_str(), m.license.c_str(), packCountsText(counts).c_str());
        ImGui::SameLine();
        const bool expanded = packs_.expanded == m.id;
        if (ImGui::SmallButton((std::string(expanded ? tr("Hide") : tr("Details")) + "##" + m.id).c_str()))
            packs_.expanded = expanded ? "" : m.id;
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(tr("Remove")) + "##" + m.id).c_str())) remove = m.id;
        ui::HelpText("Remove", nullptr, "Delete the pack and everything it brought. Your own files are never touched.",
                     "Library > Packs");
        if (expanded) {
            ImGui::Indent(24 * s);
            if (!m.description.empty()) ImGui::TextWrapped("%s", m.description.c_str());
            for (const PackContentItem& k : packs_.keymaps)
                if (k.packId == m.id) {
                    if (ImGui::SmallButton((std::string(tr("Apply keymap")) + " " + k.name + "##km." + m.id + "." + k.name).c_str()))
                        applyPackKeymap(k.file);
                    ImGui::SameLine();
                }
            for (const PackContentItem& k : packs_.pieMenus)
                if (k.packId == m.id) {
                    if (ImGui::SmallButton((std::string(tr("Add pie menu")) + " " + k.name + "##pie." + m.id + "." + k.name).c_str()))
                        importRadialMenu(k.file);
                    ImGui::SameLine();
                }
            for (const Macro& mac : macros_.list)
                if (mac.pack == m.name) {
                    if (ImGui::SmallButton((std::string(tr("Run")) + " " + mac.name + "##mrun." + m.id + "." + mac.name).c_str()))
                        requestMacroRun(mac);
                    ImGui::SameLine();
                }
            ImGui::NewLine();
            ImGui::TextDisabled("%s", p.dir.c_str());
            ImGui::Unindent(24 * s);
        }
        ImGui::PopID();
    }

    // The user's own macros.
    bool any = false;
    std::string deleteMacro;
    for (const Macro& mac : macros_.list) {
        if (!mac.pack.empty()) continue;
        if (!any) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", tr("Your macros"));
            any = true;
        }
        ImGui::PushID(mac.path.c_str());
        ImGui::TextUnformatted(mac.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%d commands", int(mac.statements.size()));
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(tr("Run")) + "##run" + mac.path).c_str())) requestMacroRun(mac);
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(tr("Edit")) + "##edit" + mac.path).c_str()))
            openMacroEditor(mac.name, mac.text, mac.path);
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(tr("Delete")) + "##del" + mac.path).c_str())) deleteMacro = mac.path;
        ImGui::PopID();
    }
    ImGui::EndChild();

    std::string err;
    if (!toggle.empty()) {
        if (setPackEnabled(toggle, toggleOn, err)) {
            refreshPacks();
            status_ = std::string(toggleOn ? "Turned on" : "Turned off") + " pack " + toggle;
        } else status_ = err;
    }
    if (!remove.empty()) {
        if (removePack(remove, err)) {
            if (packs_.expanded == remove) packs_.expanded.clear();
            refreshPacks();
            status_ = "Removed pack " + remove;
        } else status_ = err;
    }
    if (!deleteMacro.empty()) {
        std::error_code ec;
        fs::remove(fsPath(deleteMacro), ec);
        refreshMacroActions();
        status_ = ec ? "Could not delete " + deleteMacro : "Deleted macro " + utf8(fsPath(deleteMacro).stem());
    }
}

bool App::applyPackKeymap(const std::string& file) {
    std::ifstream f(fsPath(file), std::ios::binary);
    std::string text(128 * 1024 + 1, '\0');
    f.read(text.data(), std::streamsize(text.size()));
    text.resize(size_t(std::max<std::streamsize>(f.gcount(), 0)));
    KeymapFile k;
    std::string err;
    if (!f.is_open() && text.empty()) err = "Cannot read " + file;
    if (!err.empty() || !parseKeymapFile(text, k, err)) {
        status_ = "Keymap not applied: " + err;
        return false;
    }
    int applied = 0, skipped = 0;
    for (const auto& [id, chord] : k.shortcuts) {
        if (!actions_.find(id) || parseChords(chord).empty()) {
            ++skipped;
            continue;
        }
        actions_.assignShortcut(id, chord);
        ++applied;
    }
    status_ = "Keymap " + (k.name.empty() ? utf8(fsPath(file).stem()) : k.name) + ": " + std::to_string(applied) +
              " keys" + (skipped ? " (" + std::to_string(skipped) + " unknown left out)" : "") + " - Store Config keeps them";
    return applied > 0;
}

void App::drawPackKeymapRows() {
    if (packs_.keymaps.empty()) return;
    ImGui::TextDisabled("%s", tr("Pack keymaps"));
    for (const PackContentItem& k : packs_.keymaps) {
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(tr("Apply")) + " " + k.label() + "##" + k.file).c_str())) applyPackKeymap(k.file);
        ui::HelpText("Apply keymap", nullptr,
                     "Take the keys of this keymap from an installed pack (only actions SculptForge knows; a key another "
                     "action had moves here). Store Config keeps them.",
                     "Preferences > Input");
    }
}

// ---------------------------------------------------------------------------
// New Pack
// ---------------------------------------------------------------------------

void App::openNewPack() {
    packs_.candidates.clear();
    packs_.newError.clear();
    auto add = [&](PackItemType t, std::string label, std::string file, std::string name = {}, std::string data = {}) {
        NewPackItem it;
        it.type = t;
        it.label = std::move(label);
        it.file = std::move(file);
        it.name = std::move(name);
        it.data.assign(data.begin(), data.end());
        packs_.candidates.push_back(std::move(it));
    };
    for (const BrushRecipe& b : doc_.brushes)
        if (isUserLibraryBrush(b) && !b.origin.path.empty()) add(PackItemType::Brush, b.name, b.origin.path);
    auto images = [](const std::string& n) { return packItemExtensionOk(PackItemType::Alpha, n); };
    if (!library_.user.empty())
        for (const std::string& f : filesIn(library_.user + "/Alphas", images))
            add(PackItemType::Alpha, utf8(fsPath(f).stem()), f);
    for (const std::string& f : filesIn(matcapFolder(), images)) add(PackItemType::Matcap, utf8(fsPath(f).stem()), f);
    if (hasTexture(doc_.mesh)) {
        const std::string name = doc_.activeSubtoolInfo().name + " Material";
        const std::string text = materialRecipeText(*doc_.mesh.texture, name);
        MaterialRecipe probe;
        std::string err;
        if (parseMaterialRecipe(text, probe, err) && probe.layers.size() > 0)
            add(PackItemType::Material, name + " (the Part's Fill layers)", "", name + kMaterialRecipeExtension, text);
    }
    for (const auto& v : doc_.vdms)
        if (v && !v->builtin && !v->path.empty() && packItemExtensionOk(PackItemType::Vdm, v->path))
            add(PackItemType::Vdm, v->name, v->path);
    for (const RadialMenu& menu : radialMenus_)
        if (!menu.slots.empty()) {
            const std::string label = menu.name.empty() ? "Pie menu" : menu.name;
            const std::string file = brushFileName(label); // safe on every OS, then .json instead of .sfbrush
            add(PackItemType::PieMenu, label, "", file.substr(0, file.size() - std::strlen(".sfbrush")) + ".json",
                radialMenuFileText(menu));
        }
    {
        KeymapFile k;
        k.name = "My shortcuts";
        const Json overrides = actions_.overridesToJson(); // (kept alive for the loop)
        for (const auto& [id, chord] : overrides.members())
            if (chord.isString() && !chord.asString().empty() && id.rfind("macro.run.", 0) != 0)
                k.shortcuts.emplace_back(id, chord.asString());
        if (!k.shortcuts.empty())
            add(PackItemType::Keymap, "My shortcuts (" + std::to_string(k.shortcuts.size()) + " keys)", "",
                "My shortcuts.json", keymapFileText(k));
    }
    for (const Macro& m : macros_.list)
        if (m.pack.empty() && !m.path.empty()) add(PackItemType::Macro, m.name, m.path);
    packs_.newOpen = true;
}

void App::writeNewPack(const std::string& path) {
    PackManifest m;
    m.id = packs_.id;
    m.name = packs_.name;
    m.version = packs_.version;
    m.author = packs_.author;
    m.license = packs_.license;
    m.description = packs_.description;
    m.channel = packs_.insider ? "insider" : "";
    std::vector<PackSource> items;
    for (const NewPackItem& it : packs_.candidates)
        if (it.on) items.push_back({it.type, it.file, it.name, it.data});
    const std::string license = m.name + " by " + m.author + " is shared under " + m.license + ".\n";
    std::string err;
    if (writePack(path, m, items, license, "", err, &commands_)) {
        packs_.newError.clear();
        packs_.newOpen = false;
        status_ = "Wrote " + path + " (" + std::to_string(items.size()) + " items)";
    } else {
        packs_.newError = err;
        status_ = "Pack not written: " + err;
    }
}

void App::drawNewPack() {
    if (!packs_.newOpen) return;
    const float s = uiScale_;
    centerNextWindow(560 * s, 560 * s);
    bool open = true;
    if (!ImGui::Begin(tr("New Pack"), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        packs_.newOpen = open;
        return;
    }
    auto field = [&](const char* label, char* buf, size_t size, const char* help) {
        ImGui::SetNextItemWidth(-110 * s);
        ImGui::InputText(label, buf, size);
        ui::HelpText(label, nullptr, help, "Library > Packs > New Pack");
    };
    field("Name##newpack", packs_.name, sizeof packs_.name, "The pack's name, shown next to every item it brings.");
    if (!packs_.id[0] && packs_.name[0]) {
        std::string id;
        for (unsigned char c : std::string(packs_.name))
            id += std::isalnum(c) ? char(std::tolower(c)) : (id.empty() || id.back() == '-' ? '\0' : '-');
        id.erase(std::remove(id.begin(), id.end(), '\0'), id.end());
        while (!id.empty() && id.back() == '-') id.pop_back();
        std::snprintf(packs_.id, sizeof packs_.id, "%s", id.substr(0, 64).c_str());
    }
    field("Id##newpack", packs_.id, sizeof packs_.id,
          "Its folder name: lower-case letters, digits, '.', '_' or '-'. A newer version keeps the same id.");
    field("Version##newpack", packs_.version, sizeof packs_.version, "1.0, 1.1, 2.0 ...");
    field("Author##newpack", packs_.author, sizeof packs_.author, "Who made it (shown when it's installed).");
    field("License##newpack", packs_.license, sizeof packs_.license, "For example CC-BY-4.0, CC0-1.0 or your own terms.");
    ImGui::SetNextItemWidth(-110 * s);
    ImGui::InputTextMultiline("Description##newpack", packs_.description, sizeof packs_.description,
                              ImVec2(-110 * s, ImGui::GetTextLineHeight() * 3.0f));
    ui::Checkbox("Member pack (insider channel)##newpack", &packs_.insider);
    ui::HelpText("Member pack", nullptr, "Marks the pack as the monthly pack or an Insider drop. Display only: anyone can install it.",
                 "Library > Packs > New Pack");
    ImGui::Separator();
    const float listH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.6f;
    ImGui::BeginChild("##newpackitems", ImVec2(0, std::max(80.0f, listH)), ImGuiChildFlags_Borders);
    if (packs_.candidates.empty())
        ImGui::TextDisabled("%s", tr("Nothing of your own yet: save brushes, put alphas and Lit Spheres in your "
                                     "Library folders, record a macro..."));
    int lastType = -1;
    for (size_t i = 0; i < packs_.candidates.size(); ++i) {
        NewPackItem& it = packs_.candidates[i];
        if (int(it.type) != lastType) {
            lastType = int(it.type);
            std::string group = packItemLabel(it.type, 2);
            group[0] = char(std::toupper(static_cast<unsigned char>(group[0])));
            ImGui::TextDisabled("%s", group.c_str());
        }
        ui::Checkbox((it.label + "##npi." + packItemTypeId(it.type) + "." + it.label).c_str(), &it.on);
    }
    ImGui::EndChild();
    int chosen = 0;
    for (const NewPackItem& it : packs_.candidates) chosen += it.on ? 1 : 0;
    if (!packs_.newError.empty()) ImGui::TextColored(ImColor(IM_COL32(240, 110, 100, 255)), "%s", packs_.newError.c_str());
    ImGui::BeginDisabled(chosen == 0 || !packs_.name[0] || !packs_.author[0]);
    if (ImGui::Button(tr("Save Pack...##newpack"), ImVec2(140 * s, 0))) openPackDialog(kPackDialogSave);
    ImGui::EndDisabled();
    ui::HelpText("Save Pack", nullptr, "Write the .sfpack, checked exactly as an install would check it.",
                 "Library > Packs > New Pack");
    ImGui::SameLine();
    ImGui::TextDisabled("%d items", chosen);
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel##newpack"), ImVec2(100 * s, 0))) open = false;
    ImGui::End();
    if (!open) packs_.newOpen = false;
}

// ---------------------------------------------------------------------------
// Macros: confirm, run, record, edit
// ---------------------------------------------------------------------------

void App::requestMacroRun(const Macro& m) {
    if (macros_.waiting) {
        status_ = "A macro is waiting for a file: pick it or cancel first";
        return;
    }
    if (macroConfirmed(m.text)) {
        startMacroRun(m);
        return;
    }
    macros_.confirm = m;
    macros_.confirmOpen = true;
}

void App::startMacroRun(Macro m) {
    macros_.running = std::move(m);
    macros_.asks = macroAsks(macros_.running);
    macros_.answers.clear();
    if (macros_.asks.empty()) {
        runMacroNow();
        return;
    }
    macros_.waiting = true;
    status_ = macros_.running.name + ": pick a file for " + macros_.asks[0].command;
    openPackDialog(kPackDialogMacro, int(macros_.asks[0].kind));
}

void App::runMacroNow() {
    macros_.waiting = false;
    MacroRunContext ctx;
    ctx.projectDir = macroProjectDir();
    size_t next = 0;
    ctx.askPath = [&](const std::string&, const std::string&, MacroPathKind) {
        return next < macros_.answers.size() ? macros_.answers[next++] : std::string();
    };
    ctx.execute = [this](const std::string& command, const Args& args) {
        runCommand(command, args);
        return CommandResult{lastCommandOk_, status_};
    };
    ++macros_.runDepth;
    const MacroRunResult r = runMacro(doc_, commands_, macros_.running, ctx);
    --macros_.runDepth;
    macros_.ran = r.ran;
    macros_.lastOk = r.ok;
    macros_.lastOneStep = r.oneUndoStep;
    lastCommandOk_ = r.ok;
    status_ = r.message;
    if (!r.ok) SDL_Log("%s", r.message.c_str());
    if (r.oneUndoStep) clearLods();
    updateTitle();
}

void App::drawMacroConfirm() {
    if (!macros_.confirmOpen) return;
    const float s = uiScale_;
    centerNextWindow(560 * s, 460 * s);
    bool open = true;
    if (!ImGui::Begin(tr("Run Macro?"), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        macros_.confirmOpen = open;
        return;
    }
    const Macro& m = macros_.confirm;
    if (fontBold_) ImGui::PushFont(fontBold_);
    ImGui::TextUnformatted(m.name.c_str());
    if (fontBold_) ImGui::PopFont();
    ImGui::TextDisabled("%s", m.pack.empty() ? tr("Your macro") : ("From the pack " + m.pack).c_str());
    if (!m.description.empty()) ImGui::TextWrapped("%s", m.description.c_str());
    ImGui::TextWrapped("%s", ("It runs these " + std::to_string(m.statements.size()) +
                              " SculptForge commands, in order (it can't do anything else):").c_str());
    ImGui::BeginChild("##macrolines", ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.4f),
                      ImGuiChildFlags_Borders);
    ImGui::PushFont(consoleFont());
    for (const ScriptStatement& st : m.statements)
        ImGui::Text("%3d  %s", st.line, formatStatement(st.command, st.args).c_str());
    ImGui::PopFont();
    ImGui::EndChild();
    const size_t asks = macroAsks(m).size();
    ImGui::TextDisabled("%s", asks ? (std::to_string(asks) + " file(s) are asked for when it runs (\"?\"); other files "
                                                             "stay inside the project folder.").c_str()
                                   : tr("Files it uses stay inside the project folder."));
    bool run = false;
    if (ImGui::Button(tr("Run##macroconfirm"), ImVec2(120 * s, 0))) run = true;
    ui::HelpText("Run", nullptr, "Run it now. You won't be asked again for this macro unless it changes.", "Macros");
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel##macroconfirm"), ImVec2(120 * s, 0))) open = false;
    ImGui::End();
    if (run) {
        setMacroConfirmed(m.text);
        macros_.confirmOpen = false;
        startMacroRun(m);
    } else if (!open) {
        macros_.confirmOpen = false;
        status_ = "Macro " + m.name + " not run";
    }
}

void App::toggleMacroRecording() {
    if (macros_.recorder.active()) {
        macros_.recorder.stop();
        const auto& lines = macros_.recorder.lines();
        const int skipped = macros_.recorder.skipped();
        status_ = "Recorded " + std::to_string(lines.size()) + " commands" +
                  (skipped ? " (" + std::to_string(skipped) + " left out: macros can't run them)" : "") +
                  " - name it and Save";
        openMacroEditor("Recorded Macro", macroFileText("Recorded Macro", "", lines), "");
    } else {
        macros_.recorder.start();
        status_ = "Recording a macro: the commands you run are kept until you press Stop";
    }
}

void App::openMacroEditor(const std::string& name, const std::string& text, const std::string& path) {
    std::snprintf(macros_.name, sizeof macros_.name, "%s", name.c_str());
    std::fill(macros_.text.begin(), macros_.text.end(), '\0');
    std::memcpy(macros_.text.data(), text.data(), std::min(text.size(), macros_.text.size() - 1));
    macros_.path = path;
    macros_.checkedText = "\x01";
    macros_.editorOpen = true;
}

void App::drawMacroEditor() {
    if (!macros_.editorOpen) return;
    const float s = uiScale_;
    centerNextWindow(600 * s, 480 * s);
    bool open = true;
    if (!ImGui::Begin(tr("Macro Editor"), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        macros_.editorOpen = open;
        return;
    }
    ImGui::SetNextItemWidth(-80 * s);
    ImGui::InputText("Name##macroname", macros_.name, sizeof macros_.name);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", tr("One command per line, as in the console (# starts a comment). Files: a path inside "
                                "the project folder, or ? to pick one when it runs."));
    ImGui::PopStyleColor();
    ImGui::PushFont(consoleFont());
    ImGui::InputTextMultiline("##macrotext", macros_.text.data(), macros_.text.size(),
                              ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.4f),
                              ImGuiInputTextFlags_AllowTabInput);
    ImGui::PopFont();
    const std::string text(macros_.text.data());
    if (text != macros_.checkedText) {
        macros_.checkedText = text;
        Macro m;
        std::string err;
        macros_.checkOk = parseMacro(text, &commands_, m, err, macros_.name);
        macros_.check = macros_.checkOk ? std::to_string(m.statements.size()) + " commands, all allowed" : err;
    }
    ImGui::TextColored(macros_.checkOk ? ImColor(IM_COL32(120, 200, 120, 255)) : ImColor(IM_COL32(240, 110, 100, 255)),
                       "%s", macros_.check.c_str());
    ImGui::BeginDisabled(!macros_.checkOk || !macros_.name[0]);
    if (ImGui::Button(tr("Save##macroeditor"), ImVec2(100 * s, 0))) {
        const std::string dir = userMacroDir();
        std::error_code ec;
        fs::create_directories(fsPath(dir), ec);
        // Keep the header's name in step with the Name field.
        std::string body = text;
        while (body.rfind("# name:", 0) == 0) body = body.substr(std::min(body.size(), body.find('\n') + 1));
        const std::string out = "# name: " + std::string(macros_.name) + "\n" + body;
        const std::string path = utf8(fsPath(dir) / fsPath(macroFileName(macros_.name)));
        std::ofstream f(fsPath(path), std::ios::binary | std::ios::trunc);
        f << out;
        f.close(); // (written out before the macro list reads it back)
        if (f) {
            if (!macros_.path.empty() && macros_.path != path) fs::remove(fsPath(macros_.path), ec); // renamed
            macros_.path = path;
            refreshMacroActions();
            status_ = "Saved macro " + std::string(macros_.name) + " (F3 finds it; keys and pie menus can run it)";
        } else status_ = "Could not write " + path;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Run##macroeditor"), ImVec2(100 * s, 0))) {
        Macro m;
        std::string err;
        if (parseMacro(text, &commands_, m, err, macros_.name)) requestMacroRun(m);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Close##macroeditor"), ImVec2(100 * s, 0))) open = false;
    ImGui::End();
    if (!open) macros_.editorOpen = false;
}

void App::drawPacksUi() {
    drawPackReview();
    drawNewPack();
    drawMacroConfirm();
    drawMacroEditor();
    if (macros_.recorder.active()) { // a small red reminder at the top of the canvas while recording
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const std::string t = std::string(ICON_LC_CIRCLE_DOT "  ") + tr("Recording macro") + " (" +
                              std::to_string(macros_.recorder.lines().size()) + ")";
        const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
        const ImVec2 p(vp->WorkPos.x + (vp->WorkSize.x - ts.x) * 0.5f, vp->WorkPos.y + 40.0f * uiScale_);
        dl->AddRectFilled(ImVec2(p.x - 8, p.y - 4), ImVec2(p.x + ts.x + 8, p.y + ts.y + 4), IM_COL32(40, 14, 14, 230), 4.0f);
        dl->AddText(p, IM_COL32(240, 90, 80, 255), t.c_str());
    }
}

double App::packsTestValue(const std::string& name) const {
    if (name == "packs_installed") return double(packs_.installed.size());
    if (name == "packs_enabled")
        return double(std::count_if(packs_.installed.begin(), packs_.installed.end(), [](const InstalledPack& p) { return p.enabled; }));
    if (name == "packs_safe_mode") return packsSafeMode() ? 1.0 : 0.0;
    if (name == "pack_review_open") return packs_.reviewOpen ? 1.0 : 0.0;
    if (name == "pack_review_ok") return packs_.review ? 1.0 : 0.0;
    if (name.rfind("pack_review_", 0) == 0) { // pack_review_brush, pack_review_macro ...: the review's counts
        PackItemType t;
        if (packItemTypeFromId(name.substr(12), t)) return packs_.review ? double(packs_.review->counts[size_t(t)]) : -1.0;
    }
    if (name == "pack_alphas") return double(packs_.alphas.size());
    if (name == "pack_materials") return double(packs_.materials.size());
    if (name == "pack_matcaps") return double(packs_.matcaps.size());
    if (name == "pack_keymaps") return double(packs_.keymaps.size());
    if (name == "pack_brushes")
        return double(std::count_if(doc_.brushes.begin(), doc_.brushes.end(),
                                    [](const BrushRecipe& b) { return b.origin.source == BrushSource::Pack; }));
    if (name == "new_pack_open") return packs_.newOpen ? 1.0 : 0.0;
    if (name == "new_pack_items") return double(packs_.candidates.size());
    if (name == "macro_recording") return macros_.recorder.active() ? 1.0 : 0.0;
    if (name == "macro_recorded") return double(macros_.recorder.lines().size());
    if (name == "macro_confirm_open") return macros_.confirmOpen ? 1.0 : 0.0;
    if (name == "macro_editor_open") return macros_.editorOpen ? 1.0 : 0.0;
    if (name == "macro_editor_ok") return macros_.checkOk ? 1.0 : 0.0;
    if (name == "macro_waiting") return macros_.waiting ? 1.0 : 0.0;
    if (name == "macros") return double(macros_.list.size());
    if (name == "macro_ran") return double(macros_.ran);
    if (name == "macro_ok") return macros_.lastOk ? 1.0 : 0.0;
    if (name == "macro_one_step") return macros_.lastOneStep ? 1.0 : 0.0;
    if (name == "macro_actions")
        return double(std::count_if(actions_.all().begin(), actions_.all().end(),
                                    [](const Action& a) { return a.id.rfind("macro.run.", 0) == 0; }));
    if (name == "undo_steps") return double(doc_.undo.undoCount());
    return std::nan("");
}

} // namespace sf
