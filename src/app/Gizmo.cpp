// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Gizmo 3D (ZBrush's Move W / Scale E / Rotate R, Q back to Draw): a gizmo at the pivot with axis arrows, plane
// squares and a free-move centre (Move), rings and a view ring (Rotate), axis boxes and a uniform centre (Scale).
// Dragging a handle transforms the active SubTool, or only its unmasked part (posing), through a
// TransformSession (core/MeshTransform.h): every frame recomputes from the drag's start, so nothing drifts, and
// the release makes one undo step per SubTool. Shift snaps (15 degrees, the grid, 10%), Alt+drag moves the pivot
// or turns the axes only, Alt+click on the model puts the pivot there. The Gizmo 3D panel adds numeric entry.
#include "app/App.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {

bool ctrlDown(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; } // as App.cpp

constexpr const char* kPath = "Transform > Gizmo";
constexpr float kRadiusPx = 105.0f;   // arrow length / ring radius on screen
constexpr float kViewRing = 1.22f;   // the view ring, relative to the others
constexpr float kPickPx = 8.0f;
constexpr float kSnapDegrees = 15.0f;

using H = App::GizmoHandle;

int axisOf(H h) {
    switch (h) {
    case H::MoveX: case H::PlaneYZ: case H::RotateX: case H::ScaleX: return 0;
    case H::MoveY: case H::PlaneXZ: case H::RotateY: case H::ScaleY: return 1;
    case H::MoveZ: case H::PlaneXY: case H::RotateZ: case H::ScaleZ: return 2;
    default: return -1;
    }
}
bool isMove(H h) { return h >= H::MoveX && h <= H::MoveView; }
bool isRotate(H h) { return h >= H::RotateX && h <= H::RotateView; }
bool isScale(H h) { return h >= H::ScaleX && h <= H::ScaleUniform; }

ImU32 axisColor(int axis, float alpha = 1.0f) {
    const int a = int(alpha * 255.0f);
    switch (axis) {
    case 0: return IM_COL32(232, 76, 72, a);
    case 1: return IM_COL32(118, 204, 82, a);
    case 2: return IM_COL32(72, 140, 238, a);
    default: return IM_COL32(222, 222, 222, a);
    }
}
constexpr ImU32 kHot = IM_COL32(255, 214, 92, 255);

float dist2(ImVec2 a, ImVec2 b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }

float segmentDistance(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float vx = b.x - a.x, vy = b.y - a.y, l2 = vx * vx + vy * vy;
    float t = l2 > 0.0f ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / l2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return std::sqrt(dist2(p, ImVec2(a.x + vx * t, a.y + vy * t)));
}

bool insideQuad(ImVec2 p, const ImVec2 q[4]) {
    bool in = false;
    for (int i = 0, j = 3; i < 4; j = i++)
        if ((q[i].y > p.y) != (q[j].y > p.y) && p.x < (q[j].x - q[i].x) * (p.y - q[i].y) / (q[j].y - q[i].y) + q[i].x)
            in = !in;
    return in;
}

// Parameter along the line (p, a) of the point nearest to the ray (o, d).
bool rayLineParam(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p, const glm::vec3& a, float& s) {
    const glm::vec3 w = o - p;
    const float b = glm::dot(d, a), c = glm::dot(d, w), e = glm::dot(a, w);
    const float den = 1.0f - b * b; // |d| = |a| = 1
    if (den < 1e-6f) return false;
    s = (e - b * c) / den;
    return true;
}

bool rayPlane(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p, const glm::vec3& n, glm::vec3& hit) {
    const float den = glm::dot(d, n);
    if (std::abs(den) < 1e-6f) return false;
    const float t = glm::dot(p - o, n) / den;
    hit = o + d * t;
    return true;
}

// A step for Shift-snapped moves: 1, 2 or 5 times a power of ten near a twentieth of the size.
float gridStep(float size) {
    if (!(size > 0.0f)) return 0.1f;
    const float raw = size / 20.0f;
    const float p = std::pow(10.0f, std::floor(std::log10(raw)));
    const float m = raw / p;
    return p * (m < 1.5f ? 1.0f : m < 3.5f ? 2.0f : m < 7.5f ? 5.0f : 10.0f);
}

float snapTo(float v, float step) { return step > 0.0f ? std::round(v / step) * step : v; }

std::string vecText(const glm::vec3& v) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%g,%g,%g", double(v.x), double(v.y), double(v.z));
    return buf;
}

// Axes with Y along the surface normal.
glm::mat3 frameFromNormal(const glm::vec3& normal) {
    const glm::vec3 y = glm::normalize(normal);
    const glm::vec3 x = glm::normalize(glm::cross(y, std::abs(y.z) < 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0)));
    return glm::mat3(x, y, glm::cross(x, y));
}

} // namespace

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void App::registerGizmoActions() {
    auto add = [this](const char* id, const char* label, const char* icon, const char* palette, const char* shortcut,
                      std::function<void()> run, std::function<bool()> checked, const char* description) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = palette;
        a.shortcut = shortcut;
        a.run = std::move(run);
        a.checked = std::move(checked);
        a.description = description;
        actions_.add(std::move(a));
    };
    auto mode = [this](GizmoMode m) {
        return [this, m] { setGizmoMode(gizmo_.mode == m && m != GizmoMode::Off ? GizmoMode::Off : m); };
    };
    auto is = [this](GizmoMode m) { return [this, m] { return gizmo_.mode == m; }; };
    add("tool.draw", "Draw", ICON_LC_PENCIL, "Transform", "Q", mode(GizmoMode::Off), is(GizmoMode::Off),
        "Draw mode: brushes sculpt and paint (leaves the Gizmo).");
    add("tool.move", "Move", ICON_LC_MOVE, "Transform", "W", mode(GizmoMode::Move), is(GizmoMode::Move),
        "Gizmo, Move: drag an arrow to move along an axis, a square to move in a plane, the centre to move in "
        "the view. Masked parts stay; Shift snaps to the grid; Alt+drag moves only the gizmo. W again: back to Draw.");
    add("tool.scale", "Scale", ICON_LC_SCALING, "Transform", "E", mode(GizmoMode::Scale), is(GizmoMode::Scale),
        "Gizmo, Scale: drag a box to stretch along an axis or the centre to scale evenly (right or up: bigger). "
        "Shift snaps to 10% steps. E again: back to Draw.");
    add("tool.rotate", "Rotate", ICON_LC_ROTATE_CW, "Transform", "R", mode(GizmoMode::Rotate), is(GizmoMode::Rotate),
        "Gizmo, Rotate: drag a ring to turn about that axis, the outer ring to turn in the view. Mask the body "
        "and put the pivot at a joint to pose a limb. Shift snaps to 15 degrees. R again: back to Draw.");
    add("gizmo.reset_pivot", "Reset", ICON_LC_LOCATE_FIXED, kPath, "", [this] { resetGizmoPivot(); }, nullptr,
        "Put the gizmo back at the centre of what moves (the Part, or its unmasked part) with world axes.");
    add("gizmo.snap_pivot", "To Surface", ICON_LC_CROSSHAIR, kPath, "",
        [this] {
            gizmo_.snapNext = !gizmo_.snapNext;
            if (gizmo_.snapNext) status_ = "Click on the model to put the pivot there";
        },
        [this] { return gizmo_.snapNext; },
        "Put the pivot on the surface: click this, then click the model (or Alt+click the model at any time). With "
        "Surface orientation the axes follow the surface there (Y along the normal).");
    add("gizmo.orient_world", "World", ICON_LC_GLOBE, kPath, "",
        [this] { gizmo_.orientSurface = false; }, [this] { return !gizmo_.orientSurface; },
        "The gizmo's axes are the world's X, Y and Z.");
    add("gizmo.orient_surface", "Surface", ICON_LC_LOCATE, kPath, "",
        [this] {
            gizmo_.orientSurface = true;
            gizmo_.frame = frameFromNormal(gizmo_.surfaceNormal);
        },
        [this] { return gizmo_.orientSurface; },
        "The gizmo's axes follow the surface where the pivot was snapped (Y along the normal) and turn with your "
        "rotations, for moving a part along itself.");
    add("gizmo.use_mask", "Mask", ICON_LC_DROPLET, kPath, "", [this] { gizmo_.useMask = !gizmo_.useMask; },
        [this] { return gizmo_.useMask; },
        "Masked parts stay where they are and partly masked parts move partly (on by default). Off: the whole "
        "Part moves whatever the mask.");
    add("gizmo.all", "All Parts", ICON_LC_LAYERS, kPath, "", [this] { gizmo_.all = !gizmo_.all; },
        [this] { return gizmo_.all; },
        "Transform every visible Part together (ZBrush Transpose All). Each Part gets its own undo step.");
}

void App::setGizmoMode(GizmoMode mode) {
    if (drag_ == Drag::Gizmo) return;
    gizmo_.mode = mode;
    gizmo_.snapNext = false;
    gizmo_.hover = GizmoHandle::None;
    status_ = mode == GizmoMode::Move     ? "Move: drag the arrows, squares or centre (Alt+drag: the gizmo only)"
              : mode == GizmoMode::Scale  ? "Scale: drag a box or the centre (Shift: 10% steps)"
              : mode == GizmoMode::Rotate ? "Rotate: drag a ring (Shift: 15 degrees); mask first to pose a part"
                                          : "Draw";
    if (mode != GizmoMode::Off) {
        doc_.syncBvh();
        updateGizmoPivot();
    }
}

// ---------------------------------------------------------------------------
// Pivot
// ---------------------------------------------------------------------------

std::vector<int> App::gizmoTargets() const {
    std::vector<int> out;
    for (int i = 0; i < doc_.subtoolCount(); ++i) {
        const bool visible = doc_.subtools[size_t(i)].visible;
        if (i == doc_.activeSubtool ? visible : gizmo_.all && visible) out.push_back(i);
    }
    return out;
}

void App::updateGizmoPivot() {
    // Recomputed only when the scene changes: a new undo step, another SubTool, the options.
    uint64_t key = uint64_t(doc_.activeSubtoolInfo().id) * 1000003u ^ doc_.undo.revision() * 7919u ^
                   doc_.mesh.vertexCount() * 31u ^ (gizmo_.all ? 2u : 0u) ^ (gizmo_.useMask ? 4u : 0u) ^
                   uint64_t(doc_.subtoolCount()) << 48;
    if (gizmo_.all)
        for (const SubTool& s : doc_.subtools) key = key * 1099511628211ull ^ s.undo.revision() ^ (s.visible ? 1u : 0u);
    if (key == gizmo_.centreKey) return;
    gizmo_.centreKey = key;
    // A new mask (or another SubTool) means a new part to move: follow its centre again.
    uint64_t maskKey = doc_.activeSubtoolInfo().id;
    const auto& mask = doc_.mesh.mask;
    double sum = 0.0;
    for (size_t v = 0; v < mask.size(); v += 1 + mask.size() / 65536) sum += double(mask[v]) * double(v % 97 + 1);
    maskKey = maskKey * 1000003u ^ uint64_t(sum * 1024.0) ^ uint64_t(mask.size()) << 40;
    if (maskKey != gizmo_.maskKey) {
        gizmo_.maskKey = maskKey;
        gizmo_.pivotPlaced = false;
    }
    if (gizmo_.pivotPlaced) return;
    AABB box;
    for (int i : gizmoTargets()) {
        const Mesh& m = doc_.subtoolMesh(i);
        if (m.vertexCount() == 0) continue;
        box.expand(transformBounds(m, transformWeights(m, gizmo_.useMask, 0.0f)));
    }
    gizmo_.pivot = box.valid() ? box.center() : glm::vec3(0.0f);
}

void App::resetGizmoPivot() {
    gizmo_.pivotPlaced = false;
    gizmo_.orientSurface = false;
    gizmo_.frame = glm::mat3(1.0f);
    gizmo_.centreKey = ~uint64_t(0);
    updateGizmoPivot();
    status_ = "Pivot at the centre, world axes";
}

void App::snapGizmoPivot(const RayHit& hit, int subtool) {
    gizmo_.pivot = hit.position;
    gizmo_.pivotPlaced = true;
    gizmo_.snapNext = false;
    gizmo_.surfaceNormal = glm::length(hit.normal) > 0.0f ? glm::normalize(hit.normal) : glm::vec3(0, 1, 0);
    // The triangle's normal faces either way: turn it toward the viewer.
    if (glm::dot(gizmo_.surfaceNormal, camera_.forward()) > 0.0f) gizmo_.surfaceNormal = -gizmo_.surfaceNormal;
    if (gizmo_.orientSurface) gizmo_.frame = frameFromNormal(gizmo_.surfaceNormal);
    status_ = "Pivot on " + doc_.subtools[size_t(subtool)].name + " at " + vecText(hit.position);
}

glm::mat3 App::gizmoFrame() const {
    if (drag_ == Drag::Gizmo && !gizmo_.pivotOnly) return gizmo_.startFrame;
    return gizmo_.orientSurface ? gizmo_.frame : glm::mat3(1.0f);
}

float App::gizmoWorldSize() const {
    return kRadiusPx * uiScale_ * camera_.worldPerPixel(gizmo_.pivot, canvasHeight());
}

bool App::gizmoProject(const glm::vec3& p, ImVec2& out) const {
    const glm::vec4 c = camera_.projection() * camera_.view() * glm::vec4(p, 1.0f);
    if (c.w <= 1e-6f) return false;
    const CanvasRect r = canvasRect();
    out = ImVec2(r.x + (c.x / c.w * 0.5f + 0.5f) * r.w, r.y + (0.5f - c.y / c.w * 0.5f) * r.h);
    return true;
}

// ---------------------------------------------------------------------------
// Handles: shapes, picking
// ---------------------------------------------------------------------------

App::GizmoHandle App::gizmoHandleAt(float x, float y) const {
    if (!gizmoActive()) return H::None;
    const glm::vec3 c = gizmo_.pivot;
    const glm::mat3 f = gizmoFrame();
    const float L = gizmoWorldSize(), pick = kPickPx * uiScale_;
    const ImVec2 m(x, y);
    ImVec2 pc;
    if (!gizmoProject(c, pc)) return H::None;
    const glm::vec3 toEye = -camera_.forward();
    H best = H::None;
    float bestD = pick;
    auto consider = [&](H h, float d) {
        if (d < bestD) {
            bestD = d;
            best = h;
        }
    };
    if (gizmo_.mode == GizmoMode::Move || gizmo_.mode == GizmoMode::Scale) {
        const bool move = gizmo_.mode == GizmoMode::Move;
        // The centre first: it wins over the arrows' roots.
        const float centre = (move ? 12.0f : 11.0f) * uiScale_;
        if (std::sqrt(dist2(m, pc)) < centre) return move ? H::MoveView : H::ScaleUniform;
        for (int i = 0; i < 3; ++i) {
            ImVec2 a, b;
            if (!gizmoProject(c + f[i] * (0.2f * L), a) || !gizmoProject(c + f[i] * L, b)) continue;
            if (std::sqrt(dist2(a, b)) < 10.0f * uiScale_) continue; // pointing at the viewer
            consider(H(int(move ? H::MoveX : H::ScaleX) + i), segmentDistance(m, a, b));
        }
        if (move)
            for (int i = 0; i < 3; ++i) {
                if (std::abs(glm::dot(f[i], toEye)) < 0.15f) continue; // seen edge-on
                const glm::vec3 u = f[(i + 1) % 3], v = f[(i + 2) % 3];
                ImVec2 q[4];
                const float lo = 0.28f * L, hi = 0.48f * L;
                if (!gizmoProject(c + u * lo + v * lo, q[0]) || !gizmoProject(c + u * hi + v * lo, q[1]) ||
                    !gizmoProject(c + u * hi + v * hi, q[2]) || !gizmoProject(c + u * lo + v * hi, q[3]))
                    continue;
                if (insideQuad(m, q)) consider(H(int(H::PlaneYZ) + i), pick * 0.5f);
            }
        return best;
    }
    // Rotate: the axis rings (front halves first), then the view ring.
    for (int i = 0; i < 3; ++i) {
        glm::vec3 u = f[(i + 1) % 3], v = f[(i + 2) % 3];
        ImVec2 prev;
        bool havePrev = false;
        for (int k = 0; k <= 64; ++k) {
            const float t = float(k) / 64.0f * 6.2831853f;
            const glm::vec3 r = u * std::cos(t) + v * std::sin(t);
            ImVec2 p;
            if (!gizmoProject(c + r * L, p)) {
                havePrev = false;
                continue;
            }
            if (havePrev) {
                const bool front = glm::dot(r, toEye) > -0.05f;
                consider(H(int(H::RotateX) + i), segmentDistance(m, prev, p) + (front ? 0.0f : pick * 0.6f));
            }
            prev = p;
            havePrev = true;
        }
    }
    const float ring = kRadiusPx * kViewRing * uiScale_;
    consider(H::RotateView, std::abs(std::sqrt(dist2(m, pc)) - ring));
    return best;
}

bool App::gizmoHandleScreen(GizmoHandle h, ImVec2& out) const {
    // Look along the handle for a point where it is the one picked.
    const glm::vec3 c = gizmo_.pivot;
    const glm::mat3 f = gizmoFrame();
    const float L = gizmoWorldSize();
    const int axis = axisOf(h);
    std::vector<glm::vec3> tries;
    ImVec2 pc;
    if (!gizmoProject(c, pc)) return false;
    if (h == H::MoveView || h == H::ScaleUniform) {
        out = pc;
        return gizmoHandleAt(out.x, out.y) == h;
    }
    if (h == H::RotateView) {
        for (int k = 0; k < 16; ++k) {
            const float t = float(k) / 16.0f * 6.2831853f + 0.4f;
            const float r = kRadiusPx * kViewRing * uiScale_;
            const ImVec2 p(pc.x + r * std::cos(t), pc.y - r * std::sin(t));
            if (gizmoHandleAt(p.x, p.y) == h) {
                out = p;
                return true;
            }
        }
        return false;
    }
    if (isRotate(h)) {
        const glm::vec3 u = f[(axis + 1) % 3], v = f[(axis + 2) % 3];
        float bestScore = -1e9f;
        bool found = false;
        for (int k = 0; k < 48; ++k) {
            const float t = float(k) / 48.0f * 6.2831853f;
            const glm::vec3 r = u * std::cos(t) + v * std::sin(t);
            ImVec2 p;
            if (!gizmoProject(c + r * L, p) || gizmoHandleAt(p.x, p.y) != h) continue;
            const float score = glm::dot(r, -camera_.forward()); // the most front-facing point
            if (score > bestScore) {
                bestScore = score;
                out = p;
                found = true;
            }
        }
        return found;
    }
    if (h >= H::PlaneYZ && h <= H::PlaneXY) {
        const glm::vec3 u = f[(axis + 1) % 3], v = f[(axis + 2) % 3];
        return gizmoProject(c + (u + v) * (0.38f * L), out) && gizmoHandleAt(out.x, out.y) == h;
    }
    for (float s : {0.75f, 0.9f, 0.6f, 0.45f}) {
        if (gizmoProject(c + f[axis] * (s * L), out) && gizmoHandleAt(out.x, out.y) == h) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Dragging
// ---------------------------------------------------------------------------

bool App::gizmoMouseDown(float x, float y, SDL_Keymod mod) {
    if (!gizmoActive() || ctrlDown(mod)) return false; // Ctrl still masks, as in ZBrush
    if (previewLod_ >= 0) clearLods();
    updateGizmoPivot();
    const bool alt = (mod & SDL_KMOD_ALT) != 0;
    const GizmoHandle h = gizmoHandleAt(x, y);
    if (h == H::None) {
        RayHit hit;
        const int st = pickSubtool(x, y, &hit);
        if (st >= 0 && gizmo_.snapNext) {
            snapGizmoPivot(hit, st);
            return true;
        }
        if (alt && st >= 0 && st != doc_.activeSubtool) return false; // Alt+click selects another SubTool
        if (alt && st >= 0) { // a click puts the pivot here, a drag pans
            gizmo_.pendingSnap = true;
            gizmo_.snapHit = hit;
            gizmo_.snapSubtool = st;
            gizmo_.startMouse = ImVec2(x, y);
        }
        // Off the handles the mouse navigates, wherever it is.
        drag_ = alt ? Drag::Pan : Drag::Orbit;
        panFromAlt_ = alt;
        return true;
    }

    gizmo_.handle = h;
    gizmo_.pivotOnly = alt;
    gizmo_.startPivot = gizmo_.pivot;
    gizmo_.startFrame = gizmo_.orientSurface ? gizmo_.frame : glm::mat3(1.0f);
    gizmo_.startMouse = ImVec2(x, y);
    gizmo_.angle = gizmo_.lastAngle = 0.0f;
    gizmo_.tangent = false;
    gizmo_.current = MeshTransform{};
    gizmo_.readout.clear();
    if (gizmo_.pivotOnly && isScale(h)) {
        status_ = "Alt+drag moves the pivot (Move) or turns the axes (Rotate); scaling it does nothing";
        gizmo_.handle = H::None;
        return true;
    }
    glm::vec3 o, d;
    canvasRay(x, y, o, d);
    const glm::mat3& f = gizmo_.startFrame;
    const glm::vec3 c = gizmo_.pivot;
    const int axis = axisOf(h);
    if (h >= H::MoveX && h <= H::MoveZ) {
        float s = 0.0f;
        rayLineParam(o, d, c, f[axis], s);
        gizmo_.grab = glm::vec3(s, 0, 0);
    } else if (h >= H::PlaneYZ && h <= H::MoveView) {
        const glm::vec3 n = h == H::MoveView ? camera_.forward() : f[axis];
        if (!rayPlane(o, d, c, n, gizmo_.grab)) gizmo_.grab = c;
    } else if (isRotate(h)) {
        const glm::vec3 a = h == H::RotateView ? camera_.forward() : f[axis];
        glm::vec3 hit;
        if (h != H::RotateView && std::abs(glm::dot(a, camera_.forward())) < 0.25f) {
            // Edge-on ring: turn by the drag along the ring's tangent at the grabbed point.
            gizmo_.tangent = true;
            glm::vec3 r = glm::vec3(0.0f);
            if (rayPlane(o, d, c, a, hit)) r = hit - c;
            r -= a * glm::dot(r, a);
            if (glm::length(r) < 1e-6f) r = glm::cross(a, camera_.forward());
            r = glm::normalize(r) * gizmoWorldSize();
            ImVec2 p0, p1;
            const glm::vec3 tangent = glm::cross(a, r);
            if (gizmoProject(c + r, p0) && gizmoProject(c + r + tangent * 0.05f, p1)) {
                const float len = std::sqrt(dist2(p0, p1));
                gizmo_.tangentDir = len > 1e-4f ? ImVec2((p1.x - p0.x) / len, (p1.y - p0.y) / len) : ImVec2(1, 0);
            }
        } else if (rayPlane(o, d, c, a, hit) && glm::length(hit - c) > 1e-6f) {
            gizmo_.grabDir = glm::normalize(hit - c);
        } else {
            gizmo_.grabDir = glm::normalize(glm::cross(a, camera_.forward()));
        }
    }

    gizmo_.subtools.clear();
    gizmo_.sessions.clear();
    if (!gizmo_.pivotOnly) {
        for (int i : gizmoTargets()) {
            Mesh& m = doc_.subtoolMesh(i);
            SubTool& s = doc_.subtools[size_t(i)];
            if (i != doc_.activeSubtool && s.bvhDirty) {
                s.bvh.build(s.mesh);
                s.bvhDirty = false;
            }
            TransformSession session;
            if (!session.begin(m, transformWeights(m, gizmo_.useMask, gizmo_.falloff), symmetry_, gizmo_.pivot))
                continue;
            gizmo_.subtools.push_back(i);
            gizmo_.sessions.push_back(std::move(session));
        }
        if (gizmo_.sessions.empty()) {
            status_ = gizmo_.useMask && doc_.mesh.hasMask() ? "Nothing to move: everything is masked (Ctrl+click the "
                                                              "background to invert the mask)"
                                                            : "Nothing to move: the Part is hidden or empty";
            gizmo_.handle = H::None;
            return true;
        }
        playingLayer_ = -1;
    }
    drag_ = Drag::Gizmo;
    return true;
}

void App::gizmoDragTo(float x, float y) {
    const GizmoHandle h = gizmo_.handle;
    if (h == H::None) return;
    glm::vec3 o, d;
    canvasRay(x, y, o, d);
    const bool snap = (inputMods(false) & SDL_KMOD_SHIFT) != 0;
    const glm::mat3& f = gizmo_.startFrame;
    const glm::vec3 c = gizmo_.startPivot;
    const int axis = axisOf(h);
    float size = 0.0f;
    for (int i : gizmo_.subtools.empty() ? gizmoTargets() : gizmo_.subtools)
        size = std::max(size, glm::length(doc_.subtoolMesh(i).bounds().extent()));
    const float grid = gridStep(size);
    MeshTransform t;
    t.pivot = c;
    t.frame = f;
    char buf[160] = "";
    if (h >= H::MoveX && h <= H::MoveZ) {
        float s = gizmo_.grab.x;
        rayLineParam(o, d, c, f[axis], s);
        float delta = s - gizmo_.grab.x;
        if (snap) delta = snapTo(delta, grid);
        t.translate = f[axis] * delta;
        std::snprintf(buf, sizeof buf, "%c %+.3f", "XYZ"[axis], double(delta));
    } else if (h >= H::PlaneYZ && h <= H::MoveView) {
        const glm::vec3 n = h == H::MoveView ? camera_.forward() : f[axis];
        glm::vec3 hit;
        if (!rayPlane(o, d, c, n, hit)) return;
        glm::vec3 delta = hit - gizmo_.grab;
        if (snap) { // whole grid steps along the gizmo's axes
            glm::vec3 local = glm::transpose(f) * delta;
            for (int i = 0; i < 3; ++i) local[i] = snapTo(local[i], grid);
            if (h != H::MoveView) local[axis] = 0.0f;
            delta = f * local;
        }
        t.translate = delta;
        const glm::vec3 local = glm::transpose(f) * delta;
        std::snprintf(buf, sizeof buf, "%+.3f %+.3f %+.3f", double(local.x), double(local.y), double(local.z));
    } else if (isRotate(h)) {
        const glm::vec3 a = h == H::RotateView ? camera_.forward() : f[axis];
        if (gizmo_.tangent) {
            const float along = (x - gizmo_.startMouse.x) * gizmo_.tangentDir.x + (y - gizmo_.startMouse.y) * gizmo_.tangentDir.y;
            gizmo_.angle = along / (kRadiusPx * uiScale_);
        } else {
            glm::vec3 hit;
            if (rayPlane(o, d, c, a, hit) && glm::length(hit - c) > 1e-6f) {
                const glm::vec3 v = glm::normalize(hit - c);
                const float raw = std::atan2(glm::dot(glm::cross(gizmo_.grabDir, v), a), glm::dot(gizmo_.grabDir, v));
                float step = raw - gizmo_.lastAngle;
                while (step > 3.14159265f) step -= 6.2831853f;
                while (step < -3.14159265f) step += 6.2831853f;
                gizmo_.angle += step;
                gizmo_.lastAngle = raw;
            }
        }
        float angle = gizmo_.angle;
        if (snap) angle = glm::radians(snapTo(glm::degrees(angle), kSnapDegrees));
        t.rotate = glm::angleAxis(angle, a);
        std::snprintf(buf, sizeof buf, "%s %+.1f deg", h == H::RotateView ? "View" : axis == 0 ? "X" : axis == 1 ? "Y" : "Z",
                      double(glm::degrees(angle)));
    } else if (isScale(h)) {
        float s;
        if (h == H::ScaleUniform) {
            s = 1.0f + ((x - gizmo_.startMouse.x) - (y - gizmo_.startMouse.y)) / (kRadiusPx * uiScale_);
        } else {
            ImVec2 p0, p1;
            if (!gizmoProject(c, p0) || !gizmoProject(c + f[axis] * gizmoWorldSize(), p1)) return;
            const float len = std::max(1.0f, std::sqrt(dist2(p0, p1)));
            const ImVec2 dir((p1.x - p0.x) / len, (p1.y - p0.y) / len);
            s = 1.0f + ((x - gizmo_.startMouse.x) * dir.x + (y - gizmo_.startMouse.y) * dir.y) / len;
        }
        if (snap) s = snapTo(s, 0.1f);
        s = std::max(s, 0.01f);
        t.scale = h == H::ScaleUniform ? glm::vec3(s) : glm::vec3(1.0f);
        if (h != H::ScaleUniform) t.scale[axis] = s;
        std::snprintf(buf, sizeof buf, "%s %.0f%%", h == H::ScaleUniform ? "All" : axis == 0 ? "X" : axis == 1 ? "Y" : "Z",
                      double(s * 100.0f));
    }
    gizmo_.readout = buf;
    if (gizmo_.pivotOnly) {
        if (isMove(h)) gizmo_.pivot = c + t.translate;
        if (isRotate(h)) {
            gizmo_.orientSurface = true;
            gizmo_.frame = glm::mat3_cast(t.rotate) * f;
        }
        gizmo_.pivotPlaced = true;
        return;
    }
    gizmo_.current = t;
    for (size_t i = 0; i < gizmo_.sessions.size(); ++i) gizmo_.sessions[i].apply(doc_.subtoolMesh(gizmo_.subtools[i]), t);
}

void App::gizmoRelease() {
    if (gizmo_.pendingSnap) {
        gizmo_.pendingSnap = false;
        if (dist2(gizmo_.startMouse, ImVec2(mouseX_, mouseY_)) < 16.0f && gizmo_.snapSubtool == doc_.activeSubtool)
            snapGizmoPivot(gizmo_.snapHit, gizmo_.snapSubtool);
    }
    if (drag_ != Drag::Gizmo) return;
    const GizmoHandle h = gizmo_.handle;
    const std::string readout = std::move(gizmo_.readout);
    gizmo_.handle = H::None;
    gizmo_.readout.clear();
    if (gizmo_.pivotOnly || gizmo_.sessions.empty()) return;
    const char* label = isRotate(h) ? "Gizmo Rotate" : isScale(h) ? "Gizmo Scale" : "Gizmo Move";
    size_t moved = 0;
    for (size_t i = 0; i < gizmo_.sessions.size(); ++i) {
        const int st = gizmo_.subtools[i];
        const bool active = st == doc_.activeSubtool;
        SubTool& s = doc_.subtools[size_t(st)];
        const size_t n = gizmo_.sessions[i].movingVertices();
        UndoRecord r = gizmo_.sessions[i].finish(active ? doc_.mesh : s.mesh, active ? doc_.bvh : s.bvh, label);
        if (r.empty()) continue;
        (active ? doc_.undo : s.undo).push(std::move(r));
        moved += n;
    }
    gizmo_.sessions.clear();
    gizmo_.subtools.clear();
    if (moved == 0) return;
    // The gizmo moves and turns with what it moved.
    const MeshTransform& t = gizmo_.current;
    const glm::mat3 frameBefore = gizmo_.frame;
    const bool surfaceBefore = gizmo_.orientSurface;
    gizmo_.pivot = t.pivot + t.translate;
    gizmo_.pivotPlaced = true;
    if (gizmo_.orientSurface && isRotate(h)) gizmo_.frame = glm::mat3_cast(t.rotate) * gizmo_.startFrame;
    rememberGizmoStep(t.pivot, frameBefore, surfaceBefore);
    // The console shows the drag as the command that repeats it (Copy session as script replays it).
    Args args;
    if (t.translate != glm::vec3(0.0f)) args["translate"] = vecText(t.translate);
    const glm::mat3 local = glm::transpose(t.frame) * glm::mat3_cast(t.rotate) * t.frame;
    if (isRotate(h)) args["rotate"] = vecText(eulerDegrees(glm::quat_cast(local)));
    if (t.scale != glm::vec3(1.0f))
        args["scale"] = t.scale.x == t.scale.y && t.scale.y == t.scale.z ? vecText(t.scale).substr(0, vecText(t.scale).find(','))
                                                                         : vecText(t.scale);
    args["pivot"] = vecText(t.pivot);
    if (t.frame != glm::mat3(1.0f)) args["orient"] = vecText(eulerDegrees(glm::quat_cast(t.frame)));
    if (!gizmo_.useMask) args["masked"] = false;
    if (gizmo_.falloff > 0.0f) args["falloff"] = double(gizmo_.falloff);
    std::string sym;
    if (symmetry_.x) sym += 'x';
    if (symmetry_.y) sym += 'y';
    if (symmetry_.z) sym += 'z';
    if (!sym.empty() && doc_.mesh.hasMask() && gizmo_.useMask) args["symmetry"] = sym;
    if (gizmo_.all) args["all"] = true;
    status_ = std::string(label) + " " + readout + ": " + std::to_string(moved) + " vertices";
    consoleEcho("transform", args, {true, status_});
    updateTitle();
}

void App::rememberGizmoStep(const glm::vec3& before, const glm::mat3& frameBefore, bool surfaceBefore) {
    GizmoState::Step st;
    st.subtool = doc_.activeSubtoolInfo().id;
    st.depth = doc_.undo.undoCount();
    st.before = before;
    st.after = gizmo_.pivot;
    st.frameBefore = frameBefore;
    st.frameAfter = gizmo_.frame;
    st.surfaceBefore = surfaceBefore;
    st.surfaceAfter = gizmo_.orientSurface;
    gizmo_.undoSteps.push_back(st);
    gizmo_.redoSteps.clear();
    if (gizmo_.undoSteps.size() > 256) gizmo_.undoSteps.erase(gizmo_.undoSteps.begin());
}

void App::gizmoAfterUndo(bool redo) {
    const uint32_t id = doc_.activeSubtoolInfo().id;
    const size_t depth = doc_.undo.undoCount();
    auto& from = redo ? gizmo_.redoSteps : gizmo_.undoSteps;
    auto& to = redo ? gizmo_.undoSteps : gizmo_.redoSteps;
    // Undo took the stack from st.depth to st.depth - 1; redo brought it back to st.depth.
    if (from.empty() || from.back().subtool != id || from.back().depth != (redo ? depth : depth + 1)) return;
    const GizmoState::Step st = from.back();
    from.pop_back();
    to.push_back(st);
    gizmo_.pivot = redo ? st.after : st.before;
    gizmo_.frame = redo ? st.frameAfter : st.frameBefore;
    gizmo_.orientSurface = redo ? st.surfaceAfter : st.surfaceBefore;
    gizmo_.pivotPlaced = true;
}

void App::applyGizmoNumbers() {
    updateGizmoPivot();
    const glm::vec3 move(gizmo_.move[0], gizmo_.move[1], gizmo_.move[2]);
    const glm::vec3 rot(gizmo_.rotate[0], gizmo_.rotate[1], gizmo_.rotate[2]);
    const float scale = gizmo_.scale / 100.0f;
    if (!(scale > 0.0f)) {
        status_ = "Scale must be above 0%";
        return;
    }
    const glm::mat3 f = gizmoFrame();
    Args args;
    args["translate"] = vecText(move);
    args["rotate"] = vecText(rot);
    args["scale"] = double(scale);
    args["pivot"] = vecText(gizmo_.pivot);
    if (f != glm::mat3(1.0f)) args["orient"] = vecText(eulerDegrees(glm::quat_cast(f)));
    args["masked"] = gizmo_.useMask;
    if (gizmo_.falloff > 0.0f) args["falloff"] = double(gizmo_.falloff);
    std::string sym;
    if (symmetry_.x) sym += 'x';
    if (symmetry_.y) sym += 'y';
    if (symmetry_.z) sym += 'z';
    if (!sym.empty()) args["symmetry"] = sym;
    if (gizmo_.all) args["all"] = true;
    const glm::vec3 pivot = gizmo_.pivot;
    const glm::mat3 frameBefore = gizmo_.frame;
    const bool surfaceBefore = gizmo_.orientSurface;
    runCommand("transform", args);
    if (!lastCommandOk_ || !doc_.undo.canUndo()) return;
    gizmo_.pivot = pivot + move;
    gizmo_.pivotPlaced = true;
    if (gizmo_.orientSurface) gizmo_.frame = glm::mat3_cast(eulerRotation(rot, f)) * f;
    rememberGizmoStep(pivot, frameBefore, surfaceBefore);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void App::drawGizmo() {
    if (!gizmoActive()) return;
    if (drag_ != Drag::Gizmo) updateGizmoPivot();
    drawGizmoPanel();
    if (!doc_.activeSubtoolInfo().visible) return;
    const ImVec2 mouse(mouseX_, mouseY_);
    const bool overCanvas = !ImGui::GetIO().WantCaptureMouse && canvasRect().contains(mouse.x, mouse.y);
    gizmo_.hover = drag_ == Drag::Gizmo ? gizmo_.handle
                   : drag_ == Drag::None && overCanvas && !ctrlDown(inputMods(false)) ? gizmoHandleAt(mouse.x, mouse.y)
                                                                                 : H::None;
    ImDrawList* dl = ImGui::GetBackgroundDrawList(); // under the panels, over the model
    const CanvasRect cr = canvasRect();
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const glm::vec3 c = gizmo_.pivot;
    const glm::mat3 f = gizmoFrame();
    const float L = gizmoWorldSize(), s = uiScale_;
    const glm::vec3 toEye = -camera_.forward();
    ImVec2 pc;
    if (!gizmoProject(c, pc)) {
        dl->PopClipRect();
        return;
    }
    const bool dragging = drag_ == Drag::Gizmo && gizmo_.handle != H::None;
    auto colorFor = [&](H h, int axis) {
        if (gizmo_.hover == h) return kHot;
        return axisColor(axis, dragging ? 0.45f : 1.0f);
    };
    const float thick = 2.6f * s;
    const ImU32 shadow = IM_COL32(0, 0, 0, 110);

    if (gizmo_.mode == GizmoMode::Move || gizmo_.mode == GizmoMode::Scale) {
        const bool move = gizmo_.mode == GizmoMode::Move;
        if (move) // plane squares, behind the arrows
            for (int i = 0; i < 3; ++i) {
                if (std::abs(glm::dot(f[i], toEye)) < 0.15f) continue;
                const glm::vec3 u = f[(i + 1) % 3], v = f[(i + 2) % 3];
                ImVec2 q[4];
                const float lo = 0.28f * L, hi = 0.48f * L;
                if (!gizmoProject(c + u * lo + v * lo, q[0]) || !gizmoProject(c + u * hi + v * lo, q[1]) ||
                    !gizmoProject(c + u * hi + v * hi, q[2]) || !gizmoProject(c + u * lo + v * hi, q[3]))
                    continue;
                const H h = H(int(H::PlaneYZ) + i);
                const ImU32 col = colorFor(h, i);
                dl->AddConvexPolyFilled(q, 4, (col & 0x00ffffff) | (gizmo_.hover == h ? 0x90000000u : 0x50000000u));
                dl->AddPolyline(q, 4, col, ImDrawFlags_Closed, 1.5f * s);
            }
        // Arrows / boxes, far ones first.
        int order[3] = {0, 1, 2};
        std::sort(order, order + 3, [&](int a, int b) { return glm::dot(f[a], toEye) < glm::dot(f[b], toEye); });
        for (int i : order) {
            ImVec2 a, b;
            if (!gizmoProject(c + f[i] * (0.2f * L), a) || !gizmoProject(c + f[i] * L, b)) continue;
            const float len = std::sqrt(dist2(a, b));
            if (len < 10.0f * s) continue;
            const H h = H(int(move ? H::MoveX : H::ScaleX) + i);
            const ImU32 col = colorFor(h, i);
            const ImVec2 dir((b.x - a.x) / len, (b.y - a.y) / len), nrm(-dir.y, dir.x);
            dl->AddLine(a, b, shadow, thick + 2.0f * s);
            dl->AddLine(a, b, col, thick);
            if (move) {
                const float head = 15.0f * s, w = 6.0f * s;
                const ImVec2 tip(b.x + dir.x * head, b.y + dir.y * head);
                dl->AddTriangleFilled(tip, ImVec2(b.x + nrm.x * w, b.y + nrm.y * w), ImVec2(b.x - nrm.x * w, b.y - nrm.y * w), col);
            } else {
                const float h2 = 6.0f * s;
                dl->AddRectFilled(ImVec2(b.x - h2, b.y - h2), ImVec2(b.x + h2, b.y + h2), col);
                dl->AddRect(ImVec2(b.x - h2, b.y - h2), ImVec2(b.x + h2, b.y + h2), shadow, 0.0f, 0, 1.0f);
            }
            dl->AddText(smallFont(), 12.0f * s, ImVec2(b.x + dir.x * (move ? 20.0f : 11.0f) * s - 3.0f * s,
                                                       b.y + dir.y * (move ? 20.0f : 11.0f) * s - 7.0f * s),
                        col, i == 0 ? "X" : i == 1 ? "Y" : "Z");
        }
        const H centre = move ? H::MoveView : H::ScaleUniform;
        const ImU32 cc = gizmo_.hover == centre ? kHot : IM_COL32(240, 206, 96, dragging ? 140 : 255);
        if (move) {
            dl->AddCircle(pc, 10.0f * s, shadow, 32, thick + 2.0f * s);
            dl->AddCircle(pc, 10.0f * s, cc, 32, thick);
        } else {
            dl->AddCircleFilled(pc, 9.0f * s, cc, 32);
            dl->AddCircle(pc, 9.0f * s, shadow, 32, 1.0f * s);
        }
    } else {
        // Rotate: rings with their back halves dimmed, the view ring outside them.
        for (int i = 0; i < 3; ++i) {
            const glm::vec3 u = f[(i + 1) % 3], v = f[(i + 2) % 3];
            const H h = H(int(H::RotateX) + i);
            const ImU32 col = colorFor(h, i);
            ImVec2 prev;
            bool havePrev = false, prevFront = true;
            for (int k = 0; k <= 96; ++k) {
                const float t = float(k) / 96.0f * 6.2831853f;
                const glm::vec3 r = u * std::cos(t) + v * std::sin(t);
                ImVec2 p;
                if (!gizmoProject(c + r * L, p)) {
                    havePrev = false;
                    continue;
                }
                const bool front = glm::dot(r, toEye) > -0.02f;
                if (havePrev) {
                    if (front && prevFront) {
                        dl->AddLine(prev, p, shadow, thick + 2.0f * s);
                        dl->AddLine(prev, p, col, thick);
                    } else {
                        dl->AddLine(prev, p, (col & 0x00ffffff) | 0x48000000u, 1.5f * s);
                    }
                }
                prev = p;
                prevFront = front;
                havePrev = true;
            }
        }
        const ImU32 vc = gizmo_.hover == H::RotateView ? kHot : IM_COL32(222, 222, 222, dragging ? 110 : 220);
        dl->AddCircle(pc, kRadiusPx * kViewRing * s, shadow, 96, thick + 2.0f * s);
        dl->AddCircle(pc, kRadiusPx * kViewRing * s, vc, 96, thick);
        if (dragging && isRotate(gizmo_.handle) && !gizmo_.tangent) {
            // The swept angle as a pie, so the turn reads at a glance.
            const glm::vec3 a = gizmo_.handle == H::RotateView ? camera_.forward() : f[axisOf(gizmo_.handle)];
            const float ang = glm::degrees(gizmo_.current.rotate.w < 1.0f ? 2.0f * std::acos(std::clamp(gizmo_.current.rotate.w, -1.0f, 1.0f)) : 0.0f);
            const float signedAng = glm::dot(glm::vec3(gizmo_.current.rotate.x, gizmo_.current.rotate.y, gizmo_.current.rotate.z), a) < 0 ? -ang : ang;
            std::vector<ImVec2> pie{pc};
            const int steps = std::max(2, int(std::abs(signedAng) / 4.0f));
            const float radius = gizmo_.handle == H::RotateView ? L * kViewRing : L;
            for (int k = 0; k <= steps; ++k) {
                const glm::quat q = glm::angleAxis(glm::radians(signedAng) * float(k) / float(steps), a);
                ImVec2 p;
                if (gizmoProject(c + (q * gizmo_.grabDir) * radius, p)) pie.push_back(p);
            }
            if (pie.size() >= 3) {
                for (size_t k = 1; k + 1 < pie.size(); ++k) dl->AddTriangleFilled(pie[0], pie[k], pie[k + 1], IM_COL32(255, 214, 92, 50));
                dl->AddLine(pie[0], pie[1], kHot, 1.5f * s);
                dl->AddLine(pie[0], pie.back(), kHot, 1.5f * s);
            }
        }
    }
    // The pivot.
    dl->AddCircleFilled(pc, 3.5f * s, IM_COL32(255, 255, 255, 230), 16);
    dl->AddCircle(pc, 3.5f * s, shadow, 16, 1.0f);
    if (!gizmo_.readout.empty()) {
        const ImVec2 at(pc.x + 18.0f * s, pc.y + kRadiusPx * s * 0.55f);
        const ImVec2 size = ImGui::CalcTextSize(gizmo_.readout.c_str());
        dl->AddRectFilled(ImVec2(at.x - 5 * s, at.y - 3 * s), ImVec2(at.x + size.x + 5 * s, at.y + size.y + 3 * s),
                          IM_COL32(20, 20, 22, 210), 4.0f * s);
        dl->AddText(at, kHot, gizmo_.readout.c_str());
    }
    if (gizmo_.pivotOnly && drag_ == Drag::Gizmo) {
        const char* hint = "Moving the gizmo only";
        dl->AddText(ImVec2(pc.x + 18.0f * s, pc.y - kRadiusPx * s * 0.7f), IM_COL32(230, 230, 230, 230), hint);
    }
    dl->PopClipRect();
}

void App::drawGizmoPanel() {
    const CanvasRect c = canvasRect();
    ImGui::SetNextWindowPos(ImVec2(c.x + 12.0f * uiScale_, c.y + c.h - 12.0f * uiScale_), ImGuiCond_FirstUseEver,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(270.0f * uiScale_, 0.0f), ImGuiCond_Always);
    if (ImGui::Begin("Gizmo", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        drawGizmoContents(true);
    }
    ImGui::End();
}

void App::drawGizmoContents(bool compact) {
    ui::Segmented(*this, {"tool.draw", "tool.move", "tool.scale", "tool.rotate"});
    if (!compact && !gizmoActive()) {
        ImGui::TextDisabled("Q W E R: Draw, Move, Scale, Rotate");
        return;
    }
    // Pivot.
    ImGui::SetNextItemWidth(-1.0f);
    float pivot[3] = {gizmo_.pivot.x, gizmo_.pivot.y, gizmo_.pivot.z};
    if (ImGui::DragFloat3("##pivot", pivot, 0.005f, 0.0f, 0.0f, "%.3f")) {
        gizmo_.pivot = glm::vec3(pivot[0], pivot[1], pivot[2]);
        gizmo_.pivotPlaced = true;
    }
    ui::HelpText("Pivot", nullptr,
                 "Where the gizmo sits: rotation and scaling turn about this point. It follows the centre of what "
                 "moves until you place it: Alt+drag the gizmo, Alt+click the model, To Surface, or type here.",
                 kPath);
    ui::Segmented(*this, {"gizmo.reset_pivot", "gizmo.snap_pivot"});
    ui::Segmented(*this, {"gizmo.orient_world", "gizmo.orient_surface"});
    ui::Segmented(*this, {"gizmo.use_mask", "gizmo.all"});
    float pct = gizmo_.falloff * 100.0f;
    ImGui::SetNextItemWidth(-1.0f);
    if (ui::SliderInside("##gizmo_falloff", &pct, 0.0f, 50.0f, "Falloff %.0f%%")) gizmo_.falloff = pct / 100.0f;
    ui::HelpText("Falloff", nullptr,
                 "A soft joint for posing: the movement eases in over this distance from the masked part (a share "
                 "of the Part's size), so a hard mask edge bends smoothly instead of creasing. 0: the mask as it is.",
                 kPath);
    ImGui::Separator();
    // Numeric entry: one transform about the pivot, along the gizmo's axes.
    const float labelW = ImGui::CalcTextSize("Rotate ").x + ImGui::GetStyle().ItemSpacing.x;
    auto row = [&](const char* label, const char* id, float* v, int n, const char* format, const char* help) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelW + ImGui::GetStyle().WindowPadding.x);
        const float spacing = ImGui::GetStyle().ItemSpacing.x * 0.5f;
        const float w = (ImGui::GetContentRegionAvail().x - spacing * float(n - 1)) / float(n);
        for (int i = 0; i < n; ++i) {
            if (i) ImGui::SameLine(0.0f, spacing);
            ImGui::SetNextItemWidth(w);
            const std::string itemId = std::string("##") + id + (n > 1 ? std::string("_") + "xyz"[i] : std::string());
            ImGui::InputFloat(itemId.c_str(), &v[i], 0.0f, 0.0f, format);
            ui::HelpText(label, nullptr, help, kPath);
        }
    };
    row("Move", "gizmo_move", gizmo_.move, 3, "%.3g", "Distance to move along X, Y and Z (the gizmo's axes) when you press Apply.");
    row("Rotate", "gizmo_rotate", gizmo_.rotate, 3, "%.3g",
        "Degrees to turn about X, then Y, then Z (the gizmo's axes, through the pivot) when you press Apply.");
    row("Scale", "gizmo_scale", &gizmo_.scale, 1, "%.1f%%", "Size to scale to about the pivot when you press Apply (100% = unchanged).");
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Apply", ImVec2(w, 0))) applyGizmoNumbers();
    ui::HelpText("Apply", nullptr,
                 "Move, rotate and scale by the numbers above, about the pivot, as one undo step (the transform "
                 "command). Masked parts stay; with symmetry on, a masked transform is mirrored.",
                 kPath);
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(w, 0))) {
        std::fill(gizmo_.move, gizmo_.move + 3, 0.0f);
        std::fill(gizmo_.rotate, gizmo_.rotate + 3, 0.0f);
        gizmo_.scale = 100.0f;
    }
    ui::HelpText("Clear", nullptr, "Set Move and Rotate back to 0 and Scale to 100%.", kPath);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("Shift snaps (15 deg, grid, 10%%). Alt+drag the gizmo: move it only. Alt+click the model: pivot there.");
    ImGui::PopStyleColor();
}

} // namespace sf
