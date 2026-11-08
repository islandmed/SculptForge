// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Forge Spheres and the Forge Sphere tool in the viewport (Forge Spheres, core/Forge Spheres.h). While a Forge Sphere
// Part is active a left-drag on it edits the armature instead of sculpting:
//   Draw  - one mode for building (#167): click a sphere to grow a new one from it, drag its body to move it, drag
//           its outer band (the rim) to resize it; click a link to add a joint there and drag to place it. The
//           cursor and a tag beside it say which a drag will do.
//   Move  - drag a sphere to move it across the screen
//   Scale - drag a sphere up/right to grow it, down/left to shrink it
//   Alt+click a sphere deletes it with everything growing from it. Symmetry mirrors every edit.
// Pointer only (#8): Drag places child makes a Draw drag grow a new sphere and move it away from its parent
// (Ctrl+drag does it once), the last sphere clicked or added is selected (Delete Selected, Insert Between, Set as
// Root), and a right click on a sphere opens a menu with the same three.
// Dragging the background navigates as usual. Every drag is one undo step (the Part's mesh, which carries
// the armature, is replaced once at release); the panel's buttons run the Forge Sphere_* commands.
#include "app/App.h"
#include "app/Demo.h"
#include "app/Palettes.h"
#include "app/Ui.h"

#include "core/ForgeSpheres.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr const char* kForgeSpheresPath = "Tool > Forge Spheres";

bool ctrlDown(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; } // as App.cpp

constexpr float kClickSlop = 4.0f; // Draw: a press that moves less than this (UI pixels) is a click: it adds a sphere

const char* modeName(int m) { return m == 0 ? "Draw" : m == 1 ? "Move" : "Scale"; } // App::Forge SphereMode order

// Where the ray o + t*d meets the plane through p with normal n (t clamped to stay in front).
glm::vec3 planeHit(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p, const glm::vec3& n) {
    const float dn = glm::dot(d, n);
    const float t = std::abs(dn) > 1e-12f ? glm::dot(p - o, n) / dn : 0.0f;
    return o + d * std::max(t, 0.0f);
}

} // namespace

void App::registerForgeSphereUiActions() {
    auto add = [this](const char* id, const char* label, std::function<void()> run, std::function<bool()> enabled,
                      std::function<bool()> checked, const char* description, const char* command = "",
                      const char* shortcut = "") {
        Action a;
        a.id = id;
        a.label = label;
        a.palette = kForgeSpheresPath;
        a.run = std::move(run);
        a.enabled = std::move(enabled);
        a.checked = std::move(checked);
        a.description = description;
        a.command = command;
        a.shortcut = shortcut;
        actions_.add(std::move(a));
    };
    const auto active = [this] { return forgesphereActive(); };
    add("forgesphere.new", "New Forge Sphere",
        [this] {
            const int before = doc_.activePart;
            runCommand("forgesphere_new");
            if (forgesphereActive() && doc_.activePart != before) {
                forgesphereReturnPart_ = before; // a brush, Draw or Esc goes back to it (PanelTube.cpp leaveDrawModes)
                status_ += ". A brush or Esc goes back to sculpting";
            }
        },
        nullptr, nullptr,
        "Add a Forge Sphere Part: one root sphere to block out a body, creature or plant from. Click a sphere to grow "
        "limbs from it, drag them into place and proportion, check the skin with Preview (A), then Make Mesh.",
        "forgesphere_new");
    const auto mode = [this](ForgeSphereMode m) {
        return [this, m] {
            forgesphereMode_ = m;
            status_ = std::string("Forge Spheres: ") + modeName(int(m)) +
                      (m == ForgeSphereMode::Draw    ? " - click a sphere to grow a new one, drag it to move, drag its rim to resize"
                       : m == ForgeSphereMode::Move ? " - drag a sphere to move it"
                                                : " - drag a sphere up or right to grow it, down or left to shrink it");
        };
    };
    add("forgesphere.draw", "Draw", mode(ForgeSphereMode::Draw), active, [this] { return forgesphereMode_ == ForgeSphereMode::Draw; },
        "Build without switching modes: click a sphere to grow a new one from it, drag a sphere to move it, drag "
        "near its edge to resize it (the cursor shows which). Click a link between two spheres to add a joint "
        "there, and drag to place it. Alt+click a sphere to delete it.");
    add("forgesphere.move", "Move", mode(ForgeSphereMode::Move), active, [this] { return forgesphereMode_ == ForgeSphereMode::Move; },
        "Drag a sphere to move it; the links stretch to follow. Symmetry moves its mirror twin too.");
    add("forgesphere.scale", "Scale", mode(ForgeSphereMode::Scale), active, [this] { return forgesphereMode_ == ForgeSphereMode::Scale; },
        "Drag a sphere up or right to make it bigger, down or left to make it smaller.");
    add("forgesphere.place_child", "Drag places child", [this] { forgespherePlaceChild_ = !forgespherePlaceChild_; }, active,
        [this] { return forgespherePlaceChild_; },
        "Draw: dragging on a sphere grows a new sphere and moves it away from it (one drag places a limb joint) "
        "instead of moving the sphere. Off, hold Ctrl while you drag for the same.");
    const auto selected = [this] { return forgesphereActive() && forgesphereSelection() > 0; };
    add("forgesphere.delete_selected", "Delete Selected", [this] { forgesphereEdit(ForgeSphereEdit::Delete, forgesphereSelection()); },
        selected, nullptr,
        "Delete the selected sphere (the one you last clicked or added, outlined in orange) and everything growing "
        "from it. The root stays.");
    add("forgesphere.insert_selected", "Insert Between", [this] { forgesphereEdit(ForgeSphereEdit::Insert, forgesphereSelection()); },
        selected, nullptr, "Add a joint halfway along the link from the selected sphere to the one it grows from.");
    add("forgesphere.set_root", "Set as Root", [this] { forgesphereEdit(ForgeSphereEdit::SetRoot, forgesphereSelection()); }, selected,
        nullptr,
        "Make the selected sphere the root: the armature keeps its shape and grows from there (the links on the way "
        "turn around).",
        "forgesphere_set_root");
    add("forgesphere.preview", "Preview", [this] { runCommand("forgesphere_preview"); }, active,
        [this] { return doc_.mesh.forgespherePreview; },
        "Show the skin Make Mesh would build instead of the spheres. Turn it off to edit the "
        "spheres again.",
        "forgesphere_preview", "A");
    add("forgesphere.make_mesh", "Make Mesh",
        [this] { runCommand("forgesphere_make_mesh", {{"resolution", double(forgesphereResolution_)}}); }, active, nullptr,
        "Build the skin as a new, sculptable Part right below (watertight quads, one face group per link), like "
        "Make Adaptive Skin. The Forge Sphere Part is hidden and kept: show it to change the proportions and "
        "make a new mesh.",
        "forgesphere_make_mesh");
}

void App::drawForgeSpheresContents() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float full = ImGui::GetContentRegionAvail().x;
    const float half = (full - style.ItemSpacing.x) * 0.5f;
    if (!forgesphereActive()) {
        ui::ActionButton(*this, "forgesphere.new", ImVec2(full, 0));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("Block out a base mesh from connected spheres, then Make Mesh to sculpt it."));
        ImGui::PopTextWrapPos();
        return;
    }
    tray::GroupLabel(*this, "Edit");
    const float third = (full - 2.0f * style.ItemSpacing.x) / 3.0f;
    ui::ActionButton(*this, "forgesphere.draw", ImVec2(third, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "forgesphere.move", ImVec2(third, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "forgesphere.scale", ImVec2(-1, 0));
    ui::ActionButton(*this, "forgesphere.place_child", ImVec2(full, 0));
    ui::ActionButton(*this, "forgesphere.delete_selected", ImVec2(full, 0));
    ui::ActionButton(*this, "forgesphere.insert_selected", ImVec2(half, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "forgesphere.set_root", ImVec2(-1, 0));
    ui::ActionButton(*this, "forgesphere.preview", ImVec2(half, 0));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%zu %s", doc_.mesh.forgespheres.size(), doc_.mesh.forgespheres.size() == 1 ? "sphere" : "spheres");
    if (const int sel = forgesphereSelection(); sel >= 0) {
        ImGui::SameLine();
        ImGui::TextDisabled(tr(sel == 0 ? "(root selected)" : "(sphere %d selected)"), sel);
    }
    tray::GroupLabel(*this, "Make Mesh");
    ui::ActionButton(*this, "forgesphere.make_mesh", ImVec2(half, 0));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    tray::SliderInt("##zsres", &forgesphereResolution_, 32, 512, "Resolution %d");
    ui::HelpText("Resolution", nullptr,
                 "How dense Make Mesh's skin is: cells across the model's longest side. 128 suits a blockout; raise it "
                 "for thin fingers or tails.",
                 kForgeSpheresPath);
    ui::ActionButton(*this, "forgesphere.new", ImVec2(full, 0));
}

float App::worldPerPixel(const glm::vec3& at) const {
    ImVec2 sp;
    if (!partProject(at, sp)) return 0.0f;
    glm::vec3 o0, d0, o1, d1;
    partRay(sp.x, sp.y, o0, d0);
    partRay(sp.x + 1.0f, sp.y, o1, d1);
    const glm::vec3 n = glm::normalize(d0);
    return glm::length(planeHit(o1, d1, at, n) - planeHit(o0, d0, at, n));
}

int App::forgesphereAt(float x, float y, bool* link) const {
    RayHit hit;
    if (!forgesphereActive() || doc_.mesh.forgespherePreview || !pick(x, y, hit)) return -1;
    const int i = forgesphereOfGroup(doc_.mesh.facegroupAt(hit.triangle), link);
    return i >= 0 && size_t(i) < doc_.mesh.forgespheres.size() ? i : -1;
}

bool App::forgesphereNearRim(int i, float x, float y) const {
    if (i < 0 || size_t(i) >= doc_.mesh.forgespheres.size()) return false;
    const ForgeSphere& z = doc_.mesh.forgespheres[size_t(i)];
    ImVec2 c;
    const float wpp = worldPerPixel(z.center);
    if (!partProject(z.center, c) || wpp <= 0.0f) return false;
    const float r = z.radius / wpp; // the outline the overlay draws
    const float band = std::min(0.3f * r, 14.0f * uiScale_); // a small sphere keeps most of its body for moving
    return std::hypot(x - c.x, y - c.y) >= r - band;
}

bool App::forgesphereMouseDown(float x, float y, SDL_Keymod mod) {
    if (!forgesphereActive()) return false;
    RayHit hit;
    if (!pick(x, y, hit)) return false; // the background navigates
    if (doc_.mesh.forgespherePreview) {
        status_ = "Forge Spheres: Preview is on (A turns it off to edit the spheres)";
        return true;
    }
    bool link = false;
    const int i = forgesphereOfGroup(doc_.mesh.facegroupAt(hit.triangle), &link);
    if (i < 0 || size_t(i) >= doc_.mesh.forgespheres.size()) return true;
    if (mod & SDL_KMOD_ALT) { // delete the sphere and what grows from it
        if (link || i == 0) {
            status_ = i == 0 && !link ? "Forge Spheres: the root sphere stays" : "Forge Spheres: Alt+click a sphere to delete it";
            return true;
        }
        std::vector<ForgeSphere> s = doc_.mesh.forgespheres;
        const size_t before = s.size();
        forgesphereDelete(s, i, symmetry_);
        doc_.replaceMeshUndoable(forgesphereMesh(s, false), "Forge Sphere delete");
        status_ = "Forge Spheres: deleted " + std::to_string(before - s.size()) + " spheres; " + std::to_string(s.size()) + " left";
        forgesphereSelected_ = -1;
        return true;
    }
    if (!link) forgesphereSelected_ = i;
    ForgeSphereDrag& d = zsDrag_;
    d = {};
    d.start = doc_.mesh.forgespheres;
    d.before = doc_.mesh;
    d.x = x;
    d.y = y;
    d.op = forgesphereMode_;
    d.sphere = i;
    if (d.op == ForgeSphereMode::Draw && link) { // a joint on the link, then the drag places it
        forgesphereInsert(d.start, i, symmetry_, &d.sphere);
        d.op = ForgeSphereMode::Move;
        d.label = "Forge Sphere insert";
        d.changed = true;
        doc_.mesh = forgesphereMesh(d.start, false);
        doc_.mesh.topologyDirty = true;
        doc_.bvh.build(doc_.mesh);
    } else if (d.op == ForgeSphereMode::Draw) {
        d.label = "Forge Sphere add";
        d.place = forgespherePlaceChild_ || ctrlDown(mod); // the drag grows a new sphere and moves it out
        if (!d.place) { // a click adds, a drag moves or resizes: decided at release or once it moves (#167)
            d.pending = true;
            d.rim = forgesphereNearRim(i, x, y);
            ImVec2 c;
            if (partProject(d.start[size_t(i)].center, c)) d.rimFrom = std::max(std::hypot(x - c.x, y - c.y), 1.0f);
        }
    } else {
        d.label = d.op == ForgeSphereMode::Move ? "Forge Sphere move" : "Forge Sphere scale";
    }
    if (d.op == ForgeSphereMode::Move) {
        glm::vec3 o, dir;
        partRay(x, y, o, dir);
        const glm::vec3 c = d.start[size_t(d.sphere)].center;
        d.grab = c - planeHit(o, dir, c, glm::normalize(dir));
    }
    if (d.op == ForgeSphereMode::Draw && !d.pending) forgesphereDragTo(x, y); // the new sphere appears at once
    drag_ = Drag::ForgeSphere;
    return true;
}

void App::forgesphereDragTo(float x, float y) {
    ForgeSphereDrag& d = zsDrag_;
    if (d.sphere < 0) return;
    const float moved = std::hypot(x - d.x, y - d.y);
    if (d.pending) { // Draw: past the slop, a drag on the body moves the sphere and one on its rim resizes it
        if (moved < kClickSlop * uiScale_) return;
        d.pending = false;
        d.op = d.rim ? ForgeSphereMode::Scale : ForgeSphereMode::Move;
        d.label = d.rim ? "Forge Sphere scale" : "Forge Sphere move";
        if (!d.rim) {
            glm::vec3 o, dir;
            partRay(d.x, d.y, o, dir);
            const glm::vec3 c = d.start[size_t(d.sphere)].center;
            d.grab = c - planeHit(o, dir, c, glm::normalize(dir));
        }
    }
    std::vector<ForgeSphere> s = d.start;
    const ForgeSphere& z = s[size_t(d.sphere)];
    switch (d.op) {
    case ForgeSphereMode::Draw: {
        // The new sphere sits on the parent's surface where the drag started, sized by the drag.
        glm::vec3 o, dir;
        partRay(d.x, d.y, o, dir);
        glm::vec3 n = -glm::normalize(dir);
        const float tc = glm::dot(z.center - o, glm::normalize(dir));
        const glm::vec3 closest = o + glm::normalize(dir) * tc;
        const float off = glm::length(closest - z.center);
        if (off < z.radius) // the front of the sphere under the press
            n = glm::normalize(closest - glm::normalize(dir) * std::sqrt(z.radius * z.radius - off * off) - z.center);
        const glm::vec3 c = z.center + n * z.radius;
        if (d.place) {
            // Half the parent's size, starting on its surface and following the cursor in the plane through
            // that point facing the camera.
            glm::vec3 o2, dir2;
            partRay(x, y, o2, dir2);
            const glm::vec3 at = planeHit(o2, dir2, c, glm::normalize(dir));
            forgesphereAdd(s, d.sphere, at, 0.5f * z.radius, symmetry_, &d.added);
            status_ = "Forge Spheres: new sphere, " + std::to_string(glm::distance(at, z.center) / z.radius).substr(0, 4) +
                      " parent radii out";
            break;
        }
        const float wpp = worldPerPixel(c);
        const float r = moved > 3.0f * uiScale_ && wpp > 0.0f ? std::max(moved * wpp, 0.02f * z.radius) : 0.5f * z.radius;
        forgesphereAdd(s, d.sphere, c, r, symmetry_, &d.added);
        status_ = "Forge Spheres: new sphere, radius " + std::to_string(r).substr(0, 5);
        break;
    }
    case ForgeSphereMode::Move: {
        glm::vec3 o, dir;
        partRay(x, y, o, dir);
        const glm::vec3 c = planeHit(o, dir, z.center, glm::normalize(dir)) + d.grab;
        forgesphereSet(s, d.sphere, c, z.radius, symmetry_);
        break;
    }
    case ForgeSphereMode::Scale: {
        float r = z.radius;
        ImVec2 c;
        if (d.rim && partProject(z.center, c)) { // Draw's rim drag: the edge follows the cursor
            r *= std::hypot(x - c.x, y - c.y) / d.rimFrom;
        } else { // the Scale button: up/right grows, down/left shrinks
            r *= std::exp(((x - d.x) - (y - d.y)) / (120.0f * uiScale_));
        }
        forgesphereSet(s, d.sphere, z.center, std::max(r, 1e-4f), symmetry_);
        break;
    }
    }
    doc_.mesh = forgesphereMesh(s, false);
    doc_.mesh.topologyDirty = true;
    doc_.bvh.build(doc_.mesh);
    d.changed = true;
}

void App::forgesphereRelease() {
    ForgeSphereDrag& d = zsDrag_;
    if (d.pending) { // Draw: a click on a sphere grows a new one there, half its size
        d.pending = false;
        forgesphereDragTo(d.x, d.y);
    }
    if (d.changed) {
        Mesh after = std::move(doc_.mesh);
        doc_.mesh = std::move(d.before);
        doc_.replaceMeshUndoable(std::move(after), d.label);
        status_ = std::string("Forge Spheres: ") + d.label + "; " + std::to_string(doc_.mesh.forgespheres.size()) + " spheres";
        if (d.added >= 0) forgesphereSelected_ = d.added; // the new sphere
        else if (d.op != ForgeSphereMode::Draw) forgesphereSelected_ = d.sphere; // moved or resized (an inserted joint, too)
    }
    d = {};
}

int App::forgesphereSelection() const {
    return forgesphereActive() && forgesphereSelected_ >= 0 && size_t(forgesphereSelected_) < doc_.mesh.forgespheres.size() ? forgesphereSelected_
                                                                                                          : -1;
}

void App::forgesphereEdit(ForgeSphereEdit edit, int i) {
    if (!forgesphereActive() || i < 0 || size_t(i) >= doc_.mesh.forgespheres.size()) return;
    if (i == 0) {
        status_ = edit == ForgeSphereEdit::SetRoot ? "Forge Spheres: that sphere is the root already"
                                                : "Forge Spheres: the root sphere stays (select another sphere)";
        return;
    }
    std::vector<ForgeSphere> s = doc_.mesh.forgespheres;
    const size_t before = s.size();
    const char* label = "";
    int select = -1;
    switch (edit) {
    case ForgeSphereEdit::Delete:
        forgesphereDelete(s, i, symmetry_);
        label = "Forge Sphere delete";
        break;
    case ForgeSphereEdit::Insert:
        forgesphereInsert(s, i, symmetry_, &select);
        label = "Forge Sphere insert";
        break;
    case ForgeSphereEdit::SetRoot:
        forgesphereSetRoot(s, i);
        label = "Forge Sphere set root";
        select = 0;
        break;
    }
    doc_.replaceMeshUndoable(forgesphereMesh(s, doc_.mesh.forgespherePreview), label);
    forgesphereSelected_ = select;
    status_ = edit == ForgeSphereEdit::Delete   ? "Forge Spheres: deleted " + std::to_string(before - s.size()) + " spheres; " +
                                                std::to_string(s.size()) + " left"
              : edit == ForgeSphereEdit::Insert ? std::string("Forge Spheres: Forge Sphere insert; ") + std::to_string(s.size()) + " spheres"
                                            : std::string("Forge Spheres: Forge Sphere set root (the selected sphere is the root now)");
}

bool App::forgesphereRightDown(float x, float y) {
    zsMenu_ = {};
    bool link = false;
    const int i = forgesphereAt(x, y, &link);
    if (i < 0) return false;
    zsMenu_.armed = true;
    zsMenu_.sphere = i;
    zsMenu_.link = link;
    zsMenu_.x = x;
    zsMenu_.y = y;
    rightClick_.pending = true; // a click opens the menu; a drag past the slop still turns the view
    return true;
}

void App::drawForgeSphereMenu() {
    constexpr const char* kMenu = "Forge Sphere##forgesphere_menu";
    const bool appearing = zsMenu_.open;
    if (zsMenu_.open) {
        ImGui::OpenPopup(kMenu);
        zsMenu_.open = false;
    }
    if (appearing) ImGui::SetNextWindowPos(ImVec2(zsMenu_.x, zsMenu_.y));
    zsMenuShown_ = false;
    if (!ImGui::BeginPopup(kMenu)) return;
    zsMenuShown_ = true;
    const int i = zsMenu_.sphere;
    if (!forgesphereActive() || i < 0 || size_t(i) >= doc_.mesh.forgespheres.size()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    Demo::pushScope("Forge Sphere menu");
    ImGui::TextDisabled(tr(i == 0 ? "Root sphere" : "Sphere %d"), i);
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Delete Sphere"), nullptr, false, i > 0)) forgesphereEdit(ForgeSphereEdit::Delete, i);
    if (ImGui::MenuItem(tr("Insert Between"), nullptr, false, i > 0)) forgesphereEdit(ForgeSphereEdit::Insert, i);
    if (ImGui::MenuItem(tr("Set as Root"), nullptr, false, i > 0)) forgesphereEdit(ForgeSphereEdit::SetRoot, i);
    Demo::popScope();
    ImGui::EndPopup();
}

void App::drawForgeSphereOverlay() {
    forgesphereHint_ = 0;
    if (!forgesphereActive() || doc_.mesh.forgespherePreview || !doc_.activePartInfo().visible) return;
    if (ImGui::GetIO().WantCaptureMouse) return;
    const CanvasRect cr = canvasRect();
    if (!cr.contains(mouseX_, mouseY_)) return;
    const bool dragging = drag_ == Drag::ForgeSphere;
    if (drag_ != Drag::None && !dragging) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float s = uiScale_;
    if (const int sel = forgesphereSelection(); sel >= 0 && !dragging) { // the selected sphere, in orange
        const ForgeSphere& z = doc_.mesh.forgespheres[size_t(sel)];
        ImVec2 c;
        const float wpp = worldPerPixel(z.center);
        dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
        if (partProject(z.center, c) && wpp > 0.0f) dl->AddCircle(c, z.radius / wpp + 2.0f * s, IM_COL32(255, 170, 60, 220), 64, 2.0f * s);
        dl->PopClipRect();
    }
    bool link = false;
    const int i = dragging ? zsDrag_.sphere : forgesphereAt(mouseX_, mouseY_, &link);
    if (i < 0 || size_t(i) >= doc_.mesh.forgespheres.size()) return;
    const bool alt = !dragging && (inputMods() & SDL_KMOD_ALT);
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const ImU32 col = alt ? IM_COL32(255, 90, 80, 230) : IM_COL32(120, 200, 255, 230);
    if (link && !dragging) { // Draw: the joint a click would add
        const ForgeSphere& c = doc_.mesh.forgespheres[size_t(i)];
        const ForgeSphere& p = doc_.mesh.forgespheres[size_t(c.parent)];
        ImVec2 m;
        const glm::vec3 mid = (c.center + p.center) * 0.5f;
        const float wpp = worldPerPixel(mid);
        if (forgesphereMode_ == ForgeSphereMode::Draw && partProject(mid, m) && wpp > 0.0f)
            dl->AddCircle(m, 0.5f * (c.radius + p.radius) / wpp, IM_COL32(120, 200, 255, 160), 48, 1.5f * s);
    } else {
        const ForgeSphere& z = doc_.mesh.forgespheres[size_t(i)];
        ImVec2 c;
        const float wpp = worldPerPixel(z.center);
        // Draw (#167): what a drag here does - the cursor and a tag beside it say it, and the rim thickens for resize.
        enum class Hint { None, Move, Resize, Add } hint = Hint::None;
        if (dragging) {
            hint = zsDrag_.pending ? Hint::None
                   : zsDrag_.op == ForgeSphereMode::Move ? Hint::Move
                   : zsDrag_.op == ForgeSphereMode::Scale && zsDrag_.rim ? Hint::Resize
                                                                         : Hint::None;
        } else if (forgesphereMode_ == ForgeSphereMode::Draw && !alt) {
            hint = forgespherePlaceChild_ || ctrlDown(inputMods()) ? Hint::Add
                   : forgesphereNearRim(i, mouseX_, mouseY_)       ? Hint::Resize
                                                                   : Hint::Move;
        }
        if (partProject(z.center, c) && wpp > 0.0f) {
            dl->AddCircle(c, z.radius / wpp, col, 64, (hint == Hint::Resize ? 4.0f : 2.0f) * s);
        }
        forgesphereHint_ = int(hint);
        if (hint == Hint::Move) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (hint == Hint::Resize) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        if (hint != Hint::None && !dragging) {
            const char* tag = hint == Hint::Move     ? tr("click: add  drag: move")
                              : hint == Hint::Resize ? tr("drag: resize")
                                                     : tr("drag: add");
            const ImVec2 at(mouseX_ + 16.0f * s, mouseY_ + 14.0f * s);
            const ImVec2 size = ImGui::CalcTextSize(tag);
            dl->AddRectFilled(ImVec2(at.x - 4.0f * s, at.y - 2.0f * s), ImVec2(at.x + size.x + 4.0f * s, at.y + size.y + 2.0f * s),
                              IM_COL32(20, 22, 26, 200), 3.0f * s);
            dl->AddText(at, IM_COL32(220, 235, 255, 255), tag);
        }
    }
    dl->PopClipRect();
}

} // namespace sf
