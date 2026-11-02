// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fill layers, Material Recipes and Mesh Maps in the interface (core/LayerStack.h, core/MaterialRecipe.h,
// core/MeshMaps.h), under Tool > Paint Layers:
//   - New Fill Layer and the Material Recipes list;
//   - the Mesh Maps line: what is baked, Bake, and Live (re-bake the tiles near a change after each stroke);
//   - with a Fill layer active: its material and its mask effects, each with its own settings.
// Every change runs a command (fill_layer, material_recipe, mesh_maps), so it is undoable and scriptable.
//
// Mesh Maps are baked on a worker thread (updateMeshMaps, once a frame): in full when a Fill layer needs maps the
// Part doesn't have, and after sculpting only the tiles within reach of the change (live). When a bake lands, the
// Fill layers re-evaluate those tiles, so wear and dirt follow the sculpt.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"

#include "core/LayerStack.h"
#include "core/MaterialRecipe.h"
#include "core/MeshMaps.h"
#include "core/Polypaint.h"
#include "core/Texture.h"

#include <IconsLucide.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <limits>
#include <thread>

namespace sf {

namespace {
constexpr const char* kFillPath = "Tool > Paint Layers > Fill";
}

struct App::MeshMapJob {
    uint32_t part = 0;
    bool full = true;
    std::atomic<bool> done{false};
    std::atomic<float> progress{0.0f};
    std::atomic<bool> cancel{false};
    // Full bake
    std::shared_ptr<Mesh> mesh;
    int width = 0, height = 0;
    uint32_t kinds = 0;
    MeshMapSettings settings;
    std::shared_ptr<MeshMapSet> result;
    std::string error;
    // Live update
    MeshMapUpdate update;
    double started = 0.0;
};

void App::updateMeshMaps() {
    using Clock = std::chrono::steady_clock;
    const auto now = [] { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); };
    Mesh& m = doc_.mesh;
    const uint32_t part = doc_.activeSubtoolInfo().id;
    // A finished job: its maps go in when the Part still has the same texture layout.
    if (meshMapJob_) {
        if (!meshMapJob_->done) return;
        const std::shared_ptr<MeshMapJob> job = std::move(meshMapJob_);
        if (job->part != part || !hasTexture(m)) return;
        PaintTexture& t = *m.texture;
        if (job->full) {
            if (!job->result) {
                status_ = job->error == "Cancelled" ? "Mesh Maps bake cancelled" : "Mesh Maps: " + job->error;
                return;
            }
            if (!job->result->matches(m, t.width, t.height)) return; // (the Part changed meanwhile: baked again next frame)
            mutableTexture(m).meshMaps = job->result;
            notifyMeshMapsChanged(doc_, m);
            char buf[160];
            std::snprintf(buf, sizeof buf, "Mesh Maps baked at %dx%d in %.1f s", job->result->width, job->result->height,
                          job->result->seconds);
            status_ = buf;
        } else {
            if (!job->update.ok || t.meshMaps != job->update.base) return;
            std::shared_ptr<MeshMapSet> set = applyMeshMapUpdate(job->update);
            if (!set) return;
            mutableTexture(m).meshMaps = std::move(set);
            notifyMeshMapsChanged(doc_, m, &job->update.tiles);
            char buf[160];
            std::snprintf(buf, sizeof buf, "Mesh Maps: %zu tiles updated in %.0f ms", job->update.tiles.size(), (now() - job->started) * 1000.0);
            meshMapStatus_ = buf;
            lastMeshMapUpdateMs_ = (now() - job->started) * 1000.0;
            ++meshMapUpdates_;
        }
        return;
    }
    if (!hasTexture(m) || doc_.sculptor.strokeActive() || drag_ == Drag::Stroke) return;
    const PaintTexture& t = *m.texture;
    const uint32_t need = fillMapsNeeded(t);
    if (!need && !meshMapsWanted_) return;
    const MeshMapSet* have = t.meshMaps.get();
    if (meshMapsWanted_ || !have || !have->matches(m, t.width, t.height) || (have->kinds & need) != need) {
        meshMapsWanted_ = false;
        auto job = std::make_shared<MeshMapJob>();
        job->part = part;
        job->full = true;
        job->mesh = std::make_shared<Mesh>(m);
        job->mesh->texture.reset();
        job->width = t.width;
        job->height = t.height;
        job->kinds = kAllMeshMaps;
        if (have) job->settings = have->settings;
        else job->settings.aoRays = 24, job->settings.thicknessRays = 12;
        job->started = now();
        const bool live = have ? have->live : true;
        const uint64_t revision = have ? have->revision : 0;
        std::thread([job, live, revision] {
            auto set = std::make_shared<MeshMapSet>();
            set->revision = revision;
            if (bakeMeshMaps(*job->mesh, job->width, job->height, job->kinds, job->settings, *set, job->error, [&](float f) {
                    job->progress = f;
                    return !job->cancel.load();
                })) {
                set->live = live;
                job->result = std::move(set);
            }
            job->mesh.reset();
            job->done = true;
            wakeMainLoop();
        }).detach();
        meshMapJob_ = std::move(job);
        return;
    }
    // Live: after sculpting, the tiles near the change.
    if (!have->live || doc_.undo.revision() == meshMapsRevision_) return;
    meshMapsRevision_ = doc_.undo.revision();
    std::vector<uint32_t> moved;
    const AABB region = changedRegion(*have, m, &moved);
    if (!region.valid()) return;
    auto job = std::make_shared<MeshMapJob>();
    job->part = part;
    job->full = false;
    job->started = now();
    if (!prepareMeshMapUpdate(m, t.meshMaps, region, std::move(moved), job->update)) return;
    std::thread([job] {
        runMeshMapUpdate(job->update, &job->cancel);
        job->done = true;
        wakeMainLoop();
    }).detach();
    meshMapJob_ = std::move(job);
}

bool App::meshMapsBusy() const { return meshMapJob_ != nullptr; }

void App::drawFillLayerControls() {
    const Mesh& mesh = doc_.mesh;
    if (!hasTexture(mesh)) return;
    const PaintTexture& t = *mesh.texture;
    // Commands that edit the texture replace it (the old one goes into the undo step and may be freed), so `t`, the
    // Fill layer and its effects below are only valid until one runs: they wait until the panel is drawn.
    std::vector<std::pair<std::string, Args>> pending;
    struct RunPending {
        App& app;
        std::vector<std::pair<std::string, Args>>& list;
        ~RunPending() {
            for (auto& [name, args] : list) app.runCommand(name, args);
        }
    } runPending{*this, pending};
    auto run = [&pending](const char* name, Args args) { pending.emplace_back(name, std::move(args)); };
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("Fill Layers"));
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    if (ImGui::Button(ICON_LC_PLUS " Fill Layer##fill", ImVec2(half, 0)))
        run("fill_layer", {{"name", std::string("Fill")}, {"color", colorToHex(doc_.paintColor)}});
    ui::HelpText("New Fill Layer", nullptr,
                 "Add a Fill layer: a material over the whole Part through a mask you build from effects (Edge Wear, "
                 "Dirt, Gradient, Thickness, Light Direction, Noise, a painted mask, Levels, Blur, Invert). Change it any "
                 "time; it follows the sculpt.",
                 kFillPath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##recipe", tr("Material Recipe"))) {
        for (const MaterialRecipe& r : builtinMaterialRecipes()) {
            if (ImGui::Selectable((r.name + "##recipe").c_str())) run("material_recipe", {{"name", r.id}});
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.description.c_str());
        }
        for (const PackContentItem& it : packMaterials()) // from installed packs, tagged with the pack's name
            if (ImGui::Selectable((it.label() + "##recipe" + it.file).c_str()))
                run("material_recipe", {{"path", it.file}});
        ImGui::EndCombo();
    }
    ui::HelpText("Material Recipe", nullptr,
                 "Lay a ready-made stack of Fill layers on the Part: Worn Metal, Painted Edges, Dirt and Dust, Rusted "
                 "Iron. They read the Part's Mesh Maps, so each model wears where it would. Undoable.",
                 kFillPath);

    // Mesh Maps.
    const MeshMapSet* maps = t.meshMaps.get();
    if (meshMapJob_ && meshMapJob_->full) {
        ImGui::ProgressBar(meshMapJob_->progress.load(), ImVec2(-1, 0), tr("Baking Mesh Maps..."));
    } else {
        char line[160];
        if (maps && maps->matches(mesh, t.width, t.height))
            std::snprintf(line, sizeof line, "Mesh Maps %dx%d%s", maps->width, maps->height, maps->live ? ", live" : "");
        else std::snprintf(line, sizeof line, "%s", tr("No Mesh Maps yet"));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", line);
        ui::HelpText("Mesh Maps", nullptr,
                     "Ambient occlusion, curvature, thickness, position, world normal and ID of the Part on its texture's "
                     "UVs, which Fill layers read. Baked in the background when a Fill layer needs them; with Live on, "
                     "the tiles near what you sculpt are baked again after each stroke.",
                     kFillPath);
        ImGui::SameLine();
        bool live = !maps || maps->live;
        if (ui::Checkbox("Live##meshmaps", &live) && maps) run("mesh_maps", {{"action", std::string("live")}, {"live", live}});
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Bake##meshmaps"))) meshMapsWanted_ = true;
    }
    if (!meshMapStatus_.empty()) ImGui::TextDisabled("%s", meshMapStatus_.c_str());

    // The active Fill layer.
    const int layer = t.activeLayer;
    if (!isFillLayer(t, layer)) return;
    const FillLayer& f = *t.layers[size_t(layer)].fill;
    ImGui::Separator();
    ImGui::TextUnformatted(t.layers[size_t(layer)].name.c_str());
    const Args at = {{"index", double(layer)}};
    auto withLayer = [&](Args a) {
        a["index"] = double(layer);
        return a;
    };
    // Material: colour and the channels it lays down.
    glm::vec3 col = linearToSrgb(glm::clamp(f.material.color, 0.0f, 1.0f));
    if (ImGui::ColorEdit3("##fillcolor", &col.x, ImGuiColorEditFlags_NoInputs)) fillColorEdit_ = srgbToLinear(col), fillColorEditing_ = true;
    if (ImGui::IsItemDeactivatedAfterEdit() && fillColorEditing_) {
        fillColorEditing_ = false;
        run("fill_layer", withLayer({{"action", std::string("set")}, {"color", colorToHex(fillColorEdit_)}}));
    }
    ui::HelpText("Fill colour", nullptr, "The base colour the Fill layer lays down.", kFillPath);
    for (Channel c : {Channel::BaseColor, Channel::Roughness, Channel::Metallic, Channel::Height}) {
        ImGui::SameLine();
        bool on = f.material.channels & channelBit(c);
        if (ui::Checkbox((std::string(channelFormat(c).shortLabel) + "##fillch").c_str(), &on)) {
            std::string list;
            for (int k = 0; k < kChannelCount; ++k) {
                const bool has = k == int(c) ? on : (f.material.channels & (1u << k)) != 0;
                if (has) list += std::string(list.empty() ? "" : ",") + channelFormat(k).id;
            }
            if (!list.empty()) run("fill_layer", withLayer({{"action", std::string("set")}, {"channels", list}}));
        }
    }
    auto materialSlider = [&](const char* id, const char* key, float value, const char* fmt, Channel c) {
        if (!(f.material.channels & channelBit(c))) return;
        ImGui::SetNextItemWidth(-1);
        float v = value;
        ImGui::SliderFloat(id, &v, 0.0f, 1.0f, fmt);
        if (ImGui::IsItemDeactivatedAfterEdit()) run("fill_layer", withLayer({{"action", std::string("set")}, {key, double(v)}}));
    };
    materialSlider("##fillrough", "roughness", f.material.roughness, "Roughness %.2f", Channel::Roughness);
    materialSlider("##fillmetal", "metallic", f.material.metallic, "Metallic %.2f", Channel::Metallic);
    materialSlider("##fillheight", "height", f.material.height, "Height %.2f", Channel::Height);

    // Mask effects, top to bottom.
    ImGui::TextDisabled("%s", tr("Mask"));
    int remove = -1;
    for (size_t k = 0; k < f.effects.size(); ++k) {
        const MaskEffect& e = f.effects[k];
        ImGui::PushID(int(k));
        const std::string title = std::to_string(k + 1) + ". " +
                                  (e.kind == MaskEffectKind::Generator ? generatorLabel(e.generator) : maskEffectLabel(e.kind));
        auto effectArgs = [&](Args a) {
            a["action"] = std::string("effect");
            a["effect"] = double(k);
            return withLayer(std::move(a));
        };
        bool enabled = e.enabled;
        if (ui::Checkbox("##on", &enabled)) run("fill_layer", effectArgs({{"enabled", enabled}}));
        ImGui::SameLine();
        ImGui::TextUnformatted(title.c_str());
        if (k > 0) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("Multiply  ").x + ImGui::GetFrameHeight());
            if (ImGui::BeginCombo("##blend", maskBlendId(e.blend))) {
                for (int b = 0; b < kMaskBlendCount; ++b)
                    if (ImGui::Selectable(maskBlendId(MaskBlend(b)), int(e.blend) == b))
                        run("fill_layer", effectArgs({{"blend", std::string(maskBlendId(MaskBlend(b)))}}));
                ImGui::EndCombo();
            }
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
        if (ImGui::SmallButton(ICON_LC_X "##remove")) remove = int(k);
        auto slider = [&](const char* id, const char* key, float value, float lo, float hi, const char* fmt) {
            ImGui::SetNextItemWidth(-1);
            float v = value;
            ImGui::SliderFloat(id, &v, lo, hi, fmt);
            if (ImGui::IsItemDeactivatedAfterEdit()) run("fill_layer", effectArgs({{key, double(v)}}));
        };
        switch (e.kind) {
        case MaskEffectKind::Generator:
            slider("##level", "level", e.level, 0.0f, 1.0f, "Level %.2f");
            slider("##soft", "softness", e.softness, 0.0f, 1.0f, "Softness %.2f");
            slider("##breakup", "breakup", e.breakup, 0.0f, 2.0f, "Break-up %.2f");
            slider("##scale", "scale", e.scale, 0.002f, 0.3f, "Noise size %.3f");
            break;
        case MaskEffectKind::Noise:
            slider("##scale", "scale", e.scale, 0.002f, 0.3f, "Size %.3f");
            slider("##contrast", "contrast", e.contrast, 0.0f, 4.0f, "Contrast %.2f");
            break;
        case MaskEffectKind::Levels:
            slider("##inlow", "in_low", e.inLow, 0.0f, 1.0f, "In low %.2f");
            slider("##inhigh", "in_high", e.inHigh, 0.0f, 1.0f, "In high %.2f");
            slider("##gamma", "gamma", e.gamma, 0.1f, 4.0f, "Gamma %.2f");
            break;
        case MaskEffectKind::Blur: slider("##radius", "radius", e.radius, 0.0f, 32.0f, "Radius %.0f px"); break;
        case MaskEffectKind::Fill: slider("##value", "value", e.value, 0.0f, 1.0f, "Value %.2f"); break;
        case MaskEffectKind::Paint: ImGui::TextDisabled("%s", tr("Paint on the model with this layer active (Alt erases)")); break;
        default: break;
        }
        ImGui::PopID();
    }
    if (remove >= 0) run("fill_layer", withLayer({{"action", std::string("remove_effect")}, {"effect", double(remove)}}));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##addeffect", tr("Add mask effect"))) {
        for (int g = 0; g < kGeneratorKindCount; ++g)
            if (ImGui::Selectable((std::string(generatorLabel(GeneratorKind(g))) + "##gen").c_str()))
                run("fill_layer", withLayer({{"action", std::string("effect")}, {"generator", std::string(generatorId(GeneratorKind(g)))}}));
        for (MaskEffectKind k : {MaskEffectKind::Noise, MaskEffectKind::Paint, MaskEffectKind::Levels, MaskEffectKind::Blur,
                                 MaskEffectKind::Invert, MaskEffectKind::Fill})
            if (ImGui::Selectable((std::string(maskEffectLabel(k)) + "##kind").c_str()))
                run("fill_layer", withLayer({{"action", std::string("effect")}, {"kind", std::string(maskEffectId(k))}}));
        ImGui::EndCombo();
    }
    ui::HelpText("Add mask effect", nullptr,
                 "Add an effect to the bottom of the mask stack. Generators read the Mesh Maps: Edge Wear (convex edges), "
                 "Dirt (crevices), Gradient, Thickness (thin parts), Light Direction (surfaces facing up). Noise is in "
                 "3D, so it has no UV seams. Paint lets you paint the mask with the Paint brush.",
                 kFillPath);
    (void)at;
}

double App::fillTestValue(const std::string& name) const {
    const Mesh& m = doc_.mesh;
    const bool tex = hasTexture(m);
    if (name == "fill_layers") {
        if (!tex) return 0.0;
        double n = 0;
        for (const TextureLayer& l : m.texture->layers) n += l.fill != nullptr;
        return n;
    }
    if (name == "mesh_maps_busy") return meshMapJob_ ? 1.0 : 0.0;
    if (name == "mesh_map_updates") return double(meshMapUpdates_);
    if (name == "mesh_map_update_ms") return lastMeshMapUpdateMs_;
    if (name == "mesh_maps_revision") return tex && m.texture->meshMaps ? double(m.texture->meshMaps->revision) : 0.0;
    if (name == "active_fill_alpha_mean") { // the active Fill layer's mean coverage, 0-255
        if (!tex || !isFillLayer(*m.texture, m.texture->activeLayer)) return 0.0;
        const std::vector<uint8_t>& px = m.texture->layers[size_t(m.texture->activeLayer)].rgba;
        double s = 0;
        for (size_t i = 3; i < px.size(); i += 4) s += px[i];
        return px.empty() ? 0.0 : s / double(px.size() / 4);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace sf
