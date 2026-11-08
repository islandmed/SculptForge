// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Garments (Tool > Garments, the Pattern window): pattern panels drawn flat at real size (core/Garment.h).
//
// - The view is the pattern sheet in millimetres (a 10 / 100 mm grid and a scale bar): each panel filled in its own
//   colour with its name, size, notches and grain line; seams as coloured pairs of edges joined by a line, green when
//   their lengths match, red when not.
// - Drag a point (it snaps to the 5 mm grid and to other panels' points; Ctrl drags freely), a curve handle, or a
//   panel's inside to move it. Alt+click an edge adds a point there, Delete removes the selected point (or panel),
//   C makes the selected edge curved or straight again. Sew: click one edge, then the edge it is sewn to.
// - Every edit is a garment_* command (scriptable) and one step of the window's own undo (Ctrl+Z / Ctrl+Shift+Z
//   while the window has focus; with no pattern step, or focus elsewhere, they undo the scene): the pattern lives in
//   the project settings, which the document's undo (per Part, for meshes) does not cover.
#include "app/App.h"
#include "app/I18n.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include "core/Garment.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>

namespace sf {

namespace {

constexpr const char* kPath = "Tool > Garments";
constexpr size_t kUndoSteps = 100;
const SDL_DialogFileFilter kSvgFilter[] = {{"SVG", "svg"}};
const SDL_DialogFileFilter kDxfFilter[] = {{"DXF", "dxf"}};

using garment::Panel;
using garment::Pattern;

ImU32 panelColor(size_t i, float alpha) {
    static const ImU32 hues[] = {0x4f9de0, 0xe0904f, 0x6cc070, 0xc770c8, 0xd8c650, 0x5cc8c0, 0xe06a6a, 0x9a8ae6};
    const ImU32 c = hues[i % (sizeof hues / sizeof hues[0])];
    return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, int(alpha * 255));
}

float distToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b, float* along = nullptr) {
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    const float t = len2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    if (along) *along = t;
    return glm::length(p - (a + ab * t));
}

bool insideOutline(const std::vector<glm::vec2>& poly, glm::vec2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y) &&
            p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
            in = !in;
    return in;
}

std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", double(v));
    return b;
}

std::string pointsJson(const Panel& p) {
    std::string s = "[";
    for (size_t i = 0; i < p.points.size(); ++i) s += (i ? "," : "") + num(p.points[i].x) + "," + num(p.points[i].y);
    return s + "]";
}

std::string curvesJson(const Panel& p) {
    std::string s = "[";
    bool first = true;
    for (size_t e = 0; e < p.curves.size(); ++e) {
        const garment::EdgeCurve& c = p.curves[e];
        if (!c.curved) continue;
        s += (first ? "[" : ",[") + std::to_string(e) + "," + num(c.c1.x) + "," + num(c.c1.y) + "," + num(c.c2.x) + "," +
             num(c.c2.y) + "]";
        first = false;
    }
    return s + "]";
}

} // namespace

// The Pattern window's state: the pattern as last read from the document, the view, the selection, a drag, Sew's
// first edge and the window's undo (pattern JSON snapshots).
struct GarmentEditorState {
    Pattern pat;
    std::string json;         // projectSettings["garment"] as last read ("" = none)
    bool dirty = true;        // re-read the document's pattern
    bool fromCommand = false; // the change came from a garment_* command: the old pattern is an undo step
    bool restoring = false;   // an undo or redo wrote it
    Uint64 checkedAt = 0;
    std::vector<std::string> undo, redo;
    glm::vec2 center{0.0f};
    float zoom = 0.0f; // pixels per mm (0: frame on the next draw)
    bool frame = true;
    ImVec2 viewMin{0, 0}, viewSize{1, 1};
    bool hovered = false;
    bool focused = false; // the window has keyboard focus: Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y are the pattern's
    int panel = -1, point = -1, edge = -1; // the selection (panel and one of its points or edges)
    enum class Drag { None, Pan, Point, Handle, Panel } drag = Drag::None;
    int dragIndex = 0;     // Point: the point; Handle: edge * 2 + (0: c1, 1: c2)
    glm::vec2 pressMm{0.0f};
    Panel before;          // the dragged panel at the press
    bool moved = false;
    int hoverSeam = -1; // the seam row under the pointer (last frame)
    bool sewing = false, sewFlip = false;
    int sewPanel = -1, sewEdge = -1;
    bool snap = true;
    char name[64] = {};
    int nameFor = -1;
    float particle = 15.0f, thickness = 2.0f;
    int edits = 0; // commits from the window (UI tests)
    std::mutex fileMutex;
    std::string importPath, exportPath; // a file dialog's answer, taken on the next frame
};

// The window's work: reading the pattern, the view transform, edits as commands, undo.
struct GarmentUi {
    App& app;
    GarmentEditorState& s;

    ImVec2 toScreen(glm::vec2 mm) const {
        return ImVec2(s.viewMin.x + s.viewSize.x * 0.5f + (mm.x - s.center.x) * s.zoom,
                      s.viewMin.y + s.viewSize.y * 0.5f - (mm.y - s.center.y) * s.zoom);
    }
    glm::vec2 toMm(ImVec2 p) const {
        return {s.center.x + (p.x - s.viewMin.x - s.viewSize.x * 0.5f) / s.zoom,
                s.center.y - (p.y - s.viewMin.y - s.viewSize.y * 0.5f) / s.zoom};
    }
    float tolerance() const { return std::max(0.05f, 0.5f / std::max(s.zoom, 1e-3f)); }

    // Re-reads the pattern when a command changed it (or, once a second, anything else did: open, a script).
    void refresh() {
        const Uint64 now = SDL_GetTicksNS();
        if (!s.dirty && now - s.checkedAt < 1000000000ull) return;
        s.checkedAt = now;
        const Json& j = app.doc_.projectSettings["garment"];
        std::string text = j.isNull() ? std::string() : j.dump();
        if (text != s.json) {
            if (!s.restoring) {
                if (s.fromCommand) {
                    s.undo.push_back(s.json);
                    if (s.undo.size() > kUndoSteps) s.undo.erase(s.undo.begin());
                } else {
                    s.undo.clear(); // another project, or a change from outside: the old steps no longer apply
                    s.frame = true;
                }
                s.redo.clear();
            }
            s.json = std::move(text);
            s.pat = Pattern{};
            garment::documentPattern(app.doc_, s.pat);
            if (s.panel >= int(s.pat.panels.size())) s.panel = s.pat.panels.empty() ? -1 : int(s.pat.panels.size()) - 1;
            if (s.panel < 0 || s.point >= int(s.pat.panels[s.panel].points.size())) s.point = -1;
            if (s.panel < 0 || s.edge >= int(s.pat.panels[s.panel].points.size())) s.edge = -1;
            if (s.sewPanel >= int(s.pat.panels.size())) s.sewPanel = -1;
            s.particle = s.pat.particleDistance;
            s.thickness = s.pat.thickness;
            s.nameFor = -1;
        }
        s.dirty = s.fromCommand = s.restoring = false;
    }

    bool run(const char* name, Args a = {}) {
        app.runCommand(name, a);
        ++s.edits;
        refresh();
        return app.lastCommandOk_;
    }
    std::string panelRef(int i) const { return std::to_string(i + 1); }
    void setPanel(int i, const Panel& p) {
        run("garment_edit", {{"op", std::string("set_panel")}, {"panel", panelRef(i)}, {"points", pointsJson(p)},
                             {"curves", curvesJson(p)}});
    }

    void restore(std::vector<std::string>& from, std::vector<std::string>& to) {
        if (from.empty()) return;
        to.push_back(s.json);
        const std::string text = from.back();
        from.pop_back();
        Json j;
        std::string error;
        if (text.empty()) garment::setDocumentPattern(app.doc_, Pattern{});
        else if (Json::parse(text, j, error)) {
            app.doc_.projectSettings.set("garment", j);
            app.doc_.modified = true;
        }
        s.restoring = s.dirty = true;
        refresh();
        app.status_ = &from == &s.undo ? "Pattern: undone" : "Pattern: redone";
    }

    void frameAll() {
        glm::vec2 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        for (const Panel& p : s.pat.panels) {
            lo = glm::min(lo, garment::panelMin(p));
            hi = glm::max(hi, garment::panelMax(p));
        }
        if (s.pat.panels.empty()) lo = glm::vec2(-500.0f), hi = glm::vec2(500.0f);
        s.center = (lo + hi) * 0.5f;
        const glm::vec2 size = glm::max(hi - lo, glm::vec2(50.0f)) * 1.15f;
        s.zoom = std::clamp(std::min(s.viewSize.x / size.x, s.viewSize.y / size.y), 0.02f, 200.0f);
        s.frame = false;
    }

    // What is under the pointer: a point (any panel, the selected one first), an edge (with how far along it).
    bool pointAt(ImVec2 m, int& panel, int& point) const {
        const float r = 7.0f * app.uiScale_;
        float best = r;
        bool found = false;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < int(s.pat.panels.size()); ++i) {
                if ((pass == 0) != (i == s.panel)) continue;
                const Panel& p = s.pat.panels[i];
                for (int k = 0; k < int(p.points.size()); ++k) {
                    const ImVec2 q = toScreen(p.points[k]);
                    const float d = std::hypot(q.x - m.x, q.y - m.y);
                    if (d < best) best = d, panel = i, point = k, found = true;
                }
                if (found) return true;
            }
        return found;
    }
    bool edgeAt(ImVec2 m, int& panel, int& edge, float& t) const {
        const glm::vec2 at = toMm(m);
        float best = 6.0f * app.uiScale_ / s.zoom;
        bool found = false;
        for (int i = 0; i < int(s.pat.panels.size()); ++i) {
            const Panel& p = s.pat.panels[i];
            for (uint32_t e = 0; e < p.points.size(); ++e) {
                const std::vector<glm::vec2> line = garment::edgePolyline(p, e, tolerance());
                float total = 0.0f;
                for (size_t k = 1; k < line.size(); ++k) total += glm::length(line[k] - line[k - 1]);
                float run = 0.0f;
                for (size_t k = 1; k < line.size(); ++k) {
                    float u;
                    const float d = distToSegment(at, line[k - 1], line[k], &u);
                    const float seg = glm::length(line[k] - line[k - 1]);
                    if (d < best) {
                        best = d, panel = i, edge = int(e), found = true;
                        t = total > 0.0f ? (run + u * seg) / total : 0.5f;
                    }
                    run += seg;
                }
            }
        }
        return found;
    }
    int panelAt(ImVec2 m) const {
        const glm::vec2 at = toMm(m);
        for (int pass = 0; pass < 2; ++pass)
            for (int i = int(s.pat.panels.size()) - 1; i >= 0; --i)
                if ((pass == 0) == (i == s.panel) && insideOutline(garment::outline(s.pat.panels[i], 1.0f), at)) return i;
        return -1;
    }
    // A dragged point lands on another panel's point within reach, else on the 5 mm grid (Ctrl: neither).
    glm::vec2 snapped(glm::vec2 mm, int panel, int point, bool free) const {
        if (free || !s.snap) return mm;
        const float reach = 8.0f * app.uiScale_ / s.zoom;
        for (int i = 0; i < int(s.pat.panels.size()); ++i)
            for (int k = 0; k < int(s.pat.panels[i].points.size()); ++k)
                if (!(i == panel && k == point) && glm::length(s.pat.panels[i].points[k] - mm) < reach)
                    return s.pat.panels[i].points[k];
        return glm::round(mm / 5.0f) * 5.0f;
    }

    void toggleCurve() {
        if (s.panel < 0 || s.edge < 0) {
            app.status_ = "Select an edge first (click it)";
            return;
        }
        Panel p = s.pat.panels[s.panel];
        p.curves.resize(p.points.size());
        garment::EdgeCurve& c = p.curves[s.edge];
        const glm::vec2 a = p.points[s.edge], b = p.points[(s.edge + 1) % p.points.size()];
        c.curved = !c.curved;
        if (c.curved) { // handles a third of the way along, bowed out a little (outward of a counter-clockwise outline)
            const glm::vec2 out = glm::vec2(b.y - a.y, a.x - b.x) * 0.15f;
            c.c1 = glm::mix(a, b, 1.0f / 3.0f) + out;
            c.c2 = glm::mix(a, b, 2.0f / 3.0f) + out;
        }
        setPanel(s.panel, p);
    }
    void removeSelected() {
        if (s.panel < 0) return;
        if (s.point >= 0) {
            run("garment_edit", {{"op", std::string("remove_point")}, {"panel", panelRef(s.panel)}, {"point", double(s.point + 1)}});
            s.point = -1;
        } else {
            run("garment_remove_panel", {{"panel", panelRef(s.panel)}});
            s.panel = s.edge = -1;
        }
    }
    void sewClick(int panel, int edge) {
        if (s.sewPanel < 0) {
            s.sewPanel = panel, s.sewEdge = edge;
            app.status_ = "Sew: now click the edge it is sewn to (Esc cancels)";
            return;
        }
        if (panel == s.sewPanel && edge == s.sewEdge) return;
        run("garment_sew", {{"a_panel", panelRef(s.sewPanel)}, {"a_edge", double(s.sewEdge + 1)}, {"b_panel", panelRef(panel)},
                            {"b_edge", double(edge + 1)}, {"flipped", s.sewFlip}});
        s.sewPanel = s.sewEdge = -1;
    }
};

void SDLCALL App::onGarmentFileChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop();
    if (!files || !files[0]) return;
    auto* request = static_cast<std::pair<App*, bool>*>(userdata); // (app, export)
    GarmentEditorState& s = *request->first->garments_;
    std::lock_guard lock(s.fileMutex);
    (request->second ? s.exportPath : s.importPath) = files[0];
}

void App::garmentsChanged(bool garmentCommand) {
    if (!garments_) return;
    garments_->dirty = true;
    garments_->fromCommand = garments_->fromCommand || garmentCommand;
}

void App::registerGarmentActions() {
    auto add = [this](const char* id, const char* label, const char* icon, const char* shortcut, std::function<void()> run,
                      std::function<bool()> checked, std::function<bool()> enabled, const char* description,
                      const char* command = "") {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = kPath;
        a.shortcut = shortcut;
        a.run = std::move(run);
        a.checked = std::move(checked);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    if (!garments_) garments_ = std::make_shared<GarmentEditorState>();
    auto ui = [this] { return GarmentUi{*this, *garments_}; };
    auto have = [this] { return !garments_->pat.panels.empty(); };
    auto selected = [this] { return garments_->panel >= 0 && garments_->panel < int(garments_->pat.panels.size()); };
    static std::pair<App*, bool> importRequest{this, false}, exportRequest{this, true};
    add("ui.garments", "Pattern", ICON_LC_SHIRT, "",
        [this] {
            showGarments_ = !showGarments_;
            garments_->dirty = true;
            if (showGarments_) garments_->frame = true;
        },
        [this] { return showGarments_; }, nullptr,
        "Open the Pattern window: the garment's panels flat at real size. Drag points and curve handles, sew edges "
        "together, import and export SVG.");
    add("garment.new_tshirt", "T-Shirt", ICON_LC_SHIRT, "",
        [this, ui] {
            showGarments_ = true;
            ui().run("garment_new", {{"template", std::string("tshirt")}});
            garments_->frame = true;
        },
        nullptr, nullptr, "Start from a T-shirt pattern: front, back and two sleeves, sewn. Replaces the pattern.",
        "garment_new");
    add("garment.add_rect", "Rectangle", ICON_LC_RECTANGLE_HORIZONTAL, "",
        [this, ui] {
            showGarments_ = true;
            GarmentUi u = ui();
            u.run("garment_add_panel", {{"width_mm", 400.0}, {"height_mm", 300.0}});
            garments_->panel = int(garments_->pat.panels.size()) - 1;
            garments_->frame = true;
        },
        nullptr, nullptr, "Add a 40 x 30 cm rectangle panel, ready to reshape.", "garment_add_panel");
    add("garment.duplicate", "Duplicate", ICON_LC_COPY, "",
        [ui] {
            GarmentUi u = ui();
            if (u.run("garment_edit", {{"op", std::string("duplicate")}, {"panel", u.panelRef(u.s.panel)}}))
                u.s.panel = int(u.s.pat.panels.size()) - 1;
        },
        nullptr, selected, "Copy the selected panel beside it (Ctrl+D over the window).", "garment_edit");
    add("garment.mirror_copy", "Mirror Copy", ICON_LC_FLIP_HORIZONTAL_2, "",
        [ui] {
            GarmentUi u = ui();
            if (u.run("garment_mirror_copy", {{"panel", u.panelRef(u.s.panel)}})) u.s.panel = int(u.s.pat.panels.size()) - 1;
        },
        nullptr, selected, "A mirrored copy of the selected panel (the other sleeve) beside it, with no seams.",
        "garment_mirror_copy");
    add("garment.mirror", "Mirror", nullptr, "",
        [ui] {
            GarmentUi u = ui();
            u.run("garment_edit", {{"op", std::string("mirror")}, {"panel", u.panelRef(u.s.panel)}});
        },
        nullptr, selected, "Flip the selected panel left to right in place.", "garment_edit");
    add("garment.delete", "Delete Panel", ICON_LC_TRASH_2, "",
        [ui] {
            GarmentUi u = ui();
            u.s.point = -1;
            u.removeSelected();
        },
        nullptr, selected, "Remove the selected panel and its seams.", "garment_remove_panel");
    add("garment.sew", "Sew", ICON_LC_SPOOL, "",
        [this] {
            garments_->sewing = !garments_->sewing;
            garments_->sewPanel = -1;
            if (garments_->sewing) status_ = "Sew: click an edge, then the edge it is sewn to";
        },
        [this] { return garments_->sewing; }, [this] { return garments_->pat.panels.size() > 0; },
        "Sew two edges: click one, then the other. Their lengths show while you pick; Flip sews start to start.",
        "garment_sew");
    add("garment.export_svg", "Export SVG", ICON_LC_FILE_DOWN, "",
        [this] { SDL_ShowSaveFileDialog(onGarmentFileChosen, &exportRequest, window_, kSvgFilter, 1, nullptr); }, nullptr,
        have, "Save the panels at real size for printing and sewing: sewing and cutting lines, notches, grain lines.",
        "garment_export");
    add("garment.export_dxf", "Export DXF", ICON_LC_FILE_DOWN, "",
        [this] { SDL_ShowSaveFileDialog(onGarmentFileChosen, &exportRequest, window_, kDxfFilter, 1, nullptr); }, nullptr,
        have, "Save the outlines in millimetres for cutting machines.", "garment_export");
    add("garment.import_svg", "Import SVG", ICON_LC_FILE_UP, "",
        [this] { SDL_ShowOpenFileDialog(onGarmentFileChosen, &importRequest, window_, kSvgFilter, 1, nullptr, false); },
        nullptr, nullptr, "Add panels from an SVG: every closed shape, at the file's real size.", "garment_import");
    add("garment.frame", "Frame", ICON_LC_FOCUS, "", [this] { garments_->frame = true; }, nullptr, nullptr,
        "Fit every panel in the view (F over it).");
    add("garment.undo", "Undo", ICON_LC_UNDO_2, "", [this, ui] { GarmentUi u = ui(); u.restore(u.s.undo, u.s.redo); },
        nullptr, [this] { return !garments_->undo.empty(); }, "Undo the last pattern edit (Ctrl+Z over the window).");
    add("garment.redo", "Redo", ICON_LC_REDO_2, "", [this, ui] { GarmentUi u = ui(); u.restore(u.s.redo, u.s.undo); },
        nullptr, [this] { return !garments_->redo.empty(); }, "Redo it (Ctrl+Shift+Z over the window).");
}

void App::drawGarmentsContents() {
    GarmentUi u{*this, *garments_};
    u.refresh();
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ui::ActionButton(*this, "ui.garments", ImVec2(-1, 0), ui::Look::IconLabel);
    ui::ActionButton(*this, "garment.new_tshirt", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "garment.import_svg", ImVec2(-1, 0));
    const Pattern& pat = garments_->pat;
    if (pat.panels.empty()) ImGui::TextDisabled("%s", tr("No pattern yet"));
    else ImGui::TextDisabled(tr("%d panels, %d seams"), int(pat.panels.size()), int(pat.seams.size()));
}

void App::drawGarments() {
    if (!showGarments_) return;
    GarmentEditorState& s = *garments_;
    GarmentUi u{*this, s};
    const float scale = uiScale_;
    u.refresh();
    { // a file dialog answered
        std::string in, out;
        {
            std::lock_guard lock(s.fileMutex);
            in.swap(s.importPath);
            out.swap(s.exportPath);
        }
        if (!in.empty() && u.run("garment_import", {{"path", in}})) s.frame = true;
        if (!out.empty()) runCommand("garment_export", {{"path", out}});
    }
    const CanvasRect cr = canvasRect();
    ImGui::SetNextWindowPos(ImVec2(cr.x + cr.w * 0.34f, cr.y + 6 * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(cr.w * 0.66f - 6 * scale, cr.h - 12 * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(520 * scale, cr.w), std::min(360 * scale, cr.h)), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::Begin(tr("Pattern"), &showGarments_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        s.hovered = s.focused = false;
        ImGui::End();
        return;
    }
    s.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImGuiStyle& style = ImGui::GetStyle();
    Pattern& pat = s.pat;
    if (s.panel >= int(pat.panels.size())) s.panel = -1;

    // Toolbar.
    for (const char* id : {"garment.new_tshirt", "garment.add_rect", "garment.import_svg", "garment.export_svg", "garment.export_dxf"}) {
        ui::ActionButton(*this, id, ImVec2(0, 0), ui::Look::Icon);
        ImGui::SameLine();
    }
    ImGui::SameLine(0, 12 * scale);
    ui::ActionButton(*this, "garment.undo", ImVec2(0, 0), ui::Look::Icon);
    ImGui::SameLine();
    ui::ActionButton(*this, "garment.redo", ImVec2(0, 0), ui::Look::Icon);
    ImGui::SameLine(0, 12 * scale);
    ui::ActionButton(*this, "garment.sew", ImVec2(0, 0), ui::Look::IconLabel);
    ImGui::SameLine();
    ui::Checkbox("Flip##garmentflip", &s.sewFlip);
    ui::HelpText("Flip", nullptr, "Sew start to start: the fabric turns over along the seam (rarely wanted).", kPath);
    ImGui::SameLine(0, 12 * scale);
    ui::Checkbox("Snap##garmentsnap", &s.snap);
    ui::HelpText("Snap", nullptr, "Dragged points land on the 5 mm grid and on other panels' points. Ctrl drags freely.", kPath);
    ImGui::SameLine();
    const float frameW = ImGui::CalcTextSize(tr("Frame")).x + ImGui::GetFrameHeight() + style.FramePadding.x * 2;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - frameW));
    ui::ActionButton(*this, "garment.frame", ImVec2(frameW, 0));

    // The view, the sidebar on the right, a status line under them.
    const float sideW = 240 * scale;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float statusH = ImGui::GetTextLineHeightWithSpacing() + 2 * scale;
    s.viewSize = ImVec2(std::max(avail.x - sideW - style.ItemSpacing.x, 64.0f), std::max(avail.y - statusH, 64.0f));
    s.viewMin = ImGui::GetCursorScreenPos();
    if (s.frame || s.zoom <= 0.0f) u.frameAll();
    ImGui::InvisibleButton("##garmentview", s.viewSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                                            ImGuiButtonFlags_MouseButtonMiddle);
    s.hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const SDL_Keymod mod = SDL_GetModState();
    const bool ctrl = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, alt = (mod & SDL_KMOD_ALT) != 0;
    using D = GarmentEditorState::Drag;

    // Hover: a point, a curve handle of the selected panel, an edge.
    int hp = -1, hk = -1, he = -1, hePanel = -1, hh = -1;
    float ht = 0.5f;
    if (s.hovered && s.drag == D::None) {
        if (s.panel >= 0 && !s.sewing) {
            const Panel& p = pat.panels[s.panel];
            for (int e = 0; e < int(p.curves.size()) && hh < 0; ++e)
                for (int k = 0; k < 2 && p.curves[e].curved; ++k) {
                    const ImVec2 q = u.toScreen(k ? p.curves[e].c2 : p.curves[e].c1);
                    if (std::hypot(q.x - mouse.x, q.y - mouse.y) < 6 * scale) hh = e * 2 + k;
                }
        }
        if (hh < 0 && !s.sewing) u.pointAt(mouse, hp, hk);
        if (hh < 0 && hk < 0) u.edgeAt(mouse, hePanel, he, ht);
    }

    // Input.
    if (s.hovered && io.MouseWheel != 0.0f && s.drag == D::None) {
        const glm::vec2 before = u.toMm(mouse);
        s.zoom = std::clamp(s.zoom * std::pow(1.18f, io.MouseWheel), 0.02f, 200.0f);
        s.center += before - u.toMm(mouse);
    }
    if (s.hovered && s.drag == D::None &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
         (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && spaceHeld_)))
        s.drag = D::Pan;
    else if (s.hovered && s.drag == D::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        s.pressMm = u.toMm(mouse);
        s.moved = false;
        if (s.sewing) {
            if (he >= 0) u.sewClick(hePanel, he);
        } else if (hh >= 0) {
            s.drag = D::Handle, s.dragIndex = hh, s.before = pat.panels[s.panel];
        } else if (hk >= 0) {
            s.panel = hp, s.point = hk, s.edge = -1;
            s.drag = D::Point, s.dragIndex = hk, s.before = pat.panels[hp];
        } else if (he >= 0 && alt) {
            u.run("garment_edit", {{"op", std::string("split")}, {"panel", u.panelRef(hePanel)}, {"edge", double(he + 1)}, {"t", double(ht)}});
            s.panel = hePanel, s.point = he + 1, s.edge = -1;
        } else if (he >= 0) {
            s.panel = hePanel, s.edge = he, s.point = -1;
        } else if (const int at = u.panelAt(mouse); at >= 0) {
            s.panel = at, s.point = s.edge = -1;
            s.drag = D::Panel, s.before = pat.panels[at];
        } else {
            s.panel = s.point = s.edge = -1;
        }
    }
    if (s.drag == D::Pan) {
        s.center -= glm::vec2(io.MouseDelta.x, -io.MouseDelta.y) / s.zoom;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            s.drag = D::None;
    } else if (s.drag != D::None && s.panel >= 0) { // live while dragging; one command on release
        Panel& p = pat.panels[s.panel];
        const glm::vec2 at = u.toMm(mouse);
        s.moved = s.moved || glm::length(at - s.pressMm) * s.zoom > 3.0f;
        if (s.moved) {
            p = s.before;
            if (s.drag == D::Point) {
                p.points[s.dragIndex] = u.snapped(at, s.panel, s.dragIndex, ctrl);
            } else if (s.drag == D::Handle) {
                garment::EdgeCurve& c = p.curves[s.dragIndex / 2];
                (s.dragIndex & 1 ? c.c2 : c.c1) = at;
            } else {
                glm::vec2 d = at - s.pressMm;
                if (s.snap && !ctrl) d = glm::round(d / 5.0f) * 5.0f;
                for (glm::vec2& q : p.points) q += d;
                for (garment::EdgeCurve& c : p.curves) c.c1 += d, c.c2 += d;
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const D was = s.drag;
            s.drag = D::None;
            if (s.moved) {
                if (was == D::Panel) {
                    const glm::vec2 d = p.points[0] - s.before.points[0];
                    pat.panels[s.panel] = s.before;
                    u.run("garment_edit", {{"op", std::string("move")}, {"panel", u.panelRef(s.panel)}, {"dx", double(d.x)}, {"dy", double(d.y)}});
                } else {
                    const Panel edited = p;
                    pat.panels[s.panel] = s.before;
                    u.setPanel(s.panel, edited);
                }
            }
        }
    }

    // Draw.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 viewMax(s.viewMin.x + s.viewSize.x, s.viewMin.y + s.viewSize.y);
    dl->AddRectFilled(s.viewMin, viewMax, IM_COL32(28, 28, 30, 255));
    dl->PushClipRect(s.viewMin, viewMax, true);
    {
        const glm::vec2 lo = u.toMm(ImVec2(s.viewMin.x, viewMax.y)), hi = u.toMm(ImVec2(viewMax.x, s.viewMin.y));
        for (const float step : {10.0f, 100.0f}) {
            if (step * s.zoom < 6.0f) continue;
            const ImU32 col = step > 50.0f ? IM_COL32(70, 70, 76, 255) : IM_COL32(44, 44, 48, 255);
            for (float x = std::floor(lo.x / step) * step; x <= hi.x; x += step)
                dl->AddLine(u.toScreen({x, lo.y}), u.toScreen({x, hi.y}), x == 0.0f ? IM_COL32(95, 95, 102, 255) : col);
            for (float y = std::floor(lo.y / step) * step; y <= hi.y; y += step)
                dl->AddLine(u.toScreen({lo.x, y}), u.toScreen({hi.x, y}), y == 0.0f ? IM_COL32(95, 95, 102, 255) : col);
        }
    }
    const float tol = u.tolerance();
    std::vector<ImVec2> poly;
    auto toPoly = [&](const std::vector<glm::vec2>& line) {
        poly.resize(line.size());
        for (size_t i = 0; i < line.size(); ++i) poly[i] = u.toScreen(line[i]);
    };
    for (size_t i = 0; i < pat.panels.size(); ++i) {
        const Panel& p = pat.panels[i];
        const bool sel = int(i) == s.panel;
        toPoly(garment::outline(p, tol));
        if (poly.size() >= 3) dl->AddConcavePolyFilled(poly.data(), int(poly.size()), panelColor(i, sel ? 0.34f : 0.22f));
        dl->AddPolyline(poly.data(), int(poly.size()), panelColor(i, 1.0f), ImDrawFlags_Closed, (sel ? 2.0f : 1.4f) * scale);
        // Notches, the grain line, the name and size.
        for (const garment::Notch& n : p.notches) {
            glm::vec2 tan;
            const glm::vec2 at = garment::edgePoint(p, n.edge, n.t, &tan);
            const glm::vec2 out = glm::normalize(glm::vec2(tan.y, -tan.x) + 1e-9f) * (6.0f * scale / s.zoom);
            dl->AddLine(u.toScreen(at - out), u.toScreen(at + out), IM_COL32(235, 235, 235, 255), 1.5f * scale);
        }
        const glm::vec2 lo = garment::panelMin(p), hi = garment::panelMax(p), mid = (lo + hi) * 0.5f;
        const float g = glm::radians(p.grainDegrees);
        const glm::vec2 dir(std::cos(g), std::sin(g));
        const float len = 0.3f * std::min(hi.x - lo.x, hi.y - lo.y);
        const glm::vec2 side = glm::vec2(-dir.y, dir.x) * (0.8f * len); // beside the name, not through it
        const ImVec2 g0 = u.toScreen(mid + side - dir * len), g1 = u.toScreen(mid + side + dir * len);
        dl->AddLine(g0, g1, panelColor(i, 0.7f), 1.2f * scale);
        dl->AddCircleFilled(g1, 2.5f * scale, panelColor(i, 0.9f));
        char label[160];
        std::snprintf(label, sizeof label, "%s\n%.1f x %.1f cm", p.name.c_str(), double(hi.x - lo.x) * 0.1, double(hi.y - lo.y) * 0.1);
        const ImVec2 ts = ImGui::CalcTextSize(label);
        const ImVec2 c = u.toScreen(mid);
        if (ts.x < 0.8f * (hi.x - lo.x) * s.zoom && ts.y < 0.5f * (hi.y - lo.y) * s.zoom) dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y - 4 * scale), IM_COL32(230, 230, 230, 230), label);
    }
    // Seams: both sides' ranges in the seam's colour, a line between their middles (green: lengths match).
    for (size_t k = 0; k < pat.seams.size(); ++k) {
        const garment::Seam& sm = pat.seams[k];
        if (sm.a.panel >= pat.panels.size() || sm.b.panel >= pat.panels.size()) continue;
        const garment::SeamCheck chk = garment::checkSeam(pat, sm);
        auto onHovered = [&](const garment::SeamSide& sd) { return he >= 0 && int(sd.panel) == hePanel && int(sd.edge) == he; };
        const bool active = int(k) == s.hoverSeam || onHovered(sm.a) || onHovered(sm.b);
        ImVec2 mids[2];
        for (int side = 0; side < 2; ++side) {
            const garment::SeamSide& sd = side ? sm.b : sm.a;
            const Panel& p = pat.panels[sd.panel];
            if (sd.edge >= p.points.size()) continue;
            poly.clear();
            for (int j = 0; j <= 16; ++j)
                poly.push_back(u.toScreen(garment::edgePoint(p, sd.edge, sd.t0 + (sd.t1 - sd.t0) * float(j) / 16.0f)));
            dl->AddPolyline(poly.data(), int(poly.size()), panelColor(k + 3, 0.95f), (active ? 6.0f : 4.0f) * scale, ImDrawFlags_None);
            mids[side] = poly[8];
        }
        if (active) // the connector: only for the hovered seam row or the hovered edge's seam
            dl->AddLine(mids[0], mids[1], chk.lengthsMatch && !chk.twisted ? IM_COL32(90, 200, 110, 220) : IM_COL32(235, 80, 70, 230),
                        1.5f * scale);
    }
    // The selected panel's points and curve handles; the hovered edge or point.
    if (s.panel >= 0) {
        const Panel& p = pat.panels[s.panel];
        for (size_t e = 0; e < p.curves.size(); ++e) {
            if (!p.curves[e].curved) continue;
            const ImVec2 a = u.toScreen(p.points[e]), b = u.toScreen(p.points[(e + 1) % p.points.size()]);
            const ImVec2 c1 = u.toScreen(p.curves[e].c1), c2 = u.toScreen(p.curves[e].c2);
            dl->AddLine(a, c1, IM_COL32(180, 180, 190, 160), 1.0f * scale);
            dl->AddLine(b, c2, IM_COL32(180, 180, 190, 160), 1.0f * scale);
            for (int k = 0; k < 2; ++k)
                dl->AddCircleFilled(k ? c2 : c1, 4.0f * scale, hh == int(e) * 2 + k ? theme::u32(theme::accent) : IM_COL32(200, 200, 210, 255));
        }
        if (s.edge >= 0 && s.edge < int(p.points.size())) {
            toPoly(garment::edgePolyline(p, uint32_t(s.edge), tol));
            dl->AddPolyline(poly.data(), int(poly.size()), theme::u32(theme::accent), 3.0f * scale, ImDrawFlags_None);
        }
    }
    for (size_t i = 0; i < pat.panels.size(); ++i) {
        const Panel& p = pat.panels[i];
        const bool sel = int(i) == s.panel;
        for (size_t k = 0; k < p.points.size(); ++k) {
            const ImVec2 q = u.toScreen(p.points[k]);
            const float r = (sel ? 4.0f : 2.5f) * scale;
            const bool on = (sel && int(k) == s.point) || (int(i) == hp && int(k) == hk);
            dl->AddRectFilled(ImVec2(q.x - r, q.y - r), ImVec2(q.x + r, q.y + r), on ? theme::u32(theme::accent) : IM_COL32(235, 235, 240, 255));
        }
    }
    if (he >= 0) {
        toPoly(garment::edgePolyline(pat.panels[hePanel], uint32_t(he), tol));
        dl->AddPolyline(poly.data(), int(poly.size()), theme::u32(theme::accent, 0.7f), 2.5f * scale, ImDrawFlags_None);
    }
    if (s.sewing && s.sewPanel >= 0 && s.sewEdge < int(pat.panels[s.sewPanel].points.size())) {
        toPoly(garment::edgePolyline(pat.panels[s.sewPanel], uint32_t(s.sewEdge), tol));
        dl->AddPolyline(poly.data(), int(poly.size()), theme::u32(theme::accent), 4.0f * scale, ImDrawFlags_None);
    }
    { // scale bar
        float bar = 10.0f;
        for (const float b : {10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f})
            if (b * s.zoom <= 160 * scale) bar = b;
        const ImVec2 a(s.viewMin.x + 12 * scale, viewMax.y - 14 * scale), b(a.x + bar * s.zoom, a.y);
        dl->AddLine(a, b, IM_COL32(220, 220, 220, 255), 2.0f * scale);
        dl->AddLine(ImVec2(a.x, a.y - 4 * scale), ImVec2(a.x, a.y + 2 * scale), IM_COL32(220, 220, 220, 255), 1.5f * scale);
        dl->AddLine(ImVec2(b.x, b.y - 4 * scale), ImVec2(b.x, b.y + 2 * scale), IM_COL32(220, 220, 220, 255), 1.5f * scale);
        char t[24];
        if (bar >= 1000.0f) std::snprintf(t, sizeof t, "%.0f m", double(bar) / 1000.0);
        else if (bar >= 10.0f) std::snprintf(t, sizeof t, "%.0f cm", double(bar) / 10.0);
        dl->AddText(ImVec2(a.x, a.y - ImGui::GetTextLineHeight() - 5 * scale), IM_COL32(220, 220, 220, 255), t);
    }
    if (pat.panels.empty()) {
        const char* hint = tr("No pattern yet: start from a T-shirt, add a rectangle or import an SVG");
        const float wrap = std::min(s.viewSize.x - 32 * scale, 420 * scale);
        const ImVec2 ts = ImGui::CalcTextSize(hint, nullptr, false, wrap);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(s.viewMin.x + (s.viewSize.x - ts.x) * 0.5f, s.viewMin.y + s.viewSize.y * 0.45f),
                    IM_COL32(170, 170, 170, 255), hint, nullptr, wrap);
    }
    dl->PopClipRect();

    // Sidebar.
    ImGui::SameLine();
    ImGui::BeginChild("##garmentside", ImVec2(sideW, s.viewSize.y), false);
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    ImGui::TextDisabled("%s", tr("Panels"));
    for (size_t i = 0; i < pat.panels.size(); ++i) {
        ImGui::PushID(int(i));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + h * 0.2f), ImVec2(at.x + h * 0.6f, at.y + h * 0.8f), panelColor(i, 1.0f));
        ImGui::SetCursorScreenPos(ImVec2(at.x + h, at.y));
        if (ImGui::Selectable(pat.panels[i].name.c_str(), int(i) == s.panel)) s.panel = int(i), s.point = s.edge = -1;
        ImGui::PopID();
    }
    if (s.panel >= 0) {
        const Panel& p = pat.panels[s.panel];
        ImGui::Spacing();
        if (s.nameFor != s.panel) {
            std::snprintf(s.name, sizeof s.name, "%s", p.name.c_str());
            s.nameFor = s.panel;
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##garmentname", s.name, sizeof s.name);
        if (ImGui::IsItemDeactivatedAfterEdit() && p.name != s.name && s.name[0])
            u.run("garment_edit", {{"op", std::string("rename")}, {"panel", u.panelRef(s.panel)}, {"name", std::string(s.name)}});
        ui::HelpText("Name", nullptr, "The panel's name.", kPath);
        const glm::vec2 lo = garment::panelMin(p), hi = garment::panelMax(p);
        ImGui::TextDisabled(tr("%.1f x %.1f cm, %.0f cm2"), double(hi.x - lo.x) * 0.1, double(hi.y - lo.y) * 0.1,
                            double(garment::panelArea(p)) * 0.01);
        if (s.edge >= 0 && s.edge < int(p.points.size()))
            ImGui::TextDisabled(tr("%s: %.1f cm"), garment::edgeName(pat, uint32_t(s.panel), uint32_t(s.edge)).c_str(),
                                double(garment::edgeLength(p, uint32_t(s.edge))) * 0.1);
        ui::ActionButton(*this, "garment.duplicate", ImVec2(half, 0));
        ImGui::SameLine();
        ui::ActionButton(*this, "garment.mirror_copy", ImVec2(-1, 0));
        ui::ActionButton(*this, "garment.mirror", ImVec2(half, 0));
        ImGui::SameLine();
        ui::ActionButton(*this, "garment.delete", ImVec2(-1, 0));
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("Seams"));
    int unsew = -1;
    s.hoverSeam = -1;
    for (size_t k = 0; k < pat.seams.size(); ++k) {
        const garment::SeamCheck chk = garment::checkSeam(pat, pat.seams[k]);
        ImGui::PushID(int(k));
        if (ui::GlyphButton("##unsew", ICON_LC_X, ImVec2(ImGui::GetFrameHeight(), 0))) unsew = int(k);
        ui::DelayedTooltip("Unsew");
        bool rowHovered = ImGui::IsItemHovered();
        ImGui::SameLine();
        // The edge names (wrapped), then the lengths on a line of their own (never split).
        ImGui::BeginGroup();
        ImGui::TextWrapped("%s", chk.names.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, chk.lengthsMatch && !chk.twisted ? ImVec4(0.55f, 0.85f, 0.6f, 1) : ImVec4(0.95f, 0.45f, 0.4f, 1));
        ImGui::TextUnformatted(chk.lengths.c_str());
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        rowHovered = rowHovered || ImGui::IsItemHovered();
        if (rowHovered) s.hoverSeam = int(k);
        ImGui::PopID();
    }
    if (unsew >= 0) u.run("garment_unsew", {{"seam", double(unsew + 1)}});
    if (s.sewing && s.sewPanel >= 0) {
        float la = garment::edgeLength(pat.panels[s.sewPanel], uint32_t(s.sewEdge)), lb = he >= 0 ? garment::edgeLength(pat.panels[hePanel], uint32_t(he)) : 0.0f;
        if (he >= 0) ImGui::TextDisabled(tr("Sewing %.1f cm to %.1f cm"), double(la) * 0.1, double(lb) * 0.1);
        else ImGui::TextDisabled(tr("Sewing %.1f cm to..."), double(la) * 0.1);
    }
    if (!pat.panels.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("Simulation"));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##garmentparticle", &s.particle, 4.0f, 50.0f, tr("Particles %.0f mm"), ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemDeactivatedAfterEdit()) u.run("garment_set", {{"particle_mm", double(s.particle)}});
        ui::HelpText("Particles", nullptr, "Distance between the cloth's particles: smaller makes finer folds and drapes slower.", kPath);
        static const char* fabrics[] = {"silk", "cotton", "denim", "leather"};
        static const char* fabricLabels[] = {"Silk", "Cotton", "Denim", "Leather"};
        int fabric = 1;
        for (int k = 0; k < 4; ++k)
            if (pat.fabric == fabrics[k]) fabric = k;
        const char* labels[4];
        for (int k = 0; k < 4; ++k) labels[k] = tr(fabricLabels[k]);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##garmentfabric", &fabric, labels, 4)) u.run("garment_set", {{"fabric", std::string(fabrics[fabric])}});
        ui::HelpText("Garment fabric", nullptr, "How the cloth stretches, bends and weighs.", kPath);
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##garmentthick", &s.thickness, 0.0f, 20.0f, tr("Thickness %.1f mm"));
        if (ImGui::IsItemDeactivatedAfterEdit()) u.run("garment_set", {{"thickness_mm", double(s.thickness)}});
        ui::HelpText("Thickness", nullptr, "Thickness of the Part the garment becomes (0: a single surface).", kPath);
        const std::vector<std::string> problems = garment::patternProblems(pat);
        if (!problems.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.7f, 0.35f, 1));
            for (const std::string& line : problems) ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    // Status: where the pointer is, what a click would do.
    if (s.hovered) {
        const glm::vec2 at = u.toMm(mouse);
        if (he >= 0)
            ImGui::TextDisabled(tr("%.1f, %.1f cm   %s: %.1f cm (Alt+click adds a point, C curves it)"), double(at.x) * 0.1,
                                double(at.y) * 0.1, garment::edgeName(pat, uint32_t(hePanel), uint32_t(he)).c_str(),
                                double(garment::edgeLength(pat.panels[hePanel], uint32_t(he))) * 0.1);
        else ImGui::TextDisabled(tr("%.1f, %.1f cm"), double(at.x) * 0.1, double(at.y) * 0.1);
    } else {
        ImGui::TextDisabled("%s", tr("Drag points to reshape, Alt+click an edge to add one, Delete removes it"));
    }
    ImGui::End();
}

bool App::garmentKey(SDL_Keycode key, SDL_Keymod mod) {
    if (!showGarments_ || !garments_ || ImGui::GetIO().WantTextInput) return false;
    GarmentEditorState& s = *garments_;
    GarmentUi u{*this, s};
    const bool ctrl = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, shift = (mod & SDL_KMOD_SHIFT) != 0;
    // Undo / redo go to the pattern while the window has focus and the pattern has a step; otherwise the key falls
    // through to the scene's undo (never swallowed: ui_monkey seed 4).
    const bool redoKey = (key == SDLK_Z && ctrl && shift) || (key == SDLK_Y && ctrl), undoKey = key == SDLK_Z && ctrl && !shift;
    if (redoKey || undoKey) {
        std::vector<std::string>& from = undoKey ? s.undo : s.redo;
        if (!s.focused || from.empty()) return false;
        if (s.drag != GarmentEditorState::Drag::None) return true; // (the drag commits on release first)
        u.restore(from, undoKey ? s.redo : s.undo);
        return true;
    }
    if (!s.hovered) return false;
    if (s.drag != GarmentEditorState::Drag::None) return true;
    if (key == SDLK_D && ctrl) {
        if (const Action* a = actions_.find("garment.duplicate"); a && a->enabled && a->enabled()) a->run();
    } else if ((key == SDLK_DELETE || key == SDLK_BACKSPACE) && !ctrl) u.removeSelected();
    else if (key == SDLK_C && !ctrl) u.toggleCurve();
    else if (key == SDLK_F && !ctrl) s.frame = true;
    else if (key == SDLK_ESCAPE) {
        if (s.sewing && s.sewPanel >= 0) s.sewPanel = -1;
        else if (s.sewing) s.sewing = false;
        else s.panel = s.point = s.edge = -1;
    } else return false;
    return true;
}

double App::garmentTestValue(const std::string& name) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (name.rfind("garment_", 0) != 0 || !garments_) return nan;
    GarmentEditorState& s = *garments_;
    GarmentUi u{*this, s};
    u.refresh();
    if (name == "garment_window_open") return showGarments_ ? 1.0 : 0.0;
    if (name == "garment_panels") return double(s.pat.panels.size());
    if (name == "garment_seams") return double(s.pat.seams.size());
    if (name == "garment_selected") return double(s.panel + 1);
    if (name == "garment_hover_seam") return double(s.hoverSeam + 1); // the seam row under the pointer (0: none)
    if (name == "garment_undo_steps") return double(s.undo.size());
    if (name == "garment_edits") return double(s.edits);
    if (name == "garment_problems") return double(garment::patternProblems(s.pat).size());
    // The selected panel's first point: in mm, and where it is on the window (0..1, for drag in UI tests).
    const bool point = name == "garment_point_x" || name == "garment_point_y" || name == "garment_points" ||
                       name == "garment_point_u" || name == "garment_point_v";
    if (!point) return nan;
    if (s.panel < 0 || s.pat.panels[s.panel].points.empty()) return 0.0;
    const glm::vec2 p = s.pat.panels[s.panel].points[0];
    if (name == "garment_point_x") return double(p.x);
    if (name == "garment_point_y") return double(p.y);
    if (name == "garment_points") return double(s.pat.panels[s.panel].points.size());
    int ww = 1, wh = 1;
    SDL_GetWindowSize(window_, &ww, &wh);
    const ImVec2 q = u.toScreen(p);
    if (name == "garment_point_u") return double(q.x) / ww;
    return name == "garment_point_u" ? double(q.x) / ww : double(q.y) / wh;
}

} // namespace sf
