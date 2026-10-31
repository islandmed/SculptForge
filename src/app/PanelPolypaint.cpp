// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polypaint & Materials panel: the Paint brush (ZBrush Rgb / Zadd), colour
// picker and swatches, fills, and the viewport MatCap material picker.
#include "app/App.h"
#include "app/Ui.h"

#include "core/Matcap.h"
#include "core/Polypaint.h"
#include "core/Trash.h"

#include <glm/gtc/type_ptr.hpp>

#include <SDL3/SDL.h>

#include <cfloat>
#include <filesystem>
#include <iterator>

namespace sf {

namespace {

// Every material name set_material takes (custom MatCaps included, so it is built when needed).
std::string materialNames() {
    std::string names;
    for (int i = 0; i < matcapCount(); ++i)
        if (isMatcapListed(i)) names += std::string(matcapName(i)) + ", ";
    return names + "studio";
}

std::filesystem::path u8path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string u8string(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

} // namespace

std::string App::matcapFolder() const { return library_.user.empty() ? std::string() : library_.user + "/MatCaps"; }

// Custom MatCaps (#15): the studio folders (SCULPTFORGE_MATCAP_PATH), then the library's MatCaps folder.
void App::loadMatcapLibrary() {
    for (const std::string& m : loadMatcapFolders(studioMatcapDirs())) SDL_Log("MatCaps: %s", m.c_str());
    rescanMatcapFolder();
}

// The MatCaps folder as it is now: images that are new or changed (size or time) load, images that are gone leave the
// lists (unless the view uses one). Only lists the folder, so it is cheap to call whenever a material list opens.
void App::rescanMatcapFolder() {
    const std::string dir = matcapFolder();
    if (dir.empty()) return;
    namespace fs = std::filesystem;
    std::map<std::string, std::pair<uintmax_t, int64_t>> now;
    std::error_code ec;
    for (fs::directory_iterator it(u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        const std::string path = u8string(it->path());
        if (!isMatcapFile(path) || !it->is_regular_file(fileEc)) continue;
        const auto size = it->file_size(fileEc);
        const auto time = it->last_write_time(fileEc).time_since_epoch().count();
        now[path] = {size, int64_t(time)};
    }
    for (const auto& [path, stamp] : now) {
        if (hiddenMatcapFiles_.count(path)) continue;
        const auto seen = matcapFolderSeen_.find(path);
        if (seen != matcapFolderSeen_.end() && seen->second == stamp) continue;
        CustomMatcap m;
        std::string err;
        if (readMatcapImage(path, m, err)) addCustomMatcap(std::move(m));
        else SDL_Log("MatCaps: skipped %s", err.c_str());
    }
    for (const auto& [path, stamp] : matcapFolderSeen_)
        if (!now.count(path))
            if (const int i = findMatcapByPath(path); i >= 0 && i != renderSettings_.matcap) removeCustomMatcap(i);
    matcapFolderSeen_ = std::move(now);
}

// Material lists (palette, picker, LightBox) call this every frame they show: the first frame after a gap rescans.
void App::materialListShown() {
    const int frame = ImGui::GetFrameCount();
    if (frame > materialListFrame_ + 1) rescanMatcapFolder();
    materialListFrame_ = frame;
}

void App::registerPaintUiCommands() {
    // A view setting, so it lives with the app (scripts: --script "set_material name=Chrome").
    commands_.add({"set_material",
                   "Viewport material: a built-in MatCap (Sculpt, the default, is made for reading strokes), one of "
                   "your own (load_matcap) or studio (soft clay lighting in the Base colour). Polypaint multiplies "
                   "the matcap; cavity shading stays on.",
                   {{"name", ParamSpec::Type::String, "A MatCap's name (Soft Clay, Sculpt, Red Wax, Clay, Skin, Chrome, "
                                                      "White, Bronze or a custom one) or studio.", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const std::string name = argString(a, "name");
                       if (name == "studio" || name == "Studio") {
                           renderSettings_.matcap = -1;
                           return {true, "Material: Studio"};
                       }
                       const int index = findMatcap(name);
                       if (index < 0) return {false, "Unknown material '" + name + "' (" + materialNames() + ")"};
                       renderSettings_.matcap = index;
                       return {true, std::string("Material: ") + matcapName(index)};
                   }});
    commands_.add({"load_matcap",
                   "Load a MatCap: a lit-sphere image (PNG, JPG or EXR; a non-square image is cropped to its centre) "
                   "becomes a viewport material named after the file, and the view uses it. It is copied into the "
                   "library's MatCaps folder (Documents/SculptForge/MatCaps), so it is there next time; projects "
                   "that use it carry a copy.",
                   {{"path", ParamSpec::Type::String, "The image file.", true},
                    {"keep", ParamSpec::Type::Bool, "Copy it into the MatCaps folder (default true).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       CustomMatcap m;
                       std::string err;
                       if (!readMatcapImage(argString(a, "path"), m, err)) return {false, err};
                       std::string kept;
                       if (argBool(a, "keep", true) && !matcapFolder().empty()) {
                           namespace fs = std::filesystem;
                           const fs::path from = u8path(m.path), to = u8path(matcapFolder()) / from.filename();
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
                               kept = " (kept in MatCaps)";
                           }
                       }
                       hiddenMatcapFiles_.erase(m.path);
                       const int index = addCustomMatcap(std::move(m));
                       renderSettings_.matcap = index;
                       return {true, std::string("Material: ") + matcapName(index) + kept};
                   }});
    commands_.add({"remove_matcap",
                   "Remove one of your MatCaps from the material lists. One from the library's MatCaps folder goes to "
                   "the trash (the Recycle Bin on Windows); where that isn't possible (macOS for now) it is hidden "
                   "for this session and its file stays put. Built-in MatCaps can't be removed.",
                   {{"name", ParamSpec::Type::String, "The MatCap's name.", true}},
                   [this](Document&, const Args& a) -> CommandResult {
                       const std::string name = argString(a, "name");
                       const int index = findMatcap(name);
                       if (index < 0) return {false, "No MatCap called '" + name + "'"};
                       if (!isCustomMatcap(index)) return {false, std::string(matcapName(index)) + " is built in"};
                       const std::string shown = matcapName(index), path = customMatcap(index)->path;
                       namespace fs = std::filesystem;
                       std::error_code ec;
                       const bool inFolder = !path.empty() && !matcapFolder().empty() &&
                                             fs::equivalent(u8path(path).parent_path(), u8path(matcapFolder()), ec);
                       if (renderSettings_.matcap == index) renderSettings_.matcap = defaultMatcap();
                       removeCustomMatcap(index);
                       if (!inFolder)
                           return {true, "Removed " + shown + (path.empty() ? "" : " (its image stays at " + path + ")")};
                       std::string where, err;
                       if (moveToTrash(path, where, err)) {
                           matcapFolderSeen_.erase(path);
                           return {true, "Removed " + shown + ": " + u8string(u8path(path).filename()) + " is in " + where};
                       }
                       hiddenMatcapFiles_.insert(path); // a rescan must not bring it back
                       return {true, "Removed " + shown + " for this session; delete " + path +
                                         " to remove it for good (" + err + ")"};
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
    // The scene's shading; SubTools can override it (subtool_shading). Also the preference for new sessions.
    commands_.add({"set_shading",
                   "Viewport shading for every SubTool that doesn't set its own (subtool_shading): smooth (the "
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
    ImGui::TextDisabled("Background");
    ui::Segmented(*this, {"view.background_gradient", "view.background_flat"});
    // The picker edits the flat colour as shown (display values); picking one switches to Flat Colour.
    glm::vec3 c = renderSettings_.backgroundColor;
    const std::string label = std::string("Background colour##") + idSuffix;
    if (ImGui::ColorEdit3(label.c_str(), &c.x, ImGuiColorEditFlags_NoInputs)) setBackground(false, c);
    ui::HelpText("Background colour", nullptr,
                 "The view's flat background colour. Choosing one switches the background to Flat Colour. Store "
                 "Config keeps it for the next session; projects keep their own.",
                 "Preferences > Display");
}

void App::setFlatShading(bool flat) {
    renderSettings_.flatShading = flat;
    uiPrefs_.flatShading = flat; // the default for the next session (Store Config keeps it)
}

const BrushRecipe* App::strokeRecipe() const {
    return strokeBrush_ == BrushType::Smooth || !strokeSculpt_ || strokeMask_ ? nullptr : activeRecipe();
}

void App::paintAt(const DabParams& dab) {
    DabParams p = dab;
    p.strength = rgbIntensity_ * dabStrengthScale_; // paint has its own strength (ZBrush RGB Intensity)
    if (texturePainter_.active()) texturePainter_.dab(doc_.mesh, doc_.bvh, p, doc_.paintColor, symmetry_);
    else if (!paintsTexture(doc_.mesh)) paintDab(doc_.sculptor, doc_.mesh, doc_.bvh, p, doc_.paintColor, symmetry_);
}

void App::beginPaintStroke() {
    paintSnapshot_.reset();
    const Mesh& m = doc_.mesh;
    if (strokePaint_ && paintsTexture(m)) { // texture painting: tiles go into the stroke's undo step
        if (!texturePainter_.begin(doc_.mesh)) {
            const PaintTexture& t = *m.texture;
            if (t.activeLayer >= 0 && t.activeLayer < int(t.layers.size()))
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
    // The colours the stroke recorded are the white it started from; restoring "no polypaint" replaces them.
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
        status_ = "Paint colour " + colorToHex(doc_.paintColor) + " (from the texture)";
        return;
    }
    if (!doc_.mesh.hasColors()) {
        status_ = "The mesh has no polypaint to pick from";
        return;
    }
    doc_.paintColor = sampleColor(doc_.mesh, hit.triangle, hit.position);
    status_ = "Paint colour " + colorToHex(doc_.paintColor);
}

// Color palette: the Paint brush (ZBrush Rgb / Zadd), the colour, swatches and fills.
void App::drawPalette_Color() {
    ImGui::SeparatorText("Paint");
    const bool painting = paintMode_;
    if (painting) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("Paint brush (P)", ImVec2(-1, 0))) paintMode_ = !paintMode_;
    if (painting) ImGui::PopStyleColor();
    ui::HelpAction(*this, "draw.paint");
    ImGui::Checkbox("RGB while sculpting", &paintRgb_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sculpt brushes also paint the colour (ZBrush Rgb with Zadd)");

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
    ImGui::TextDisabled("C over the model picks its colour");
    {
        const Mesh& m = doc_.mesh;
        const bool layered = !m.paintLayers.empty() && m.activePaintLayer >= 0 && m.activePaintLayer < int(m.paintLayers.size());
        if (hasTexture(m)) {
            // Where paint goes, when the SubTool has a texture (Tool > Texture Map).
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Paint into");
            ImGui::SameLine();
            ui::Segmented(*this, {"draw.paint_vertex", "draw.paint_texture"});
        } else {
            // No texture yet: one click starts texture painting (Auto UV first when there are no UVs).
            ui::ActionButton(*this, "draw.new_texture", ImVec2(-1, 0));
        }
        if (paintsTexture(m)) {
            const PaintTexture& t = *m.texture;
            const bool texLayered = t.activeLayer >= 0 && t.activeLayer < int(t.layers.size());
            ImGui::TextDisabled("Painting on: texture, %s", texLayered ? t.layers[size_t(t.activeLayer)].name.c_str() : "Base");
        } else
        ImGui::TextDisabled("Painting on: %s", layered ? m.paintLayers[size_t(m.activePaintLayer)].name.c_str() : "Base");
        ui::HelpText("Paint layer", nullptr,
                     "The paint layer the Paint brush and fills write into. Add and arrange layers in Tool > Paint Layers.",
                     "Color");
    }

    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const std::string hex = colorToHex(doc_.paintColor);
    if (ImGui::Button("Fill", ImVec2(half, 0))) runCommand("paint_fill", {{"color", hex}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fill the unmasked area (the whole mesh if nothing is masked)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!doc_.mesh.hasMask());
    if (ImGui::Button("Fill masked", ImVec2(half, 0)))
        runCommand("paint_fill", {{"color", hex}, {"area", std::string("masked")}});
    ImGui::EndDisabled();
    if (ImGui::Button("Cavity", ImVec2(half, 0))) runCommand("paint_from_cavity");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Darken creases and cavities (respects the mask)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!doc_.mesh.hasColors() && !paintsTexture(doc_.mesh));
    if (ImGui::Button("Clear", ImVec2(half, 0))) runCommand("paint_clear");
    ImGui::EndDisabled();
    ui::HelpText("Clear", nullptr, "Remove all polypaint from the SubTool, paint layers included (undoable).", "Color");
    ImGui::Checkbox("Show polypaint", &renderSettings_.showPolypaint);
    if (hasTexture(doc_.mesh)) {
        ImGui::SameLine();
        ImGui::Checkbox("Texture##show", &renderSettings_.showTexture);
        ui::HelpAction(*this, "view.texture");
    }
}

// Material palette: the material grid, the studio material's colour and the shading (the scene's and the active
// SubTool's own).
void App::drawPalette_Material() {
    sf::drawMaterialGrid(*this, GridMode::Palette); // Pickers.cpp, as in the material picker (M)
    if (ImGui::Button("Load MatCap...", ImVec2(-1, 0))) loadMatcapDialog();
    ui::HelpText("Load MatCap", nullptr,
                 "Use a MatCap image of your own: a lit sphere filling a square PNG, JPG or EXR. It joins the list "
                 "and is kept in Documents/SculptForge/MatCaps; images you put in that folder show up when a material list "
                 "opens. Right-click one of your MatCaps to remove it.",
                 "Material");
    ImGui::ColorEdit3("Base colour", &renderSettings_.baseColor.x, ImGuiColorEditFlags_NoInputs);
    ui::HelpText("Base colour", nullptr, "Colour of the Studio material (the clay colour).", "Material");
    drawShadingControls();
}

void App::drawShadingControls() {
    ImGui::TextDisabled("Shading");
    ui::Segmented(*this, {"material.smooth", "material.flat"});
    ImGui::TextDisabled("This SubTool");
    ui::Segmented(*this, {"subtool.shading_default", "subtool.shading_smooth", "subtool.shading_flat"});
}

} // namespace sf
