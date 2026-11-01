// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Material channels in the interface (core/TextureChannels.h, core/MapExport.h):
//   - Tool > Texture Map > Material Channels: which channels the texture has, the solo channel view and Export
//     Maps with a preset;
//   - Color > Material: what the Paint brush lays on each channel (a checkbox, value swatches and a slider per
//     channel) and material swatches that set every channel at once; C over the model picks every channel;
//   - Tool > Paint Layers: a row of channel chips under each texture layer (click: the layer affects that channel
//     or not; right-click: its blend mode and opacity).
// Every change goes through a command (texture_channels, channel_view, texture_layer_channel, export_maps), so it
// is undoable where it edits the texture and scriptable; the brush material is a setting (paint_material).
#include "app/App.h"
#include "app/Ui.h"

#include "core/MapExport.h"
#include "core/MeshMaps.h"
#include "core/Polypaint.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"

#include <IconsLucide.h>
#include <SDL3/SDL_dialog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace sf {

namespace {

constexpr const char* kMapPath = "Tool > Texture Map";
constexpr const char* kColorPath = "Color > Material";
constexpr const char* kLayersPath = "Tool > Paint Layers";

// Export Maps answers on another thread: the folder waits here for the next frame.
std::mutex g_exportMutex;
std::string g_exportFolder;
void SDLCALL onExportFolder(void*, const char* const* files, int) {
    wakeMainLoop();
    if (!files || !files[0]) return;
    std::lock_guard lock(g_exportMutex);
    g_exportFolder = files[0];
}

// Material swatches: every channel at once (our own starting points, not measured materials).
struct MaterialSwatch {
    const char* name;
    glm::vec3 color; // display (sRGB) colour
    float roughness, metallic;
};
const MaterialSwatch kMaterialSwatches[] = {
    {"Bare metal", {0.78f, 0.78f, 0.80f}, 0.30f, 1.0f}, {"Gold", {1.0f, 0.86f, 0.57f}, 0.25f, 1.0f},
    {"Copper", {0.98f, 0.73f, 0.62f}, 0.35f, 1.0f},     {"Rust", {0.52f, 0.25f, 0.12f}, 0.85f, 0.15f},
    {"Gloss paint", {0.75f, 0.10f, 0.08f}, 0.15f, 0.0f}, {"Rubber", {0.08f, 0.08f, 0.08f}, 0.90f, 0.0f},
    {"Stone", {0.55f, 0.53f, 0.50f}, 0.80f, 0.0f},      {"Clay", {0.72f, 0.55f, 0.45f}, 0.65f, 0.0f},
};

// The channels a texture can turn on (all but base colour).
constexpr Channel kMaterialChannels[] = {Channel::Roughness, Channel::Metallic, Channel::Normal, Channel::Height,
                                         Channel::AO,        Channel::Emissive, Channel::Opacity};

std::string channelHelp(Channel c) {
    switch (c) {
    case Channel::Roughness: return "How rough the surface is: 0 is mirror-like, 1 fully matte.";
    case Channel::Metallic: return "0 for non-metals (paint, stone, skin), 1 for bare metal.";
    case Channel::Normal: return "Fine surface direction (a tangent-space normal map).";
    case Channel::Height: return "Painted relief, 0.5 = none. It also adds to the normal (height to normal).";
    case Channel::AO: return "Ambient occlusion: how much light reaches crevices (1 = open).";
    case Channel::Emissive: return "Light the surface gives off (black = none).";
    case Channel::Opacity: return "How opaque the surface is (exported in the base colour's alpha).";
    default: return "The surface colour (always there).";
    }
}

float* materialValue(BrushMaterial& m, Channel c) {
    switch (c) {
    case Channel::Roughness: return &m.roughness;
    case Channel::Metallic: return &m.metallic;
    case Channel::Height: return &m.height;
    case Channel::AO: return &m.ao;
    case Channel::Opacity: return &m.opacity;
    default: return nullptr;
    }
}

} // namespace

void App::registerPbrUiActions() {
    // (The material channel controls are drawn directly in their palettes; Export Maps is an action so it can sit
    // on a shelf and in the command palette.)
    Action a;
    a.id = "texture.export_maps";
    a.label = "Export Maps...";
    a.palette = kMapPath;
    a.run = [this] { SDL_ShowOpenFolderDialog(onExportFolder, nullptr, window_, nullptr, false); };
    a.enabled = [this] { return hasTexture(doc_.mesh); };
    a.description = "Write the texture's channels as image files for an engine or DCC, with the export preset chosen "
                    "beside the button (Unreal, Unity URP / HDRP, glTF, Godot, Blender, USD).";
    a.command = "export_maps";
    actions_.add(std::move(a));
}

void App::drawTextureChannelsContents() {
    {
        std::string folder;
        {
            std::lock_guard lock(g_exportMutex);
            folder.swap(g_exportFolder);
        }
        const auto& presets = builtinExportPresets();
        if (!folder.empty() && !presets.empty())
            runCommand("export_maps", {{"preset", presets[size_t(std::clamp(exportPresetChoice_, 0, int(presets.size()) - 1))].id},
                                       {"folder", folder}});
    }
    const Mesh& m = doc_.mesh;
    if (!hasTexture(m)) return;
    const PaintTexture& t = *m.texture;
    // Commands that edit the texture replace it (captureTexture: the old one goes into the undo step and may be
    // freed), so `t` is only valid until one runs: they wait in `pending` until everything is drawn.
    std::vector<std::pair<std::string, Args>> pending;
    struct RunPending {
        App& app;
        std::vector<std::pair<std::string, Args>>& list;
        ~RunPending() {
            for (auto& [name, args] : list) app.runCommand(name, args);
        }
    } runPending{*this, pending};
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SeparatorText("Material Channels");
    // Channel toggles, two per row.
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    int column = 0;
    for (Channel c : kMaterialChannels) {
        bool on = channelEnabled(t, c);
        if (column) ImGui::SameLine(half + style.ItemSpacing.x + ImGui::GetCursorStartPos().x);
        const std::string label = std::string(channelFormat(c).label) + "##texchan";
        if (ImGui::Checkbox(label.c_str(), &on))
            pending.push_back({"texture_channels", {{on ? "enable" : "disable", std::string(channelFormat(c).id)}}});
        ui::HelpText(channelFormat(c).label, nullptr,
                     (channelHelp(c) + " Turning a channel on costs no memory until it is painted; turning it off "
                                       "discards its paint (undoable).")
                         .c_str(),
                     kMapPath);
        column = 1 - column;
    }
    char mem[64];
    std::snprintf(mem, sizeof mem, "Channel memory %.1f MB", double(channelBytes(t)) / (1024.0 * 1024.0));
    ImGui::TextDisabled("%s", mem);

    // Solo channel view.
    const char* viewLabel = doc_.channelView == Channel::BaseColor ? "Normal view" : channelFormat(doc_.channelView).label;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##channelview", viewLabel)) {
        if (ImGui::Selectable("Normal view", doc_.channelView == Channel::BaseColor))
            pending.push_back({"channel_view", {{"channel", std::string("off")}}});
        for (Channel c : kMaterialChannels) {
            if (!channelEnabled(t, c)) continue;
            const std::string label = std::string("Solo ") + channelFormat(c).label;
            if (ImGui::Selectable(label.c_str(), doc_.channelView == c))
                pending.push_back({"channel_view", {{"channel", std::string(channelFormat(c).id)}}});
        }
        ImGui::EndCombo();
    }
    ui::HelpText("Channel view", nullptr,
                 "Show one channel on its own on the model: roughness, metallic, height, AO and opacity in grey, the "
                 "normal as its colours (with the height's normal in it), emission in colour. Painting updates it live.",
                 kMapPath);

    // Export Maps with a preset.
    const auto& presets = builtinExportPresets();
    if (!presets.empty()) {
        exportPresetChoice_ = std::clamp(exportPresetChoice_, 0, int(presets.size()) - 1);
        ImGui::SetNextItemWidth(half);
        if (ImGui::BeginCombo("##exportpreset", presets[size_t(exportPresetChoice_)].label.c_str())) {
            for (int i = 0; i < int(presets.size()); ++i) {
                if (ImGui::Selectable(presets[size_t(i)].label.c_str(), i == exportPresetChoice_)) exportPresetChoice_ = i;
                if (ImGui::IsItemHovered() && !presets[size_t(i)].description.empty())
                    ImGui::SetTooltip("%s", presets[size_t(i)].description.c_str());
            }
            ImGui::EndCombo();
        }
        ui::HelpText("Export preset", nullptr,
                     "Which files Export Maps writes and how channels are packed. Your own presets are JSON files "
                     "(export_maps preset=path/to/preset.json; see the guide's Export presets page).",
                     kMapPath);
        ImGui::SameLine();
        ui::ActionButton(*this, "texture.export_maps", ImVec2(-1, 0));
    }
}

void App::drawBrushMaterialContents() {
    const Mesh& m = doc_.mesh;
    if (!paintsTexture(m) || !hasMaterialChannels(*m.texture)) return;
    const PaintTexture& t = *m.texture;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SeparatorText("Material");
    BrushMaterial& mat = doc_.paintMaterial;
    // Material swatches: every enabled channel at once.
    const float sw = (ImGui::GetContentRegionAvail().x - 3 * style.ItemSpacing.x) / 4.0f;
    for (int i = 0; i < int(std::size(kMaterialSwatches)); ++i) {
        const MaterialSwatch& s = kMaterialSwatches[i];
        ImGui::PushID(i);
        // The swatch shows colour; a metal's darker rim hints at its metalness, a rough one is flatter.
        if (ImGui::ColorButton("##matswatch", ImVec4(s.color.x, s.color.y, s.color.z, 1.0f), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(sw, sw * 0.45f))) {
            doc_.paintColor = srgbToLinear(s.color);
            mat.roughness = s.roughness;
            mat.metallic = s.metallic;
            mat.channels |= channelBit(Channel::BaseColor) | channelBit(Channel::Roughness) | channelBit(Channel::Metallic);
            status_ = std::string("Brush material: ") + s.name;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s: roughness %.2f, metallic %.0f", s.name, double(s.roughness), double(s.metallic));
        ImGui::PopID();
        if (i % 4 != 3) ImGui::SameLine();
    }
    ui::HelpText("Material swatches", nullptr,
                 "Set the brush's colour, roughness and metallic at once, and make it paint those channels.", kColorPath);

    // One row per channel the texture has: whether the brush paints it, quick values, and the value.
    for (int ci = 0; ci < kChannelCount; ++ci) {
        const Channel c = Channel(ci);
        if (!channelEnabled(t, c)) continue;
        ImGui::PushID(ci);
        bool paints = (mat.channels & channelBit(c)) != 0;
        if (ImGui::Checkbox(channelFormat(c).label, &paints)) mat.channels ^= channelBit(c);
        ui::HelpText(channelFormat(c).label, nullptr,
                     (channelHelp(c) + " Ticked: the Paint brush paints this channel. C over the model picks every "
                                       "channel at once.")
                         .c_str(),
                     kColorPath);
        if (float* v = materialValue(mat, c)) {
            // Five grey swatches (0, 25, 50, 75, 100%) right-aligned on the checkbox's line, then the slider.
            const float box = ImGui::GetFrameHeight() * 0.8f;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 5 * box - 4 * style.ItemInnerSpacing.x);
            for (int k = 0; k < 5; ++k) {
                const float g = float(k) / 4.0f;
                ImGui::PushID(k);
                if (ImGui::ColorButton("##val", ImVec4(g, g, g, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(box, box))) {
                    *v = g;
                    mat.channels |= channelBit(c);
                }
                ImGui::PopID();
                if (k < 4) ImGui::SameLine(0, style.ItemInnerSpacing.x);
            }
            ImGui::SetNextItemWidth(-1);
            const std::string fmt = std::string(channelFormat(c).label) + " %.2f";
            if (ui::SliderInside("##value", v, 0.0f, 1.0f, fmt.c_str())) mat.channels |= channelBit(c);
        } else if (c == Channel::BaseColor) {
            ImGui::SameLine();
            const glm::vec3 s = linearToSrgb(doc_.paintColor);
            ImGui::ColorButton("##basecolor", ImVec4(s.x, s.y, s.z, 1.0f), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
        } else if (c == Channel::Emissive) {
            ImGui::SameLine();
            glm::vec3 s = linearToSrgb(mat.emissive);
            if (ImGui::ColorEdit3("##emissive", &s.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                mat.emissive = srgbToLinear(s);
                mat.channels |= channelBit(c);
            }
        } else if (c == Channel::Normal) {
            // The direction as two tilts (x, y); 0, 0 = flat.
            float tilt[2] = {mat.normal.x, mat.normal.y};
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat2("##tilt", tilt, -0.9f, 0.9f, "%.2f")) {
                const float z = std::sqrt(std::max(0.05f, 1.0f - tilt[0] * tilt[0] - tilt[1] * tilt[1]));
                mat.normal = glm::normalize(glm::vec3(tilt[0], tilt[1], z));
                mat.channels |= channelBit(c);
            }
            ui::HelpText("Normal tilt", nullptr, "Which way painted normals lean (x, y); 0, 0 paints a flat normal.", kColorPath);
        }
        ImGui::PopID();
    }
    mat.color = doc_.paintColor;
}

Args App::drawLayerChannelChips(int layer) {
    Args run;
    const Mesh& m = doc_.mesh;
    if (!hasTexture(m) || layer < 0 || layer >= int(m.texture->layers.size())) return run;
    const PaintTexture& t = *m.texture;
    const TextureLayer& l = t.layers[size_t(layer)];
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::Indent(ImGui::GetTextLineHeight());
    bool first = true;
    for (int ci = 0; ci < kChannelCount; ++ci) {
        const Channel c = Channel(ci);
        if (!channelEnabled(t, c)) continue;
        const LayerChannel& lc = l.channels[size_t(ci)];
        if (!first) ImGui::SameLine(0, style.ItemInnerSpacing.x);
        first = false;
        ImGui::PushID(ci);
        const bool on = lc.enabled;
        const uint8_t blend = layerChannelBlend(t, layer, c);
        const float opacity = layerChannelOpacity(t, layer, c);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? style.Colors[ImGuiCol_ButtonActive] : style.Colors[ImGuiCol_FrameBg]);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * 0.6f, 0.0f));
        // A dot after the letters marks a channel that doesn't use Normal blending at full opacity.
        const std::string chip = std::string(channelFormat(c).shortLabel) +
                                 ((blend != 0 || opacity < 1.0f) && ci > 0 ? "*" : "") + "##chip";
        if (ImGui::SmallButton(chip.c_str()))
            run = {{"index", double(layer)}, {"channel", std::string(channelFormat(c).id)}, {"enabled", !on}};
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered() && !ui::CtrlHeld())
            ImGui::SetTooltip("%s: %s, %s, %d%%\nClick: on / off. Right-click: blend and opacity.", channelFormat(c).label,
                              on ? "on" : "off", channelBlendLabel(blend), int(std::lround(opacity * 100.0f)));
        ui::HelpText("Layer channel", nullptr,
                     "Whether this layer affects the channel. Right-click for the channel's blend mode (Normal "
                     "Combine lays normal detail onto the normals below) and opacity.",
                     kLayersPath);
        if (ImGui::BeginPopupContextItem("##chipmenu")) {
            ImGui::TextDisabled("%s on '%s'", channelFormat(c).label, l.name.c_str());
            const int modes = c == Channel::Normal ? kChannelBlendCount : kBlendModeCount;
            for (int b = 0; b < modes; ++b)
                if (ImGui::MenuItem(channelBlendLabel(uint8_t(b)), nullptr, b == blend) && b != blend)
                    run = {{"index", double(layer)}, {"channel", std::string(channelFormat(c).id)},
                           {"blend", std::string(channelBlendId(uint8_t(b)))}};
            ImGui::Separator();
            float pct = opacity * 100.0f;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
            ImGui::SliderFloat("##chipopacity", &pct, 0.0f, 100.0f, "Opacity %.0f%%");
            if (ImGui::IsItemDeactivatedAfterEdit())
                run = {{"index", double(layer)}, {"channel", std::string(channelFormat(c).id)}, {"opacity", double(pct / 100.0f)}};
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::Unindent(ImGui::GetTextLineHeight());
    return run;
}

void App::prewarmTexturePaint() {
    Mesh& m = doc_.mesh;
    if (!(paintMode_ || paintRgb_) || drag_ == Drag::Stroke || !paintsTexture(m)) return;
    // The UV bins (once per UV layout; cheap to check every frame).
    prewarmTexturePainting(m);
    // The texel maps under the hovering brush, again once it has moved a quarter of its size.
    const CanvasRect canvas = canvasRect();
    if (ImGui::GetIO().WantCaptureMouse || mouseX_ < canvas.x || mouseY_ < canvas.y || mouseX_ > canvas.x + canvas.w ||
        mouseY_ > canvas.y + canvas.h)
        return;
    doc_.syncBvh();
    RayHit hit;
    if (!pick(mouseX_, mouseY_, hit)) return;
    const float r = radiusPx_ * camera_.worldPerPixel(hit.position, canvasHeight());
    if (glm::distance(hit.position, prewarmCenter_) < 0.25f * r && std::abs(r - prewarmRadius_) < 0.2f * r) return;
    prewarmCenter_ = hit.position;
    prewarmRadius_ = r;
    std::vector<glm::vec3> centers;
    for (const SymCopy& mirror : Sculptor::copies(symmetry_)) centers.push_back(mirror.point(hit.position));
    prewarmTexturePainting(m, &doc_.bvh, centers, r * 1.25f); // a little wider: the pen moves before it lands
}

// Benchmarks (UI tests' print draw_ms): the 3D view drawn `frames` times into the window's framebuffer, waiting
// for the GPU each time, in milliseconds per frame.
double App::benchmarkDrawMs(int frames) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w <= 0 || h <= 0) return 0.0;
    syncScene();
    renderer_.draw(camera_, w, h, renderSettings_, canvasViewport(w, h));
    gl::Finish();
    const Uint64 start = SDL_GetTicksNS();
    for (int i = 0; i < frames; ++i) {
        renderer_.draw(camera_, w, h, renderSettings_, canvasViewport(w, h));
        gl::Finish();
    }
    return double(SDL_GetTicksNS() - start) * 1e-6 / double(std::max(frames, 1));
}

double App::pbrTestValue(const std::string& name) const {
    if (const double v = fillTestValue(name); !std::isnan(v)) return v;
    const Mesh& m = doc_.mesh;
    const bool tex = hasTexture(m);
    if (name == "material_channels") {
        if (!tex) return 0.0;
        double n = 0;
        for (Channel c : kMaterialChannels) n += channelEnabled(*m.texture, c);
        return n;
    }
    if (name == "channel_view") return double(int(doc_.channelView));
    if (name == "frame_ms") return frameSeconds_ * 1000.0; // (smoothed frame interval; fixed in UI tests)
    if (name == "draw_ms") return const_cast<App*>(this)->benchmarkDrawMs(20); // the 3D view drawn 20 times, GPU included
    if (name == "mesh_maps") return tex && m.texture->meshMaps ? double(m.texture->meshMaps->kinds) : 0.0;
    if (name == "view_pbr") return renderSettings_.pbr() ? 1.0 : 0.0;
    if (name == "environment") return double(renderSettings_.environment);
    if (name == "environment_pending") return renderer_.environmentPending() ? 1.0 : 0.0;
    if (name == "pbr_planes") return double(renderer_.pbrPlanes(doc_.activeSubtoolInfo().id));
    if (name == "brush_channels") return double(doc_.paintMaterial.channels);
    if (name == "roughness_painted") {
        if (!tex || !channelEnabled(*m.texture, Channel::Roughness)) return 0.0;
        const TileImage& img = channelImage(*m.texture, Channel::Roughness);
        const float def = channelFormat(Channel::Roughness).defaultValue[0];
        double n = 0;
        for (size_t ti = 0; ti < img.tileCount(); ++ti) {
            if (!img.tile(ti)) continue;
            const int x0 = int(ti % size_t(img.tilesX())) * kTileImageTile, y0 = int(ti / size_t(img.tilesX())) * kTileImageTile;
            for (int y = y0; y < std::min(y0 + kTileImageTile, img.height()); ++y)
                for (int x = x0; x < std::min(x0 + kTileImageTile, img.width()); ++x) n += std::abs(img.value(x, y) - def) > 0.02f;
        }
        return n;
    }
    if (name == "shown_mean_r") {
        if (!tex) return 0.0;
        const std::vector<uint8_t>& px = m.texture->shown();
        double sum = 0;
        for (size_t i = 0; i < px.size(); i += 4) sum += px[i];
        return px.empty() ? 0.0 : sum / double(px.size() / 4);
    }
    if (name.rfind("layer_channel_", 0) == 0) {
        Channel c;
        if (!tex || !parseChannel(name.substr(14), c)) return 0.0;
        const PaintTexture& t = *m.texture;
        if (t.activeLayer < 0 || t.activeLayer >= int(t.layers.size())) return 0.0;
        return t.layers[size_t(t.activeLayer)].channels[size_t(c)].enabled ? 1.0 : 0.0;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace sf
