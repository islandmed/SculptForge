// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AgentTools.h"

#include "core/Commands.h"
#include "core/Multires.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace sf {

namespace {

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4g", v);
    return buf;
}

std::string vecText(const glm::vec3& v) { return num(v.x) + "," + num(v.y) + "," + num(v.z); }

bool parseVec(const std::string& s, glm::vec3& out) {
    return std::sscanf(s.c_str(), " %f , %f , %f", &out.x, &out.y, &out.z) == 3;
}

// Part by name (exact) or index; "" = the active Part.
int partArg(const Document& doc, const std::string& value) {
    if (value.empty()) return doc.activePart;
    if (const int i = doc.findPart(value); i >= 0) return i;
    char* end = nullptr;
    const long n = std::strtol(value.c_str(), &end, 10);
    return end && *end == '\0' && n >= 0 && n < doc.partCount() ? int(n) : -1;
}

std::string partLine(const Document& doc, int i) {
    const AABB b = doc.partMesh(i).bounds();
    const glm::vec3 size = b.valid() ? b.max - b.min : glm::vec3(0.0f);
    const glm::vec3 centre = b.valid() ? (b.max + b.min) * 0.5f : glm::vec3(0.0f);
    return "size " + vecText(size) + " (w,h,d), centre " + vecText(centre);
}

} // namespace

void registerAgentTools(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    CommandRegistry* registry = &reg; // (the registry outlives its commands)

    reg.add({"blockout",
             "Block out a form in one call: a Forge Sphere armature (root, limbs, head...) skinned into a sculptable "
             "Part. Each sphere is \"x,y,z,radius,parent\" (parent = the index of an earlier sphere in this list; the "
             "first sphere is the root, its parent is ignored), separated by '|'. Y is up, the model faces +Z, "
             "units are scene units (a new sphere has radius 1). Each step is its own undo step.",
             {{"spheres", T::String, "\"x,y,z,r,parent | ...\", e.g. \"0,0,0,0.6,-1 | 0,0.9,0.1,0.4,0 | 0,1.5,0.2,0.3,1\".", true},
              {"symmetry", T::String, "Mirror the limbs across these planes, e.g. \"x\" (default none).", false},
              {"name", T::String, "Part name (default \"Blockout\").", false},
              {"make_mesh", T::Bool, "Skin it into a sculptable Part (default true).", false},
              {"resolution", T::Number, "Skin cells across the longest side (16-1024, default 160).", false}},
             [registry](Document& doc, const Args& a) -> CommandResult {
                 struct S { glm::vec3 c; double r; int parent; };
                 std::vector<S> spheres;
                 std::stringstream in(argString(a, "spheres"));
                 std::string item;
                 while (std::getline(in, item, '|')) {
                     if (item.find_first_not_of(" \t") == std::string::npos) continue;
                     S s{};
                     float r = 0.0f;
                     int parent = -1;
                     const int got = std::sscanf(item.c_str(), " %f , %f , %f , %f , %d", &s.c.x, &s.c.y, &s.c.z, &r, &parent);
                     if (got < 4 || !(r > 0.0f) || !std::isfinite(r))
                         return {false, "sphere " + std::to_string(spheres.size()) + ": expected \"x,y,z,radius,parent\" with radius > 0"};
                     s.r = r;
                     s.parent = spheres.empty() ? -1 : parent;
                     if (!spheres.empty() && (parent < 0 || parent >= int(spheres.size())))
                         return {false, "sphere " + std::to_string(spheres.size()) + ": parent must be an earlier sphere (0 to " +
                                            std::to_string(spheres.size() - 1) + ")"};
                     spheres.push_back(s);
                     if (spheres.size() > 256) return {false, "at most 256 spheres"};
                 }
                 if (spheres.empty()) return {false, "give at least one sphere"};

                 auto run = [&](const std::string& name, const Args& args) { return registry->execute(doc, name, args); };
                 CommandResult r = run("forgesphere_new", {{"name", argString(a, "name", "Blockout")}, {"radius", spheres[0].r}});
                 if (!r.ok) return r;
                 r = run("forgesphere_set", {{"index", 0.0}, {"position", vecText(spheres[0].c)}});
                 if (!r.ok) return r;
                 // forgesphere_add reports the new index; with symmetry the mirror copies take indices too.
                 std::vector<int> index{0};
                 const std::string symmetry = argString(a, "symmetry");
                 for (size_t i = 1; i < spheres.size(); ++i) {
                     const size_t before = doc.mesh.forgespheres.size();
                     Args add{{"parent", double(index[size_t(spheres[i].parent)])},
                              {"position", vecText(spheres[i].c)},
                              {"radius", spheres[i].r}};
                     if (!symmetry.empty()) add["symmetry"] = symmetry;
                     r = run("forgesphere_add", add);
                     if (!r.ok) return {false, "sphere " + std::to_string(i) + ": " + r.message};
                     index.push_back(int(before));
                 }
                 std::string out = "Blocked out " + std::to_string(doc.mesh.forgespheres.size()) + " spheres";
                 if (argBool(a, "make_mesh", true)) {
                     r = run("forgesphere_make_mesh", {{"resolution", argNumber(a, "resolution", 160)}});
                     if (!r.ok) return r;
                     out += "; " + r.message;
                 }
                 return {true, out};
             }});

    reg.add({"measure",
             "Measure proportions: a Part's size (width, height, depth), centre and its share of the whole scene; "
             "with compare=, the two Parts' size ratios and the offset between their centres; with from= and to=, "
             "the distance between two points.",
             {{"part", T::String, "Part name or index (default: the active Part).", false},
              {"compare", T::String, "Another Part to compare it with.", false},
              {"from", T::String, "A point \"x,y,z\" (with to=: their distance).", false},
              {"to", T::String, "A point \"x,y,z\".", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (a.contains("from") || a.contains("to")) {
                     glm::vec3 p, q;
                     if (!parseVec(argString(a, "from"), p) || !parseVec(argString(a, "to"), q))
                         return {false, "from and to must both be \"x,y,z\""};
                     return {true, "Distance " + num(glm::length(q - p)) + " (delta " + vecText(q - p) + ")"};
                 }
                 const int i = partArg(doc, argString(a, "part"));
                 if (i < 0) return {false, "No Part \"" + argString(a, "part") + "\" (see scene_overview)"};
                 const AABB b = doc.partMesh(i).bounds();
                 if (!b.valid()) return {false, doc.parts[size_t(i)].name + " is empty"};
                 const glm::vec3 size = b.max - b.min;
                 std::string out = doc.parts[size_t(i)].name + ": " + partLine(doc, i);
                 const float longest = std::max({size.x, size.y, size.z, 1e-9f});
                 out += "; proportions w:h:d = " + num(size.x / longest) + ":" + num(size.y / longest) + ":" + num(size.z / longest);
                 const AABB scene = doc.visibleBounds();
                 if (scene.valid() && scene.max.y > scene.min.y)
                     out += "; " + num(100.0 * size.y / (scene.max.y - scene.min.y)) + "% of the scene's height";
                 if (a.contains("compare")) {
                     const int j = partArg(doc, argString(a, "compare"));
                     if (j < 0) return {false, "No Part \"" + argString(a, "compare") + "\" (see scene_overview)"};
                     const AABB c = doc.partMesh(j).bounds();
                     if (!c.valid()) return {false, doc.parts[size_t(j)].name + " is empty"};
                     const glm::vec3 other = c.max - c.min;
                     auto ratio = [](float x, float y) { return y > 1e-9f ? num(x / y) : std::string("-"); };
                     out += "\n" + doc.parts[size_t(j)].name + ": " + partLine(doc, j) + "\nratio " +
                            doc.parts[size_t(i)].name + "/" + doc.parts[size_t(j)].name + ": width " + ratio(size.x, other.x) +
                            ", height " + ratio(size.y, other.y) + ", depth " + ratio(size.z, other.z) +
                            "; centre offset " + vecText((c.max + c.min) * 0.5f - (b.max + b.min) * 0.5f);
                 }
                 return {true, out};
             }});

    reg.add({"scene_overview",
             "What's in the scene: every Part (index, name, triangles, subdivision levels, size, centre, active / "
             "hidden), the active brush and the size of the whole scene. Use it before and after big steps.",
             {},
             [](Document& doc, const Args&) -> CommandResult {
                 std::string out = std::to_string(doc.partCount()) + " Part" + (doc.partCount() == 1 ? "" : "s") + ":";
                 for (int i = 0; i < doc.partCount(); ++i) {
                     const Part& p = doc.parts[size_t(i)];
                     const Mesh& m = doc.partMesh(i);
                     out += "\n" + std::to_string(i) + ". " + p.name + ": " + std::to_string(m.triangleCount()) + " triangles";
                     if (subdivLevelCount(m)) out += ", " + subdivLevelText(m);
                     if (!m.forgespheres.empty()) out += ", Forge Spheres (" + std::to_string(m.forgespheres.size()) + ")";
                     if (m.triangleCount()) out += ", " + partLine(doc, i);
                     if (i == doc.activePart) out += ", active";
                     if (!p.visible) out += ", hidden";
                 }
                 const AABB scene = doc.visibleBounds();
                 if (scene.valid()) out += "\nScene (visible): size " + vecText(scene.max - scene.min) + ", centre " +
                                           vecText((scene.max + scene.min) * 0.5f);
                 out += "\nActive brush: " + (doc.activeBrush >= 0 && doc.activeBrush < int(doc.brushes.size())
                                                  ? doc.brushes[size_t(doc.activeBrush)].name
                                                  : std::string("built-in"));
                 out += "\nUndo steps on the active Part: " + std::to_string(doc.undo.undoCount());
                 return {true, out};
             }});
}

} // namespace sf
