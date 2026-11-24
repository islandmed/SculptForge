// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Polygon: Polygon mode (core/PolyMode.h). While it is on, clicks on the model select Points, Edges or Faces
// (Shift adds, Ctrl removes, a drag selects a box; double-click an edge for its loop, a face for its face group), the
// selection is drawn over the model, the Transform gizmo (W / E / R) moves it, and the operations of the registry
// (core/PolyOps.h) run on it from the panel, each as one undo step. Every change runs the poly_select / poly_op
// command, so the console and scripts show it. Picking a brush or Esc leaves the mode.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Palettes.h"
#include "app/Ui.h"

#include "core/EdgeSelect.h"
#include "core/PolyMode.h"
#include "core/PolyOps.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {

constexpr const char* kPolyPath = "Tool > Polygon";
// More than this of a kind are thinned out evenly in the overlay (a box over a whole 1M-quad cage stays quick).
constexpr size_t kDrawnFaces = 60000, kDrawnEdges = 150000, kDrawnPoints = 100000;
constexpr ImU32 kSelected = IM_COL32(255, 168, 48, 255), kSelectedFill = IM_COL32(255, 168, 48, 70),
                kHover = IM_COL32(250, 246, 220, 255), kRemove = IM_COL32(255, 96, 80, 255),
                kPreview = IM_COL32(90, 205, 255, 235), kShadow = IM_COL32(12, 12, 16, 170);

bool ctrlKey(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }

std::string vec3Text(const glm::vec3& v) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%g %g %g", double(v.x), double(v.y), double(v.z));
    return buf;
}

// The polygon of triangle `t` (its lower half named): its 3 or 4 corners in winding order.
int polygonCorners(const Mesh& m, uint32_t t, uint32_t out[4]) {
    glm::uvec4 f;
    int n = m.exportFace(t, f);
    if (n == 0) n = m.exportFace(m.quadPartnerAt(t), f);
    for (int i = 0; i < n; ++i) out[i] = f[i];
    return n;
}

const char* elementStatus(PolyElement e) {
    switch (e) {
    case PolyElement::Points: return "Points";
    case PolyElement::Edges: return "Edges";
    case PolyElement::Faces: return "Faces";
    }
    return "Faces";
}

} // namespace

void App::registerPolygonUiActions() {
    auto add = [this](const char* id, const char* label, const char* icon, std::function<void()> run, const char* description,
                      const char* command, std::function<bool()> enabled = nullptr, std::function<bool()> checked = nullptr) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = kPolyPath;
        a.run = std::move(run);
        a.enabled = enabled ? std::move(enabled) : [this] { return polyMode_; };
        a.checked = std::move(checked);
        a.description = description;
        a.command = command ? command : "";
        actions_.add(std::move(a));
    };
    add("polygon.mode", "Polygon mode", ICON_LC_PENTAGON, [this] { setPolyMode(!polyMode_); },
        "Model with polygons: select Points, Edges or Faces of the Part on the model (click, Shift adds, Ctrl removes, drag a "
        "box; double-click an edge for its loop, a face for its face group), move them with the Transform gizmo and run the "
        "operations below on them. For low and medium-density meshes without subdivision or Micro levels. Esc, the button "
        "or any brush goes back to sculpting.",
        nullptr, [this] { return doc_.mesh.triangleCount() > 0; }, [this] { return polyMode_; });
    auto element = [&](const char* id, const char* label, const char* icon, PolyElement e, const char* description) {
        add(id, label, icon, [this, e] { runCommand("poly_mode", {{"element", std::string(polyElementName(e))}}); }, description,
            "poly_mode", nullptr, [this, e] { return polyMode_ && doc_.polySelection.element == e; });
    };
    element("polygon.points", "Points", ICON_LC_CIRCLE_DOT, PolyElement::Points,
            "Select points (vertices). Switching converts the selection: a face's points, the points an edge joins.");
    element("polygon.edges", "Edges", ICON_LC_VECTOR_SQUARE, PolyElement::Edges,
            "Select edges. Switching converts the selection: a face's edges, or the edges whose points are both selected.");
    element("polygon.faces", "Faces", ICON_LC_SQUARE, PolyElement::Faces,
            "Select faces (a quad is one face). Switching converts the selection: the faces whose points or edges are all selected.");
    auto select = [&](const char* id, const char* label, const char* icon, const char* op, const char* description) {
        add(id, label, icon, [this, op] { runCommand("poly_select", {{"op", std::string(op)}}); }, description, "poly_select");
    };
    select("polygon.select_all", "All", nullptr, "all", "Select every point, edge or face of the Part (the shown ones).");
    select("polygon.select_none", "None", nullptr, "none", "Deselect everything.");
    select("polygon.invert", "Invert", nullptr, "invert", "Select what isn't selected, and drop what is.");
    select("polygon.grow", "Grow", ICON_LC_PLUS, "grow", "Add every point, edge or face that touches a selected one.");
    select("polygon.shrink", "Shrink", ICON_LC_MINUS, "shrink",
           "Drop the selected points, edges or faces at the selection's border (Shrink takes back what Grow added; a closed "
           "edge loop keeps every edge).");
    add("polygon.loop", "Loop", nullptr,
        [this] {
            for (uint32_t e : doc_.polySelection.edgeList(doc_.mesh))
                runCommand("poly_select", {{"op", std::string("loop")}, {"index", double(e)}, {"add", true}});
            status_ = "Edge loops: " + polySelectionText(doc_.polySelection, doc_.mesh);
        },
        "Extend each selected edge to its edge loop: across the quads, on through every vertex with four edges, until it "
        "comes back round or meets a pole or a border (a double-click on an edge does the same).",
        "poly_select", [this] { return polyMode_ && doc_.polySelection.element == PolyElement::Edges && !doc_.polySelection.empty(); });
    add("polygon.ring", "Ring", nullptr,
        [this] {
            for (uint32_t e : doc_.polySelection.edgeList(doc_.mesh))
                runCommand("poly_select", {{"op", std::string("ring")}, {"index", double(e)}, {"add", true}});
            status_ = "Edge rings: " + polySelectionText(doc_.polySelection, doc_.mesh);
        },
        "Extend each selected edge to its edge ring: the edges across the quads beside it, on until the ring comes back "
        "round or meets a triangle or a border (what an added edge loop would cut through).",
        "poly_select", [this] { return polyMode_ && doc_.polySelection.element == PolyElement::Edges && !doc_.polySelection.empty(); });
    add("polygon.group", "By Group", nullptr,
        [this] {
            const Mesh& m = doc_.mesh;
            std::vector<uint32_t> groups;
            for (uint32_t t : doc_.polySelection.triangles(m)) groups.push_back(m.facegroupAt(t));
            std::sort(groups.begin(), groups.end());
            groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
            for (uint32_t g : groups) runCommand("poly_select", {{"op", std::string("group")}, {"group", double(g)}, {"add", true}});
            status_ = "Face groups: " + polySelectionText(doc_.polySelection, doc_.mesh);
        },
        "Extend the selection to the whole face groups the selected faces are in (a double-click on a face does the same).",
        "poly_select", [this] { return polyMode_ && !doc_.polySelection.triangles(doc_.mesh).empty(); });
    // One action per operation of the registry, so each can take a key or a shelf slot like any command.
    for (const PolyOp& op : PolyOpRegistry::instance().all()) {
        const std::string id = "polygon.op." + op.id, opId = op.id;
        Action a;
        a.id = id;
        a.label = op.label;
        a.palette = kPolyPath;
        a.description = op.description;
        a.command = "poly_op";
        a.run = [this, opId] { polyRunOp(opId); };
        a.enabled = [this] { return polyMode_ && !doc_.polySelection.empty(); };
        actions_.add(std::move(a));
    }
}

void App::setPolyMode(bool on) {
    if (on == polyMode_) return;
    if (on) {
        const std::string why = polyModeRefusal(doc_.mesh);
        if (!why.empty()) {
            status_ = why;
            return;
        }
        if (guidesMode_) setGuidesMode(false);
        if (edgesMode_) setEdgesMode(false);
        leaveDrawModes(); // Draw Tube, Insert, Place Landmarks...
        polyMode_ = true;
        polyFrom_ = {brush_, doc_.activeBrush, paintMode_};
        doc_.polySelection.sync(doc_.mesh, doc_.edgeSelection.topo);
        polyWireBefore_ = renderSettings_.wireframe;
        renderSettings_.wireframe = true; // the polygons are what is being edited
        status_ = "Polygon mode (" + std::string(elementStatus(doc_.polySelection.element)) +
                  "): click selects, Shift adds, Ctrl removes, drag a box; double-click an edge for its loop, a face for its "
                  "group; W / E / R move the selection; " +
                  polySelectionText(doc_.polySelection, doc_.mesh);
    } else {
        polyMode_ = false;
        if (renderSettings_.wireframe && !polyWireBefore_) renderSettings_.wireframe = false;
        polyOverlay_.previewOk = false;
        polyOverlay_.previewOp.clear();
        if (drag_ == Drag::Select && selectTarget_ == SelectTarget::Poly) {
            drag_ = Drag::None;
            selectPath_.clear();
        }
        status_ = "Polygon mode off";
    }
}

void App::syncPolyMode() {
    if (!polyMode_) return;
    // Picking a brush (picker, shelf, keys, Library), Paint, the guides brush or Edges mode leaves the mode; so does a
    // Part the mode refuses (another Part picked, levels added).
    if (brush_ != polyFrom_.brush || doc_.activeBrush != polyFrom_.recipe || paintMode_ != polyFrom_.paint || guidesMode_ || edgesMode_) {
        setPolyMode(false);
        return;
    }
    const std::string why = polyModeRefusal(doc_.mesh);
    if (!why.empty()) {
        setPolyMode(false);
        status_ = why;
    }
}

bool App::polyElementAt(float x, float y, uint32_t& index, RayHit* hitOut) {
    doc_.syncBvh();
    RayHit hit;
    if (!pick(x, y, hit)) return false;
    const Mesh& m = doc_.mesh;
    if (hit.triangle >= m.triangleCount() || m.triangleHidden(hit.triangle)) return false;
    PolySelection& sel = doc_.polySelection;
    const EdgeTopology& topo = sel.sync(m, doc_.edgeSelection.topo);
    switch (sel.element) {
    case PolyElement::Points: index = polyNearestVertex(m, hit.triangle, hit.position); break;
    case PolyElement::Edges: index = edgeNearest(m, topo, hit.triangle, hit.position); break;
    case PolyElement::Faces: index = edgePolygon(m, hit.triangle); break;
    }
    if (index == ~0u) return false;
    if (hitOut) *hitOut = hit;
    return true;
}

bool App::polyMouseDown(float x, float y, SDL_Keymod mod) {
    if (!polyMode_ || previewLod_ >= 0 || (mod & SDL_KMOD_ALT)) return false; // Alt navigates / picks a Part
    if (gizmoActive() && gizmoHandleAt(x, y) != GizmoHandle::None) return false; // the gizmo's handles move the selection
    if (!polyModeRefusal(doc_.mesh).empty()) {
        setPolyMode(false);
        return false;
    }
    polyPressClicks_ = mouseClicks_; // (reset after the press: read it at the release)
    beginSelection(x, y, SelectTarget::Poly, SelectShape::Rect); // a click or a box, decided at the release
    return true;
}

void App::polySelectMouseUp() {
    if (!polyMode_ || selectPath_.empty()) return;
    const SDL_Keymod mod = inputMods();
    const bool remove = ctrlKey(mod), add = !remove && (mod & SDL_KMOD_SHIFT) != 0;
    Args args;
    if (add) args["add"] = true;
    if (remove) args["remove"] = true;
    if (!selectMoved_) {
        uint32_t index = ~0u;
        RayHit hit;
        const ImVec2 p = selectPath_.front();
        if (!polyElementAt(p.x, p.y, index, &hit)) { // the background: a plain click deselects
            if (!add && !remove && !doc_.polySelection.empty()) runCommand("poly_select", {{"op", std::string("none")}});
            return;
        }
        const PolyElement e = doc_.polySelection.element;
        std::string op = e == PolyElement::Points ? "point" : e == PolyElement::Edges ? "edge" : "face";
        if (polyPressClicks_ >= 2 && e == PolyElement::Edges) op = "loop";
        if (polyPressClicks_ >= 2 && e == PolyElement::Faces) {
            op = "group";
            args["group"] = double(doc_.mesh.facegroupAt(hit.triangle));
        } else {
            args["index"] = double(index);
        }
        args["op"] = op;
        runCommand("poly_select", args);
        return;
    }
    // A box: the elements inside it that face the camera.
    args["op"] = std::string("rect");
    args["eye"] = vec3Text(partCamera().position());
    runSelection("poly_select", "poly_select", args);
}

void App::polyRunOp(const std::string& id) {
    const PolyOp* op = PolyOpRegistry::instance().find(id);
    if (!op) return;
    Args args = polyOpValues_[id];
    args["op"] = id;
    polyOverlay_.previewOk = false;
    polyOverlay_.previewOp.clear();
    runCommand("poly_op", args);
}

void App::drawPolyOverlay() {
    syncPolyMode();
    if (!polyMode_) return;
    const Mesh& m = doc_.mesh;
    if (!doc_.activePartInfo().visible || m.triangleCount() == 0) return;
    PolySelection& sel = doc_.polySelection;
    const EdgeTopology& topo = sel.sync(m, doc_.edgeSelection.topo);
    PolyOverlay& ov = polyOverlay_;
    if (ov.topo != sel.topo.get() || ov.revision != sel.revision || ov.element != sel.element) {
        ov.topo = sel.topo.get();
        ov.revision = sel.revision;
        ov.element = sel.element;
        ov.items.clear();
        switch (sel.element) {
        case PolyElement::Points:
            for (uint32_t v = 0; v < sel.points.size(); ++v)
                if (sel.points[v]) ov.items.push_back(v);
            break;
        case PolyElement::Edges:
            for (uint32_t e = 0; e < sel.edges.size(); ++e)
                if (sel.edges[e]) ov.items.push_back(e);
            break;
        case PolyElement::Faces:
            for (uint32_t t = 0; t < sel.faces.size(); ++t)
                if (sel.faces[t] && edgePolygon(m, t) == t) ov.items.push_back(t);
            break;
        }
        ov.visibleKey = 0;
    }

    const CanvasRect cr = canvasRect();
    const Camera cam = partCamera();
    const glm::vec3 eye = cam.position();
    auto triFaces = [&](uint32_t t, const glm::vec3& p) {
        const glm::uvec3 tri = m.triangles[t];
        return glm::dot(glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]), eye - p) > 0.0f;
    };
    auto polygonFaces = [&](uint32_t t) {
        const glm::vec3 p = m.positions[m.triangles[t].x];
        const uint32_t u = m.quadPartnerAt(t);
        return triFaces(t, p) || (u != Mesh::kNoQuad && triFaces(u, p));
    };
    auto edgeFaces = [&](uint32_t e) {
        const uint32_t c = topo.edgeCorner[e], d = topo.twin[c];
        const glm::vec3 mid = (m.positions[topo.ends[e].x] + m.positions[topo.ends[e].y]) * 0.5f;
        return triFaces(c / 3, mid) || (d != kNoCorner && triFaces(d / 3, mid));
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
        const uint64_t rev = doc_.undo.revision();
        mix(&rev, sizeof rev);
        if (key != ov.visibleKey) {
            ov.visibleKey = key;
            ov.visible.assign(ov.items.size(), 0);
            for (size_t i = 0; i < ov.items.size(); ++i) {
                const uint32_t id = ov.items[i];
                bool shown = false;
                switch (sel.element) {
                case PolyElement::Points:
                    shown = id >= m.normals.size() || glm::dot(m.normals[id], eye - m.positions[id]) > 0.0f;
                    break;
                case PolyElement::Edges: shown = edgeFaces(id); break;
                case PolyElement::Faces: shown = polygonFaces(id); break;
                }
                ov.visible[i] = shown;
            }
        }
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList(); // under the panels, over the model
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const float s = uiScale_;
    auto line = [&](uint32_t a, uint32_t b, ImU32 col, float width) {
        ImVec2 pa, pb;
        if (!partProject(m.positions[a], pa) || !partProject(m.positions[b], pb)) return;
        dl->AddLine(pa, pb, kShadow, width + 2.0f * s);
        dl->AddLine(pa, pb, col, width);
    };
    auto point = [&](uint32_t v, ImU32 col, float r) {
        ImVec2 p;
        if (!partProject(m.positions[v], p)) return;
        dl->AddCircleFilled(p, r + 1.5f * s, kShadow, 12);
        dl->AddCircleFilled(p, r, col, 12);
    };
    auto face = [&](uint32_t t, ImU32 fill, ImU32 col, float width) {
        uint32_t vs[4];
        const int n = polygonCorners(m, t, vs);
        ImVec2 pts[4];
        for (int i = 0; i < n; ++i)
            if (!partProject(m.positions[vs[i]], pts[i])) return;
        if ((fill >> IM_COL32_A_SHIFT) != 0) dl->AddConvexPolyFilled(pts, n, fill);
        dl->AddPolyline(pts, n, col, ImDrawFlags_Closed, width);
    };
    const size_t cap = sel.element == PolyElement::Points ? kDrawnPoints : sel.element == PolyElement::Edges ? kDrawnEdges : kDrawnFaces;
    const size_t step = std::max<size_t>(1, ov.items.size() / cap);
    for (size_t i = 0; i < ov.items.size(); i += step) {
        if (!ov.visible[i]) continue;
        const uint32_t id = ov.items[i];
        switch (sel.element) {
        case PolyElement::Points: point(id, kSelected, 3.0f * s); break;
        case PolyElement::Edges: line(topo.ends[id].x, topo.ends[id].y, kSelected, 2.0f * s); break;
        case PolyElement::Faces: face(id, kSelectedFill, kSelected, 1.5f * s); break;
        }
    }
    // The op preview: the result's selected faces, outlined.
    if (ov.previewOk)
        for (const auto& [a, b] : ov.previewEdges) {
            ImVec2 pa, pb;
            if (!partProject(a, pa) || !partProject(b, pb)) continue;
            dl->AddLine(pa, pb, kShadow, 3.5f * s);
            dl->AddLine(pa, pb, kPreview, 1.75f * s);
        }
    // The element a click would take, under the cursor.
    const ImVec2 mouse(mouseX_, mouseY_);
    ov.hovered = ~0u;
    if (drag_ == Drag::None && !ImGui::GetIO().WantCaptureMouse && cr.contains(mouse.x, mouse.y) &&
        !(gizmoActive() && gizmoHandleAt(mouse.x, mouse.y) != GizmoHandle::None)) {
        uint32_t id;
        if (polyElementAt(mouse.x, mouse.y, id)) {
            const ImU32 col = ctrlKey(inputMods()) ? kRemove : kHover;
            switch (sel.element) {
            case PolyElement::Points: point(id, col, 4.0f * s); break;
            case PolyElement::Edges: line(topo.ends[id].x, topo.ends[id].y, col, 2.5f * s); break;
            case PolyElement::Faces: face(id, 0, col, 2.5f * s); break;
            }
            ov.hovered = id;
        }
    }
    dl->PopClipRect();
}

void App::drawPolygonContents() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    const float third = (ImGui::GetContentRegionAvail().x - 2.0f * style.ItemSpacing.x) / 3.0f;
    ui::ActionButton(*this, "polygon.mode", ImVec2(-1, 0), ui::Look::IconLabel);
    if (!polyMode_) {
        const std::string why = polyModeRefusal(doc_.mesh);
        if (why.empty()) ImGui::TextDisabled("%s", tr("Polygon mode selects points, edges and faces of the Part to model with"));
        else ImGui::TextWrapped("%s", why.c_str());
    }
    ImGui::BeginDisabled(!polyMode_);
    ui::Segmented(*this, {"polygon.points", "polygon.edges", "polygon.faces"});
    ui::ActionButton(*this, "polygon.select_all", ImVec2(third, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "polygon.select_none", ImVec2(third, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "polygon.invert", ImVec2(-1, 0));
    ui::ActionButton(*this, "polygon.grow", ImVec2(half, 0), ui::Look::IconLabel);
    ImGui::SameLine();
    ui::ActionButton(*this, "polygon.shrink", ImVec2(-1, 0), ui::Look::IconLabel);
    ui::ActionButton(*this, "polygon.loop", ImVec2(third, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "polygon.ring", ImVec2(third, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "polygon.group", ImVec2(-1, 0));
    if (polyMode_) ImGui::TextDisabled("%s", polySelectionText(doc_.polySelection, doc_.mesh).c_str());

    ImGui::Spacing();
    tray::GroupLabel(*this, "Operations");
    PolyOverlay& ov = polyOverlay_;
    std::string previewOp;
    Args previewArgs;
    for (const PolyOp& op : PolyOpRegistry::instance().all()) {
        ImGui::PushID(op.id.c_str());
        const std::string actionId = "polygon.op." + op.id;
        ui::ActionButton(*this, actionId.c_str(), ImVec2(-1, 0));
        Args& vals = polyOpValues_[op.id];
        bool active = false;
        for (const PolyOpParam& p : op.params) {
            if (p.isBool) {
                bool b = argBool(vals, p.name, p.value != 0.0);
                if (ImGui::Checkbox(tr(p.label.c_str()), &b)) vals[p.name] = b;
            } else {
                float v = float(argNumber(vals, p.name, p.value));
                const std::string fmt = std::string(tr(p.label.c_str())) + (p.max - p.min >= 10.0 ? " %.0f" : " %.2f");
                ImGui::SetNextItemWidth(-1);
                if (ui::SliderInside(("##" + p.name).c_str(), &v, float(p.min), float(p.max), fmt.c_str(), ImGuiSliderFlags_AlwaysClamp))
                    vals[p.name] = double(v);
                active = active || ImGui::IsItemActive();
            }
            if (!p.help.empty()) ui::HelpText(p.label.c_str(), nullptr, p.help.c_str(), kPolyPath);
        }
        if (active && op.preview && polyMode_) {
            previewOp = op.id;
            previewArgs = vals;
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();

    // A live preview while an op's slider moves (previewPolyOp never touches the Part).
    if (previewOp.empty()) {
        ov.previewOk = false;
        ov.previewOp.clear();
        return;
    }
    if (ov.previewOp == previewOp && ov.previewArgs == previewArgs) return;
    ov.previewOp = previewOp;
    ov.previewArgs = previewArgs;
    ov.previewEdges.clear();
    PolyOpOutput out;
    std::string error;
    ov.previewOk = previewPolyOp(doc_, previewOp, previewArgs, out, error);
    if (!ov.previewOk) return;
    const Mesh& r = out.mesh;
    for (uint32_t t : out.triangles) {
        if (t >= r.triangleCount()) continue;
        const glm::uvec3 tri = r.triangles[t];
        const uint32_t u = t < r.quadPartners.size() ? r.quadPartners[t] : Mesh::kNoQuad;
        const int diag = u < r.triangleCount() ? r.quadDiagonal(t, u) : -1;
        for (int k = 0; k < 3; ++k) {
            if (k == diag) continue;
            const uint32_t a = tri[k], b = tri[(k + 1) % 3];
            if (a < r.vertexCount() && b < r.vertexCount()) ov.previewEdges.emplace_back(r.positions[a], r.positions[b]);
        }
    }
}

} // namespace sf
