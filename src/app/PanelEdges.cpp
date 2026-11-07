// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Geometry > Edges (#108): Edges mode picks edges of the base level on the model (click, Shift adds, Ctrl
// removes, double-click takes the edge loop) and selects them by angle, along open borders, grows and shrinks the
// selection; Crease sets a crease (0..1) on the selected edges for subdivision. The selection is core/EdgeSelect.h's
// (Document::edgeSelection), drawn here over the model with the creased edges (shown at the base level in every
// mode); every change runs the edge_select / edge_crease command, so the console and scripts show it.
#include "app/App.h"
#include "app/Palettes.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include "core/EdgeSelect.h"
#include "core/Multires.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {

constexpr const char* kEdgesPath = "Tool > Geometry > Edges";
// Selected edges tested against the model for what it hides (one ray each, when the view changes); more than this
// only drop the ones on faces turned away.
constexpr size_t kOccludedEdges = 4000;
constexpr size_t kDrawnEdges = 150000; // more are thinned out evenly

bool ctrlKey(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }

} // namespace

void App::registerEdgeUiActions() {
    auto add = [this](const char* id, const char* label, std::function<void()> run, const char* description,
                      const char* command, const char* icon = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = kEdgesPath;
        a.run = std::move(run);
        a.enabled = [this] { return edgesMode_; };
        a.description = description;
        a.command = command ? command : "";
        actions_.add(std::move(a));
    };
    {
        Action a;
        a.id = "geometry.edges_mode";
        a.label = "Edges mode";
        a.icon = ICON_LC_VECTOR_SQUARE;
        a.palette = kEdgesPath;
        a.run = [this] { setEdgesMode(!edgesMode_); };
        a.checked = [this] { return edgesMode_; };
        a.enabled = [this] { return doc_.mesh.triangleCount() > 0; };
        a.description = "Select edges of the base level on the model, for edge creases: click an edge, Shift+click adds, "
                        "Ctrl+click removes, double-click takes its edge loop. A Part with subdivision levels steps down "
                        "to SDiv 1 (its base) first. Click the button again to sculpt.";
        actions_.add(std::move(a));
    }
    add("geometry.edges_angle", "Select by Angle",
        [this] { runCommand("edge_select", {{"op", std::string("angle")}, {"degrees", double(edgeAngle_)}, {"within", edgeWithin_}}); },
        "Select every edge where the faces on either side meet at more than the Angle (30 degrees by default): the hard "
        "edges of a model. With Only within selection it keeps just those among the edges selected now.",
        "edge_select");
    add("geometry.edges_open", "Open Edges", [this] { runCommand("edge_select", {{"op", std::string("open")}, {"add", true}}); },
        "Add the open (border) edges: the rims of holes and of open surfaces.", "edge_select");
    add("geometry.edges_grow", "Grow", [this] { runCommand("edge_select", {{"op", std::string("grow")}}); },
        "Add every edge that touches a selected one.", "edge_select", ICON_LC_PLUS);
    add("geometry.edges_shrink", "Shrink", [this] { runCommand("edge_select", {{"op", std::string("shrink")}}); },
        "Drop the selected edges at the loose ends of the selection (Shrink takes back what Grow added; closed loops "
        "keep every edge).",
        "edge_select", ICON_LC_MINUS);
    add("geometry.edges_invert", "Invert", [this] { runCommand("edge_select", {{"op", std::string("invert")}}); },
        "Select the edges that aren't selected, and drop those that are.", "edge_select");
    add("geometry.edges_clear", "Deselect All", [this] { runCommand("edge_select", {{"op", std::string("none")}}); },
        "Deselect every edge.", "edge_select");
    add("geometry.edges_crease", "Crease", [this] { runCommand("edge_crease", {{"value", double(edgeCrease_)}}); },
        "Crease the selected edges by the Crease value (1 = fully sharp), for subdivision. Creased edges show tinted on "
        "the base level.",
        "edge_crease");
    add("geometry.edges_uncrease", "Uncrease", [this] { runCommand("edge_crease", {{"clear", true}}); },
        "Remove the crease from the selected edges.", "edge_crease");
}

void App::setEdgesMode(bool on) {
    if (on == edgesMode_) return;
    if (on) {
        if (subdivCurrentLevel(doc_.mesh) > 0) runCommand("subdiv_level", {{"level", 1.0}}); // edges live on the base
        if (subdivCurrentLevel(doc_.mesh) > 0) return; // (it couldn't step down; the status line says why)
        if (gizmoActive()) setGizmoMode(GizmoMode::Off);
        if (guidesMode_) setGuidesMode(false);
        edgesMode_ = true;
        edgesFrom_ = {brush_, doc_.activeBrush, paintMode_};
        doc_.edgeSelection.sync(doc_.mesh);
        status_ = "Edges: click an edge, Shift+click adds, Ctrl+click removes, double-click selects its loop; " +
                  edgeSelectionText(doc_.edgeSelection);
    } else {
        edgesMode_ = false;
        status_ = "Draw";
    }
}

void App::syncEdgesMode() {
    // Picking a brush (picker, shelf, keys, Library), Paint, the Gizmo or the guides brush leaves Edges mode.
    if (edgesMode_ && (brush_ != edgesFrom_.brush || doc_.activeBrush != edgesFrom_.recipe || paintMode_ != edgesFrom_.paint ||
                       gizmoActive() || guidesMode_))
        setEdgesMode(false);
}

uint32_t App::edgeUnderCursor(float x, float y) {
    RayHit hit;
    if (!pick(x, y, hit)) return kNoEdge;
    const EdgeTopology& topo = doc_.edgeSelection.sync(doc_.mesh);
    return edgeNearest(doc_.mesh, topo, hit.triangle, hit.position);
}

bool App::edgesMouseDown(float x, float y, SDL_Keymod mod) {
    if (!edgesMode_ || previewLod_ >= 0 || (mod & SDL_KMOD_ALT)) return false;
    if (subdivCurrentLevel(doc_.mesh) > 0) { // stepped up meanwhile (D): the mode waits for the base again
        status_ = "Edges: step down to SDiv 1 (Shift+D) to select edges";
        return false;
    }
    doc_.syncBvh();
    RayHit hit;
    if (!pick(x, y, hit)) return false; // the background navigates
    const uint32_t e = edgeNearest(doc_.mesh, doc_.edgeSelection.sync(doc_.mesh), hit.triangle, hit.position);
    if (e == kNoEdge) return true; // (on the model, so never a stroke)
    Args args{{"op", std::string(mouseClicks_ >= 2 ? "loop" : "edge")}, {"edge", double(e)}};
    if (ctrlKey(mod)) args["remove"] = true;
    else if (mod & SDL_KMOD_SHIFT) args["add"] = true;
    runCommand("edge_select", args);
    return true;
}

void App::drawEdgeOverlay() {
    syncEdgesMode();
    const Mesh& m = doc_.mesh;
    if (!edgesMode_ && !m.hasCreases()) return; // (sculpting pays nothing more)
    if (!doc_.activePartInfo().visible || m.triangleCount() == 0 || subdivCurrentLevel(m) > 0) return;
    EdgeSelection& sel = doc_.edgeSelection;
    const EdgeTopology& topo = sel.sync(m);
    EdgeOverlay& ov = edgeOverlay_;
    // Creases change only with an undo step (or a new topology), so the undo revision tells when to look again.
    const uint64_t creaseKey = m.hasCreases() ? doc_.undo.revision() * 2 + 1 + (edgesMode_ ? 0 : 1ull << 63) : 0;
    if (ov.topo != sel.topo.get() || ov.revision != sel.revision || ov.creaseKey != creaseKey) {
        ov.topo = sel.topo.get();
        ov.revision = sel.revision;
        ov.creaseKey = creaseKey;
        ov.edges.clear();
        ov.crease.clear();
        if (edgesMode_)
            for (uint32_t e = 0; e < sel.selected.size(); ++e)
                if (sel.selected[e]) ov.edges.push_back(e), ov.crease.push_back(edgeCrease(m, topo, e));
        ov.selectedEdges = ov.edges.size();
        ov.creased = size_t(std::count_if(ov.crease.begin(), ov.crease.end(), [](float c) { return c > 0.0f; }));
        if (m.hasCreases())
            for (uint32_t e = 0; e < topo.edgeCount(); ++e) {
                const float c = edgeCrease(m, topo, e);
                if (c > 0.0f && !(edgesMode_ && sel.selected[e])) ov.edges.push_back(e), ov.crease.push_back(c), ++ov.creased;
            }
        ov.visibleKey = 0;
    }

    const CanvasRect cr = canvasRect();
    const Camera cam = partCamera();
    const glm::vec3 eye = cam.position();
    // An edge shows when a polygon beside it faces the camera and (for up to kOccludedEdges) nothing is in front of it.
    auto faces = [&](uint32_t corner, const glm::vec3& p) {
        if (corner == ~0u) return false;
        const uint32_t t = corner / 3;
        const glm::uvec3 tri = m.triangles[t];
        return glm::dot(glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]), eye - p) > 0.0f;
    };
    auto midpoint = [&](uint32_t e) { return (m.positions[topo.ends[e].x] + m.positions[topo.ends[e].y]) * 0.5f; };
    auto shown = [&](uint32_t e, const glm::vec3& mid) {
        const uint32_t c = topo.edgeCorner[e];
        return faces(c, mid) || faces(topo.twin[c], mid);
    };
    {
        uint64_t key = 1469598103934665603ull;
        auto mix = [&](const void* data, size_t bytes) {
            const auto* b = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < bytes; ++i) key = (key ^ b[i]) * 1099511628211ull;
        };
        const glm::mat4 vp = cam.projection() * cam.view();
        mix(&vp, sizeof vp);
        mix(&cr, sizeof cr);
        const uint64_t rev = doc_.undo.revision(), selRev = sel.revision;
        mix(&rev, sizeof rev);
        mix(&selRev, sizeof selRev);
        if (key != ov.visibleKey) {
            ov.visibleKey = key;
            ov.visible.assign(ov.edges.size(), 0);
            const bool occlusion = ov.edges.size() <= kOccludedEdges;
            if (occlusion) doc_.syncBvh();
            const float eps = 0.005f * glm::length(m.bounds().extent());
            for (size_t i = 0; i < ov.edges.size(); ++i) {
                const glm::vec3 mid = midpoint(ov.edges[i]);
                if (!shown(ov.edges[i], mid)) continue;
                if (occlusion) {
                    ImVec2 sp;
                    if (!partProject(mid, sp)) continue;
                    glm::vec3 origin, dir;
                    partRay(sp.x, sp.y, origin, dir);
                    const float t = glm::dot(mid - origin, dir) / std::max(glm::dot(dir, dir), 1e-20f);
                    RayHit hit;
                    if (doc_.bvh.raycast(m, origin, dir, hit) && hit.t < t - eps / std::sqrt(glm::dot(dir, dir))) continue;
                }
                ov.visible[i] = 1;
            }
        }
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList(); // under the panels, over the model
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const float s = uiScale_;
    auto line = [&](uint32_t e, ImU32 col, float width) {
        ImVec2 a, b;
        if (!partProject(m.positions[topo.ends[e].x], a) || !partProject(m.positions[topo.ends[e].y], b)) return;
        dl->AddLine(a, b, IM_COL32(12, 12, 16, 170), width + 2.0f * s);
        dl->AddLine(a, b, col, width);
    };
    // Creased edges: blue, brighter and wider the sharper; selected ones orange (a creased one a thicker line).
    auto creaseColor = [](float c) {
        const float t = std::clamp(c, 0.0f, 1.0f);
        return IM_COL32(int(70 + 40 * t), int(130 + 100 * t), 255, int(150 + 105 * t));
    };
    const size_t step = std::max<size_t>(1, ov.edges.size() / kDrawnEdges);
    for (size_t i = ov.selectedEdges; i < ov.edges.size(); i += step)
        if (ov.visible[i]) line(ov.edges[i], creaseColor(ov.crease[i]), (1.25f + ov.crease[i]) * s);
    for (size_t i = 0; i < ov.selectedEdges; i += step)
        if (ov.visible[i]) line(ov.edges[i], IM_COL32(255, 168, 48, 255), (ov.crease[i] > 0.0f ? 3.0f : 2.0f) * s);

    // The edge a click would take, under the cursor.
    const ImVec2 mouse(mouseX_, mouseY_);
    if (edgesMode_ && drag_ == Drag::None && !ImGui::GetIO().WantCaptureMouse && cr.contains(mouse.x, mouse.y)) {
        doc_.syncBvh();
        const uint32_t e = edgeUnderCursor(mouse.x, mouse.y);
        if (e != kNoEdge) {
            const bool removing = ctrlKey(inputMods());
            line(e, removing ? IM_COL32(255, 96, 80, 255) : IM_COL32(250, 246, 220, 255), 2.5f * s);
        }
        ov.hovered = e;
    } else {
        ov.hovered = kNoEdge;
    }
    dl->PopClipRect();
}

void App::drawEdgesSection() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    ImGui::Spacing();
    tray::GroupLabel(*this, "Edges");
    ui::ActionButton(*this, "geometry.edges_mode", ImVec2(-1, 0), ui::Look::IconLabel);
    ImGui::BeginDisabled(!edgesMode_);
    ui::ActionButton(*this, "geometry.edges_angle", ImVec2(half, 0));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##edgeangle", &edgeAngle_, 1.0f, 179.0f, "Angle %.0f deg", ImGuiSliderFlags_AlwaysClamp);
    ui::HelpText("Angle", nullptr,
                 "Select by Angle takes the edges where the faces on either side meet at more than this many degrees "
                 "(0 = flat). 30 finds the hard edges of most models; lower finds softer ones too.",
                 kEdgesPath);
    ImGui::Checkbox(tr("Only within selection"), &edgeWithin_);
    ui::HelpText("Only within selection", nullptr,
                 "Select by Angle keeps only the edges among those selected now (e.g. a loop's sharp stretch).", kEdgesPath);
    ui::ActionButton(*this, "geometry.edges_grow", ImVec2(half, 0), ui::Look::IconLabel);
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.edges_shrink", ImVec2(-1, 0), ui::Look::IconLabel);
    const float third = (ImGui::GetContentRegionAvail().x - 2.0f * style.ItemSpacing.x) / 3.0f;
    ui::ActionButton(*this, "geometry.edges_open", ImVec2(third, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.edges_invert", ImVec2(third, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.edges_clear", ImVec2(-1, 0));
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    ui::SliderInside("##edgecrease", &edgeCrease_, 0.05f, 1.0f, "Crease %.2f", ImGuiSliderFlags_AlwaysClamp);
    ui::HelpText("Crease", nullptr,
                 "How sharp Crease makes the selected edges: 1 is fully sharp, lower values are softer.",
                 kEdgesPath);
    ui::ActionButton(*this, "geometry.edges_crease", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "geometry.edges_uncrease", ImVec2(-1, 0));
    ImGui::EndDisabled();
    if (edgesMode_) {
        const EdgeSelection& sel = doc_.edgeSelection;
        const size_t creased = doc_.mesh.hasCreases() && edgeOverlay_.topo == sel.topo.get() ? edgeOverlay_.creased : 0;
        if (creased) ImGui::TextDisabled("%s, %zu %s", edgeSelectionText(sel).c_str(), creased, tr("creased"));
        else ImGui::TextDisabled("%s", edgeSelectionText(sel).c_str());
    }
    else ImGui::TextDisabled("%s", tr("Edges mode selects edges of the base level"));
}

} // namespace sf
