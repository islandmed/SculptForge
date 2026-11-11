// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scene's Boolean and its commands (see PartBoolean.h).

#include "core/PartBoolean.h"

#include "core/Commands.h"
#include "core/Parts.h"

#include <algorithm>
#include <cctype>

namespace sf {

namespace {

BooleanOp opFor(PartBoolean mode) {
    return mode == PartBoolean::Subtract ? BooleanOp::Subtract
         : mode == PartBoolean::Intersect ? BooleanOp::Intersect
                                          : BooleanOp::Union;
}

BooleanOperand operandOf(const Document& doc, int i, PartBoolean mode, bool light) {
    BooleanOperand o;
    Mesh scratch;
    const Mesh& placed = partPlacedMesh(doc, i, scratch);
    o.mesh.positions = placed.positions;
    o.mesh.triangles = placed.triangles;
    if (!light) {
        o.mesh.facegroups = placed.facegroups;
        o.mesh.colors = placed.colors;
        o.mesh.uvs = placed.uvs;
    }
    o.mode = mode;
    o.partId = doc.parts[size_t(i)].id;
    o.name = doc.parts[size_t(i)].name;
    return o;
}

std::string countText(size_t n) { return std::to_string(n) + (n == 1 ? " triangle" : " triangles"); }

// The new Part from `operands` (Parts `sources`), as Make Boolean Mesh and boolean_parts make it.
CommandResult addBooleanPart(Document& doc, std::vector<BooleanOperand> operands, const std::vector<int>& sources,
                             const BooleanSceneOptions& options, const char* label) {
    const size_t count = operands.size();
    Mesh out;
    std::string error;
    BooleanInfo info;
    if (!combineOperands(std::move(operands), options, out, error, &info)) return {false, error};
    const int last = *std::max_element(sources.begin(), sources.end());
    const uint32_t returnTo = doc.activePartInfo().id;
    const uint32_t folder = doc.parts[size_t(last)].folder;
    const size_t triangles = out.triangleCount();
    const int at = doc.addPart(std::move(out), doc.uniquePartName("Boolean"), false, last + 1);
    doc.parts[size_t(at)].folder = folder;
    auto step = std::make_shared<PartStep>();
    step->id = doc.parts[size_t(at)].id;
    step->at = at;
    step->returnTo = returnTo;
    for (int i : sources) // (all above `at`: their indices didn't move)
        if (doc.parts[size_t(i)].visible) {
            doc.parts[size_t(i)].visible = false;
            step->hid.push_back(doc.parts[size_t(i)].id);
        }
    doc.selectPart(at);
    UndoRecord r; // in the new Part's history: undo takes it away and shows the sources again
    r.label = label;
    r.part = std::move(step);
    doc.undo.push(std::move(r));
    doc.modified = true;
    std::string msg = std::string(label) + ": " + std::to_string(count) + " Parts -> " + doc.activePartInfo().name + ", " +
                      countText(triangles);
    msg += info.voxel ? " (voxel: " + info.exactError + ")" : " (exact)";
    return {true, msg};
}

bool optionsFrom(const Args& a, BooleanSceneOptions& o, std::string& error) {
    o.weld = argBool(a, "weld", o.weld);
    o.keepGroups = argBool(a, "keep_groups", o.keepGroups);
    o.cutGroup = argBool(a, "cut_group", o.cutGroup);
    o.voxelFallback = argBool(a, "voxel_fallback", o.voxelFallback);
    const double r = argNumber(a, "resolution", o.voxelResolution);
    if (!(r >= 32 && r <= 512)) {
        error = "Boolean: resolution is 32-512";
        return false;
    }
    o.voxelResolution = int(r);
    return true;
}

// A Part named by `key` in the args: its index (digits) or name.
int partArg(const Document& doc, const Args& a, const std::string& key) {
    const std::string v = argString(a, key);
    if (v.empty()) return -1;
    if (std::all_of(v.begin(), v.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        const int i = std::atoi(v.c_str());
        return i < doc.partCount() ? i : -1;
    }
    return doc.findPart(v);
}

} // namespace

std::vector<int> booleanPartIndices(const Document& doc) {
    std::vector<int> out;
    for (int i = 0; i < doc.partCount(); ++i) {
        const Part& p = doc.parts[size_t(i)];
        if (p.visible && p.boolean != PartBoolean::None && doc.partMesh(i).triangleCount() > 0) out.push_back(i);
    }
    return out;
}

std::vector<BooleanOperand> booleanOperands(const Document& doc, bool light) {
    std::vector<BooleanOperand> out;
    for (int i : booleanPartIndices(doc)) out.push_back(operandOf(doc, i, doc.parts[size_t(i)].boolean, light));
    return out;
}

bool combineOperands(std::vector<BooleanOperand> operands, const BooleanSceneOptions& options, Mesh& out,
                     std::string& error, BooleanInfo* info) {
    if (operands.size() < 2) {
        error = "Boolean: give at least two shown Parts a Boolean (Add, Subtract or Intersect)";
        return false;
    }
    // Face groups: each source's own, kept apart (or one per source), and a new one for what a cutter leaves.
    uint32_t next = 0;
    for (size_t k = 0; k < operands.size(); ++k) {
        Mesh& m = operands[k].mesh;
        const bool cutter = k > 0 && (operands[k].mode == PartBoolean::Subtract || operands[k].mode == PartBoolean::Intersect);
        if ((cutter && options.cutGroup) || !options.keepGroups || !m.hasFaceGroups()) {
            m.facegroups.assign(m.triangleCount(), next++);
            continue;
        }
        uint32_t top = 0;
        for (uint32_t& g : m.facegroups) {
            top = std::max(top, g);
            g += next;
        }
        next += top + 1;
    }
    if (info) *info = {};
    BooleanInfo last;
    bool voxel = false;
    Mesh current = std::move(operands[0].mesh);
    for (size_t k = 1; k < operands.size(); ++k) {
        Mesh result;
        std::string why;
        if (!booleanMeshes(current, operands[k].mesh, opFor(operands[k].mode), result, why, options, &last)) {
            error = why + " (at " + operands[k].name + ")";
            return false;
        }
        if (last.voxel) {
            voxel = true;
            if (info) info->exactError = last.exactError + " (at " + operands[k].name + ")";
        }
        current = std::move(result);
    }
    if (info) {
        const std::string why = info->exactError;
        *info = last;
        info->voxel = voxel;
        info->exactError = voxel ? why : std::string();
    }
    out = std::move(current);
    return true;
}

CommandResult makeBooleanMesh(Document& doc, const BooleanSceneOptions& options) {
    const std::vector<int> sources = booleanPartIndices(doc);
    if (sources.size() < 2) return {false, "Boolean: give at least two shown Parts a Boolean (Add, Subtract or Intersect)"};
    return addBooleanPart(doc, booleanOperands(doc), sources, options, "Make Boolean Mesh");
}

void registerBooleanCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    const std::vector<ParamSpec> optionParams = {
        {"weld", T::Bool, "Weld the seams: one closed surface (default true). Off: each source keeps its own copy of the "
                          "vertices along the cut (a hard edge).", false},
        {"keep_groups", T::Bool, "Each source keeps its face groups (default true); off: one face group per source.", false},
        {"cut_group", T::Bool, "The faces a Subtract or Intersect Part leaves on the result get a new face group per "
                               "Part (default true).", false},
        {"voxel_fallback", T::Bool, "When the exact Boolean can't run (a Part isn't closed or runs through itself), "
                                    "combine voxels instead (a remeshed result; default true).", false},
        {"resolution", T::Number, "Voxel fallback: cells across the result (32-512, default 256).", false}};

    reg.add({"part_boolean",
             "Set how a Part takes part in the scene's Boolean: none, add, subtract or intersect. The shown Parts with "
             "a Boolean combine in list order (the first is the base); the view shows the result, and "
             "make_boolean_mesh turns it into a new Part.",
             {{"mode", T::String, "none, add, subtract or intersect.", true},
              {"index", T::Number, "Part index (0 = top of the list). Default: the active one.", false},
              {"name", T::String, "Part name (instead of index).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 int i = doc.activePart;
                 if (a.contains("name")) i = doc.findPart(argString(a, "name"));
                 else if (a.contains("index")) i = int(argNumber(a, "index", -1));
                 if (i < 0 || i >= doc.partCount()) return {false, "No such Part"};
                 PartBoolean mode = PartBoolean::None;
                 if (!parsePartBoolean(argString(a, "mode"), mode)) return {false, "Boolean: mode is none, add, subtract or intersect"};
                 Part& p = doc.parts[size_t(i)];
                 if (p.boolean != mode) {
                     p.boolean = mode;
                     doc.modified = true;
                 }
                 const size_t n = booleanPartIndices(doc).size();
                 return {true, p.name + ": Boolean " + partBooleanName(mode) + " (" + std::to_string(n) + " shown Part" +
                                   (n == 1 ? "" : "s") + " take part)"};
             }});

    std::vector<ParamSpec> pairParams = {
        {"op", T::String, "union, subtract or intersect (subtract: a minus b).", true},
        {"a", T::String, "The first Part: its name or list index.", true},
        {"b", T::String, "The second Part: its name or list index.", true}};
    pairParams.insert(pairParams.end(), optionParams.begin(), optionParams.end());
    reg.add({"boolean_parts",
             "Boolean of two Parts into a new Part (made active; the two are left as they are, hidden). Exact: the "
             "result is watertight when the Parts are, and keeps their faces away from the cut. Undoable.",
             pairParams,
             [](Document& doc, const Args& a) -> CommandResult {
                 BooleanOp op = BooleanOp::Union;
                 if (!parseBooleanOp(argString(a, "op"), op)) return {false, "Boolean: op is union, subtract or intersect"};
                 const int ia = partArg(doc, a, "a"), ib = partArg(doc, a, "b");
                 if (ia < 0 || ib < 0) return {false, "Boolean: no Part '" + argString(a, ia < 0 ? "a" : "b") + "'"};
                 if (ia == ib) return {false, "Boolean: a and b are the same Part"};
                 BooleanSceneOptions o;
                 std::string error;
                 if (!optionsFrom(a, o, error)) return {false, error};
                 const PartBoolean mode = op == BooleanOp::Subtract ? PartBoolean::Subtract
                                        : op == BooleanOp::Intersect ? PartBoolean::Intersect
                                                                     : PartBoolean::Add;
                 std::vector<BooleanOperand> ops;
                 ops.push_back(operandOf(doc, ia, PartBoolean::Add, false));
                 ops.push_back(operandOf(doc, ib, mode, false));
                 const char* label = op == BooleanOp::Union ? "Boolean Union" : op == BooleanOp::Subtract ? "Boolean Subtract"
                                                                                                         : "Boolean Intersect";
                 return addBooleanPart(doc, std::move(ops), {ia, ib}, o, label);
             }});

    reg.add({"make_boolean_mesh",
             "Make Boolean Mesh: the scene's Boolean (the shown Parts with a Boolean, combined in list order) as a new "
             "Part, made active; the sources are left as they are, hidden. Undoable.",
             optionParams,
             [](Document& doc, const Args& a) -> CommandResult {
                 BooleanSceneOptions o;
                 std::string error;
                 if (!optionsFrom(a, o, error)) return {false, error};
                 return makeBooleanMesh(doc, o);
             }});
}

} // namespace sf
