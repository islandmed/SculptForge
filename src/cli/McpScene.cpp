// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/McpScene.h"

#include "core/Document.h"
#include "core/ManagedPolicy.h"
#include "core/Micro.h"
#include "core/Multires.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr const char* kJson = "application/json";
constexpr int kDefaultHistory = 20, kMaxHistory = 200; // undo steps sculptforge://history lists
constexpr int kMaxListedParts = 256;                   // Parts sculptforge://part/{index} lists one by one

Json prop(const char* type, const char* description) {
    Json p = Json::object();
    p.set("type", type);
    p.set("description", description);
    return p;
}

Json vec3Json(const glm::vec3& v) {
    Json a = Json::array();
    for (int i = 0; i < 3; ++i) a.push(double(v[i]));
    return a;
}

Json boundsJson(const AABB& b) {
    Json o = Json::object();
    o.set("min", vec3Json(b.min));
    o.set("max", vec3Json(b.max));
    return o;
}

// The bounds of every visible Part, where the scene shows them (a moved linked copy included).
AABB sceneBounds(const Document& doc) {
    AABB all;
    for (int i = 0; i < doc.partCount(); ++i) {
        if (!doc.parts[size_t(i)].visible) continue;
        const Part& part = doc.parts[size_t(i)];
        const AABB b = doc.partMesh(i).bounds();
        if (!b.valid()) continue;
        if (!part.moved()) {
            all.expand(b);
            continue;
        }
        for (int corner = 0; corner < 8; ++corner)
            all.expand(glm::vec3(part.transform * glm::vec4(corner & 1 ? b.max.x : b.min.x, corner & 2 ? b.max.y : b.min.y,
                                                              corner & 4 ? b.max.z : b.min.z, 1.0f)));
    }
    return all;
}

Json sceneTotals(const Document& doc) {
    double triangles = 0, vertices = 0;
    int visible = 0;
    for (int i = 0; i < doc.partCount(); ++i) {
        if (!doc.parts[size_t(i)].visible) continue;
        ++visible;
        triangles += double(doc.partMesh(i).triangleCount());
        vertices += double(doc.partMesh(i).vertexCount());
    }
    Json o = Json::object();
    o.set("triangles", triangles);
    o.set("vertices", vertices);
    o.set("visibleParts", visible);
    if (const AABB b = sceneBounds(doc); b.valid()) o.set("bounds", boundsJson(b));
    return o;
}

Json activePartJson(const Document& doc) {
    Json o = Json::object();
    o.set("index", doc.activePart);
    o.set("name", doc.activePartInfo().name);
    return o;
}

Json partSummary(const Document& doc, int i) {
    const Part& part = doc.parts[size_t(i)];
    const Mesh& mesh = doc.partMesh(i);
    Json o = Json::object();
    o.set("index", i);
    o.set("name", part.name);
    o.set("visible", part.visible);
    o.set("active", i == doc.activePart);
    o.set("triangles", double(mesh.triangleCount()));
    o.set("vertices", double(mesh.vertexCount()));
    o.set("subdivisionLevels", subdivLevelCount(mesh));
    o.set("linkedCopies", doc.linkCount(i));
    if (part.moved()) o.set("moved", true);
    return o;
}

Json partDetail(const Document& doc, int i) {
    const Part& part = doc.parts[size_t(i)];
    const Mesh& mesh = doc.partMesh(i);
    Json o = partSummary(doc, i);
    o.set("id", double(part.id));
    o.set("shading", shadingName(part.shading));
    o.set("boolean", partBooleanName(part.boolean));
    if (part.booleanBlend > 0.0f) o.set("boolean_blend", double(part.booleanBlend));
    if (const int f = doc.partFolderIndex(part.folder); f >= 0) o.set("folder", doc.partFolders[size_t(f)].name);
    if (const AABB b = mesh.bounds(); b.valid()) o.set("bounds", boundsJson(b));

    // Subdivision levels (the Part's own stack, as subdiv_info lists it).
    Json levels = Json::object();
    const int n = subdivLevelCount(mesh);
    levels.set("count", n);
    levels.set("current", n ? subdivCurrentLevel(mesh) + 1 : 0); // 1-based like the app
    Json list = Json::array();
    for (int l = 0; l < n; ++l) list.push(double(subdivLevelTriangles(mesh, l)));
    levels.set("triangles", list);
    o.set("subdivision", levels);

    // The micro level above the highest subdivision level (micro_info).
    if (mesh.micro) {
        const MicroMesh& mm = *mesh.micro;
        Json micro = Json::object();
        micro.set("level", mm.level);
        micro.set("gridPerBaseTriangle", double(mm.n));
        micro.set("baseTriangles", double(mm.baseTriangles));
        micro.set("microTriangles", double(mm.microTriangles()));
        micro.set("bytes", double(mm.bytes()));
        micro.set("displacementStep", double(mm.step));
        micro.set("displacementRange", double(mm.rangeWorld()));
        micro.set("masked", mm.hasMask());
        o.set("micro", micro);
    } else {
        o.set("micro", Json());
    }

    // Material: vertex colour, paint layers, the painted texture and the channels it carries.
    Json material = Json::object();
    material.set("vertexColors", mesh.hasColors());
    material.set("paintLayers", double(mesh.paintLayers.size()));
    if (mesh.texture) {
        const PaintTexture& tex = *mesh.texture;
        Json t = Json::object();
        t.set("width", tex.width);
        t.set("height", tex.height);
        t.set("layers", double(tex.layers.size()));
        Json channels = Json::array();
        channels.push(channelFormat(Channel::BaseColor).id);
        for (int c = 1; c < kChannelCount; ++c)
            if (tex.channels[size_t(c)].enabled) channels.push(channelFormat(c).id);
        t.set("channels", channels);
        material.set("texture", t);
    } else {
        material.set("texture", Json());
    }
    o.set("material", material);

    // Attributes the Part carries.
    Json attributes = Json::object();
    attributes.set("uvs", mesh.hasUvs());
    attributes.set("mask", mesh.hasMask());
    attributes.set("faceGroups", mesh.hasFaceGroups());
    Json sculptLayers = Json::array();
    for (const SculptLayer& layer : mesh.layers) {
        Json l = Json::object();
        l.set("name", layer.name);
        l.set("weight", double(layer.weight));
        sculptLayers.push(l);
    }
    attributes.set("sculptLayers", sculptLayers);
    o.set("attributes", attributes);
    return o;
}

Json historyJson(const Document& doc, int limit) {
    const UndoStack& undo = doc.undo; // the active Part's history (the one undo takes back)
    Json o = Json::object();
    o.set("part", activePartJson(doc));
    o.set("undoDepth", double(undo.undoCount()));
    o.set("canRedo", undo.canRedo());
    if (undo.canRedo()) o.set("nextRedo", undo.redoLabel());
    Json steps = Json::array();
    for (size_t back = 0; back < size_t(limit); ++back) { // newest first: step 0 is what undo takes back next
        const UndoRecord* r = undo.undoStep(back);
        if (!r) break;
        Json s = Json::object();
        s.set("back", double(back));
        s.set("label", r->label);
        steps.push(s);
    }
    o.set("steps", steps);
    return o;
}

Json resource(const std::string& uri, const char* name, const char* title, const std::string& description) {
    Json r = Json::object();
    r.set("uri", uri);
    r.set("name", name);
    r.set("title", title);
    r.set("description", description);
    r.set("mimeType", kJson);
    return r;
}

// "sculptforge://history?limit=50" -> the path part and the limit.
bool splitUri(const std::string& uri, std::string& path, int& limit) {
    static const std::string kScheme = "sculptforge://";
    if (uri.rfind(kScheme, 0) != 0) return false;
    path = uri.substr(kScheme.size());
    limit = kDefaultHistory;
    if (const size_t q = path.find('?'); q != std::string::npos) {
        const std::string query = path.substr(q + 1);
        path.resize(q);
        if (query.rfind("limit=", 0) != 0 || query.size() == 6 || query.size() > 9 ||
            query.find_first_not_of("0123456789", 6) != std::string::npos)
            return false;
        limit = std::clamp(std::stoi(query.substr(6)), 1, kMaxHistory);
    }
    return true;
}

} // namespace

bool mcpMayReadScene(std::string* why) { return policy::mcpToolAllowed("scene_info", {}, why); } // (category "info")

Json mcpOutputSchema(bool withScene) {
    Json props = Json::object();
    props.set("ok", prop("boolean", "Whether the call worked (false = the text says why)."));
    props.set("message", prop("string", "The same line the text content holds."));
    props.set("ms", prop("integer", "How long the call took, in milliseconds."));
    Json required = Json::array();
    required.push("ok");
    required.push("message");
    required.push("ms");
    if (withScene) {
        props.set("triangles", prop("integer", "Triangles of the active Part (what mesh_info reports)."));
        props.set("vertices", prop("integer", "Vertices of the active Part."));
        Json bounds = Json::object();
        bounds.set("type", "object");
        bounds.set("description", "Bounding box of the active Part in scene units; absent for an empty mesh.");
        Json corner = Json::object();
        corner.set("type", "array");
        corner.set("items", prop("number", "x, y or z"));
        corner.set("minItems", 3);
        corner.set("maxItems", 3);
        Json boundProps = Json::object();
        boundProps.set("min", corner);
        boundProps.set("max", corner);
        bounds.set("properties", boundProps);
        Json boundRequired = Json::array();
        boundRequired.push("min");
        boundRequired.push("max");
        bounds.set("required", boundRequired);
        props.set("bounds", bounds);
        props.set("parts", prop("integer", "How many Parts the scene holds."));
        Json active = Json::object();
        active.set("type", "object");
        active.set("description", "The active Part: edits apply to it.");
        Json activeProps = Json::object();
        activeProps.set("index", prop("integer", "Its place in part_list (0 = top)."));
        activeProps.set("name", prop("string", "Its name."));
        active.set("properties", activeProps);
        props.set("activePart", active);
        props.set("undoDepth", prop("integer", "Undo steps the active Part's history holds."));
        props.set("canRedo", prop("boolean", "Whether redo has a step to bring back."));
        Json scene = Json::object();
        scene.set("type", "object");
        scene.set("description", "Totals over every visible Part, with their combined bounds.");
        props.set("scene", scene);
        for (const char* k : {"triangles", "vertices", "parts", "activePart", "undoDepth", "canRedo", "scene"}) required.push(k);
    }
    Json schema = Json::object();
    schema.set("type", "object");
    schema.set("properties", props);
    schema.set("required", required);
    return schema;
}

Json mcpStructuredResult(const Document& doc, bool ok, const std::string& message, int64_t ms) {
    Json o = Json::object();
    o.set("ok", ok);
    o.set("message", message);
    o.set("ms", double(ms));
    if (!mcpMayReadScene()) return o; // (a studio that hides "info" tools hides the numbers too)
    o.set("triangles", double(doc.mesh.triangleCount()));
    o.set("vertices", double(doc.mesh.vertexCount()));
    if (const AABB b = doc.mesh.bounds(); b.valid()) o.set("bounds", boundsJson(b));
    o.set("parts", doc.partCount());
    o.set("activePart", activePartJson(doc));
    o.set("undoDepth", double(doc.undo.undoCount()));
    o.set("canRedo", doc.undo.canRedo());
    o.set("scene", sceneTotals(doc));
    return o;
}

Json mcpResourceList(const Document& doc) {
    Json list = Json::array();
    if (mcpMayReadScene()) {
        list.push(resource("sculptforge://scene", "scene", "Scene summary",
                           "Triangles, vertices, bounds, Parts and undo depth of the open scene."));
        list.push(resource("sculptforge://parts", "parts", "Parts", "Every Part: name, size, visibility, levels."));
        list.push(resource("sculptforge://history", "history", "Undo history",
                           "The last undo steps of the active Part, newest first (add ?limit=N, up to 200)."));
        for (int i = 0; i < std::min(doc.partCount(), kMaxListedParts); ++i)
            list.push(resource("sculptforge://part/" + std::to_string(i), "part", doc.parts[size_t(i)].name.c_str(),
                               "Statistics, subdivision levels, micro level and material of this Part."));
    }
    Json result = Json::object();
    result.set("resources", list);
    return result;
}

Json mcpResourceTemplates() {
    Json templates = Json::array();
    Json t = Json::object();
    t.set("uriTemplate", "sculptforge://part/{index}");
    t.set("name", "part");
    t.set("title", "One Part");
    t.set("description", "Statistics, subdivision levels, micro level and material of the Part at this index (part_list).");
    t.set("mimeType", kJson);
    templates.push(t);
    Json result = Json::object();
    result.set("resourceTemplates", templates);
    return result;
}

bool mcpResourceRead(const Document& doc, const std::string& uri, Json& result, std::string& error) {
    if (!mcpMayReadScene(&error)) return false;
    std::string path;
    int limit = kDefaultHistory;
    Json body;
    if (!splitUri(uri, path, limit)) {
        error = "Unknown resource: " + uri;
        return false;
    }
    const bool plain = uri.find('?') == std::string::npos;
    if (path == "scene" && plain) {
        body = mcpStructuredResult(doc, true, "", 0);
        body = [&] { // the call's own fields (ok, message, ms) don't belong to a resource
            Json o = Json::object();
            for (const auto& [k, v] : body.members())
                if (k != "ok" && k != "message" && k != "ms") o.set(k, v);
            o.set("name", doc.assetName());
            o.set("path", doc.path);
            o.set("modified", doc.dirty());
            o.set("splats", double(doc.splats.size()));
            return o;
        }();
    } else if (path == "parts" && plain) {
        Json parts = Json::array();
        for (int i = 0; i < doc.partCount(); ++i) parts.push(partSummary(doc, i));
        body = Json::object();
        body.set("activePart", doc.activePart);
        body.set("parts", parts);
    } else if (path == "history") {
        body = historyJson(doc, limit);
    } else if (path.rfind("part/", 0) == 0 && plain) {
        const std::string index = path.substr(5);
        if (index.empty() || index.size() > 6 || index.find_first_not_of("0123456789") != std::string::npos ||
            std::stoi(index) >= doc.partCount()) {
            error = "No Part at \"" + index + "\" (part_list or sculptforge://parts show the indices).";
            return false;
        }
        body = partDetail(doc, std::stoi(index));
    } else {
        error = "Unknown resource: " + uri;
        return false;
    }
    Json content = Json::object();
    content.set("uri", uri);
    content.set("mimeType", kJson);
    content.set("text", body.dump());
    Json contents = Json::array();
    contents.push(content);
    result = Json::object();
    result.set("contents", contents);
    return true;
}

} // namespace sf
