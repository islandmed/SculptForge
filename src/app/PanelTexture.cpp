// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texture Map sub-palette (Tool palette): texture painting on UVs (core/Texture.h). Auto UV unwraps the
// Part on a worker thread (core/AutoUv.h, xatlas); New gives it a 1K-8K texture, after which the Paint
// brush, Rgb sculpting, fills and the Paint Layers palette work on the texture (the Vertex / Texture toggle
// switches back). Vertex paint <-> Texture convert, Import / Export load and save PNGs, Show UVs draws the
// texture with the UV layout over it. Everything but Auto UV and the file dialogs goes through commands,
// so it is undoable and scriptable.
#include "app/App.h"
#include "app/NativeDialog.h"
#include "app/Demo.h"
#include "app/Ui.h"

#include "core/VertexPaint.h"
#include "core/TextureTransfer.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <thread>
#include <utility>
#include <unordered_map>

namespace sf {

namespace {

constexpr const char* kPath = "Tool > Texture Map";
constexpr int kSizes[] = {1024, 2048, 4096, 8192};
constexpr const char* kSizeLabels[] = {"1K", "2K", "4K", "8K"};

// File dialogs answer on another thread: they leave the chosen path here for pollAutoUv.
enum TextureFile { kExportTexture, kImportTexture };
struct TextureDialog {
    App* app = nullptr;
    TextureFile kind = kExportTexture;
};
TextureDialog g_dialogs[2];
std::mutex g_fileMutex;
std::vector<std::pair<TextureFile, std::string>> g_files;

const SDL_DialogFileFilter kPngFilter[] = {{"PNG image", "png"}};
const SDL_DialogFileFilter kImageFilters[] = {{"Images", "png;jpg;jpeg;tga;bmp;psd"}, {"All files", "*"}};

void SDLCALL onTextureFile(void* user, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
    if (!files || !files[0]) return;
    auto* d = static_cast<TextureDialog*>(user);
    std::string path = files[0];
    if (d->kind == kExportTexture && std::filesystem::path(std::u8string(path.begin(), path.end())).extension().empty())
        path += ".png";
    std::lock_guard lock(g_fileMutex);
    g_files.push_back({d->kind, std::move(path)});
}

// Line segments of the UV layout in 0..1: every triangle edge on small meshes, only the UV island borders
// (edges whose two sides have different UVs, or no other side) on big ones.
std::vector<glm::vec2> uvLayoutLines(const Mesh& m) {
    std::vector<glm::vec2> out;
    const size_t nt = m.triangleCount();
    if (m.uvs.size() != nt * 3) return out;
    if (nt <= 20000) {
        out.reserve(nt * 6);
        for (size_t t = 0; t < nt; ++t)
            for (int e = 0; e < 3; ++e) out.insert(out.end(), {m.uvs[t * 3 + size_t(e)], m.uvs[t * 3 + size_t(e + 1) % 3]});
        return out;
    }
    std::unordered_map<uint64_t, uint32_t> edges; // (a, b) with a < b -> corner index of the edge's start
    edges.reserve(nt * 2);
    auto key = [](uint32_t a, uint32_t b) { return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a; };
    std::vector<uint8_t> matched(nt * 3, 0);
    for (uint32_t t = 0; t < nt; ++t)
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = m.triangles[t][e], b = m.triangles[t][(e + 1) % 3];
            const uint32_t corner = t * 3 + uint32_t(e);
            auto [it, inserted] = edges.try_emplace(key(a, b), corner);
            if (inserted) continue;
            // The twin: same UVs at both ends means the edge is inside an island.
            const uint32_t o = it->second, ot = o / 3, oe = o % 3;
            const glm::vec2 oa = m.uvs[o], ob = m.uvs[ot * 3 + (oe + 1) % 3];
            const glm::vec2 ua = m.uvs[corner], ub = m.uvs[t * 3 + uint32_t(e + 1) % 3];
            const bool same = m.triangles[ot][oe] == a ? (oa == ua && ob == ub) : (oa == ub && ob == ua);
            if (same) matched[o] = matched[corner] = 1;
        }
    for (uint32_t c = 0; c < nt * 3; ++c)
        if (!matched[c]) out.insert(out.end(), {m.uvs[c], m.uvs[(c / 3) * 3 + (c % 3 + 1) % 3]});
    return out;
}

} // namespace

void App::registerTextureUiActions() {
    auto add = [this](const char* id, const char* label, const char* palette, std::function<void()> run,
                      std::function<bool()> checked, std::function<bool()> enabled, const char* description,
                      const char* command = "") {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = palette;
        a.run = std::move(run);
        a.checked = std::move(checked);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    auto textured = [this] { return hasTexture(doc_.mesh); };
    add("draw.paint_vertex", "Vertex", "Draw", [this] { runCommand("paint_target", {{"target", std::string("vertex")}}); },
        [this] { return !paintsTexture(doc_.mesh); }, textured,
        "Paint into the vertices (vertex paint): the colour resolution is the mesh's. Shown when the Part has a "
        "texture (Tool > Texture Map).");
    add("draw.paint_texture", "Texture", "Draw", [this] { runCommand("paint_target", {{"target", std::string("texture")}}); },
        [this] { return paintsTexture(doc_.mesh); }, textured,
        "Paint into the Part's texture: the colour resolution comes from the texture, not the mesh. The Paint "
        "brush, Rgb sculpting, fills and Paint Layers all work on it.");
    // Shown on the top shelf and in the Color palette while painting a Part that has no texture yet.
    add("draw.new_texture", "New Texture", "Draw", [this] { startNewTexture(); }, nullptr,
        [this] { return !autoUv_ && doc_.mesh.triangleCount() > 0; },
        "Start texture painting: give the Part a texture (the size chosen in Tool > Texture Map, 2K by default) "
        "so paint goes into texels instead of vertices, and the detail comes from the texture, not the mesh. "
        "Without UVs, Auto UV unwraps the model first (in the background, with its progress in the status bar).",
        "texture_create");
    add("view.texture", "Texture", "Render", [this] { renderSettings_.showTexture = !renderSettings_.showTexture; },
        [this] { return renderSettings_.showTexture; }, nullptr,
        "Show the painted texture on the model (multiplied over the material, like vertex paint).");
    add("texture.auto_uv", "Auto UV", kPath, [this] { startAutoUv(); }, nullptr,
        [this] { return !autoUv_ && doc_.mesh.triangleCount() > 0; },
        "Unwrap the Part automatically: cut it into UV islands and pack them, so any sculpt can carry a texture. "
        "Replaces existing UVs (undoable). Runs in the background; big meshes can take a minute or more.",
        "uv_auto");
    add("texture.new", "New Texture", kPath,
        [this] { runCommand("texture_create", {{"size", std::to_string(kSizes[std::clamp(textureSizeChoice_, 0, 3)])}}); },
        nullptr, [this] { return doc_.mesh.hasUvs(); },
        "Give the Part a texture of the chosen size to paint into (white). Needs UVs: Auto UV makes them. "
        "Replaces an existing texture (undoable).",
        "texture_create");
    add("texture.show_uvs", "Show UVs", kPath, [this] { showUvView_ = !showUvView_; }, [this] { return showUvView_; },
        [this] { return doc_.mesh.hasUvs(); }, "Show the texture with the UV layout drawn over it, below these buttons.");
    add("texture.from_vertexpaint", "Vertex Paint > Texture", kPath, [this] { runCommand("texture_from_vertexpaint"); }, nullptr,
        [this] { return doc_.mesh.hasUvs() && doc_.mesh.hasColors(); },
        "Bake the vertex paint (all paint layers) into the active texture layer. Makes a 2K texture if there is none.",
        "texture_from_vertexpaint");
    add("texture.to_vertexpaint", "Texture > Vertex Paint", kPath, [this] { runCommand("vertexpaint_from_texture"); }, nullptr,
        textured,
        "Copy the texture into the vertex paint of every vertex (detail finer than the mesh is lost).",
        "vertexpaint_from_texture");
    add("texture.export", "Export Texture...", kPath,
        [this] { sf::dialogs::saveFile(onTextureFile, &g_dialogs[kExportTexture], window_, kPngFilter, 1, nullptr); },
        nullptr, textured, "Save the texture (every visible layer composited) as a PNG image.", "texture_export");
    add("texture.import", "Import Texture...", kPath,
        [this] { sf::dialogs::openFile(onTextureFile, &g_dialogs[kImportTexture], window_, kImageFilters, 2, nullptr, false); },
        nullptr, [this] { return doc_.mesh.hasUvs(); },
        "Load an image as the Part's texture (replacing its layers; undoable).", "texture_import");
    add("texture.delete", "Delete Texture", kPath, [this] { runCommand("texture_delete"); }, nullptr,
        [this] { return doc_.mesh.texture != nullptr; },
        "Remove the Part's texture (undoable). Painting goes back to the vertices.", "texture_delete");
    add("geometry.keep_texture", "Keep Texture", "Tool > Remesh", [this] { keepTexture_ = !keepTexture_; },
        [this] { return keepTexture_; }, nullptr,
        "Voxel Remesh and Quad Remesh keep a painted texture: the new mesh gets Auto UVs and the texture (every layer) "
        "is projected onto it from the old surface, in the same undo step. Off: the texture is dropped. Merging "
        "Parts keeps textures too. Shown under Remesh and Quad Remesh when the Part has a texture.",
        "");
    for (int i = 0; i < 2; ++i) g_dialogs[i] = {this, TextureFile(i)};
}

void App::drawKeepTextureCheckbox(const char* id, const char* path) {
    ImGui::Checkbox(id, &keepTexture_);
    ui::HelpText("Keep Texture", nullptr,
                 "Keep the painted texture: the new mesh gets Auto UVs and the texture (every layer) is projected onto "
                 "it from the old surface, in the same undo step (it adds the Auto UV time). Off: the texture is "
                 "dropped. Scripts: keep_texture=false.",
                 path);
}

void App::startNewTexture() {
    if (autoUv_ || doc_.mesh.triangleCount() == 0) return;
    if (!doc_.mesh.hasUvs()) {
        startAutoUv();
        textureAfterAutoUv_ = autoUv_ != nullptr; // pollAutoUv makes the texture when the UVs are in
        return;
    }
    runCommand("texture_create", {{"size", std::to_string(kSizes[std::clamp(textureSizeChoice_, 0, 3)])}});
}

CommandResult App::startAutoUvCommand(const Args& a) {
    if (doc_.mesh.triangleCount() == 0) return {false, "There is no mesh to unwrap"};
    if (autoUv_) return {false, "Auto UV is already running"};
    // (resolution as the core command reads it: core/TextureCommands.cpp)
    const int res = a.contains("resolution") ? int(argNumber(a, "resolution", kDefaultTextureSize))
                    : doc_.mesh.texture       ? std::max(doc_.mesh.texture->width, doc_.mesh.texture->height)
                                              : kDefaultTextureSize;
    startAutoUv(res, argBool(a, "keep_texture", true), true);
    return {true, "Auto UV: unwrapping " + std::to_string(doc_.mesh.triangleCount()) + " triangles in the background"};
}

void App::startAutoUv(int resolution, bool keepTexture, bool command) {
    if (autoUv_ || doc_.mesh.triangleCount() == 0) return;
    commandResult_.reset();
    auto job = std::make_unique<AutoUvJob>();
    job->keepTexture = keepTexture;
    job->command = command;
    job->progress = std::make_shared<std::atomic<float>>(0.0f);
    job->cancel = std::make_shared<std::atomic<bool>>(false);
    job->result = std::make_shared<AutoUvResult>();
    job->error = std::make_shared<std::string>();
    job->part = doc_.activePartInfo().id;
    job->triangles = doc_.mesh.triangleCount();
    const int res = resolution > 0       ? resolution
                    : doc_.mesh.texture ? std::max(doc_.mesh.texture->width, doc_.mesh.texture->height)
                                        : kSizes[std::clamp(textureSizeChoice_, 0, 3)];
    std::promise<bool> promise;
    job->done = promise.get_future();
    std::thread([promise = std::move(promise), positions = doc_.mesh.positions, triangles = doc_.mesh.triangles, res,
                 progress = job->progress, cancel = job->cancel, result = job->result, error = job->error]() mutable {
        const bool ok = computeAutoUv(positions, triangles, res, *result, *error, [&](float f) {
            progress->store(f);
            return !cancel->load();
        });
        promise.set_value(ok);
    }).detach();
    autoUv_ = std::move(job);
    status_ = "Auto UV: unwrapping " + std::to_string(doc_.mesh.triangleCount()) + " triangles...";
}

void App::pollAutoUv() {
    std::vector<std::pair<TextureFile, std::string>> files;
    {
        std::lock_guard lock(g_fileMutex);
        files.swap(g_files);
    }
    for (const auto& [kind, path] : files)
        runCommand(kind == kExportTexture ? "texture_export" : "texture_import", {{"path", path}});

    if (!autoUv_) return;
    // Cancel: xatlas only looks at it between its stages (the charts stage is most of the time), so the job is
    // dropped now; the detached worker owns copies of what it reads and its result goes nowhere.
    const bool cancelled = autoUv_->cancel->load();
    if (!cancelled && autoUv_->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        status_ = "Auto UV: " + std::to_string(int(autoUv_->progress->load() * 100.0f)) + "%";
        return;
    }
    if (jobWaitsForStroke()) return; // (the result lands when the stroke ends)
    const std::unique_ptr<AutoUvJob> job = std::move(autoUv_);
    const bool makeTexture = std::exchange(textureAfterAutoUv_, false);
    // The uv_auto command's result, for whoever waits on it (console, AI link, UI test) and the console's log.
    auto finish = [&](bool ok, const std::string& message) {
        status_ = message;
        if (!job->command) return;
        commandResult_ = CommandResult{ok, message};
        console_.print(ok ? Console::Kind::Result : Console::Kind::Error, message);
        console_.seenStatus(status_);
        if (!ok) SDL_Log("uv_auto failed: %s", message.c_str());
    };
    if (cancelled) {
        textureAfterAutoUv_ = false;
        finish(false, "Auto UV was cancelled");
        return;
    }
    if (!job->done.get()) {
        finish(false, *job->error);
        return;
    }
    // The Part it unwrapped may be inactive now, or changed topology meanwhile.
    int index = -1;
    for (int i = 0; i < doc_.partCount(); ++i)
        if (doc_.parts[size_t(i)].id == job->part) index = i;
    if (index < 0 || doc_.partMesh(index).triangleCount() != job->triangles) {
        finish(false, "Auto UV skipped: the mesh changed while it was being unwrapped");
        return;
    }
    const JobPart part(*this, index);
    Mesh m = doc_.mesh;
    // A painted texture and its layers move onto the new UVs, in the same undo step (core/TextureTransfer.h).
    const std::string note = setUvsKeepingTexture(m, std::move(job->result->uvs), job->keepTexture);
    doc_.replaceMeshUndoable(std::move(m), "Auto UV");
    uvLinesKey_ = 0;
    uvEditorChanged();
    updateTitle();
    const std::string islands = "Auto UV: " + std::to_string(job->result->charts) + " UV islands";
    if (job->command) {
        finish(true, islands + note);
        return;
    }
    if (makeTexture && !hasTexture(doc_.mesh)) {
        runCommand("texture_create", {{"size", std::to_string(kSizes[std::clamp(textureSizeChoice_, 0, 3)])}});
        if (hasTexture(doc_.mesh)) status_ = islands + ", new " + std::to_string(doc_.mesh.texture->width / 1024) +
                                             "K texture: paint goes into it";
        return;
    }
    status_ = islands + (!note.empty() ? note : hasTexture(doc_.mesh) ? "" : ". Now make a texture: New Texture");
}

void App::drawTextureMapContents() {
    const Mesh& m = doc_.mesh;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;

    // Status line: UVs, texture size, where paint goes.
    if (autoUv_) {
        ImGui::ProgressBar(autoUv_->progress->load(), ImVec2(half, 0), tr("Unwrapping..."));
        ui::HelpText("Auto UV", nullptr, "Unwrapping the Part in the background; you can keep working.", kPath);
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel##autouv"), ImVec2(-1, 0))) autoUv_->cancel->store(true);
    } else if (!m.hasUvs()) {
        ImGui::TextDisabled(tr("No UVs: Auto UV makes them"));
    } else if (!hasTexture(m)) {
        ImGui::TextDisabled(tr("UVs, no texture yet"));
    } else {
        const PaintTexture& t = *m.texture;
        ImGui::TextDisabled(tr(t.layers.empty() ? "%dx%d texture, %d layer" : "%dx%d texture, %d layers"), t.width, t.height,
                            int(t.layers.size()) + 1);
    }

    ui::ActionButton(*this, "texture.auto_uv", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "texture.show_uvs", ImVec2(-1, 0));
    ui::ActionButton(*this, "ui.uv_editor", ImVec2(-1, 0), ui::Look::IconLabel);

    ImGui::SetNextItemWidth(half);
    ImGui::Combo("##texsize", &textureSizeChoice_, kSizeLabels, 4);
    ui::HelpText("Texture size", nullptr,
                 "Size of a new texture: 1K, 2K (the default), 4K or 8K texels square. Bigger holds finer paint and uses "
                 "more memory (8K: 256 MB per layer).",
                 kPath);
    ImGui::SameLine();
    ui::ActionButton(*this, "texture.new", ImVec2(-1, 0));

    if (hasTexture(m)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(tr("Paint into"));
        ImGui::SameLine();
        ui::Segmented(*this, {"draw.paint_vertex", "draw.paint_texture"});
    }
    ui::ActionButton(*this, "texture.from_vertexpaint", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "texture.to_vertexpaint", ImVec2(-1, 0));
    ui::ActionButton(*this, "texture.import", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "texture.export", ImVec2(-1, 0));
    ui::ActionButton(*this, "texture.delete", ImVec2(half, 0));
    if (hasTexture(m)) {
        ImGui::SameLine();
        ImGui::Checkbox(tr("Show texture"), &renderSettings_.showTexture);
        ui::HelpAction(*this, "view.texture");
    }

    // UV view: the texture (or a dark square) with the UV layout over it, v = 0 at the bottom.
    if (showUvView_ && m.hasUvs()) {
        const float side = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const GLuint tex = hasTexture(m) ? renderer_.paintTexture(doc_.meshKey(doc_.activePart)) : 0;
        if (tex) dl->AddImage(ImTextureID(uintptr_t(tex)), p, ImVec2(p.x + side, p.y + side), ImVec2(0, 1), ImVec2(1, 0));
        else dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side), IM_COL32(40, 40, 46, 255));
        const uint64_t key = (uint64_t(doc_.activePartInfo().id) << 40) ^ uint64_t(uintptr_t(m.uvs.data())) ^ m.uvs.size();
        if (key != uvLinesKey_) {
            uvLines_ = uvLayoutLines(m);
            uvLinesKey_ = key;
        }
        const size_t step = std::max<size_t>(1, uvLines_.size() / 2 / 150000) * 2; // cap what one frame draws
        const ImU32 col = tex ? IM_COL32(20, 20, 30, 150) : IM_COL32(150, 200, 255, 150);
        auto at = [&](const glm::vec2& uv) { return ImVec2(p.x + uv.x * side, p.y + (1.0f - uv.y) * side); };
        dl->PushClipRect(p, ImVec2(p.x + side, p.y + side), true);
        for (size_t i = 0; i + 1 < uvLines_.size(); i += step) dl->AddLine(at(uvLines_[i]), at(uvLines_[i + 1]), col, 1.0f);
        dl->PopClipRect();
        dl->AddRect(p, ImVec2(p.x + side, p.y + side), IM_COL32(90, 90, 100, 255));
        ImGui::Dummy(ImVec2(side, side));
        ui::HelpText("UV view", nullptr,
                     "The texture with the Part's UV layout over it (island borders on big meshes). Paint on the model; "
                     "the texture fills in here.",
                     kPath);
    }
    drawTextureChannelsContents(); // Material Channels: toggles, solo view, Export Maps (PanelPbr.cpp)
}

} // namespace sf
