// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Snap menu (Devon on Discord): one key (Shift+S with the Blender-style keys; assignable in every style) opens a
// plain list at the cursor to put the active Part's origin, the Part itself or the gizmo's pivot somewhere exact.
// Each item runs a command (part_origin, part_to_world, gizmo_pivot), so scripts and MCP agents do the same.
#include "app/App.h"
#include "app/I18n.h"
#include "core/PartOrigin.h"
#include "core/PathStroke.h" // parseVec3

#include <IconsLucide.h>
#include <imgui.h>

#include <cstdio>
#include <string>

namespace sf {

namespace {

std::string pointText(const glm::vec3& p) {
    char at[96];
    std::snprintf(at, sizeof at, "%.9g,%.9g,%.9g", double(p.x), double(p.y), double(p.z));
    return at;
}

} // namespace

void App::registerSnapMenu() {
    Action a;
    a.id = "transform.snap_menu";
    a.label = "Snap menu";
    a.icon = ICON_LC_MAGNET;
    a.palette = "Transform";
    a.description = "A list at the cursor: put the active Part's origin at its bounds centre, the world centre or the "
                    "gizmo's pivot; move the Part to the world centre or onto the X symmetry plane; put the gizmo's "
                    "pivot at what moves, the Part's origin or the world centre.";
    a.run = [this] { snapMenuRequest_ = true; };
    actions_.add(std::move(a)); // (Shift+S with the Blender-style keys: applyInputStyle in Input.cpp)

    commands_.add({"gizmo_pivot",
                   "Put the Transform gizmo's pivot (the point it turns and scales about) at what will move (the "
                   "active Part, or its unmasked part), the active Part's origin, the world centre or a point. Needs "
                   "the app (not the CLI).",
                   {{"to", ParamSpec::Type::String, "selection, origin, world or point.", true},
                    {"point", ParamSpec::Type::String, "x,y,z for to=point.", false}},
                   [this](Document& doc, const Args& a) -> CommandResult {
                       const std::string to = argString(a, "to");
                       glm::vec3 at(0.0f);
                       if (to == "selection") {
                           AABB box;
                           for (int i : gizmoTargets()) {
                               const Mesh& m = doc.partMesh(i);
                               if (!m.vertexCount()) continue;
                               const AABB local = transformBounds(m, transformWeights(m, gizmo_.useMask, 0.0f));
                               const glm::mat4& t = doc.parts[size_t(i)].transform; // (a linked copy's place, #94)
                               for (int c = 0; c < 8 && local.valid(); ++c)
                                   box.expand(glm::vec3(t * glm::vec4(c & 1 ? local.max.x : local.min.x,
                                                                      c & 2 ? local.max.y : local.min.y,
                                                                      c & 4 ? local.max.z : local.min.z, 1.0f)));
                           }
                           if (!box.valid()) return {false, "Nothing to move"};
                           at = box.center();
                       } else if (to == "origin") {
                           const Part& p = doc.activePartInfo();
                           if (!p.hasOrigin) return {false, p.name + " has no origin (Tool > Parts > Origin > Set...)"};
                           at = partOriginInScene(p);
                       } else if (to == "world") {
                           at = glm::vec3(0.0f);
                       } else if (to == "point") {
                           if (!parseVec3(argString(a, "point"), at)) return {false, "Give point=x,y,z"};
                       } else {
                           return {false, "Unknown to='" + to + "' (selection, origin, world, point)"};
                       }
                       gizmo_.pivot = at;
                       gizmo_.pivotPlaced = true; // stays there until it's moved, reset or a new mask
                       return {true, "Gizmo pivot at " + pointText(at)};
                   }});
}

void App::drawSnapMenu() {
    if (snapMenuRequest_) {
        snapMenuRequest_ = false;
        ImGui::SetNextWindowPos(ImVec2(mouseX_, mouseY_), ImGuiCond_Always);
        ImGui::OpenPopup("##snap menu");
    }
    if (!ImGui::BeginPopup("##snap menu")) return;
    const Part& p = doc_.activePartInfo();
    const std::string gizmoAt = pointText(gizmo_.pivot);
    ImGui::TextDisabled(tr("Snap: %s"), p.name.c_str());
    ImGui::SeparatorText(tr("Origin"));
    if (ImGui::MenuItem(tr("Origin to bounds centre"))) runCommand("part_origin", {{"set", std::string("bounds")}});
    if (ImGui::MenuItem(tr("Origin to world centre"))) runCommand("part_origin", {{"set", std::string("world")}});
    if (ImGui::MenuItem(tr("Origin to gizmo pivot"), nullptr, false, gizmoActive()))
        runCommand("part_origin", {{"set", std::string("point")}, {"point", gizmoAt}});
    ImGui::SeparatorText(tr("Part"));
    if (ImGui::MenuItem(tr("Part to world centre"))) runCommand("part_to_world");
    if (ImGui::MenuItem(tr("Part onto the X symmetry plane"))) runCommand("part_to_world", {{"axis", std::string("x")}});
    ImGui::SeparatorText(tr("Gizmo pivot"));
    ImGui::BeginDisabled(!gizmoActive());
    if (ImGui::MenuItem(tr("Pivot to what moves"))) runCommand("gizmo_pivot", {{"to", std::string("selection")}});
    if (ImGui::MenuItem(tr("Pivot to origin"), nullptr, false, p.hasOrigin)) runCommand("gizmo_pivot", {{"to", std::string("origin")}});
    if (ImGui::MenuItem(tr("Pivot to world centre"))) runCommand("gizmo_pivot", {{"to", std::string("world")}});
    ImGui::EndDisabled();
    if (!gizmoActive()) ImGui::TextDisabled("%s", tr("Turn the gizmo on to place its pivot"));
    ImGui::EndPopup();
}

} // namespace sf
