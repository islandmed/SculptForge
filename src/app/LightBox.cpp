// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/LightBox.h"

#include "app/App.h"
#include "app/Pickers.h"
#include "app/Ui.h"
#include "core/Matcap.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "render/GL.h"

#include <IconsLucide.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>

namespace sf {

namespace {

namespace fs = std::filesystem;

constexpr float kTile = 96.0f;   // thumbnail size (logical px)
constexpr float kLabel = 22.0f;  // label under it
constexpr float kGap = 8.0f;     // between tiles
constexpr float kHeader = 28.0f; // tab row
constexpr ImU32 kAccent = IM_COL32(240, 136, 46, 255);
constexpr ImU32 kStripBg = IM_COL32(20, 20, 22, 250); // #141416 at 98%
constexpr ImU32 kHeaderLine = IM_COL32(46, 46, 50, 255);

fs::path fsPath(const std::string& p) { return fs::path(std::u8string(p.begin(), p.end())); }
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string lower(std::string s) {
    for (auto& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool isUnder(const std::string& path, const std::string& root) {
    if (root.empty() || path.size() < root.size() || path.compare(0, root.size(), root) != 0) return false;
    return path.size() == root.size() || path[root.size()] == '/' || path[root.size()] == '\\';
}

const char* glyphFor(AssetKind k) {
    switch (k) {
    case AssetKind::Folder:
        return ICON_LC_FOLDER;
    case AssetKind::Up:
        return ICON_LC_FOLDER_UP;
    case AssetKind::Project:
    case AssetKind::Mesh:
    case AssetKind::Primitive:
        return ICON_LC_BOX;
    case AssetKind::BrushType:
    case AssetKind::Recipe:
    case AssetKind::BrushFile:
        return ICON_LC_BRUSH;
    case AssetKind::Alpha:
    case AssetKind::AlphaFile:
        return ICON_LC_CIRCLE_DASHED;
    case AssetKind::Material:
        return ICON_LC_SUN;
    case AssetKind::Image:
        return ICON_LC_IMAGE;
    case AssetKind::Splat:
        return ICON_LC_SPARKLES;
    }
    return ICON_LC_BOX;
}

std::string sizeText(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ull * 1024ull)
        std::snprintf(buf, sizeof buf, "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    else
        std::snprintf(buf, sizeof buf, "%.0f KB", std::max(1.0, double(bytes) / 1024.0));
    return buf;
}
std::string dateText(int64_t t) {
    const std::time_t tt = std::time_t(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

const BrushAlpha& builtinAlpha(int i) {
    static const BrushAlpha alphas[] = {BrushAlpha::round(), BrushAlpha::square(), BrushAlpha::noise()};
    return alphas[std::clamp(i, 0, 2)];
}
const char* const kAlphaNames[] = {"Round", "Square", "Noise"};

void SDLCALL onFolderChosen(void* userdata, const char* const* files, int) {
    if (files && files[0]) static_cast<LightBox*>(userdata)->folderChosen(files[0]);
}

} // namespace

float LightBox::height(int rows, float scale) {
    return (kHeader + float(std::clamp(rows, 1, 4)) * (kTile + kLabel + 4.0f) + 12.0f) * scale;
}

void LightBox::show(LibraryTab tab) {
    open_ = true;
    if (place_.tab != tab || !place_.folder.empty()) {
        back_.push_back(place_);
        forward_.clear();
        place_ = {tab, ""};
        scrollTo_ = 0.0f;
    }
}

void LightBox::folderChosen(const std::string& path) {
    std::lock_guard lock(folderMutex_);
    chosenFolder_ = path;
}

void LightBox::navigate(App&, Place to) {
    if (to == place_) return;
    back_.push_back(place_);
    if (back_.size() > 64) back_.erase(back_.begin());
    forward_.clear();
    place_ = std::move(to);
    filter_[0] = 0;
    selected_.clear();
    scrollTo_ = 0.0f;
}

// ---------------------------------------------------------------------------
// What a tab shows
// ---------------------------------------------------------------------------

void LightBox::rootEntries(App& app, std::vector<AssetEntry>& out) {
    const LibraryTab tab = place_.tab;
    const LibraryRoots& roots = app.library_;
    auto folderTile = [&](const std::string& name, const std::string& path) {
        AssetEntry e;
        e.kind = AssetKind::Folder;
        e.name = name;
        e.path = path;
        out.push_back(std::move(e));
    };
    auto scan = [&](const std::string& dir, bool newestFirst) -> std::vector<AssetEntry> {
        if (dir.empty()) return {};
        const auto* list = app.libraryScanner_.get(
            dir, libraryTabId(tab), [tab](const std::string& p) { return fileKindForTab(tab, p); }, newestFirst);
        std::vector<AssetEntry> files;
        if (list)
            for (const auto& e : *list)
                if (e.kind != AssetKind::Folder) files.push_back(e);
        return files;
    };
    const std::string userDir =
        roots.user.empty() || !*libraryTabFolder(tab) ? std::string() : roots.user + "/" + libraryTabFolder(tab);
    const std::string builtinDir = roots.builtin.empty() ? std::string() : roots.builtin + "/demo";
    // The Built-in and User folders (ZBrush: Maxon and User), and folders the artist added.
    const bool folders = tab == LibraryTab::Project || tab == LibraryTab::Tool || tab == LibraryTab::Alpha ||
                         tab == LibraryTab::Image || tab == LibraryTab::Splats || tab == LibraryTab::Brush;
    if (folders) {
        const bool builtinHasFiles = !builtinDir.empty() && (tab == LibraryTab::Tool || tab == LibraryTab::Image);
        if (builtinHasFiles) folderTile("Built-in", builtinDir);
        if (!userDir.empty()) folderTile("User", userDir);
        for (const std::string& r : extraRoots_)
            folderTile(utf8(fsPath(r).filename()), r);
    }
    switch (tab) {
    case LibraryTab::Project: {
        std::vector<AssetEntry> files = scan(userDir, true);
        for (const std::string& p : app.recentFiles_)
            if (isProjectFile(p) &&
                std::none_of(files.begin(), files.end(), [&](const AssetEntry& e) { return e.path == p; })) {
                AssetEntry e;
                e.kind = AssetKind::Project;
                e.name = utf8(fsPath(p).stem());
                e.path = p;
                files.push_back(std::move(e));
            }
        out.insert(out.end(), files.begin(), files.end());
        break;
    }
    case LibraryTab::Tool: {
        for (int i = 0; i < int(Primitive::Count); ++i) {
            AssetEntry e;
            e.kind = AssetKind::Primitive;
            e.index = i;
            e.name = primitiveName(Primitive(i));
            out.push_back(std::move(e));
        }
        const auto files = scan(userDir, false);
        out.insert(out.end(), files.begin(), files.end());
        break;
    }
    case LibraryTab::Brush:
        for (const BrushItem& b : PickerUi::brushItems(app, true)) {
            AssetEntry e;
            e.name = b.name;
            e.path = b.path;
            switch (b.kind) {
            case BrushItem::Kind::Type:
                e.kind = AssetKind::BrushType, e.index = b.index;
                break;
            case BrushItem::Kind::Paint:
                e.kind = AssetKind::BrushType, e.index = kPaintBrush;
                break;
            case BrushItem::Kind::Recipe:
                e.kind = AssetKind::Recipe, e.index = b.index;
                break;
            case BrushItem::Kind::File:
                e.kind = AssetKind::BrushFile;
                break;
            case BrushItem::Kind::Guides: // a tool, not a brush file: only in the pickers
                continue;
            }
            out.push_back(std::move(e));
        }
        break;
    case LibraryTab::Alpha: {
        for (int i = 0; i < 3; ++i) {
            AssetEntry e;
            e.kind = AssetKind::Alpha;
            e.index = i;
            e.name = kAlphaNames[i];
            out.push_back(std::move(e));
        }
        const auto files = scan(userDir, false);
        out.insert(out.end(), files.begin(), files.end());
        break;
    }
    case LibraryTab::Material:
        for (int k = 0; k <= matcapCount(); ++k) { // the MatCaps (the default first), then Studio
            const int i = k < matcapCount() ? matcapInListOrder(k) : -1;
            AssetEntry e;
            e.kind = AssetKind::Material;
            e.index = i;
            e.name = i < 0 ? "Studio" : matcapName(i);
            out.push_back(std::move(e));
        }
        break;
    case LibraryTab::Image:
    case LibraryTab::Splats: {
        const auto files = scan(userDir, false);
        out.insert(out.end(), files.begin(), files.end());
        break;
    }
    case LibraryTab::QuickSave: {
        // Autosave first (it is the newest by nature), then QuickSaves newest first.
        if (!app.prefDir_.empty()) {
            const std::string autosave = app.prefDir_ + "autosave.sfproj";
            std::error_code ec;
            if (fs::is_regular_file(fsPath(autosave), ec)) {
                AssetEntry e;
                e.kind = AssetKind::Project;
                e.name = "Autosave";
                e.path = autosave;
                e.mtime = modifiedTime(autosave);
                out.push_back(std::move(e));
            }
        }
        // "QuickSave 2026-10-21 23-15-04" reads better as its time: "Oct 21  23:15:04".
        for (AssetEntry e : scan(userDir, true)) {
            if (e.name.rfind("QuickSave ", 0) == 0 && e.mtime) {
                const std::time_t t = std::time_t(e.mtime);
                std::tm tm{};
#if defined(_WIN32)
                localtime_s(&tm, &t);
#else
                localtime_r(&t, &tm);
#endif
                char buf[32];
                std::strftime(buf, sizeof buf, "%b %d  %H:%M:%S", &tm);
                e.name = buf;
            }
            out.push_back(std::move(e));
        }
        break;
    }
    case LibraryTab::Recent:
        for (const std::string& p : app.recentFiles_) {
            AssetEntry e;
            e.kind = fileKindForTab(LibraryTab::Recent, p);
            if (e.kind == AssetKind::Folder) continue;
            e.name = utf8(fsPath(p).filename());
            e.path = p;
            out.push_back(std::move(e));
        }
        break;
    default:
        break;
    }
}

std::vector<AssetEntry> LightBox::entries(App& app) {
    std::vector<AssetEntry> out;
    if (place_.folder.empty()) {
        rootEntries(app, out);
    } else {
        AssetEntry up;
        up.kind = AssetKind::Up;
        up.name = "..";
        out.push_back(up);
        const LibraryTab tab = place_.tab;
        if (const auto* list = app.libraryScanner_.get(
                place_.folder, libraryTabId(tab), [tab](const std::string& p) { return fileKindForTab(tab, p); },
                tab == LibraryTab::QuickSave || tab == LibraryTab::Project))
            out.insert(out.end(), list->begin(), list->end());
    }
    if (filter_[0]) {
        const std::string f = lower(filter_);
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](const AssetEntry& e) {
                                     return e.kind != AssetKind::Up && lower(e.name).find(f) == std::string::npos;
                                 }),
                  out.end());
    }
    return out;
}

namespace {
bool isActiveBrushType(const App& app, int index) {
    BrushItem b;
    b.kind = index == kPaintBrush ? BrushItem::Kind::Paint : BrushItem::Kind::Type;
    b.index = index;
    return PickerUi::isActive(app, b);
}
} // namespace

ImTextureID LightBox::thumbnail(App& app, const AssetEntry& e) {
    ThumbnailCache& t = app.thumbs_;
    switch (e.kind) {
    case AssetKind::Project:
    case AssetKind::Mesh:
    case AssetKind::Image:
    case AssetKind::AlphaFile:
        return t.file(e.path);
    case AssetKind::Primitive:
        return t.primitive(e.index);
    case AssetKind::BrushType:
        return t.brush(e.index);
    case AssetKind::Recipe:
        return e.index >= 0 && e.index < int(app.doc_.brushes.size()) ? t.recipe(app.doc_.brushes[size_t(e.index)])
                                                                      : ImTextureID(0);
    case AssetKind::Alpha:
        return t.alpha(builtinAlpha(e.index));
    case AssetKind::Material:
        return t.material(e.index);
    default:
        return ImTextureID(0);
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void LightBox::activate(App& app, const AssetEntry& e, bool append) {
    switch (e.kind) {
    case AssetKind::Up: {
        const std::string parent = utf8(fsPath(place_.folder).parent_path());
        // Leaving the folder a tab started in goes back to the tab's root.
        const LibraryRoots& r = app.library_;
        const std::string tabUser = r.user.empty() ? std::string() : r.user + "/" + libraryTabFolder(place_.tab);
        const bool atTop = place_.folder == tabUser || place_.folder == r.builtin + "/demo" ||
                           std::find(extraRoots_.begin(), extraRoots_.end(), place_.folder) != extraRoots_.end();
        navigate(app, {place_.tab, atTop ? std::string() : parent});
        return;
    }
    case AssetKind::Folder:
        navigate(app, {place_.tab, e.path});
        return;
    case AssetKind::Primitive: {
        if (append) {
            app.runCommand("subtool_append", {{"type", lower(primitiveName(Primitive(e.index)))}});
            return;
        }
        const Primitive p = Primitive(e.index);
        app.confirmDiscard([&app, p] {
            if (p == Primitive::Sphere)
                app.runCommand("new_sphere");
            else if (p == Primitive::Plane)
                app.runCommand("new_plane");
            else {
                app.runCommand("new_cube");
                if (app.lastCommandOk_) app.camera_.frame(app.sceneBounds());
            }
        });
        return;
    }
    case AssetKind::Project:
    case AssetKind::Mesh:
        if (append)
            app.runCommand("subtool_append", {{"path", e.path}});
        else
            app.openPathConfirmed(e.path);
        return;
    case AssetKind::BrushType: {
        BrushItem b;
        b.kind = e.index == kPaintBrush ? BrushItem::Kind::Paint : BrushItem::Kind::Type;
        b.index = e.index;
        b.name = e.name;
        PickerUi::select(app, b);
        return;
    }
    case AssetKind::Recipe: {
        BrushItem b;
        b.kind = BrushItem::Kind::Recipe;
        b.index = e.index;
        b.name = e.name;
        PickerUi::select(app, b);
        return;
    }
    case AssetKind::BrushFile:
        app.runCommand("brush_load", {{"path", e.path}});
        return;
    case AssetKind::Alpha:
        app.runCommand("brush_set_alpha", {{"name", lower(kAlphaNames[std::clamp(e.index, 0, 2)])}});
        return;
    case AssetKind::AlphaFile:
        app.runCommand("brush_set_alpha", {{"path", e.path}, {"invert", app.alphaInvert_}});
        return;
    case AssetKind::Material:
        app.runCommand("set_material", {{"name", std::string(e.index < 0 ? "studio" : matcapName(e.index))}});
        return;
    case AssetKind::Image:
        imageMenu_ = e.path;
        return; // what to do with it: a small menu
    case AssetKind::Splat:
        app.runCommand("open", {{"path", e.path}});
        return;
    }
}

void LightBox::contextMenu(App& app, const AssetEntry& e) {
    ImGui::TextDisabled("%s", e.name.c_str());
    ImGui::Separator();
    const bool mesh = e.kind == AssetKind::Mesh || e.kind == AssetKind::Project || e.kind == AssetKind::Primitive;
    if (ImGui::MenuItem(e.kind == AssetKind::Folder || e.kind == AssetKind::Up ? "Open folder" : "Open"))
        activate(app, e, false);
    if (mesh && ImGui::MenuItem("Append as SubTool", "Shift+double-click")) activate(app, e, true);
    if (e.kind == AssetKind::Image) {
        if (ImGui::MenuItem("Generate 3D...")) app.openPath(e.path);
        if (ImGui::MenuItem("Relief from image"))
            app.confirmDiscard([&app, p = e.path] { app.runCommand("image_relief", {{"path", p}}); });
        if (ImGui::MenuItem("Inflate image"))
            app.confirmDiscard([&app, p = e.path] { app.runCommand("image_inflate", {{"path", p}}); });
        if (ImGui::MenuItem("Project onto model")) app.runCommand("image_project", {{"path", e.path}});
        if (ImGui::MenuItem("Use as alpha"))
            app.runCommand("brush_set_alpha", {{"path", e.path}, {"invert", app.alphaInvert_}});
    }
    if (!e.path.empty() && e.kind != AssetKind::Up) {
        ImGui::Separator();
        if (ImGui::MenuItem("Show in file manager")) {
            const std::string dir = e.kind == AssetKind::Folder ? e.path : utf8(fsPath(e.path).parent_path());
            if (!SDL_OpenURL(("file://" + dir).c_str())) app.status_ = "Couldn't open " + dir + ": " + SDL_GetError();
        }
        if (e.kind != AssetKind::Folder && ImGui::MenuItem("Refresh thumbnail")) app.thumbs_.refresh(e.path);
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

std::string LightBox::folderLabel(const App& app, const std::string& folder) const {
    const LibraryRoots& r = app.library_;
    if (!r.builtin.empty() && isUnder(folder, r.builtin)) return "Built-in";
    if (!r.user.empty() && isUnder(folder, r.user)) return "User";
    return utf8(fsPath(folder).filename());
}

void LightBox::drawBreadcrumb(App& app) {
    // Tab > Built-in/User > sub > sub: each crumb goes back there.
    std::vector<std::pair<std::string, Place>> crumbs;
    crumbs.push_back({libraryTabName(place_.tab), {place_.tab, ""}});
    if (!place_.folder.empty()) {
        const LibraryRoots& r = app.library_;
        std::string top;
        const std::string tabUser = r.user.empty() ? std::string() : r.user + "/" + libraryTabFolder(place_.tab);
        if (!tabUser.empty() && isUnder(place_.folder, tabUser))
            top = tabUser;
        else if (!r.builtin.empty() && isUnder(place_.folder, r.builtin + "/demo"))
            top = r.builtin + "/demo";
        else
            for (const auto& x : extraRoots_)
                if (isUnder(place_.folder, x)) top = x;
        if (top.empty()) top = place_.folder;
        crumbs.push_back({folderLabel(app, top), {place_.tab, top}});
        const fs::path rel = fsPath(place_.folder).lexically_relative(fsPath(top));
        fs::path acc = fsPath(top);
        for (const auto& part : rel) {
            if (part == ".") continue;
            acc /= part;
            crumbs.push_back({utf8(part), {place_.tab, utf8(acc)}});
        }
    }
    for (size_t i = 0; i < crumbs.size(); ++i) {
        if (i) {
            ImGui::SameLine(0, 2);
            ImGui::TextDisabled(ICON_LC_CHEVRON_RIGHT);
            ImGui::SameLine(0, 2);
        }
        const bool last = i + 1 == crumbs.size();
        ImGui::PushID(int(i));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, last ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                  : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::SmallButton(crumbs[i].first.c_str()) && !last) navigate(app, crumbs[i].second);
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
}

void LightBox::drawHeader(App& app, float width) {
    const float scale = app.uiScale_;
    ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // Title (an item, so tests and help find "LightBox").
    {
        const char* title = ICON_LC_IMAGES "  LightBox";
        const ImVec2 ts = ImGui::CalcTextSize(title);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("LightBox", ImVec2(ts.x, ImGui::GetFrameHeight()));
        dl->AddText(ImVec2(p.x, p.y + (ImGui::GetFrameHeight() - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text),
                    title);
        ui::HelpText("LightBox", ",",
                     "Browse projects, models, brushes, alphas, materials, images and splats. Double-click to load; "
                     "Shift+double-click a model to append it as a SubTool; drag sideways to scrub.",
                     "LightBox");
    }
    ImGui::SameLine(0, 14 * scale);

    // Tabs: text buttons, the active one underlined in accent.
    for (int i = 0; i < int(LibraryTab::Count); ++i) {
        const auto tab = LibraryTab(i);
        const bool on = place_.tab == tab;
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 18));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 30));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              on ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
        if (ImGui::Button(libraryTabName(tab)) && !on) navigate(app, {tab, ""});
        ImGui::PopStyleColor(4);
        if (on) {
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            dl->AddRectFilled(ImVec2(a.x + 4, b.y - 2), ImVec2(b.x - 4, b.y), kAccent, 1.0f);
        }
        static const char* const tips[] = {
            "Your projects (.sfproj) and recently opened projects.",
            "Models to start from or append as SubTools: primitives and model files.",
            "Every brush: built-in, custom and .sfbrush files in your library.",
            "Brush alphas: built-in stamps and images in your Alphas folder.",
            "Viewport materials: Studio and the MatCaps.",
            "Reference images: generate 3D, relief, inflate, project onto the model or use as an alpha.",
            "Gaussian splat captures (.ply, .splat) to sculpt against.",
            "QuickSaves and the autosave, newest first.",
            "Recently opened files of every kind.",
        };
        ui::HelpText(libraryTabName(tab), nullptr, tips[i], "LightBox");
        ImGui::SameLine(0, 0);
    }
    ImGui::SameLine(0, 10 * scale);

    // History.
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::BeginDisabled(back_.empty());
    if (ImGui::Button(ICON_LC_CHEVRON_LEFT "###Back")) {
        forward_.push_back(place_);
        place_ = back_.back();
        back_.pop_back();
        scrollTo_ = 0.0f;
    }
    ImGui::EndDisabled();
    ui::HelpText("Back", nullptr, "The previous tab or folder.", "LightBox");
    ImGui::SameLine(0, 2);
    ImGui::BeginDisabled(forward_.empty());
    if (ImGui::Button(ICON_LC_CHEVRON_RIGHT "###Forward")) {
        back_.push_back(place_);
        place_ = forward_.back();
        forward_.pop_back();
        scrollTo_ = 0.0f;
    }
    ImGui::EndDisabled();
    ui::HelpText("Forward", nullptr, "Forward again after going back.", "LightBox");
    ImGui::PopStyleColor();
    if (!place_.folder.empty()) {
        ImGui::SameLine(0, 8 * scale);
        drawBreadcrumb(app);
    }

    // Right side: [Save now] on the QuickSave tab, filter, rows, open folder, fixes & suggestions, close. Narrow canvases
    // get icon-only buttons and a shorter filter first.
    const bool quickTab = place_.tab == LibraryTab::QuickSave;
    const float rowsW = 4 * (ImGui::GetFrameHeight() + 2.0f);
    const float closeW = ImGui::GetFrameHeight();
    const float right = origin.x + width;
    ImGui::SameLine();
    const float leftEnd = ImGui::GetCursorScreenPos().x;
    auto buttonW = [&](const char* text) {
        return ImGui::CalcTextSize(text, nullptr, true).x + style.FramePadding.x * 2;
    };
    float filterW = 160.0f * scale;
    bool compact = false, compactFeedback = false; // the feedback button keeps its words longest
    auto groupW = [&] {
        const float saveW =
            quickTab ? buttonW(compact ? ICON_LC_SAVE : ICON_LC_SAVE "  Save now") + 8.0f * scale : 0.0f;
        const float folderW = buttonW(compact ? ICON_LC_FOLDER_PLUS : ICON_LC_FOLDER_PLUS "  Open folder...") +
                              buttonW(ICON_LC_SMARTPHONE) + 8.0f * scale;
        const float feedbackW =
            buttonW(compactFeedback ? ICON_LC_MESSAGE_SQUARE_PLUS : ICON_LC_MESSAGE_SQUARE_PLUS "  Fixes & suggestions");
        return saveW + filterW + rowsW + folderW + feedbackW + closeW + 4 * 8.0f * scale;
    };
    const float room = right - leftEnd - 8.0f;
    if (room < groupW()) compact = true;
    // A shorter filter before an icon-only "Fixes & suggestions" (it is the startup screen's way to reach us).
    if (room < groupW() && filterW - (groupW() - room) < 70.0f * scale) compactFeedback = true;
    if (room < groupW()) filterW = std::max(70.0f * scale, filterW - (groupW() - room));
    ImGui::SetCursorScreenPos(ImVec2(std::max(leftEnd + 8.0f, right - groupW()), origin.y));
    if (quickTab) {
        if (ImGui::Button(compact ? ICON_LC_SAVE "###QuickSave now" : ICON_LC_SAVE "  Save now###QuickSave now"))
            app.quickSave();
        ui::HelpAction(app, "file.quicksave");
        ImGui::SameLine(0, 8 * scale);
    }
    ImGui::SetNextItemWidth(filterW);
    ImGui::InputTextWithHint("##lbfilter", ICON_LC_SEARCH "  Filter", filter_, sizeof filter_);
    ui::HelpText("Filter", nullptr, "Show only the items whose names contain this text.", "LightBox");
    ImGui::SameLine(0, 8 * scale);
    for (int r = 1; r <= 4; ++r) {
        const bool on = app.layout_.lightboxRows == r;
        char label[16];
        std::snprintf(label, sizeof label, "%d##lbrows", r);
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kAccent));
        if (ImGui::Button(label, ImVec2(ImGui::GetFrameHeight(), 0))) app.layout_.lightboxRows = r;
        if (on) ImGui::PopStyleColor();
        ui::HelpText("Rows", nullptr, "How many rows of thumbnails the LightBox shows (1-4).", "LightBox");
        ImGui::SameLine(0, 2);
    }
    ImGui::SameLine(0, 8 * scale);
    // The phone link (PanelPhone.cpp): scans, photos and splats from the phone land in the library's Captures.
    if (ImGui::Button(ICON_LC_SMARTPHONE "###From phone")) app.actions_.find("file.capture_phone")->run();
    ui::DelayedTooltip("Capture from phone");
    ui::HelpAction(app, "file.capture_phone");
    ImGui::SameLine(0, 8 * scale);
    if (ImGui::Button(compact ? ICON_LC_FOLDER_PLUS "###Open folder"
                              : ICON_LC_FOLDER_PLUS "  Open folder...###Open folder"))
        SDL_ShowOpenFolderDialog(onFolderChosen, this, app.window_, nullptr, false);
    ui::HelpText("Open folder", nullptr, "Add a folder of your own to the LightBox (shown at the root of every tab).",
                 "LightBox");
    ImGui::SameLine(0, 8 * scale);
    if (ImGui::Button(compactFeedback ? ICON_LC_MESSAGE_SQUARE_PLUS "###Fixes & suggestions"
                              : ICON_LC_MESSAGE_SQUARE_PLUS "  Fixes & suggestions###Fixes & suggestions")) {
        if (app.interactive()) app.actions_.find("help.feedback")->run();
        app.status_ = "Submit a request for a fix or a suggestion: "
                      "https://github.com/islandmed/SculptForge/issues/new/choose";
    }
    ui::HelpAction(app, "help.feedback");
    ImGui::SameLine(0, 8 * scale);
    if (ImGui::Button(ICON_LC_X "##lbclose", ImVec2(closeW, 0))) open_ = false;
    ui::HelpText("Close LightBox", ",", "Hide the LightBox (comma toggles it).", "LightBox");

    const float y = origin.y + kHeader * scale - 2.0f;
    dl->AddLine(ImVec2(origin.x - 8, y), ImVec2(right + 8, y), kHeaderLine, 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + kHeader * scale + 2.0f));
}

void LightBox::draw(App& app, const CanvasRect& canvas) {
    {
        std::lock_guard lock(folderMutex_);
        if (!chosenFolder_.empty()) {
            if (std::find(extraRoots_.begin(), extraRoots_.end(), chosenFolder_) == extraRoots_.end())
                extraRoots_.push_back(chosenFolder_);
            app.status_ = "Added " + chosenFolder_ + " to the LightBox";
            Place to{place_.tab, chosenFolder_};
            chosenFolder_.clear();
            open_ = true;
            navigate(app, to);
        }
    }
    // The layout holds what Store Config saves (open, rows, tab, placement).
    UiLayout& layout = app.layout_;
    if (!syncedLayout_) {
        syncedLayout_ = true;
        place_.tab = libraryTabFromId(layout.lightboxTab);
        open_ = open_ || layout.lightboxOpen;
    }
    layout.lightboxOpen = open_;
    layout.lightboxTab = libraryTabId(place_.tab);
    layout.lightboxRows = std::clamp(layout.lightboxRows, 1, 4);
    app.libraryScanner_.poll(ImGui::GetTime());
    if (!open_) {
        pressing_ = tileDrag_ = false;
        return;
    }

    const float scale = app.uiScale_;
    const int rows = layout.lightboxRows;
    const float h = std::min(height(rows, scale), canvas.h);
    const float y = layout.lightboxBottom ? canvas.y + canvas.h - h : canvas.y;
    ImGui::SetNextWindowPos(ImVec2(canvas.x, y));
    ImGui::SetNextWindowSize(ImVec2(canvas.w, h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kStripBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * scale, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    const bool visible =
        ImGui::Begin("LightBox", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    if (!visible) {
        ImGui::End();
        return;
    }
    ImDrawList* wdl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    // 1 px edge toward the canvas.
    const float edge = layout.lightboxBottom ? wp.y : wp.y + ws.y - 1.0f;
    wdl->AddLine(ImVec2(wp.x, edge), ImVec2(wp.x + ws.x, edge), IM_COL32(14, 14, 16, 255));

    ImGui::SetCursorPosY(std::round((kHeader * scale - ImGui::GetFrameHeight()) * 0.5f));
    drawHeader(app, ws.x - 16.0f * scale);

    const std::vector<AssetEntry> items = entries(app);
    const float tile = kTile * scale, rowH = (kTile + kLabel + 4.0f) * scale, gap = kGap * scale;
    const int columns = int((items.size() + size_t(rows) - 1) / size_t(rows));
    const float contentW = float(columns) * (tile + gap);

    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * scale);
    ImGui::SetNextWindowContentSize(ImVec2(contentW, float(rows) * rowH));
    ImGui::BeginChild("##lbstrip", ImVec2(0, float(rows) * rowH + 8.0f * scale), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    if (scrollTo_ >= 0.0f) {
        ImGui::SetScrollX(scrollTo_);
        scrollTo_ = -1.0f;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const bool stripHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    // The wheel scrolls sideways, a tile column per notch.
    if (stripHovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
        ImGui::SetScrollX(ImGui::GetScrollX() - (io.MouseWheel + io.MouseWheelH) * (tile + gap));
    // Left-drag scrubs the strip (after 4 px, so a click stays a click).
    if (stripHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        pressing_ = true;
        scrubbed_ = tileDrag_ = false;
        pressX_ = io.MousePos.x;
        pressY_ = io.MousePos.y;
        pressScroll_ = ImGui::GetScrollX();
        pressTile_ = -1;
    }
    if (pressing_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !tileDrag_) {
        if (std::abs(io.MousePos.x - pressX_) > 4.0f) scrubbed_ = true;
        if (scrubbed_) ImGui::SetScrollX(pressScroll_ - (io.MousePos.x - pressX_));
        // Pulled out of the LightBox onto the canvas: drop it there to load it.
        const float outside = layout.lightboxBottom ? wp.y - io.MousePos.y : io.MousePos.y - (wp.y + ws.y);
        if (pressTile_ >= 0 && outside > 12.0f) {
            tileDrag_ = true;
            dragEntry_ = items[size_t(pressTile_)];
            ImGui::SetScrollX(pressScroll_);
        }
    }

    const float scrollX = ImGui::GetScrollX();
    const float viewW = ImGui::GetWindowWidth();
    const ImVec2 base = ImGui::GetCursorScreenPos();
    for (size_t i = 0; i < items.size(); ++i) {
        const AssetEntry& e = items[i];
        const int col = int(i) / rows, row = int(i) % rows;
        const float x = float(col) * (tile + gap);
        if (x + tile < scrollX - tile || x > scrollX + viewW + tile) continue; // off screen: no thumbnail work
        ImGui::SetCursorScreenPos(ImVec2(base.x + x, base.y + float(row) * rowH + 4.0f * scale));
        const ImTextureID tex = thumbnail(app, e);
        const std::string key = e.path.empty() ? e.name : e.path;
        const bool selected = key == selected_ ||
                              (e.kind == AssetKind::Material && app.renderSettings_.matcap == e.index) ||
                              (e.kind == AssetKind::BrushType && isActiveBrushType(app, e.index)) ||
                              (e.kind == AssetKind::Recipe && !app.paintMode_ && app.doc_.activeBrush == e.index);
        const std::string id = e.name + "##lb" + std::to_string(i);
        const bool clicked = AssetTile(id.c_str(), tex, glyphFor(e.kind), e.name, tile, selected);
        if (ImGui::IsItemActivated()) pressTile_ = int(i);
        if (clicked && !scrubbed_) selected_ = key;
        const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (doubleClicked && !scrubbed_) {
            selected_ = key;
            activate(app, e, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
        }
        // Plain hover: the full name and path. Ctrl: the details.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !io.KeyCtrl && !pressing_ &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(e.name.c_str());
            if (!e.path.empty()) ImGui::TextDisabled("%s", e.path.c_str());
            ImGui::EndTooltip();
        }
        std::string details =
            e.kind == AssetKind::Folder || e.kind == AssetKind::Up ? "Double-click to open the folder."
            : e.kind == AssetKind::Mesh || e.kind == AssetKind::Primitive || e.kind == AssetKind::Project
                ? "Double-click to open (replaces the scene); Shift+double-click appends it as a SubTool."
            : e.kind == AssetKind::Image ? "Double-click for what to do with the image."
                                         : "Double-click to use it.";
        if (e.size) details += "\n" + sizeText(e.size) + ", modified " + dateText(e.mtime);
        ui::HelpText(e.name.c_str(), nullptr, details.c_str(), e.path.empty() ? "LightBox" : e.path.c_str());
        if (ImGui::BeginPopupContextItem()) {
            contextMenu(app, e);
            ImGui::EndPopup();
        }
    }
    // Only folders (or nothing): say how things get here.
    const bool onlyFolders = std::all_of(items.begin(), items.end(), [](const AssetEntry& e) {
        return e.kind == AssetKind::Folder || e.kind == AssetKind::Up;
    });
    if (onlyFolders) {
        const char* hint = filter_[0] ? nullptr
                           : place_.tab == LibraryTab::Project
                               ? "Nothing here yet. Projects you save (Ctrl+S) into the User folder show up here."
                           : place_.tab == LibraryTab::QuickSave
                               ? "Nothing here yet. QuickSave keeps timestamped copies of your scene here."
                           : place_.tab == LibraryTab::Recent ? "Nothing here yet. Files you open show up here."
                           : place_.tab == LibraryTab::Image
                               ? "Nothing here yet. Put reference images in the User folder."
                           : place_.tab == LibraryTab::Splats
                               ? "Nothing here yet. Put Gaussian splat captures (.ply, .splat) in the User folder."
                               : "Nothing here yet.";
        const float x =
            base.x +
            (items.empty() ? 8.0f : float((items.size() + size_t(rows) - 1) / size_t(rows)) * (tile + gap) + 12.0f);
        ImGui::SetCursorScreenPos(ImVec2(x, base.y + 4.0f * scale + tile * 0.5f - ImGui::GetTextLineHeight() * 0.5f));
        if (hint)
            ImGui::TextDisabled("%s", hint);
        else
            ImGui::TextDisabled("Nothing here matches \"%s\".", filter_);
    }
    AssetTileHoverGlow();
    ImGui::EndChild();

    // Image tiles: what to do with the picture.
    if (!imageMenu_.empty()) {
        ImGui::OpenPopup("##lbimage");
        if (ImGui::BeginPopup("##lbimage")) {
            AssetEntry e;
            e.kind = AssetKind::Image;
            e.path = imageMenu_;
            e.name = utf8(fsPath(imageMenu_).filename());
            ImGui::TextDisabled("%s", e.name.c_str());
            ImGui::Separator();
            bool done = false;
            if (ImGui::MenuItem(ICON_LC_SPARKLES "  Generate 3D...")) app.openPath(e.path), done = true;
            if (ImGui::MenuItem("Relief from image")) {
                app.confirmDiscard([&app, p = e.path] { app.runCommand("image_relief", {{"path", p}}); });
                done = true;
            }
            if (ImGui::MenuItem("Inflate image")) {
                app.confirmDiscard([&app, p = e.path] { app.runCommand("image_inflate", {{"path", p}}); });
                done = true;
            }
            if (ImGui::MenuItem("Project onto model")) app.runCommand("image_project", {{"path", e.path}}), done = true;
            if (ImGui::MenuItem("Use as alpha"))
                app.runCommand("brush_set_alpha", {{"path", e.path}, {"invert", app.alphaInvert_}}), done = true;
            if (done) imageMenu_.clear();
            ImGui::EndPopup();
        } else {
            imageMenu_.clear();
        }
    }
    ImGui::End();

    // A tile dragged out of the strip follows the cursor; releasing it over the canvas loads it.
    if (tileDrag_) {
        const ImTextureID tex = thumbnail(app, dragEntry_);
        const float s = 64.0f * scale;
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImVec2 a(io.MousePos.x - s * 0.5f, io.MousePos.y - s * 0.5f), b(a.x + s, a.y + s);
        fg->AddRectFilled(a, b, IM_COL32(42, 42, 46, 220), 4.0f);
        if (tex) fg->AddImageRounded(tex, a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 220), 4.0f);
        fg->AddRect(a, b, kAccent, 4.0f, 0, 1.5f);
    }
    if (pressing_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (tileDrag_ && canvas.contains(io.MousePos.x, io.MousePos.y) && !ImGui::GetCurrentContext()->HoveredWindow)
            activate(app, dragEntry_, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
        pressing_ = tileDrag_ = false;
    }
}

void LightBox::drawPreferences(App& app) {
    UiLayout& layout = app.layout_;
    ImGui::Checkbox("Show at startup##lightbox", &showAtStartup);
    ui::HelpText("Show at startup", nullptr,
                 "Open the LightBox on the Project tab when SculptForge starts (as ZBrush does).",
                 "Preferences > LightBox");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##lbrowspref", &layout.lightboxRows, 1, 4, "Rows %d");
    ui::HelpText("Rows", nullptr, "Rows of thumbnails in the LightBox strip.", "Preferences > LightBox");
    int placement = layout.lightboxBottom ? 1 : 0;
    ImGui::TextUnformatted("Placement");
    ImGui::SameLine();
    if (ImGui::RadioButton("Top##lbplace", placement == 0)) layout.lightboxBottom = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Bottom##lbplace", placement == 1)) layout.lightboxBottom = true;
    ui::HelpText("Placement", nullptr, "Show the LightBox along the top or the bottom of the canvas.",
                 "Preferences > LightBox");
    ImGui::TextDisabled("Library folder");
    ImGui::TextWrapped("%s", app.library_.user.empty() ? "(none)" : app.library_.user.c_str());
    if (!app.library_.user.empty() && ImGui::Button(ICON_LC_FOLDER_OPEN "  Show library folder"))
        SDL_OpenURL(("file://" + app.library_.user).c_str());
}

// ---------------------------------------------------------------------------
// App glue: library folders, actions, QuickSave, project thumbnails
// ---------------------------------------------------------------------------

void App::initLightBox() {
    if (const char* base = SDL_GetBasePath()) library_.builtin = findBuiltinAssets(base);
    const bool recording = !options_.recordPath.empty() || !options_.renderPath.empty();
    if (!options_.uiTest.empty()) {
        // UI tests get an empty library of their own (like their brush library), never the artist's.
        const fs::path base =
            options_.uiOutDir.empty() ? fs::temp_directory_path() / "sculptforge-ui-test" : fsPath(options_.uiOutDir);
        library_.user = utf8(base / "library");
        std::error_code ec;
        fs::remove_all(fsPath(library_.user), ec);
    } else if (!recording) {
        const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
        library_.user = !uiPrefs_.libraryFolder.empty() ? uiPrefs_.libraryFolder // Preferences > LightBox
                                                        : defaultUserLibrary(docs ? docs : "", prefDir_);
    }
    if (!library_.user.empty()) {
        std::string err;
        if (!ensureUserLibrary(library_.user, err)) {
            SDL_Log("LightBox library: %s", err.c_str());
            library_.user.clear();
        }
    }
    // Thumbnails are cached on disk for the artist; tests and recordings render fresh ones.
    if (interactive() && options_.uiTest.empty() && !prefDir_.empty()) thumbs_.setCacheDir(prefDir_ + "thumbs");
    if (const char* dir = SDL_getenv("SCULPTFORGE_THUMBNAIL_CACHE")) thumbs_.setCacheDir(dir); // to look at them

    // Cube is a LightBox primitive like Sphere and Plane (new_sphere / new_plane live in the core).
    commands_.add({"new_cube",
                   "Replace the scene with a new cube ready for sculpting (one polygroup per side).",
                   {{"resolution", ParamSpec::Type::Number, "Cells along each edge (1-256). Default 48.", false}},
                   [](Document& doc, const Args& a) -> CommandResult {
                       const int res = std::clamp(int(argNumber(a, "resolution", 48)), 1, 256);
                       doc.setMesh(makeCube(res), "Cube");
                       doc.path.clear();
                       return {true, "Created cube with " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
                   }});

    // Projects carry a 256 px picture of the current view, so the LightBox shows them without loading.
    setProjectThumbnailProvider([this](const Document&, Image& out) {
        if (!glContext_ || SDL_GL_GetCurrentContext() != glContext_) return false; // not the UI thread
        constexpr GLenum kFramebufferBinding = 0x8CA6, kViewport = 0x0BA2;
        GLint fbo = 0, viewport[4] = {};
        gl::GetIntegerv(kFramebufferBinding, &fbo);
        gl::GetIntegerv(kViewport, viewport);
        Camera cam = camera_;
        cam.aspect = 1.0f;
        std::string err;
        out.width = out.height = kProjectThumbnailSize;
        const bool ok = renderer_.renderToImage(cam, out.width, out.height, renderSettings_, out.rgba, err);
        gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(fbo));
        gl::Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        return ok;
    });

    auto add = [this](const char* id, const char* label, const char* icon, const char* palette, const char* shortcut,
                      const char* description, std::function<void()> run, std::function<bool()> checked = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = palette;
        a.shortcut = shortcut;
        a.description = description;
        a.run = std::move(run);
        a.checked = std::move(checked);
        actions_.add(std::move(a));
    };
    add(
        "ui.lightbox", "LightBox", ICON_LC_IMAGES, "LightBox", ",",
        "Show or hide the LightBox: thumbnails of your projects, models, brushes, alphas, materials, images, splats "
        "and "
        "QuickSaves along the top of the canvas. Double-click to load.",
        [this] { lightbox_.toggle(); }, [this] { return lightbox_.open(); });
    add("ui.picker.brush", "Brush picker", ICON_LC_BRUSH, "Brush", "B",
        "Pop up every brush under the cursor. Type the first letter of a brush, then the orange letter on its tile "
        "(B S T = Standard).",
        [] { openPicker(Picker::Brush, ImVec2(-1, -1)); });
    add("ui.picker.stroke", "Stroke picker", ICON_LC_SPLINE, "Stroke", "",
        "Pop up the stroke types: Dots, DragRect, DragDot, Spray and LazyMouse.",
        [] { openPicker(Picker::Stroke, ImVec2(-1, -1)); });
    add("ui.picker.alpha", "Alpha picker", ICON_LC_CIRCLE_DASHED, "Alpha", "",
        "Pop up the brush alphas: built-in stamps and images from your library.",
        [] { openPicker(Picker::Alpha, ImVec2(-1, -1)); });
    add("ui.picker.material", "Material picker", ICON_LC_SUN, "Material", "M",
        "Pop up the materials under the cursor. Hover one to preview it on the model; click to keep it.",
        [] { openPicker(Picker::Material, ImVec2(-1, -1)); });
    add("ui.size_popup", "Draw Size at cursor", ICON_LC_CIRCLE, "Draw", "S",
        "A Draw Size slider under the cursor: drag it, or type a number and press Enter.",
        [this] { PickerUi::sizePopup(*this, PickerUi::Slider::DrawSize); });
    // U / I / O (Z Intensity, RGB Intensity, Focal Shift) are in Input.cpp.
    add("file.quicksave", "QuickSave", ICON_LC_SAVE, "File", "",
        "Save a timestamped copy of the whole scene to the QuickSave folder of your library (the last 10 are kept). "
        "Your project file is not changed. Find them in the LightBox QuickSave tab.",
        [this] { quickSave(); });

    // ZBrush opens the LightBox at startup on the Project tab (never in tests, renders or recordings).
    if (uiPrefs_.lightboxAtStartup && interactive() && options_.uiTest.empty() && options_.openPath.empty() &&
        !options_.blender)
        lightbox_.show(LibraryTab::Project);
}

void App::drawLightBoxUi() {
    thumbs_.pump(renderer_);
    lightbox_.draw(*this, canvasRect());
    drawPickers(*this);
}

bool App::lightBoxKey(SDL_Keycode key, SDL_Keymod mod) {
    // Ctrl+B / Ctrl+Shift+B: the brush picker with the mask / selection brushes (ZBrush: B with Ctrl held).
    const bool heldPicker = key == SDLK_B && (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && !(mod & SDL_KMOD_ALT);
    if (!heldPicker && (mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT))) return false;
    const char* id = key == SDLK_COMMA ? "ui.lightbox"
                     : key == SDLK_B   ? "ui.picker.brush"
                     : key == SDLK_M   ? "ui.picker.material"
                     : key == SDLK_S   ? "ui.size_popup"
                                       : nullptr;
    const Action* a = id ? actions_.find(id) : nullptr;
    if (!a || !a->run) return false;
    actions_.touch(a->id);
    a->run();
    return true;
}

void App::quickSave() {
    if (library_.user.empty()) {
        status_ = "QuickSave needs a library folder (Preferences > LightBox)";
        return;
    }
    const std::string folder = library_.user + "/QuickSave";
    std::error_code ec;
    fs::create_directories(fsPath(folder), ec);
    const auto now = std::chrono::system_clock::now();
    const std::string path =
        nextQuickSavePath(folder, std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    storeProjectSettings();
    std::string err;
    // A copy: the document keeps its own path and unsaved state, as with ZBrush's QuickSave.
    if (!saveProject(path, doc_, err)) {
        status_ = "QuickSave failed: " + err;
        lastCommandOk_ = false;
        return;
    }
    pruneQuickSaves(folder);
    libraryScanner_.invalidate();
    status_ = "QuickSaved " + utf8(fsPath(path).filename());
    lastCommandOk_ = true;
}

} // namespace sf
