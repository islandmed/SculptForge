// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > ZSpheres and the ZSphere tool in the viewport (ZBrush's ZSpheres, core/ZSpheres.h). While a ZSphere
// SubTool is active a left-drag on it edits the armature instead of sculpting:
//   Draw  - drag on a sphere to grow a new one from it (the drag sets its size); click a link to add a joint there
//           and drag to place it
//   Move  - drag a sphere to move it across the screen
//   Scale - drag a sphere up/right to grow it, down/left to shrink it
//   Alt+click a sphere deletes it with everything growing from it. Symmetry mirrors every edit.
// Dragging the background navigates as usual. Every drag is one undo step (the SubTool's mesh, which carries
// the armature, is replaced once at release); the panel's buttons run the zsphere_* commands.
#include "app/App.h"
#include "app/Palettes.h"
#include "app/Ui.h"

#include "core/ZSpheres.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr const char* kZSpheresPath = "Tool > Forge Spheres";

const char* modeName(int m) { return m == 0 ? "Draw" : m == 1 ? "Move" : "Scale"; } // App::ZSphereMode order

// Where the ray o + t*d meets the plane through p with normal n (t clamped to stay in front).
glm::vec3 planeHit(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p, const glm::vec3& n) {
    const float dn = glm::dot(d, n);
    const float t = std::abs(dn) > 1e-12f ? glm::dot(p - o, n) / dn : 0.0f;
    return o + d * std::max(t, 0.0f);
}

} // namespace

void App::registerZSphereUiActions() {
    auto add = [this](const char* id, const char* label, std::function<void()> run, std::function<bool()> enabled,
                      std::function<bool()> checked, const char* description, const char* command = "",
                      const char* shortcut = "") {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = kZSpheresPath;
        a.run = std::move(run);
        a.enabled = std::move(enabled);
        a.checked = std::move(checked);
        a.description = description;
        a.command = command;
        a.shortcut = shortcut;
        actions_.add(std::move(a));
    };
    const auto active = [this] { return zsphereActive(); };
    add("zsphere.new", "New Forge Sphere", [this] { runCommand("zsphere_new"); }, nullptr, nullptr,
        "Add a Forge Sphere Part: one root sphere to block out a body, creature or plant from. Drag on a sphere to grow "
        "limbs from it, Move and Scale them into proportion, check the skin with Preview (A), then Make Mesh.",
        "zsphere_new");
    const auto mode = [this](ZSphereMode m) {
        return [this, m] {
            zsphereMode_ = m;
            status_ = std::string("Forge Spheres: ") + modeName(int(m)) +
                      (m == ZSphereMode::Draw    ? " - drag on a sphere to grow a new one; click a link to add a joint"
                       : m == ZSphereMode::Move ? " - drag a sphere to move it"
                                                : " - drag a sphere up or right to grow it, down or left to shrink it");
        };
    };
    add("zsphere.draw", "Draw", mode(ZSphereMode::Draw), active, [this] { return zsphereMode_ == ZSphereMode::Draw; },
        "Drag on a sphere to grow a new sphere from it (how far you drag sets its size). Click a link between two "
        "spheres to add a joint there, and drag to place it. Alt+click a sphere to delete it.");
    add("zsphere.move", "Move", mode(ZSphereMode::Move), active, [this] { return zsphereMode_ == ZSphereMode::Move; },
        "Drag a sphere to move it; the links stretch to follow. Symmetry moves its mirror twin too.");
    add("zsphere.scale", "Scale", mode(ZSphereMode::Scale), active, [this] { return zsphereMode_ == ZSphereMode::Scale; },
        "Drag a sphere up or right to make it bigger, down or left to make it smaller.");
    add("zsphere.preview", "Preview", [this] { runCommand("zsphere_preview"); }, active,
        [this] { return doc_.mesh.zspherePreview; },
        "Show the skin Make Mesh would build instead of the spheres (ZBrush's Preview). Turn it off to edit the "
        "spheres again.",
        "zsphere_preview", "A");
    add("zsphere.make_mesh", "Make Mesh",
        [this] { runCommand("zsphere_make_mesh", {{"resolution", double(zsphereResolution_)}}); }, active, nullptr,
        "Build the skin as a new, sculptable Part right below (watertight quads, one face group per link), like "
        "ZBrush's Make Adaptive Skin. The Forge Sphere Part is hidden and kept: show it to change the proportions and "
        "make a new mesh.",
        "zsphere_make_mesh");
}

void App::drawZSpheresContents() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float full = ImGui::GetContentRegionAvail().x;
    const float half = (full - style.ItemSpacing.x) * 0.5f;
    if (!zsphereActive()) {
        ui::ActionButton(*this, "zsphere.new", ImVec2(full, 0));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Block out a base mesh from connected spheres, then Make Mesh to sculpt it.");
        ImGui::PopTextWrapPos();
        return;
    }
    tray::GroupLabel(*this, "Edit");
    const float third = (full - 2.0f * style.ItemSpacing.x) / 3.0f;
    ui::ActionButton(*this, "zsphere.draw", ImVec2(third, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "zsphere.move", ImVec2(third, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "zsphere.scale", ImVec2(-1, 0));
    ui::ActionButton(*this, "zsphere.preview", ImVec2(half, 0));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%zu %s", doc_.mesh.zspheres.size(), doc_.mesh.zspheres.size() == 1 ? "sphere" : "spheres");
    tray::GroupLabel(*this, "Make Mesh");
    ui::ActionButton(*this, "zsphere.make_mesh", ImVec2(half, 0));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    tray::SliderInt("##zsres", &zsphereResolution_, 32, 512, "Resolution %d");
    ui::HelpText("Resolution", nullptr,
                 "How dense Make Mesh's skin is: cells across the model's longest side. 128 suits a blockout; raise it "
                 "for thin fingers or tails.",
                 kZSpheresPath);
    ui::ActionButton(*this, "zsphere.new", ImVec2(full, 0));
}

float App::worldPerPixel(const glm::vec3& at) const {
    ImVec2 sp;
    if (!gizmoProject(at, sp)) return 0.0f;
    glm::vec3 o0, d0, o1, d1;
    canvasRay(sp.x, sp.y, o0, d0);
    canvasRay(sp.x + 1.0f, sp.y, o1, d1);
    const glm::vec3 n = glm::normalize(d0);
    return glm::length(planeHit(o1, d1, at, n) - planeHit(o0, d0, at, n));
}

int App::zsphereAt(float x, float y, bool* link) const {
    RayHit hit;
    if (!zsphereActive() || doc_.mesh.zspherePreview || !pick(x, y, hit)) return -1;
    const int i = zsphereOfGroup(doc_.mesh.polygroupAt(hit.triangle), link);
    return i >= 0 && size_t(i) < doc_.mesh.zspheres.size() ? i : -1;
}

bool App::zsphereMouseDown(float x, float y, SDL_Keymod mod) {
    if (!zsphereActive()) return false;
    RayHit hit;
    if (!pick(x, y, hit)) return false; // the background navigates
    if (doc_.mesh.zspherePreview) {
        status_ = "Forge Spheres: Preview is on (A turns it off to edit the spheres)";
        return true;
    }
    bool link = false;
    const int i = zsphereOfGroup(doc_.mesh.polygroupAt(hit.triangle), &link);
    if (i < 0 || size_t(i) >= doc_.mesh.zspheres.size()) return true;
    if (mod & SDL_KMOD_ALT) { // delete the sphere and what grows from it
        if (link || i == 0) {
            status_ = i == 0 && !link ? "Forge Spheres: the root sphere stays" : "Forge Spheres: Alt+click a sphere to delete it";
            return true;
        }
        std::vector<ZSphere> s = doc_.mesh.zspheres;
        const size_t before = s.size();
        zsphereDelete(s, i, symmetry_);
        doc_.replaceMeshUndoable(zsphereMesh(s, false), "Forge Sphere delete");
        status_ = "Forge Spheres: deleted " + std::to_string(before - s.size()) + " spheres; " + std::to_string(s.size()) + " left";
        return true;
    }
    ZSphereDrag& d = zsDrag_;
    d = {};
    d.start = doc_.mesh.zspheres;
    d.before = doc_.mesh;
    d.x = x;
    d.y = y;
    d.op = zsphereMode_;
    d.sphere = i;
    if (d.op == ZSphereMode::Draw && link) { // a joint on the link, then the drag places it
        zsphereInsert(d.start, i, symmetry_, &d.sphere);
        d.op = ZSphereMode::Move;
        d.label = "Forge Sphere insert";
        d.changed = true;
        doc_.mesh = zsphereMesh(d.start, false);
        doc_.mesh.topologyDirty = true;
        doc_.bvh.build(doc_.mesh);
    } else if (d.op == ZSphereMode::Draw) {
        d.label = "Forge Sphere add";
    } else {
        d.label = d.op == ZSphereMode::Move ? "Forge Sphere move" : "Forge Sphere scale";
    }
    if (d.op == ZSphereMode::Move) {
        glm::vec3 o, dir;
        canvasRay(x, y, o, dir);
        const glm::vec3 c = d.start[size_t(d.sphere)].center;
        d.grab = c - planeHit(o, dir, c, glm::normalize(dir));
    }
    if (d.op == ZSphereMode::Draw) zsphereDragTo(x, y); // the new sphere appears at once, at half its parent's size
    drag_ = Drag::ZSphere;
    return true;
}

void App::zsphereDragTo(float x, float y) {
    ZSphereDrag& d = zsDrag_;
    if (d.sphere < 0) return;
    std::vector<ZSphere> s = d.start;
    const ZSphere& z = s[size_t(d.sphere)];
    const float moved = std::hypot(x - d.x, y - d.y);
    switch (d.op) {
    case ZSphereMode::Draw: {
        // The new sphere sits on the parent's surface where the drag started, sized by the drag.
        glm::vec3 o, dir;
        canvasRay(d.x, d.y, o, dir);
        glm::vec3 n = -glm::normalize(dir);
        const float tc = glm::dot(z.center - o, glm::normalize(dir));
        const glm::vec3 closest = o + glm::normalize(dir) * tc;
        const float off = glm::length(closest - z.center);
        if (off < z.radius) // the front of the sphere under the press
            n = glm::normalize(closest - glm::normalize(dir) * std::sqrt(z.radius * z.radius - off * off) - z.center);
        const glm::vec3 c = z.center + n * z.radius;
        const float wpp = worldPerPixel(c);
        const float r = moved > 3.0f * uiScale_ && wpp > 0.0f ? std::max(moved * wpp, 0.02f * z.radius) : 0.5f * z.radius;
        zsphereAdd(s, d.sphere, c, r, symmetry_);
        status_ = "Forge Spheres: new sphere, radius " + std::to_string(r).substr(0, 5);
        break;
    }
    case ZSphereMode::Move: {
        glm::vec3 o, dir;
        canvasRay(x, y, o, dir);
        const glm::vec3 c = planeHit(o, dir, z.center, glm::normalize(dir)) + d.grab;
        zsphereSet(s, d.sphere, c, z.radius, symmetry_);
        break;
    }
    case ZSphereMode::Scale: {
        const float amount = ((x - d.x) - (y - d.y)) / (120.0f * uiScale_);
        zsphereSet(s, d.sphere, z.center, std::max(z.radius * std::exp(amount), 1e-4f), symmetry_);
        break;
    }
    }
    doc_.mesh = zsphereMesh(s, false);
    doc_.mesh.topologyDirty = true;
    doc_.bvh.build(doc_.mesh);
    d.changed = true;
}

void App::zsphereRelease() {
    ZSphereDrag& d = zsDrag_;
    if (d.changed) {
        Mesh after = std::move(doc_.mesh);
        doc_.mesh = std::move(d.before);
        doc_.replaceMeshUndoable(std::move(after), d.label);
        status_ = std::string("Forge Spheres: ") + d.label + "; " + std::to_string(doc_.mesh.zspheres.size()) + " spheres";
    }
    d = {};
}

void App::drawZSphereOverlay() {
    if (!zsphereActive() || doc_.mesh.zspherePreview || !doc_.activeSubtoolInfo().visible) return;
    if (ImGui::GetIO().WantCaptureMouse) return;
    const CanvasRect cr = canvasRect();
    if (!cr.contains(mouseX_, mouseY_)) return;
    const bool dragging = drag_ == Drag::ZSphere;
    if (drag_ != Drag::None && !dragging) return;
    bool link = false;
    const int i = dragging ? zsDrag_.sphere : zsphereAt(mouseX_, mouseY_, &link);
    if (i < 0 || size_t(i) >= doc_.mesh.zspheres.size()) return;
    const bool alt = !dragging && (inputMods() & SDL_KMOD_ALT);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const float s = uiScale_;
    const ImU32 col = alt ? IM_COL32(255, 90, 80, 230) : IM_COL32(120, 200, 255, 230);
    if (link && !dragging) { // Draw: the joint a click would add
        const ZSphere& c = doc_.mesh.zspheres[size_t(i)];
        const ZSphere& p = doc_.mesh.zspheres[size_t(c.parent)];
        ImVec2 m;
        const glm::vec3 mid = (c.center + p.center) * 0.5f;
        const float wpp = worldPerPixel(mid);
        if (zsphereMode_ == ZSphereMode::Draw && gizmoProject(mid, m) && wpp > 0.0f)
            dl->AddCircle(m, 0.5f * (c.radius + p.radius) / wpp, IM_COL32(120, 200, 255, 160), 48, 1.5f * s);
    } else {
        const ZSphere& z = doc_.mesh.zspheres[size_t(i)];
        ImVec2 c;
        const float wpp = worldPerPixel(z.center);
        if (gizmoProject(z.center, c) && wpp > 0.0f) dl->AddCircle(c, z.radius / wpp, col, 64, 2.0f * s);
    }
    dl->PopClipRect();
}

} // namespace sf
