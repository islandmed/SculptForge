// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bake sub-palette (Tool palette): bakes maps from a high-poly source (one SubTool, or every visible one but
// the active) onto the active SubTool's UVs (core/Bake.h): tangent-space normal, object-space normal,
// displacement, AO, cavity and colour, at 1K-8K with ray distances, anti-aliasing and padding. The bake runs
// on a worker thread on copies of the meshes, with progress and Cancel, and writes PNGs (and a float EXR for
// displacement) into the output folder. The tangent-space normal map then shows on the low-poly in the viewport
// (the Renderer's normal map shader), so people can check it next to the high-poly. A bake run from the
// console or a script (the bake command) shows its normal map too.
#include "app/App.h"
#include "app/Demo.h"
#include "app/Ui.h"

#include "core/Bake.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <thread>

namespace sf {

namespace {

constexpr const char* kPath = "Tool > Bake";
constexpr int kSizes[] = {1024, 2048, 4096, 8192};
constexpr const char* kSizeLabels[] = {"1K", "2K", "4K", "8K"};

std::string utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string safeName(std::string name) {
    for (char& c : name)
        if (std::strchr("/\\:*?\"<>|", c)) c = '_';
    return name.empty() ? std::string("bake") : name;
}

uint64_t uvHash(const Mesh& m) {
    uint64_t h = 1469598103934665603ull ^ m.uvs.size();
    const auto* b = reinterpret_cast<const uint8_t*>(m.uvs.data());
    const size_t n = m.uvs.size() * sizeof(glm::vec2);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

std::string fileUrl(const std::string& path) {
    std::string p = path;
    for (char& c : p)
        if (c == '\\') c = '/';
    std::string url = p.rfind('/', 0) == 0 ? "file://" : "file:///";
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : p) {
        if (std::isalnum(c) || std::strchr("/:-_.~", c)) url += char(c);
        else url += {'%', hex[c >> 4], hex[c & 15]};
    }
    return url;
}

// An integer slider drawn like ui::SliderInside (label inside, thin accent grab).
bool sliderInt(const char* id, int* v, int lo, int hi, const char* format) {
    float f = float(*v);
    if (!ui::SliderInside(id, &f, float(lo), float(hi), format)) return false;
    *v = std::clamp(int(std::lround(f)), lo, hi);
    return true;
}

} // namespace

struct BakeUiState {
    BakeSettings settings;
    int sizeChoice = 1;          // 2K
    uint32_t source = 0;         // SubTool id to bake from; 0 = every visible SubTool but the active one
    char folder[1024] = "";      // empty = beside the last export / the project / <library>/Bakes
    bool exr = true;
    bool preview = true;         // show the baked normal map on the low-poly
    float frontPct = 2.0f, rearPct = 2.0f, aoDistancePct = 5.0f;

    struct Job {
        std::future<bool> done;
        std::shared_ptr<std::atomic<float>> progress;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<BakeResult> result;
        std::shared_ptr<std::string> error;
        std::shared_ptr<std::vector<std::string>> files;
        uint32_t subtool = 0;
        size_t triangles = 0;
        std::string folder;
    };
    std::unique_ptr<Job> job;

    // The last bake, shown on its SubTool while the mesh keeps its triangles and UVs.
    std::shared_ptr<const BakeResult> result;
    uint32_t subtool = 0;
    size_t triangles = 0;
    uint64_t uvKey = 0;
    const void* uvData = nullptr; // the UV array last checked against uvKey
    NormalMapView view;
    uint64_t serial = 0;
    std::vector<std::string> files;
    std::string writtenFolder; // where the last bake wrote its maps

    void show(uint32_t id, const Mesh& mesh, std::shared_ptr<const BakeResult> r, std::vector<std::string> written,
              std::string where) {
        result = std::move(r);
        subtool = id;
        triangles = mesh.triangleCount();
        uvKey = uvHash(mesh);
        uvData = mesh.uvs.data();
        files = std::move(written);
        writtenFolder = std::move(where);
        if (result && settings.autoDistance && result->diagonal > 0.0f) { // show what Auto measured
            frontPct = 100.0f * result->front / result->diagonal;
            rearPct = 100.0f * result->rear / result->diagonal;
        }
        view = NormalMapView();
        if (result && result->has(BakeMap::Normal)) {
            view.serial = ++serial;
            view.width = result->width;
            view.height = result->height;
            view.rgb = &result->normal;
            view.tangents = &result->tangents;
            view.flipGreen = result->flipGreen;
        }
    }
};

// File dialog answers arrive on another thread.
namespace {
std::mutex g_folderMutex;
std::string g_pickedFolder;
void SDLCALL onBakeFolder(void*, const char* const* files, int) {
    if (!files || !files[0]) return;
    std::lock_guard lock(g_folderMutex);
    g_pickedFolder = files[0];
}
} // namespace

const NormalMapView* App::bakeNormalMap(uint32_t id, const Mesh& mesh) {
    if (!bake_ || !bake_->preview || !bake_->result || bake_->subtool != id || !bake_->view.serial) return nullptr;
    BakeUiState& b = *bake_;
    if (mesh.triangleCount() != b.triangles || mesh.uvs.size() != b.result->tangents.size()) return nullptr;
    if (mesh.uvs.data() != b.uvData) { // the UV array moved (undo, a SubTool switch): same UVs?
        if (uvHash(mesh) != b.uvKey) return nullptr;
        b.uvData = mesh.uvs.data();
    }
    return &b.view;
}

bool App::bakeBusy() const { return bake_ && bake_->job; }

std::string App::bakedNormalMapFile(bool& directX) {
    if (!bake_ || !bake_->result || !bake_->view.serial) return {};
    for (int i = 0; i < doc_.subtoolCount(); ++i) {
        const SubTool& st = doc_.subtools[size_t(i)];
        if (st.id != bake_->subtool || !st.visible) continue;
        const Mesh& m = doc_.subtoolMesh(i);
        if (m.triangleCount() != bake_->triangles || uvHash(m) != bake_->uvKey) return {};
        for (const std::string& f : bake_->files) {
            const std::string suffix = "_normal.png";
            if (f.size() >= suffix.size() && f.compare(f.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                std::filesystem::exists(std::filesystem::path(std::u8string(f.begin(), f.end())))) {
                directX = bake_->result->flipGreen;
                return f;
            }
        }
    }
    return {};
}

double App::bakeTestValue(const std::string& name) {
    const BakeUiState* b = bake_.get();
    if (name == "bake_running") return b && b->job ? 1.0 : 0.0;
    if (name == "baked_maps") {
        int n = 0;
        for (int i = 0; b && b->result && i < kBakeMapCount; ++i) n += b->result->has(BakeMap(i));
        return n;
    }
    if (name == "bake_size") return b && b->result ? double(b->result->width) : 0.0;
    if (name == "bake_files") return b ? double(b->files.size()) : 0.0;
    if (name == "normal_map_preview") return bakeNormalMap(doc_.activeSubtoolInfo().id, displayedMesh()) ? 1.0 : 0.0;
    return std::numeric_limits<double>::quiet_NaN();
}

void App::registerBakeUiActions() {
    if (!bake_) bake_ = std::make_shared<BakeUiState>();
    auto add = [this](const char* id, const char* label, std::function<void()> run, std::function<bool()> checked,
                      std::function<bool()> enabled, const char* description, const char* command = "",
                      const char* icon = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = kPath;
        a.run = std::move(run);
        a.checked = std::move(checked);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    add("bake.run", "Bake", [this] { startBake(); }, nullptr,
        [this] { return !bake_->job && doc_.mesh.hasUvs() && doc_.subtoolCount() > 1; },
        "Bake the chosen maps from the source SubTool(s) onto this SubTool's UVs, in the background. Maps are saved as "
        "PNGs (displacement also as a 32-bit EXR) in the output folder, and the normal map shows on the model.",
        "bake");
    add("bake.cancel", "Cancel Bake", [this] {
            if (bake_->job) bake_->job->cancel->store(true);
        },
        nullptr, [this] { return bake_->job != nullptr; }, "Stop the bake that is running. Nothing is written.");
    add("bake.preview", "Preview Normal Map", [this] { bake_->preview = !bake_->preview; },
        [this] { return bake_->preview; }, [this] { return bake_->result && bake_->result->has(BakeMap::Normal); },
        "Show the baked tangent-space normal map on the low-poly in the viewport (decoded with the same MikkTSpace "
        "tangents a game engine uses), to check it against the high-poly. Turn it off to see the bare low-poly.");
    add("bake.open_folder", "Open Folder", [this] {
            const std::string& dir = bake_->writtenFolder;
            if (!dir.empty() && !SDL_OpenURL(fileUrl(dir).c_str())) status_ = "Couldn't open " + dir + ": " + SDL_GetError();
        },
        nullptr, [this] { return !bake_->writtenFolder.empty(); }, "Open the folder the last bake wrote its maps to.");
    add("bake.choose_folder", "Output Folder...",
        [this] { SDL_ShowOpenFolderDialog(onBakeFolder, nullptr, window_, nullptr, false); }, nullptr, nullptr,
        "Choose the folder baked maps are written to. Empty: beside your last export, else the project's folder, "
        "else Bakes in your library folder.",
        "", ICON_LC_FOLDER_OPEN);
    // A bake run as a command (console, scripts, UI tests) shows its normal map too.
    setBakeListener([this](const Document& doc, std::shared_ptr<const BakeResult> r, const std::vector<std::string>& files) {
        const std::string folder = files.empty() ? std::string() : utf8(std::filesystem::path(std::u8string(files[0].begin(), files[0].end())).parent_path());
        bake_->show(doc.activeSubtoolInfo().id, doc.mesh, std::move(r), files, folder);
    });
}

void App::startBake() {
    BakeUiState& b = *bake_;
    if (b.job) return;
    if (!doc_.mesh.hasUvs()) {
        status_ = "Bake needs UVs on this SubTool (the low-poly): Tool > Texture Map > Auto UV";
        return;
    }
    // Source SubTools: the chosen one, or every visible SubTool but this one.
    std::string source = "all";
    for (int i = 0; i < doc_.subtoolCount(); ++i)
        if (b.source && doc_.subtools[size_t(i)].id == b.source) source = doc_.subtools[size_t(i)].name;
    std::vector<int> idx;
    std::string err;
    if (!bakeSourceSubtools(doc_, source, idx, err)) {
        status_ = err;
        return;
    }
    if (doc_.sculptor.strokeActive()) endStroke();
    BakeSettings st = b.settings;
    st.size = kSizes[std::clamp(b.sizeChoice, 0, 3)];
    st.front = b.frontPct / 100.0f;
    st.rear = b.rearPct / 100.0f;
    st.aoDistance = b.aoDistancePct / 100.0f;
    if (!st.maps) {
        status_ = "Tick at least one map to bake";
        return;
    }
    // Where the maps go.
    std::string folder = b.folder;
    if (folder.empty()) folder = lastExportFolder_;
    if (folder.empty() && !doc_.path.empty()) folder = defaultBakeFolder(doc_);
    if (folder.empty() && !library_.user.empty()) folder = library_.user + "/Bakes";
    if (folder.empty()) folder = utf8(std::filesystem::temp_directory_path() / "SculptForge Bakes");

    auto job = std::make_unique<BakeUiState::Job>();
    job->progress = std::make_shared<std::atomic<float>>(0.0f);
    job->cancel = std::make_shared<std::atomic<bool>>(false);
    job->result = std::make_shared<BakeResult>();
    job->error = std::make_shared<std::string>();
    job->files = std::make_shared<std::vector<std::string>>();
    job->subtool = doc_.activeSubtoolInfo().id;
    job->triangles = doc_.mesh.triangleCount();
    job->folder = folder;
    // The worker bakes copies, so sculpting can go on meanwhile.
    auto low = std::make_shared<Mesh>(doc_.mesh);
    auto sources = std::make_shared<std::vector<Mesh>>();
    for (int i : idx) sources->push_back(doc_.subtoolMesh(i));
    std::promise<bool> promise;
    job->done = promise.get_future();
    std::thread([promise = std::move(promise), low, sources, st, folder, name = safeName(doc_.activeSubtoolInfo().name),
                 exr = b.exr, progress = job->progress, cancel = job->cancel, result = job->result, error = job->error,
                 files = job->files]() mutable {
        std::vector<const Mesh*> src;
        for (const Mesh& m : *sources) src.push_back(&m);
        bool ok = bakeMaps(*low, src, st, *result, *error, [&](float f) {
            progress->store(f);
            return !cancel->load();
        });
        if (ok) ok = saveBakeMaps(*result, folder, name, exr, *files, *error);
        promise.set_value(ok);
    }).detach();
    b.job = std::move(job);
    status_ = "Baking " + std::to_string(st.size) + " maps from " + std::to_string(idx.size()) + " SubTool" +
              (idx.size() == 1 ? "" : "s") + "...";
}

void App::pollBake() {
    if (!bake_) return;
    BakeUiState& b = *bake_;
    {
        std::lock_guard lock(g_folderMutex);
        if (!g_pickedFolder.empty()) {
            std::snprintf(b.folder, sizeof b.folder, "%s", g_pickedFolder.c_str());
            g_pickedFolder.clear();
        }
    }
    if (!b.job) return;
    if (b.job->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        status_ = "Baking: " + std::to_string(int(b.job->progress->load() * 100.0f)) + "%";
        return;
    }
    const std::unique_ptr<BakeUiState::Job> job = std::move(b.job);
    if (!job->done.get()) {
        status_ = *job->error == "Cancelled" ? std::string("Bake cancelled") : "Bake failed: " + *job->error;
        return;
    }
    int index = -1;
    for (int i = 0; i < doc_.subtoolCount(); ++i)
        if (doc_.subtools[size_t(i)].id == job->subtool) index = i;
    if (index >= 0 && doc_.subtoolMesh(index).triangleCount() == job->triangles)
        b.show(job->subtool, doc_.subtoolMesh(index), job->result, *job->files, job->folder);
    else
        b.files = *job->files, b.writtenFolder = job->folder;
    status_ = bakeSummary(*job->result) + ". Saved to " + job->folder;
}

void App::drawBakeContents() {
    BakeUiState& b = *bake_;
    const Mesh& m = doc_.mesh;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float x0 = ImGui::GetCursorPosX(); // line start, inside the section indent
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;

    // What the maps go onto.
    if (!m.hasUvs()) {
        ImGui::TextDisabled("No UVs: Tool > Texture Map > Auto UV");
    } else {
        ImGui::TextDisabled("Onto %s (%zu tris, UVs)", doc_.activeSubtoolInfo().name.c_str(), m.triangleCount());
    }
    ui::HelpText("Low-poly", nullptr,
                 "Maps are baked onto the active SubTool's UVs: make the low-poly active (it needs UVs from import or "
                 "from Auto UV in Tool > Texture Map), and keep the high-poly SubTool(s) visible.",
                 kPath);

    // Source.
    std::string current = "All visible SubTools";
    bool found = false;
    for (int i = 0; i < doc_.subtoolCount(); ++i)
        if (b.source && doc_.subtools[size_t(i)].id == b.source && i != doc_.activeSubtool) {
            current = doc_.subtools[size_t(i)].name;
            found = true;
        }
    if (!found) b.source = 0;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Source");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##bakesource", current.c_str())) {
        if (ImGui::Selectable("All visible SubTools", b.source == 0)) b.source = 0;
        for (int i = 0; i < doc_.subtoolCount(); ++i) {
            if (i == doc_.activeSubtool) continue;
            const SubTool& st = doc_.subtools[size_t(i)];
            ImGui::PushID(int(st.id));
            if (ImGui::Selectable(st.name.c_str(), b.source == st.id)) b.source = st.id;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ui::HelpText("Source", nullptr,
                 "The high-poly to bake from: one SubTool, or every visible SubTool except the active one (the low-poly). "
                 "Hidden faces of the source are ignored.",
                 kPath);

    // Maps, two columns.
    static const char* kMapHelp[kBakeMapCount] = {
        "Tangent-space normal map (MikkTSpace tangents, as Blender, Unreal, Unity and Godot compute them). Shown on "
        "the model after baking.",
        "Object-space normal map: the high-poly's normals in model space (no tangents).",
        "Distance from the low-poly to the high-poly along its normal: a 16-bit PNG with mid-grey = 0 (the scale is "
        "reported when the bake finishes) and a 32-bit EXR in world units.",
        "Ambient occlusion: how much of the sky each point of the high-poly sees. Darker in creases and folds.",
        "Cavity: the high-poly's fine surface curvature. Mid-grey on smooth surfaces, dark in cracks, light on ridges.",
        "Colour transfer: the source's polypaint, or its painted texture.",
    };
    for (int i = 0; i < kBakeMapCount; ++i) {
        if (i % 2) {
            ImGui::SameLine();
            ImGui::SetCursorPosX(x0 + half + style.ItemSpacing.x); // the right column, inside the section indent
        }
        bool on = (b.settings.maps & (1u << i)) != 0;
        if (ImGui::Checkbox(bakeMapLabel(BakeMap(i)), &on)) b.settings.maps = on ? b.settings.maps | (1u << i) : b.settings.maps & ~(1u << i);
        ui::HelpText(bakeMapLabel(BakeMap(i)), nullptr, kMapHelp[i], kPath);
    }

    // Size and anti-aliasing.
    ImGui::SetNextItemWidth(half);
    ImGui::Combo("##bakesize", &b.sizeChoice, kSizeLabels, 4);
    ui::HelpText("Map size", nullptr,
                 "Width and height of the maps: 1K, 2K (the default), 4K or 8K. Bake time and memory grow with the area "
                 "(an 8K bake holds about 200 MB per map while it runs).",
                 kPath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    sliderInt("##bakeaa", &b.settings.samples, 1, 4, "AA %.0fx");
    ui::HelpText("Anti-aliasing", nullptr,
                 "Supersampling: n x n rays per texel, averaged. 1 is fastest; 2 (the default) gives clean edges; 4 costs "
                 "16 rays per texel.",
                 kPath);

    // Ray distances.
    ImGui::Checkbox("Auto distance", &b.settings.autoDistance);
    ui::HelpText("Auto distance", nullptr,
                 "Measure how far the high-poly lies in front of and behind the low-poly before baking, and cast rays just "
                 "that far. Turn it off to set Front and Rear yourself (e.g. when rays hit a nearby part).",
                 kPath);
    ImGui::BeginDisabled(b.settings.autoDistance);
    ImGui::SetNextItemWidth(half);
    ui::SliderInside("##bakefront", &b.frontPct, 0.1f, 20.0f, "Front %.1f%%", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Max Frontal Distance", nullptr,
                 "How far in front of the low-poly rays start, as a percentage of the model's size (its bounding-box "
                 "diagonal). Raise it when details that stick out come out flat or missing.",
                 kPath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##bakerear", &b.rearPct, 0.1f, 20.0f, "Rear %.1f%%", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Max Rear Distance", nullptr,
                 "How far behind the low-poly rays look, as a percentage of the model's size. Raise it when carved detail "
                 "is missing; lower it when rays reach the far side of thin parts.",
                 kPath);
    ImGui::EndDisabled();

    ImGui::SetNextItemWidth(half);
    sliderInt("##bakepad", &b.settings.padding, 0, 32, "Padding %.0f px");
    ui::HelpText("Padding", nullptr,
                 "Texels filled around each UV island with the island's edge colours, so seams don't show once the maps "
                 "are mipmapped in an engine. 8 is plenty for 2K; use more for 4K and 8K.",
                 kPath);
    ImGui::SameLine();
    const char* greens[] = {"OpenGL (Y+)", "DirectX (Y-)"};
    int green = b.settings.flipGreen ? 1 : 0;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##bakegreen", &green, greens, 2)) b.settings.flipGreen = green == 1;
    ui::HelpText("Normal map convention", nullptr,
                 "OpenGL (green up) suits Blender, Unity, Godot and Substance. DirectX (green down) is Unreal's native "
                 "convention; Send to Unreal flips an OpenGL map on import, so either works there.",
                 kPath);

    if (b.settings.maps & bakeBit(BakeMap::AmbientOcclusion)) {
        ImGui::SetNextItemWidth(half);
        sliderInt("##bakeaorays", &b.settings.aoRays, 8, 256, "AO rays %.0f");
        ui::HelpText("AO rays", nullptr, "Rays per texel for ambient occlusion: more is smoother and slower.", kPath);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ui::SliderInside("##bakeaodist", &b.aoDistancePct, 0.5f, 50.0f, "AO reach %.1f%%", ImGuiSliderFlags_Logarithmic);
        ui::HelpText("AO reach", nullptr,
                     "How far away geometry still occludes, as a percentage of the model's size. Short: only creases "
                     "darken. Long: whole cavities and the insides of shapes darken too.",
                     kPath);
    }

    // Output.
    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - style.ItemSpacing.x);
    ImGui::InputTextWithHint("##bakefolder", lastExportFolder_.empty() ? "Output folder (default: project / library)"
                                                                       : "Output folder (default: last export's)",
                             b.folder, sizeof b.folder);
    ui::HelpText("Output folder", nullptr,
                 "Where the maps are written, as <SubTool>_normal.png, _ao.png, _displacement.png (+ .exr) and so on. "
                 "Empty: beside your last export, else the project's folder, else Bakes in your library folder.",
                 kPath);
    ImGui::SameLine();
    ui::ActionButton(*this, "bake.choose_folder", ImVec2(ImGui::GetFrameHeight(), 0), ui::Look::Icon);
    ImGui::Checkbox("EXR displacement", &b.exr);
    ui::HelpText("EXR displacement", nullptr,
                 "Also write the displacement as a 32-bit float OpenEXR in world units (for renderers that take real "
                 "distances). The 16-bit PNG is always written.",
                 kPath);

    // Bake / progress.
    if (b.job) {
        ImGui::ProgressBar(b.job->progress->load(), ImVec2(half, 0), "Baking...");
        ui::HelpText("Baking", nullptr, "Baking on every core in the background; you can keep working.", kPath);
        ImGui::SameLine();
        ui::ActionButton(*this, "bake.cancel", ImVec2(-1, 0));
    } else {
        ui::ActionButton(*this, "bake.run", ImVec2(-1, 0));
    }

    // The last bake.
    if (b.result) {
        const BakeResult& r = *b.result;
        ImGui::TextDisabled("%dx%d in %.1f s from %zu tris", r.width, r.height, r.seconds, r.sourceTriangles);
        if (r.has(BakeMap::Displacement))
            ImGui::TextDisabled("Displacement: mid-grey = 0, white = +%.4g", double(r.displacementScale));
        if (r.missRatio() >= 0.0005f) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::color::danger));
            ImGui::TextWrapped("%.1f%% of texels found no surface: raise Front / Rear", double(100.0f * r.missRatio()));
            ImGui::PopStyleColor();
        }
        ImGui::Checkbox("Preview Normal Map", &b.preview);
        ui::HelpAction(*this, "bake.preview");
        ImGui::SameLine();
        ui::ActionButton(*this, "bake.open_folder", ImVec2(-1, 0));
    }
}

} // namespace sf
