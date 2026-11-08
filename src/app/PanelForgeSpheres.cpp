// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Forge Spheres and the Forge Sphere tool in the viewport (Forge Spheres, core/Forge Spheres.h). While a Forge Sphere
// Part is active a left-drag on it edits the armature instead of sculpting:
//   Draw  - drag on a sphere to grow a new one from it (the drag sets its size); click a link to add a joint there
//           and drag to place it
//   Move  - drag a sphere to move it across the screen
//   Scale - drag a sphere up/right to grow it, down/left to shrink it
//   Alt+click a sphere deletes it with everything growing from it. Symmetry mirrors every edit.
// Pointer only (#8): Drag places child makes a Draw drag move the new sphere away from its parent (Ctrl+drag does
// it once), the last sphere clicked or added is selected (Delete Selected, Insert Between, Set as Root), and a
// right click on a sphere opens a menu with the same three.
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
    add("forgesphere.new", "New Forge Sphere", [this] { runCommand("forgesphere_new"); }, nullptr, nullptr,
        "Add a Forge Sphere Part: one root sphere to block out a body, creature or plant from. Drag on a sphere to grow "
        "limbs from it, Move and Scale them into proportion, check the skin with Preview (A), then Make Mesh.",
        "forgesphere_new");
    const auto mode = [this](ForgeSphereMode m) {
        return [this, m] {
            forgesphereMode_ = m;
            status_ = std::string("Forge Spheres: ") + modeName(int(m)) +
                      (m == ForgeSphereMode::Draw    ? " - drag on a sphere to grow a new one; click a link to add a joint"
                       : m == ForgeSphereMode::Move ? " - drag a sphere to move it"
                                                : " - drag a sphere up or right to grow it, down or left to shrink it");
        };
    };
    add("forgesphere.draw", "Draw", mode(ForgeSphereMode::Draw), active, [this] { return forgesphereMode_ == ForgeSphereMode::Draw; },
        "Drag on a sphere to grow a new sphere from it (how far you drag sets its size). Click a link between two "
        "spheres to add a joint there, and drag to place it. Alt+click a sphere to delete it.");
    add("forgesphere.move", "Move", mode(ForgeSphereMode::Move), active, [this] { return forgesphereMode_ == ForgeSphereMode::Move; },
        "Drag a sphere to move it; the links stretch to follow. Symmetry moves its mirror twin too.");
    add("forgesphere.scale", "Scale", mode(ForgeSphereMode::Scale), active, [this] { return forgesphereMode_ == ForgeSphereMode::Scale; },
        "Drag a sphere up or right to make it bigger, down or left to make it smaller.");
    add("forgesphere.place_child", "Drag places child", [this] { forgespherePlaceChild_ = !forgespherePlaceChild_; }, active,
        [this] { return forgespherePlaceChild_; },
        "Draw: dragging on a sphere moves the new sphere away from it (one drag places a limb joint) instead of "
        "sizing it. Off, hold Ctrl while you drag for the same.");
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
        d.place = forgespherePlaceChild_ || ctrlDown(mod); // the drag moves the new sphere out instead of sizing it
    } else {
        d.label = d.op == ForgeSphereMode::Move ? "Forge Sphere move" : "Forge Sphere scale";
    }
    if (d.op == ForgeSphereMode::Move) {
        glm::vec3 o, dir;
        partRay(x, y, o, dir);
        const glm::vec3 c = d.start[size_t(d.sphere)].center;
        d.grab = c - planeHit(o, dir, c, glm::normalize(dir));
    }
    if (d.op == ForgeSphereMode::Draw) forgesphereDragTo(x, y); // the new sphere appears at once, at half its parent's size
    drag_ = Drag::ForgeSphere;
    return true;
}

void App::forgesphereDragTo(float x, float y) {
    ForgeSphereDrag& d = zsDrag_;
    if (d.sphere < 0) return;
    std::vector<ForgeSphere> s = d.start;
    const ForgeSphere& z = s[size_t(d.sphere)];
    const float moved = std::hypot(x - d.x, y - d.y);
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
        const float amount = ((x - d.x) - (y - d.y)) / (120.0f * uiScale_);
        forgesphereSet(s, d.sphere, z.center, std::max(z.radius * std::exp(amount), 1e-4f), symmetry_);
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
    if (d.changed) {
        Mesh after = std::move(doc_.mesh);
        doc_.mesh = std::move(d.before);
        doc_.replaceMeshUndoable(std::move(after), d.label);
        status_ = std::string("Forge Spheres: ") + d.label + "; " + std::to_string(doc_.mesh.forgespheres.size()) + " spheres";
        if (d.added >= 0) forgesphereSelected_ = d.added; // the new sphere
        else if (d.op == ForgeSphereMode::Move) forgesphereSelected_ = d.sphere; // (an inserted joint, too)
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
        if (partProject(z.center, c) && wpp > 0.0f) dl->AddCircle(c, z.radius / wpp, col, 64, 2.0f * s);
    }
    dl->PopClipRect();
}

} // namespace sf
