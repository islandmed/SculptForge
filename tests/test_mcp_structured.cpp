// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What an MCP agent reads without parsing text (cli/McpScene.h): every tool's outputSchema (valid JSON Schema), every
// result's structuredContent (the same numbers mesh_info prints, one undo step per call), and the scene resources
// (resources/list, resources/read, resources/templates/list): read-only, no undo step. The studio policy and the
// audit log are in test_studio_policy.cpp.
#include "TestHarness.h"

#include "cli/McpServer.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>

using namespace sf;

// CHECK that also says which tool, schema path or URI it was about.
#define CHECK_MSG(cond, what)                                           \
    do {                                                                \
        if (!(cond)) std::printf("  (%s)\n", std::string(what).c_str()); \
        CHECK(cond);                                                    \
    } while (0)

namespace {

struct Session {
    CommandRegistry reg;
    Document doc;
    int nextId = 1;

    Json rpc(const std::string& method, const std::string& params = "{}") {
        Json j;
        std::string err;
        const std::string out = handleMcpMessage(
            reg, doc, R"({"jsonrpc":"2.0","id":)" + std::to_string(nextId++) + R"(,"method":")" + method + R"(","params":)" + params + "}");
        CHECK(Json::parse(out, j, err));
        return j;
    }
    Json call(const std::string& tool, const std::string& arguments = "{}") {
        return rpc("tools/call", R"({"name":")" + tool + R"(","arguments":)" + arguments + "}");
    }
    Json readResource(const std::string& uri) {
        Json r = rpc("resources/read", R"({"uri":")" + uri + R"("})");
        if (!r["result"]["contents"][0]["text"].isString()) return r; // an error reply
        Json body;
        std::string err;
        CHECK(Json::parse(r["result"]["contents"][0]["text"].asString(), body, err));
        return body;
    }
};

// A JSON Schema of the subset the server writes: types, properties, required, items, min/maxItems. Returns the
// problems found (empty = valid).
std::string schemaProblems(const Json& s, const std::string& where) {
    static const std::set<std::string> kTypes = {"object", "array", "string", "number", "integer", "boolean", "null"};
    if (!s.isObject() || !s["type"].isString() || !kTypes.count(s["type"].asString())) return where + ": no valid type";
    const std::string type = s["type"].asString();
    if (s.has("description") && !s["description"].isString()) return where + ": description is not a string";
    if (type == "object") {
        if (s.has("properties") && !s["properties"].isObject()) return where + ": properties is not an object";
        for (const auto& [name, sub] : s["properties"].members())
            if (std::string p = schemaProblems(sub, where + "." + name); !p.empty()) return p;
        if (s.has("required")) {
            if (!s["required"].isArray()) return where + ": required is not an array";
            std::set<std::string> seen;
            for (const Json& r : s["required"].items()) {
                if (!r.isString() || !s["properties"].has(r.asString())) return where + ": required names no property";
                if (!seen.insert(r.asString()).second) return where + ": required repeats a name";
            }
        }
    }
    if (type == "array" && s.has("items"))
        if (std::string p = schemaProblems(s["items"], where + "[]"); !p.empty()) return p;
    for (const char* k : {"minItems", "maxItems"})
        if (s.has(k) && !s[k].isNumber()) return where + ": " + k + " is not a number";
    return {};
}

// `value` against `schema` (the same subset): returns the first mismatch, or "".
std::string conforms(const Json& value, const Json& schema, const std::string& where) {
    const std::string type = schema["type"].asString();
    const bool right = type == "object"    ? value.isObject()
                       : type == "array"   ? value.isArray()
                       : type == "string"  ? value.isString()
                       : type == "boolean" ? value.isBool()
                       : type == "integer" ? value.isNumber() && value.asNumber() == std::floor(value.asNumber())
                                           : value.isNumber();
    if (!right) return where + " is not " + type;
    if (type == "object") {
        for (const Json& r : schema["required"].items())
            if (!value.has(r.asString())) return where + " lacks " + r.asString();
        for (const auto& [name, sub] : schema["properties"].members())
            if (value.has(name))
                if (std::string p = conforms(value[name], sub, where + "." + name); !p.empty()) return p;
    }
    if (type == "array") {
        if (schema.has("minItems") && value.size() < size_t(schema["minItems"].asNumber())) return where + " is too short";
        if (schema.has("maxItems") && value.size() > size_t(schema["maxItems"].asNumber())) return where + " is too long";
        if (schema.has("items"))
            for (const Json& v : value.items())
                if (std::string p = conforms(v, schema["items"], where + "[]"); !p.empty()) return p;
    }
    return {};
}

} // namespace

TEST(mcp_tools_list_has_valid_schemas) {
    Session s;
    const Json list = s.rpc("tools/list");
    CHECK(list["result"]["tools"].size() > 50);
    std::set<std::string> names;
    for (const Json& t : list["result"]["tools"].items()) {
        const std::string name = t["name"].asString();
        names.insert(name);
        CHECK_MSG(schemaProblems(t["inputSchema"], name + ".inputSchema").empty(), schemaProblems(t["inputSchema"], name + ".inputSchema"));
        CHECK_MSG(schemaProblems(t["outputSchema"], name + ".outputSchema").empty(), schemaProblems(t["outputSchema"], name + ".outputSchema"));
        // The scene's numbers are promised on every tool.
        for (const char* k : {"triangles", "vertices", "parts", "activePart", "undoDepth", "ms"})
            CHECK_MSG(t["outputSchema"]["properties"].has(k) && t["outputSchema"]["required"].size() >= 6, name + " lacks " + k);
    }
    CHECK(names.count("mesh_info") && names.count("viewport_image") && names.count("view_closeup"));
}

TEST(mcp_structured_content_matches_mesh_info) {
    Session s;
    const Json list = s.rpc("tools/list");
    const Json& schema = list["result"]["tools"][0]["outputSchema"];

    const Json made = s.call("new_sphere", R"({"subdivisions":3})");
    const Json& sc = made["result"]["structuredContent"];
    CHECK(!made["result"]["isError"].asBool() && sc["ok"].asBool());
    CHECK(sc["message"].asString() == made["result"]["content"][0]["text"].asString()); // (the text stays for older clients)
    CHECK(conforms(sc, schema, "structuredContent").empty());

    const Json info = s.call("mesh_info");
    const Json& si = info["result"]["structuredContent"];
    CHECK(conforms(si, schema, "structuredContent").empty());
    CHECK(si["triangles"].asNumber() == double(s.doc.mesh.triangleCount()) && si["triangles"].asNumber() > 100);
    CHECK(si["vertices"].asNumber() == double(s.doc.mesh.vertexCount()));
    // The text says the same: "<V> vertices, <T> triangles, bounds (x, y, z) - (x, y, z)".
    const std::string text = info["result"]["content"][0]["text"].asString();
    unsigned long long v = 0, t = 0;
    double b[6] = {};
    CHECK(std::sscanf(text.c_str(), "%llu vertices, %llu triangles, bounds (%lf, %lf, %lf) - (%lf, %lf, %lf)", &v, &t, &b[0],
                      &b[1], &b[2], &b[3], &b[4], &b[5]) == 8);
    CHECK(double(v) == si["vertices"].asNumber() && double(t) == si["triangles"].asNumber());
    for (int i = 0; i < 3; ++i) {
        CHECK(std::abs(si["bounds"]["min"][size_t(i)].asNumber() - b[i]) < 1e-4);
        CHECK(std::abs(si["bounds"]["max"][size_t(i)].asNumber() - b[3 + i]) < 1e-4);
    }
    CHECK(si["parts"].asNumber() == 1 && si["activePart"]["index"].asNumber() == 0 && !si["activePart"]["name"].asString().empty());
    CHECK(si["scene"]["triangles"].asNumber() == si["triangles"].asNumber() && si["scene"]["visibleParts"].asNumber() == 1);
    CHECK(si["ms"].asNumber() >= 0);
}

TEST(mcp_structured_undo_depth_and_errors) {
    Session s;
    s.call("new_sphere", R"({"subdivisions":2})");
    const double base = s.call("mesh_info")["result"]["structuredContent"]["undoDepth"].asNumber();
    const Json one = s.call("subdivide");
    CHECK(one["result"]["structuredContent"]["undoDepth"].asNumber() == base + 1); // one call = one undo step
    CHECK(one["result"]["structuredContent"]["canRedo"].asBool() == false);
    const Json back = s.call("undo");
    CHECK(back["result"]["structuredContent"]["undoDepth"].asNumber() == base && back["result"]["structuredContent"]["canRedo"].asBool());
    // A failed call still says where the scene stands, and says ok = false.
    const Json bad = s.call("part_select", R"({"index":99})");
    CHECK(bad["result"]["isError"].asBool() && !bad["result"]["structuredContent"]["ok"].asBool());
    CHECK(bad["result"]["structuredContent"]["triangles"].asNumber() == double(s.doc.mesh.triangleCount()));
    // The picture tools carry the same fields.
    const Json pic = s.call("viewport_image", R"({"size":256})");
    CHECK(pic["result"]["structuredContent"]["triangles"].asNumber() == double(s.doc.mesh.triangleCount()));
    CHECK(pic["result"]["content"][0]["type"].asString() == "image");
    const Json refused = s.call("view_closeup", R"({"point":"nowhere"})");
    CHECK(refused["result"]["isError"].asBool() && !refused["result"]["structuredContent"]["ok"].asBool());
}

TEST(mcp_resources_round_trip) {
    Session s;
    const Json init = s.rpc("initialize", R"({"protocolVersion":"2025-06-18","capabilities":{}})");
    CHECK(init["result"]["capabilities"]["resources"].isObject() && !init["result"]["capabilities"]["resources"]["listChanged"].asBool());
    s.call("new_sphere", R"({"subdivisions":3})");
    s.call("part_append", R"({"type":"cube"})");
    s.call("part_select", R"({"index":0})");

    const Json list = s.rpc("resources/list");
    std::set<std::string> uris;
    for (const Json& r : list["result"]["resources"].items()) {
        uris.insert(r["uri"].asString());
        CHECK(r["mimeType"].asString() == "application/json" && !r["name"].asString().empty() && !r["description"].asString().empty());
    }
    for (const char* u : {"sculptforge://scene", "sculptforge://parts", "sculptforge://history", "sculptforge://part/0", "sculptforge://part/1"})
        CHECK_MSG(uris.count(u), u);
    CHECK(!uris.count("sculptforge://part/2"));
    const Json templates = s.rpc("resources/templates/list");
    CHECK(templates["result"]["resourceTemplates"][0]["uriTemplate"].asString() == "sculptforge://part/{index}");

    // Reads leave the scene alone: no undo step, same revision.
    const size_t depth = s.doc.undo.undoCount();
    const uint64_t revision = s.doc.undo.revision();
    const Json scene = s.readResource("sculptforge://scene");
    const Json info = s.call("mesh_info")["result"]["structuredContent"]; // (a tool call: undo depth is unchanged too)
    CHECK(scene["triangles"].asNumber() == info["triangles"].asNumber() && scene["vertices"].asNumber() == info["vertices"].asNumber());
    CHECK(scene["parts"].asNumber() == 2 && scene["scene"]["visibleParts"].asNumber() == 2 && scene["bounds"]["min"].size() == 3);
    CHECK(!scene.has("ok") && !scene.has("ms") && scene.has("modified"));

    const Json parts = s.readResource("sculptforge://parts");
    CHECK(parts["parts"].size() == 2 && parts["activePart"].asNumber() == 0 && parts["parts"][1]["name"].asString() == s.doc.parts[1].name);
    CHECK(parts["parts"][0]["active"].asBool() && !parts["parts"][1]["active"].asBool());
    CHECK(parts["parts"][0]["triangles"].asNumber() == double(s.doc.partMesh(0).triangleCount()));

    // The inactive Part reads from its own data.
    const Json cube = s.readResource("sculptforge://part/1");
    CHECK(cube["index"].asNumber() == 1 && cube["triangles"].asNumber() == double(s.doc.partMesh(1).triangleCount()));
    CHECK(cube["subdivision"]["count"].asNumber() == 0 && cube["micro"].isNull() && cube["material"]["vertexColors"].isBool());
    CHECK(cube["bounds"]["max"].size() == 3 && cube["attributes"]["sculptLayers"].isArray());

    CHECK(s.doc.undo.revision() == revision);
    CHECK(s.doc.undo.undoCount() == depth);
}

TEST(mcp_resource_part_levels_and_history) {
    Session s;
    s.call("new_sphere", R"({"subdivisions":2})");
    s.call("subdiv_divide");
    s.call("subdiv_divide");
    const Json part = s.readResource("sculptforge://part/0");
    const int levels = int(part["subdivision"]["count"].asNumber());
    CHECK(levels >= 2 && part["subdivision"]["triangles"].size() == size_t(levels));
    CHECK(part["subdivision"]["current"].asNumber() == levels);
    CHECK(part["subdivision"]["triangles"][size_t(levels - 1)].asNumber() == part["triangles"].asNumber());

    const Json history = s.readResource("sculptforge://history");
    CHECK(history["undoDepth"].asNumber() == double(s.doc.undo.undoCount()) && history["steps"].size() >= 2);
    CHECK(history["steps"][0]["back"].asNumber() == 0 && !history["steps"][0]["label"].asString().empty());
    CHECK(history["steps"][0]["label"].asString() == s.doc.undo.undoLabel()); // newest first: what undo takes back next
    CHECK(!history["canRedo"].asBool());
    s.call("undo");
    CHECK(s.readResource("sculptforge://history")["canRedo"].asBool() && s.readResource("sculptforge://history")["nextRedo"].isString());
    const Json one = s.readResource("sculptforge://history?limit=1");
    CHECK(one["steps"].size() == 1);
}

TEST(mcp_resources_refuse_what_they_do_not_know) {
    Session s;
    s.call("new_sphere", R"({"subdivisions":2})");
    for (const char* uri : {"sculptforge://nope", "sculptforge://part/7", "sculptforge://part/-1", "sculptforge://part/", "sculptforge://part/1x",
                            "sculptforge://scene?x=1", "sculptforge://history?limit=abc", "file:///etc/passwd", "sculptforge://../etc"}) {
        const Json r = s.rpc("resources/read", std::string(R"({"uri":")") + uri + R"("})");
        CHECK_MSG(r.has("error") && r["error"]["code"].asNumber() == -32002 && !r.has("result"), uri);
    }
    CHECK(s.rpc("resources/read", "{}").has("error")); // no uri
    CHECK(s.rpc("resources/subscribe", R"({"uri":"sculptforge://scene"})")["error"]["code"].asNumber() == -32601); // (not offered)
}
