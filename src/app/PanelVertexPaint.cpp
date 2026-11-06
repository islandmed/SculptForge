// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Vertex paint & Materials panel: the Paint brush (Rgb / Raise), colour
// picker and swatches, fills, and the viewport Lit Sphere material picker.
#include "app/App.h"
#include "app/AssetLibrary.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include "core/LitSphere.h"
#include "core/VertexPaint.h"
#include "core/Trash.h"

#include <glm/gtc/type_ptr.hpp>

#include <SDL3/SDL.h>

#include <cfloat>
#include <filesystem>
#include <iterator>

namespace sf {

namespace {

// Every material name set_material takes (custom Lit Spheres included, so it is built when needed).
std::string materialNames() {
    std::string names;
    for (int i = 0; i < litsphereCount(); ++i)
        if (isLitSphereListed(i)) names += std::string(litsphereName(i)) + ", ";
    return names + "studio";
}

std::filesystem::path u8path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string u8string(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

} // namespace

std::string App::litsphereFolder() const { return library_.user.empty() ? std::string() : library_.user + "/" + libraryTabFolder(LibraryTab::Material); }

// Custom Lit Spheres (#15): the studio folders (SCULPTFORGE_LITSPHERE_PATH), then the library's Lit Spheres folder.
void App::loadLitSphereLibrary() {
    for (const std::string& m : loadLitSphereFolders(studioLitSphereDirs())) SDL_Log("Lit Spheres: %s", m.c_str());
    rescanLitSphereFolder();
}

// The Lit Spheres folder as it is now: images that are new or changed (size or time) load, images that are gone leave the
// lists (unless the view uses one). Only lists the folder, so it is cheap to call whenever a material list opens.
void App::rescanLitSphereFolder() {
    const std::string dir = litsphereFolder();
    if (dir.empty()) return;
    namespace fs = std::filesystem;
    std::map<std::string, std::pair<uintmax_t, int64_t>> now;
    std::map<std::string, std::string> groupOf; // path -> its sub-folder ("" = the folder itself)
    // The folder and its sub-folders, one level deep (#124): each sub-folder is a heading in the lists.
    const auto list = [&](const fs::path& folder, const std::string& group) {
        std::error_code ec;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            const std::string path = u8string(it->path());
            if (!isLitSphereFile(path) || !it->is_regular_file(fileEc)) continue;
            const auto size = it->file_size(fileEc);
            const auto time = it->last_write_time(fileEc).time_since_epoch().count();
            now[path] = {size, int64_t(time)};
            groupOf[path] = group;
        }
    };
    list(u8path(dir), "");
    for (const std::string& sub : litsphereSubfolders()) list(u8path(dir) / u8path(sub), sub);
    for (const auto& [path, stamp] : now) {
        if (hiddenLitSphereFiles_.count(path)) continue;
        const auto seen = litsphereFolderSeen_.find(path);
        if (seen != litsphereFolderSeen_.end() && seen->second == stamp) continue;
        CustomLitSphere m;
        std::string err;
        if (readLitSphereImage(path, m, err)) {
            m.group = groupOf[path];
            addCustomLitSphere(std::move(m));
        } else {
            SDL_Log("Lit Spheres: skipped %s", err.c_str());
        }
    }
    for (const auto& [path, stamp] : litsphereFolderSeen_)
        if (!now.count(path))
            if (const int i = findLitSphereByPath(path); i >= 0 && i != renderSettings_.litsphere) removeCustomLitSphere(i);
    litsphereFolderSeen_ = std::move(now);
}

std::vector<std::string> App::litsphereSubfolders() const {
    std::vector<std::string> subs;
    const std::string dir = litsphereFolder();
    if (dir.empty()) return subs;
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::directory_iterator it(u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code dirEc;
        const std::string name = u8string(it->path().filename());
        if (!name.empty() && name[0] != '.' && it->is_directory(dirEc)) subs.push_back(name);
    }
    std::sort(subs.begin(), subs.end());
    return subs;
}

// Why Rename / Move to folder can't change this Lit Sphere's file, or "" when they can: only images in the library's
// Lit Spheres folder (or a sub-folder of it) that SculptForge may write are ours to rename.
std::string App::litsphereLocked(int index) const {
    const CustomLitSphere* c = customLitSphere(index);
    if (!c) return "Built-in Lit Spheres can't be renamed or moved.";
    if (std::find(packs_.litspheres.begin(), packs_.litspheres.end(), index) != packs_.litspheres.end())
        return "This Lit Sphere comes from a pack, which stays as it was installed.";
    if (c->path.empty())
        return "This Lit Sphere came with the open project and has no file of its own. Load Lit Sphere... with its image "
               "to keep it in your library.";
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path file = u8path(c->path), parent = file.parent_path();
    const bool ours = !litsphereFolder().empty() && (fs::equivalent(parent, u8path(litsphereFolder()), ec) ||
                                                     fs::equivalent(parent.parent_path(), u8path(litsphereFolder()), ec));
    if (!ours) {
        for (const std::string& d : studioLitSphereDirs())
            if (fs::equivalent(parent, u8path(d), ec))
                return "This Lit Sphere comes from a studio folder (SCULPTFORGE_LITSPHERE_PATH), which SculptForge only reads.";
        return "Only Lit Spheres in your library's Lit Spheres folder can be renamed or moved.";
    }
    const fs::perms p = fs::status(file, ec).permissions();
    if (!ec && (p & fs::perms::owner_write) == fs::perms::none) return "Its file is read-only.";
    return {};
}

// Moves the image of a custom Lit Sphere in the Lit Spheres folder to `to` (no overwrite) and keeps the lists in step.
bool App::moveLitSphereFile(int index, const std::string& to, const std::string& group, std::string& error) {
    namespace fs = std::filesystem;
    const std::string from = customLitSphere(index)->path;
    std::error_code ec;
    if (fs::exists(u8path(to), ec) && !fs::equivalent(u8path(from), u8path(to), ec)) { // (equivalent: only the case changes)
        error = u8string(u8path(to).filename()) + " is already there";
        return false;
    }
    fs::create_directories(u8path(to).parent_path(), ec);
    ec.clear();
    fs::rename(u8path(from), u8path(to), ec);
    if (ec) {
        error = "Couldn't move " + from + ": " + ec.message();
        return false;
    }
    relocateCustomLitSphere(index, to, group);
    if (const auto seen = litsphereFolderSeen_.find(from); seen != litsphereFolderSeen_.end()) { // not new on the next scan
        const auto stamp = seen->second;
        litsphereFolderSeen_.erase(seen);
        litsphereFolderSeen_[to] = stamp;
    }
    return true;
}

// Material lists (palette, picker, Library) call this every frame they show: the first frame after a gap rescans.
void App::materialListShown() {
    const int frame = ImGui::GetFrameCount();
    if (frame > materialListFrame_ + 1) rescanLitSphereFolder();
    materialListFrame_ = frame;
}

void App::registerPaintUiCommands() {
    // A view setting, so it lives with the app (scripts: --script "set_material name=Chrome").
    commands_.add({"set_material",
                   "Viewport material: a built-in Lit Sphere (Sculpt, the default, is made for reading strokes), one of "
                   "your own (loaded with Load Lit Sphere) or studio (soft clay lighting in the Base colour). Vertex Paint multiplies "
                   "the lit sphere; cavity shading stays on.",
                   {{"name", ParamSpec::Type::String, "A Lit Sphere's name (Soft Clay, Sculpt, Red Wax, Clay, Skin, Chrome, "
                                                      "White, Bronze or a custom one) or studio.", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const std::string name = argString(a, "name");
                       if (name == "studio" || name == "Studio") {
                           renderSettings_.litsphere = -1;
                           return {true, "Material: Studio"};
                       }
                       const int index = findLitSphere(name);
                       if (index < 0) return {false, "Unknown material '" + name + "' (" + materialNames() + ")"};
                       renderSettings_.litsphere = index;
                       return {true, std::string("Material: ") + litsphereName(index)};
                   }});
    commands_.add({"load_litsphere",
                   "Load a Lit Sphere: a lit-sphere image (PNG, JPG or EXR; a non-square image is cropped to its centre) "
                   "becomes a viewport material named after the file, and the view uses it. It is copied into the "
                   "library's Lit Spheres folder (Documents/SculptForge/Lit Spheres), so it is there next time; projects "
                   "that use it carry a copy.",
                   {{"path", ParamSpec::Type::String, "The image file.", true},
                    {"keep", ParamSpec::Type::Bool, "Copy it into the library's Lit Spheres folder (default true).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       CustomLitSphere m;
                       std::string err;
                       if (!readLitSphereImage(argString(a, "path"), m, err)) return {false, err};
                       std::string kept;
                       if (argBool(a, "keep", true) && !litsphereFolder().empty()) {
                           namespace fs = std::filesystem;
                           const fs::path from = u8path(m.path), to = u8path(litsphereFolder()) / from.filename();
                           std::error_code ec;
                           fs::create_directories(to.parent_path(), ec);
                           if (!fs::equivalent(from, to, ec)) {
                               ec.clear();
                               fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
                           }
                           if (ec) {
                               kept = " (not kept: " + ec.message() + ")";
                           } else {
                               const std::u8string s = to.u8string();
                               m.path.assign(s.begin(), s.end());
                               kept = " (kept in the Lit Spheres folder)";
                           }
                       }
                       hiddenLitSphereFiles_.erase(m.path);
                       const int index = addCustomLitSphere(std::move(m));
                       renderSettings_.litsphere = index;
                       return {true, std::string("Material: ") + litsphereName(index) + kept};
                   }});
    commands_.add({"remove_litsphere",
                   "Remove one of your Lit Spheres from the material lists. One from the library's Lit Spheres folder goes to "
                   "the trash (the Recycle Bin on Windows); where that isn't possible (macOS for now) it is hidden "
                   "for this session and its file stays put. Built-in Lit Spheres can't be removed.",
                   {{"name", ParamSpec::Type::String, "The Lit Sphere's name.", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const std::string name = argString(a, "name");
                       const int index = findLitSphere(name);
                       if (index < 0) return {false, "No Lit Sphere called '" + name + "'"};
                       if (!isCustomLitSphere(index)) return {false, std::string(litsphereName(index)) + " is built in"};
                       const std::string shown = litsphereName(index), path = customLitSphere(index)->path;
                       namespace fs = std::filesystem;
                       std::error_code ec;
                       const bool inFolder = !path.empty() && !litsphereFolder().empty() &&
                                             fs::equivalent(u8path(path).parent_path(), u8path(litsphereFolder()), ec);
                       if (renderSettings_.litsphere == index) renderSettings_.litsphere = defaultLitSphere();
                       removeCustomLitSphere(index);
                       if (!inFolder)
                           return {true, "Removed " + shown + (path.empty() ? "" : " (its image stays at " + path + ")")};
                       std::string where, err;
                       if (moveToTrash(path, where, err)) {
                           litsphereFolderSeen_.erase(path);
                           return {true, "Removed " + shown + ": " + u8string(u8path(path).filename()) + " is in " + where};
                       }
                       hiddenLitSphereFiles_.insert(path); // a rescan must not bring it back
                       return {true, "Removed " + shown + " for this session; delete " + path +
                                         " to remove it for good (" + err + ")"};
                   }});
    commands_.add({"rename_litsphere",
                   "Rename one of your Lit Spheres (#124): its image in the library's Lit Spheres folder is renamed too. "
                   "Projects that use it still find it (they match its pixels). Built-in, studio-folder, pack and "
                   "project-only Lit Spheres can't be renamed; a name another Lit Sphere has is refused.",
                   {{"name", ParamSpec::Type::String, "The Lit Sphere's name.", true},
                    {"to", ParamSpec::Type::String, "The new name (also the file name, without its extension).", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const std::string name = argString(a, "name"), to = argString(a, "to");
                       const int index = findLitSphere(name);
                       if (index < 0) return {false, "No Lit Sphere called '" + name + "'"};
                       if (const std::string why = litsphereLocked(index); !why.empty()) return {false, why};
                       const std::string old = litsphereName(index);
                       if (to == old) return {true, "Unchanged: " + old};
                       std::string err;
                       if (!checkLitSphereFileName(to, err)) return {false, err};
                       if (const int other = findLitSphere(to); other >= 0 && other != index)
                           return {false, std::string("There is already a Lit Sphere called ") + litsphereName(other)};
                       namespace fs = std::filesystem;
                       const CustomLitSphere* c = customLitSphere(index);
                       const fs::path from = u8path(c->path);
                       const std::string dest = u8string(from.parent_path() / u8path(to + u8string(from.extension())));
                       if (!moveLitSphereFile(index, dest, c->group, err) || !renameCustomLitSphere(index, to, err))
                           return {false, err};
                       return {true, "Renamed " + old + " to " + to};
                   }});
    commands_.add({"move_litsphere",
                   "Move one of your Lit Spheres into a sub-folder of the library's Lit Spheres folder (#124), which the "
                   "material lists show as a heading; the folder is made if it isn't there. An empty folder moves it "
                   "back to the Lit Spheres folder itself.",
                   {{"name", ParamSpec::Type::String, "The Lit Sphere's name.", true},
                    {"folder", ParamSpec::Type::String, "The sub-folder's name (one level; empty: none).", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const std::string name = argString(a, "name"), folder = argString(a, "folder");
                       const int index = findLitSphere(name);
                       if (index < 0) return {false, "No Lit Sphere called '" + name + "'"};
                       if (const std::string why = litsphereLocked(index); !why.empty()) return {false, why};
                       std::string err;
                       if (!folder.empty() && !checkLitSphereFileName(folder, err)) return {false, "Folder: " + err};
                       namespace fs = std::filesystem;
                       const CustomLitSphere* c = customLitSphere(index);
                       if (c->group == folder) return {true, std::string(litsphereName(index)) + " is already there"};
                       fs::path dir = u8path(litsphereFolder());
                       if (!folder.empty()) dir /= u8path(folder);
                       if (!moveLitSphereFile(index, u8string(dir / u8path(c->path).filename()), folder, err))
                           return {false, err};
                       ++materialListFrame_; // (nothing else to redo: the lists group by the registry)
                       return {true, std::string("Moved ") + litsphereName(index) + " to " +
                                         (folder.empty() ? std::string("Lit Spheres") : folder)};
                   }});
    // Viewport shading: Lit Sphere or PBR (render/Renderer.h RenderSettings, core/EnvironmentMath.h).
    commands_.add({"viewport_shading",
                   "Viewport shading: lit_sphere (the material sphere or studio lights, the default) or pbr (the "
                   "texture's material channels lit by an environment: GGX reflections, diffuse light from the "
                   "environment, exposure and a tone map). Only changes what the view shows.",
                   {{"mode", ParamSpec::Type::String, "lit_sphere or pbr (default: unchanged).", false},
                    {"environment", ParamSpec::Type::String,
                     "studio, photo_studio, small_studio, outdoor_sky, or a file name from SCULPTFORGE_HDRI_PATH.", false},
                    {"rotation", ParamSpec::Type::Number, "Environment rotation in degrees about the up axis.", false},
                    {"exposure", ParamSpec::Type::Number, "Exposure in stops (-6 to 6; default 0).", false},
                    {"tone_map", ParamSpec::Type::String, "aces (default), agx, reinhard or none.", false},
                    {"background", ParamSpec::Type::Bool, "Show the environment behind the model.", false},
                    {"blur", ParamSpec::Type::Number, "Background blur, 0 (sharp) to 1.", false},
                    {"roughness", ParamSpec::Type::Number, "Roughness of meshes without a material texture (0-1).", false},
                    {"metallic", ParamSpec::Type::Number, "Metallic of meshes without a material texture (0-1).", false},
                    {"cavity", ParamSpec::Type::Number, "How much creases darken under PBR, 0-1 (default 0.5).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       RenderSettings v = renderSettings_;
                       if (a.contains("mode")) {
                           const std::string m = argString(a, "mode");
                           if (m == "pbr") v.shading = RenderSettings::Shading::Pbr;
                           else if (m == "lit_sphere" || m == "litsphere" || m == "lit")
                               v.shading = RenderSettings::Shading::LitSphere;
                           else return {false, "mode must be lit_sphere or pbr"};
                       }
                       if (a.contains("environment")) {
                           const int e = findEnvironment(argString(a, "environment"));
                           if (e < 0) {
                               std::string names;
                               for (const EnvironmentInfo& i : environments()) names += (names.empty() ? "" : ", ") + i.id;
                               return {false, "Unknown environment (" + names + ")"};
                           }
                           v.environment = e;
                       }
                       if (a.contains("tone_map") && !parseToneMap(argString(a, "tone_map"), v.toneMap))
                           return {false, "tone_map must be aces, agx, reinhard or none"};
                       v.environmentRotation = float(std::fmod(argNumber(a, "rotation", v.environmentRotation), 360.0));
                       v.exposure = std::clamp(float(argNumber(a, "exposure", v.exposure)), -6.0f, 6.0f);
                       v.environmentBackground = argBool(a, "background", v.environmentBackground);
                       v.backgroundBlur = std::clamp(float(argNumber(a, "blur", v.backgroundBlur)), 0.0f, 1.0f);
                       v.pbrRoughness = std::clamp(float(argNumber(a, "roughness", v.pbrRoughness)), 0.0f, 1.0f);
                       v.pbrMetallic = std::clamp(float(argNumber(a, "metallic", v.pbrMetallic)), 0.0f, 1.0f);
                       v.pbrCavity = std::clamp(float(argNumber(a, "cavity", v.pbrCavity)), 0.0f, 1.0f);
                       renderSettings_ = v;
                       if (!v.pbr()) return {true, "Viewport shading: Lit Sphere"};
                       char buf[200];
                       std::snprintf(buf, sizeof buf, "Viewport shading: PBR, %s, %+.1f EV, %s%s",
                                     environments()[size_t(v.environment)].label.c_str(), double(v.exposure),
                                     toneMapLabel(v.toneMap), v.environmentBackground ? ", background on" : "");
                       return {true, buf};
                   }});
    // The view's background (#21). Also the preference for new sessions.
    commands_.add({"set_background",
                   "The viewport background: gradient (the default charcoal radial gradient) or flat, one solid "
                   "colour.",
                   {{"mode", ParamSpec::Type::String, "gradient or flat (default: flat when a color is given).", false},
                    {"color", ParamSpec::Type::String,
                     "The flat colour: #rrggbb, r,g,b (0-1 or 0-255) or a name such as black or gray.", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       if (!a.contains("mode") && !a.contains("color")) return {false, "Give mode= or color="};
                       glm::vec3 color = renderSettings_.backgroundColor;
                       if (a.contains("color")) {
                           glm::vec3 linear;
                           if (!parseColor(argString(a, "color"), linear))
                               return {false, "Bad colour '" + argString(a, "color") + "'"};
                           color = linearToSrgb(linear); // the background is drawn in display values
                       }
                       const std::string mode = argString(a, "mode", a.contains("color") ? "flat" : "");
                       if (mode != "gradient" && mode != "flat")
                           return {false, "Unknown background '" + mode + "' (gradient, flat)"};
                       setBackground(mode == "gradient", color);
                       return {true, mode == "gradient" ? std::string("Background: Gradient")
                                                        : "Background: Flat Colour " + colorToHex(srgbToLinear(color))};
                   }});
    // The scene's shading; Parts can override it (part_shading). Also the preference for new sessions.
    commands_.add({"set_shading",
                   "Viewport shading for every Part that doesn't set its own (part_shading): smooth (the "
                   "default) or flat, where each polygon shows its own face, so the mesh's structure reads.",
                   {{"mode", ParamSpec::Type::String, "smooth or flat.", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       Shading mode;
                       if (!parseShading(argString(a, "mode"), mode) || mode == Shading::Default)
                           return {false, "Unknown shading '" + argString(a, "mode") + "' (smooth, flat)"};
                       setFlatShading(mode == Shading::Flat);
                       return {true, mode == Shading::Flat ? "Shading: Flat" : "Shading: Smooth"};
                   }});
}

void App::setBackground(bool gradient, const glm::vec3& color) {
    renderSettings_.gradientBackground = uiPrefs_.gradientBackground = gradient;
    renderSettings_.backgroundColor = uiPrefs_.backgroundColor = glm::clamp(color, 0.0f, 1.0f);
}

void App::drawBackgroundControls(const char* idSuffix) {
    ImGui::TextDisabled("%s", tr("Background"));
    ui::Segmented(*this, {"view.background_gradient", "view.background_flat"});
    // The picker edits the flat colour as shown (display values); picking one switches to Flat Colour.
    glm::vec3 c = renderSettings_.backgroundColor;
    const std::string label = std::string("Background colour##") + idSuffix;
    if (ImGui::ColorEdit3(label.c_str(), &c.x, ImGuiColorEditFlags_NoInputs)) setBackground(false, c);
    ui::HelpText("Background colour", nullptr,
                 "The view's flat background colour. Choosing one switches the background to Flat Colour. It is "
                 "saved automatically for the next session; projects keep their own.",
                 "Preferences > Display");
}

void App::setFlatShading(bool flat) {
    renderSettings_.flatShading = flat;
    uiPrefs_.flatShading = flat; // the default for the next session (saved automatically)
}

const BrushRecipe* App::strokeRecipe() const {
    return strokeBrush_ == BrushType::Smooth || !strokeSculpt_ || strokeMask_ ? nullptr : activeRecipe();
}

void App::paintAt(const DabParams& dab) {
    DabParams p = dab;
    p.strength = rgbIntensity_ * dabStrengthScale_; // paint has its own strength (Colour intensity)
    if (texturePainter_.active()) { // the brush material: colour plus the material channels it paints
        BrushMaterial m = doc_.paintMaterial;
        m.color = doc_.paintColor;
        texturePainter_.dab(doc_.mesh, doc_.bvh, p, m, symmetry_);
    }
    else if (!paintsTexture(doc_.mesh)) paintDab(doc_.sculptor, doc_.mesh, doc_.bvh, p, doc_.paintColor, symmetry_);
}

void App::beginPaintStroke() {
    paintSnapshot_.reset();
    const Mesh& m = doc_.mesh;
    if (strokePaint_ && paintsTexture(m)) { // texture painting: tiles go into the stroke's undo step
        if (!texturePainter_.begin(doc_.mesh, doc_.paintMaterial.channels)) {
            const PaintTexture& t = *m.texture;
            if (!(doc_.paintMaterial.channels & enabledChannelMask(t)))
                status_ = "The brush paints no channel this texture has (Color > Material)";
            else if (t.activeLayer >= 0 && t.activeLayer < int(t.layers.size()))
                status_ = "Texture layer '" + t.layers[size_t(t.activeLayer)].name + "' is " +
                          (t.layers[size_t(t.activeLayer)].locked ? "locked" : "hidden");
        }
        return;
    }
    if (strokePaint_ && !m.paintLayers.empty() && m.activePaintLayer >= 0 &&
        m.activePaintLayer < int(m.paintLayers.size()) && !paintLayerEditable(m, m.activePaintLayer)) {
        const PaintLayer& l = m.paintLayers[size_t(m.activePaintLayer)];
        status_ = "Paint layer '" + l.name + "' is " + (l.locked ? "locked" : "hidden");
    }
    // (Only the paint stack: a copy of the whole mesh would be gigabytes at tens of millions of polygons.)
    if (strokePaint_ && !doc_.mesh.hasColors()) paintSnapshot_ = std::make_unique<PaintStackUndo>(PaintStackUndo::capture(doc_.mesh));
}

void App::finishPaintStroke(UndoRecord& record) {
    if (texturePainter_.active()) texturePainter_.end(record);
    if (!paintSnapshot_) return;
    // The colours the stroke recorded are the white it started from; restoring "no vertex paint" replaces them.
    record.colors = {};
    record.paintAlpha = {};
    record.paint = std::move(paintSnapshot_);
}

void App::pickColorUnderCursor() {
    doc_.syncBvh();
    RayHit hit;
    if (!pick(mouseX_, mouseY_, hit)) return;
    if (paintsTexture(doc_.mesh)) {
        doc_.paintColor = sampleTextureAt(doc_.mesh, hit.triangle, hit.position);
        if (hasMaterialChannels(*doc_.mesh.texture)) {
            // C picks every channel: the brush material becomes the texture's material under the cursor, and the
            // brush then paints all of the texture's channels.
            BrushMaterial picked = doc_.paintMaterial;
            sampleMaterialAt(doc_.mesh, hit.triangle, hit.position, picked);
            doc_.paintMaterial = picked;
            doc_.paintMaterial.color = doc_.paintColor;
            char buf[160];
            std::snprintf(buf, sizeof buf, "Picked the material: %s, roughness %.2f, metallic %.2f (every channel)",
                          colorToHex(doc_.paintColor).c_str(), double(picked.roughness), double(picked.metallic));
            status_ = buf;
            return;
        }
        status_ = "Paint colour " + colorToHex(doc_.paintColor) + " (from the texture)";
        return;
    }
    if (!doc_.mesh.hasColors()) {
        status_ = "The mesh has no vertex paint to pick from";
        return;
    }
    doc_.paintColor = sampleColor(doc_.mesh, hit.triangle, hit.position);
    status_ = "Paint colour " + colorToHex(doc_.paintColor);
}

// Color palette: the Paint brush (Rgb / Raise), the colour, swatches and fills.
void App::drawPalette_Color() {
    ImGui::SeparatorText(tr("Paint"));
    const bool painting = paintMode_;
    if (painting) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(tr("Paint brush (P)"), ImVec2(-1, 0))) paintMode_ = !paintMode_;
    if (painting) ImGui::PopStyleColor();
    ui::HelpAction(*this, "draw.paint");
    ImGui::Checkbox(tr("RGB while sculpting"), &paintRgb_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Sculpt brushes also paint the colour"));

    // The picker edits display (sRGB) values; the mesh stores linear colour.
    glm::vec3 srgb = linearToSrgb(doc_.paintColor);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::ColorPicker3("##paintcolor", glm::value_ptr(srgb),
                            ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview |
                                ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_DisplayHex))
        doc_.paintColor = srgbToLinear(srgb);
    static const glm::vec3 kSwatches[] = {{1.0f, 1.0f, 1.0f},   {0.1f, 0.1f, 0.1f},   {0.85f, 0.12f, 0.1f},
                                          {0.95f, 0.5f, 0.12f}, {0.95f, 0.85f, 0.2f}, {0.2f, 0.65f, 0.25f},
                                          {0.15f, 0.35f, 0.85f}, {0.5f, 0.25f, 0.7f}, {0.87f, 0.66f, 0.55f},
                                          {0.45f, 0.28f, 0.16f}};
    const float swatch = (ImGui::GetContentRegionAvail().x - 4 * ImGui::GetStyle().ItemSpacing.x) / 5.0f;
    for (int i = 0; i < int(std::size(kSwatches)); ++i) {
        const glm::vec3& c = kSwatches[i];
        ImGui::PushID(i);
        if (ImGui::ColorButton("##swatch", ImVec4(c.x, c.y, c.z, 1.0f), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(swatch, swatch * 0.6f)))
            doc_.paintColor = srgbToLinear(c);
        ImGui::PopID();
        if (i % 5 != 4) ImGui::SameLine();
    }
    ImGui::TextDisabled(tr(paintsTexture(doc_.mesh) && hasMaterialChannels(*doc_.mesh.texture) ? "C over the model picks its material" : "C over the model picks its colour"));
    drawBrushMaterialContents(); // per-channel brush values when the texture has material channels (PanelPbr.cpp)
    {
        const Mesh& m = doc_.mesh;
        const bool layered = !m.paintLayers.empty() && m.activePaintLayer >= 0 && m.activePaintLayer < int(m.paintLayers.size());
        if (hasTexture(m)) {
            // Where paint goes, when the Part has a texture (Tool > Texture Map).
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", tr("Paint into"));
            ImGui::SameLine();
            ui::Segmented(*this, {"draw.paint_vertex", "draw.paint_texture"});
        } else {
            // No texture yet: one click starts texture painting (Auto UV first when there are no UVs).
            ui::ActionButton(*this, "draw.new_texture", ImVec2(-1, 0));
        }
        if (paintsTexture(m)) {
            const PaintTexture& t = *m.texture;
            const bool texLayered = t.activeLayer >= 0 && t.activeLayer < int(t.layers.size());
            ImGui::TextDisabled(tr("Painting on: texture, %s"), texLayered ? t.layers[size_t(t.activeLayer)].name.c_str() : "Base");
        } else
        ImGui::TextDisabled(tr("Painting on: %s"), layered ? m.paintLayers[size_t(m.activePaintLayer)].name.c_str() : "Base");
        ui::HelpText("Paint layer", nullptr,
                     "The paint layer the Paint brush and fills write into. Add and arrange layers in Tool > Paint Layers.",
                     "Color");
    }

    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const std::string hex = colorToHex(doc_.paintColor);
    if (ImGui::Button(tr("Fill"), ImVec2(half, 0))) runCommand("paint_fill", {{"color", hex}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Fill the unmasked area (the whole mesh if nothing is masked)"));
    ImGui::SameLine();
    ImGui::BeginDisabled(!doc_.mesh.hasMask());
    if (ImGui::Button(tr("Fill masked"), ImVec2(half, 0)))
        runCommand("paint_fill", {{"color", hex}, {"area", std::string("masked")}});
    ImGui::EndDisabled();
    if (ImGui::Button(tr("Cavity"), ImVec2(half, 0))) runCommand("paint_from_cavity");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Darken creases and cavities (respects the mask)"));
    ImGui::SameLine();
    ImGui::BeginDisabled(!doc_.mesh.hasColors() && !paintsTexture(doc_.mesh));
    if (ImGui::Button(tr("Clear"), ImVec2(half, 0))) runCommand("paint_clear");
    ImGui::EndDisabled();
    ui::HelpText("Clear", nullptr, "Remove all vertex paint from the Part, paint layers included (undoable).", "Color");
    drawLineGradientControls(); // LineGradient.cpp
    ImGui::Checkbox(tr("Show vertex paint"), &renderSettings_.showVertexPaint);
    ImGui::Checkbox(tr("Colour tints the material"), &renderSettings_.colorTintsMaterial);
    ui::HelpText("Colour tints the material", nullptr,
                 "On a Part with no vertex paint, the colour above tints its Lit Sphere material, so you can try a "
                 "skin, stone or metal tone without painting. Grey and black leave the material as it is. Vertex "
                 "paint, once there, shows instead.",
                 "Color");
    if (hasTexture(doc_.mesh)) {
        ImGui::SameLine();
        ImGui::Checkbox(tr("Texture##show"), &renderSettings_.showTexture);
        ui::HelpAction(*this, "view.texture");
    }
}

// Material palette: the material grid, the studio material's colour and the shading (the scene's and the active
// Part's own).
void App::drawPalette_Material() {
    sf::drawMaterialGrid(*this, GridMode::Palette); // Pickers.cpp, as in the material picker (M)
    if (ImGui::Button(tr("Load Lit Sphere..."), ImVec2(-1, 0))) loadLitSphereDialog();
    ui::HelpText("Load Lit Sphere", nullptr,
                 "Use a Lit Sphere image of your own: a lit sphere filling a square PNG, JPG or EXR. It joins the list "
                 "and is kept in Documents/SculptForge/Lit Spheres; images you put in that folder show up when a material list "
                 "opens. Right-click one of your Lit Spheres to remove it.",
                 "Material");
    ImGui::ColorEdit3(tr("Base colour"), &renderSettings_.baseColor.x, ImGuiColorEditFlags_NoInputs);
    ui::HelpText("Base colour", nullptr, "Colour of the Studio material (the clay colour).", "Material");
    drawShadingControls();
    drawViewportShadingControls();
    drawLightControls(); // PanelView.cpp
    ImGui::Spacing();
    drawSaveRenderControls();
}

// Material > Viewport Shading: Lit Sphere or PBR, and the PBR view's environment. Every change runs
// viewport_shading, so scripts and the console see the same thing.
void App::drawViewportShadingControls() {
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("Viewport Shading"));
    ui::Segmented(*this, {"view.shading_lit_sphere", "view.shading_pbr"});
    if (!renderSettings_.pbr()) return;
    RenderSettings& v = renderSettings_;
    const auto& envs = environments();
    const int current = std::clamp(v.environment, 0, int(envs.size()) - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##environment", envs[size_t(current)].label.c_str())) {
        for (int i = 0; i < int(envs.size()); ++i) {
            if (ImGui::Selectable(envs[size_t(i)].label.c_str(), i == current))
                runCommand("viewport_shading", {{"environment", envs[size_t(i)].id}});
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", envs[size_t(i)].credit.c_str());
        }
        ImGui::EndCombo();
    }
    ui::HelpText("Environment", nullptr,
                 "The light around the model: Studio (generated), Photo Studio, Small Studio and Outdoor Sky. HDR "
                 "files (.hdr, .exr) in the folders of SCULPTFORGE_HDRI_PATH are listed after them.",
                 "Material > Viewport Shading");
    // Sliders change the view live and run the command once when let go (one console line per change).
    auto slider = [&](const char* label, const char* key, float* value, float lo, float hi, const char* fmt, const char* help) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat(label, value, lo, hi, fmt);
        if (ImGui::IsItemDeactivatedAfterEdit()) runCommand("viewport_shading", {{key, double(*value)}});
        ui::HelpText(label + 2, nullptr, help, "Material > Viewport Shading");
    };
    slider("##Exposure", "exposure", &v.exposure, -6.0f, 6.0f, tr("Exposure %+.1f EV"),
           "Brightness of the light in stops: +1 doubles it.");
    slider("##Rotation", "rotation", &v.environmentRotation, -180.0f, 180.0f, tr("Rotation %.0f deg"),
           "Turns the environment about the up axis, moving the highlights.");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##tonemap", (std::string(tr("Tone map")) + ": " + toneMapLabel(v.toneMap)).c_str())) {
        for (int i = 0; i < kToneMapCount; ++i)
            if (ImGui::Selectable(toneMapLabel(ToneMap(i)), int(v.toneMap) == i))
                runCommand("viewport_shading", {{"tone_map", std::string(toneMapId(ToneMap(i)))}});
        ImGui::EndCombo();
    }
    ui::HelpText("Tone map", nullptr,
                 "How bright light maps to the screen: ACES (filmic contrast), AgX (softer, keeps bright colours from "
                 "going flat), Reinhard (gentle) or None.",
                 "Material > Viewport Shading");
    bool background = v.environmentBackground;
    if (ui::Checkbox("Show environment##envbg", &background))
        runCommand("viewport_shading", {{"background", background}});
    ui::HelpText("Show environment", nullptr, "Show the environment behind the model instead of the background.",
                 "Material > Viewport Shading");
    if (v.environmentBackground)
        slider("##Blur", "blur", &v.backgroundBlur, 0.0f, 1.0f, tr("Blur %.2f"), "How soft the environment behind the model is.");
    slider("##Roughness", "roughness", &v.pbrRoughness, 0.0f, 1.0f, tr("Roughness %.2f"),
           "Roughness of Parts without a material texture (their colour comes from vertex paint or the base colour).");
    slider("##Metallic", "metallic", &v.pbrMetallic, 0.0f, 1.0f, tr("Metallic %.2f"),
           "Metallic of Parts without a material texture.");
    slider("##Cavity", "cavity", &v.pbrCavity, 0.0f, 1.0f, tr("Cavity %.2f"),
           "How much the sculpt's creases darken, so detail reads under even light. 0 is plain PBR.");
}

void App::drawShadingControls() {
    ImGui::TextDisabled("%s", tr("Shading"));
    ui::Segmented(*this, {"material.smooth", "material.flat"});
    ImGui::TextDisabled("%s", tr("This Part"));
    ui::Segmented(*this, {"part.shading_default", "part.shading_smooth", "part.shading_flat"});
}

} // namespace sf
