// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/McpTools.h"

#include "ai/ImageTo3DService.h"
#include "core/DetailPass.h"
#include "core/Document.h"
#include "core/Image.h"
#include "core/PathStroke.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sf {

namespace {

// --- Recipes (MCP prompts) ---

struct RecipeArg {
    const char* name;
    const char* description;
    bool required;
};

struct Recipe {
    const char* name;
    const char* title;
    const char* description;
    std::vector<RecipeArg> args;
    // The message the agent gets; {name} is replaced with that argument (or its default after '|': {count|20}).
    const char* text;
};

} // namespace

const char* const kMcpSculptWorkflow =
    "Sculpting to finished detail goes in stages, and each needs a denser mesh (sizes below are fractions of the "
    "model's size, its bounding-box diagonal; scene_overview gives it, stroke radii are in scene units): "
    "1. Block out the big forms (blockout, new_head or new_starter_mesh; Move, Clay Block, Inflate at radius 5-15%). "
    "2. remesh to an even surface, subdivide to about 0.5-1M triangles, and shape the secondary forms (brow, "
    "cheekbones, eyelids, lips, ear folds) at radius 1.5-4%, strength 0.3-0.6. "
    "3. Before any fine detail, subdivide again to 3-6M triangles (edges under 0.15% of the size): on a coarser mesh "
    "pores and wrinkles only roughen it. "
    "4. Detail, region by region (forehead, around the eyes, nose, cheeks, lips, neck): detail_pass (pores, skin, "
    "wrinkles, scales, scars) and strokes with the detail presets (Detail Crease for eyelids, lips, nostrils and "
    "folds; Detail Wrinkle; Detail Scar) at radius 0.3-1%, strength 0.2-0.5. "
    "Judge detail with view_closeup on the spot (span about 10% of the size): a whole-model view is too far away to "
    "show it. Several dozen detail strokes and passes is normal for a finished head: keep about a third of your "
    "steps for stages 3 and 4, and don't restart the block-out once the forms read.";

namespace {

// Every recipe ends with the same working rules.
constexpr const char* kRules =
    "\n\nWorking rules: call viewport_image after each step to check the result before going on. Every tool call is "
    "one undo step, so if a step looks wrong, call undo and try again with other values. Use mesh_info (and "
    "scene_overview, measure where available) for numbers. File paths are on the user's computer: ask before "
    "writing anywhere outside the folder they named.";

const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> list = {
        {"creature_head", "Creature head from a description",
         "Block out, sculpt and detail a creature head from a short description.",
         {{"description", "What the creature looks like, e.g. \"an old swamp troll with heavy brows\".", true},
          {"output", "Where to save the result (a .glb or .sfproj path). Optional.", false}},
         "Sculpt a creature head: {description}.\n"
         "1. Block out the big forms with blockout (spheres for skull, jaw, brow, nose, ears; symmetry x) or start "
         "from new_head, then shape the primary forms with stroke (Move, Clay Block, Inflate at radius 5-15% of the "
         "head's size), checking front and side views.\n"
         "2. Remesh to an even surface (remesh), subdivide to about 0.5-1M triangles and sculpt the secondary forms "
         "(brow folds, cheekbones, eyelids, lips, nostrils, ear folds) at radius 1.5-4%, symmetry x.\n"
         "3. Subdivide to 3-6M triangles, then detail region by region: detail_pass kind=pores and kind=skin on the "
         "cheeks, nose and forehead; kind=wrinkles around the eyes, on the brow and the neck; Detail Crease strokes on "
         "the eyelids, lips and nostrils; Detail Scar or kind=scars where the design wants them; kind=scales for scaly "
         "hide. Look at each region with view_closeup (span about 10% of the head) and redo what reads weak.\n"
         "4. Optionally paint it (set_color, paint_from_cavity).\n"
         "5. Save to {output|a file the user names} and say what you made."},
        {"stylised_bust", "Stylised bust from a reference image",
         "Turn a reference picture into a stylised bust: generate or block out, then refine by eye.",
         {{"image", "Path to the reference image on the user's computer.", true},
          {"style", "The look, e.g. \"chunky game character\" or \"smooth vinyl toy\". Optional.", false}},
         "Make a stylised bust from the reference image {image} in the style: {style|clean stylised}.\n"
         "1. If image_to_3d is available (AI features on), generate a starting model from the image; otherwise "
         "block out head, neck and shoulders with blockout or new_sphere and stroke.\n"
         "2. Remesh it even (remesh), then compare viewport_image against the reference from the front and the side.\n"
         "3. Push the proportions toward the style with large Move strokes, then simplify the planes.\n"
         "4. Subdivide and sharpen the key edges (eyelids, lips, hair clumps).\n"
         "5. Export a .glb next to the image and report the triangle count."},
        {"game_ready", "Game-ready asset (retopology, UVs, bake, LODs)",
         "Take the current high-poly sculpt to a game asset: retopology, UVs, baked maps and LODs.",
         {{"engine", "unreal, godot or unity. Optional (default: a plain .glb set).", false},
          {"triangles", "Target triangles for the game mesh, e.g. 8000. Optional.", false},
          {"folder", "Output folder.", true}},
         "Make the current sculpt game-ready for {engine|any engine (glTF)}, about {triangles|8000} triangles, into {folder}.\n"
         "1. part_duplicate the sculpt to keep the high-poly source.\n"
         "2. On the copy, qremesh to the target count (quads, even flow), then check its silhouette with "
         "viewport_image against the source.\n"
         "3. uv_unwrap it, then bake normal, ambient occlusion and colour maps from the high-poly Part (bake).\n"
         "4. Export: for Unreal use export_unreal; otherwise export_lods (4 levels, ratio 0.5) as .glb.\n"
         "5. Report every file written with its triangle count."},
        {"rock_variations", "Rock variations",
         "A set of different rocks from one base, each exported, for scattering in a game or a scene.",
         {{"count", "How many rocks (default 20).", false}, {"folder", "Output folder.", true}},
         "Make {count|20} distinct rocks and export each to {folder} as rock_01.glb, rock_02.glb...\n"
         "For each rock: new_sphere, transform to a random squash and stretch, remesh, a few large Flatten or Trim "
         "strokes for planes and broken faces (vary the seed), micro_noise or small strokes for surface grain, "
         "decimate to a game-friendly size, then export_glb. Keep them different in silhouette: check a few with "
         "viewport_image. End with a list of the files."},
        {"teach_me", "Teach me step by step",
         "A patient lesson: the agent does one small step at a time, shows it, and explains why.",
         {{"topic", "What to learn, e.g. \"sculpting a nose\" or \"retopology\". Optional.", false}},
         "Teach me {topic|the basics of digital sculpting} in SculptForge, one small step at a time. For each step: "
         "say what you are about to do and why, do it with one tool call, call viewport_image so I see it, explain "
         "what changed, then wait for me before the next step. Mention the matching tool or menu in the app so I can "
         "try it by hand. Keep it short and friendly; undo anything I don't like."},
    };
    return list;
}

const Recipe* findRecipe(const std::string& name) {
    for (const Recipe& r : recipes())
        if (name == r.name) return &r;
    return nullptr;
}

std::string argText(const Json& args, const std::string& name) {
    const Json& v = args[name];
    if (v.isString()) return v.asString();
    if (v.isNumber()) {
        const double d = v.asNumber();
        return d == std::floor(d) ? std::to_string(static_cast<long long>(d)) : std::to_string(d);
    }
    return {};
}

std::string fillRecipe(const Recipe& r, const Json& args) {
    std::string out, text = r.text;
    for (size_t i = 0; i < text.size();) {
        const size_t open = text.find('{', i);
        const size_t close = open == std::string::npos ? open : text.find('}', open);
        if (close == std::string::npos) {
            out += text.substr(i);
            break;
        }
        out += text.substr(i, open - i);
        const std::string key = text.substr(open + 1, close - open - 1);
        const size_t bar = key.find('|');
        const std::string value = argText(args, key.substr(0, bar));
        out += !value.empty() ? value : bar == std::string::npos ? std::string() : key.substr(bar + 1);
        i = close + 1;
    }
    return out + kRules;
}

// --- Headless viewport_image: a small z-buffered rasteriser over the visible Parts ---

constexpr int kDefaultSize = 768, kMinSize = 256, kMaxSize = 1536;

struct View {
    glm::vec3 center{0.0f}, right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, -1};
    float halfExtent = 1.0f; // world units from the centre to the image edge
    float nearClip = std::numeric_limits<float>::max(); // nothing nearer the viewer than this (world units in front of the centre)
    bool raking = false;     // a low key light that makes fine relief show (close-ups)
};

void drawMesh(const Mesh& mesh, const glm::mat4& place, const View& view, int size, std::vector<float>& depth,
              std::vector<glm::vec3>& color) {
    const glm::vec3 clay(0.78f, 0.74f, 0.70f);
    const glm::vec3 light = view.raking ? glm::normalize(-0.45f * view.forward + 0.8f * view.up + 0.4f * view.right)
                                        : glm::normalize(-view.forward + 0.6f * view.up + 0.5f * view.right); // key light, upper right
    const bool smooth = mesh.normals.size() == mesh.positions.size();
    const glm::mat3 turn(place);
    const float toPixels = 0.5f * float(size) / view.halfExtent;
    std::vector<glm::vec3> screen(mesh.positions.size()); // x, y in pixels (y down), z = depth toward the viewer
    for (size_t v = 0; v < mesh.positions.size(); ++v) {
        const glm::vec3 d = glm::vec3(place * glm::vec4(mesh.positions[v], 1.0f)) - view.center;
        screen[v] = {0.5f * float(size) + glm::dot(d, view.right) * toPixels,
                     0.5f * float(size) - glm::dot(d, view.up) * toPixels, -glm::dot(d, view.forward)};
    }
    for (const glm::uvec3& t : mesh.triangles) {
        const glm::vec3 &a = screen[t.x], &b = screen[t.y], &c = screen[t.z];
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::abs(area) < 1e-12f) continue;
        if (std::min({a.z, b.z, c.z}) > view.nearClip) continue;
        const glm::vec3 faceN = glm::normalize(turn * glm::cross(mesh.positions[t.y] - mesh.positions[t.x],
                                                                 mesh.positions[t.z] - mesh.positions[t.x]));
        const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
        const int x1 = std::min(size - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
        const int y1 = std::min(size - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                const float z = w0 * a.z + w1 * b.z + w2 * c.z;
                if (z > view.nearClip) continue;
                float& stored = depth[size_t(y) * size_t(size) + size_t(x)];
                if (z <= stored) continue;
                stored = z;
                glm::vec3 n = smooth ? turn * (w0 * mesh.normals[t.x] + w1 * mesh.normals[t.y] + w2 * mesh.normals[t.z]) : faceN;
                n = glm::length(n) > 0.0f ? glm::normalize(n) : faceN;
                if (glm::dot(n, view.forward) > 0.0f) n = -n; // back faces lit like front faces
                const glm::vec3 base = mesh.hasColors()
                                           ? w0 * mesh.colors[t.x] + w1 * mesh.colors[t.y] + w2 * mesh.colors[t.z]
                                           : clay;
                const float diffuse = std::max(0.0f, glm::dot(n, light));
                const float rim = std::pow(1.0f - std::abs(glm::dot(n, view.forward)), 3.0f);
                color[size_t(y) * size_t(size) + size_t(x)] = base * (0.25f + 0.75f * diffuse) + glm::vec3(0.15f * rim);
            }
    }
}

void aim(View& view, const glm::vec3& eyeDir) {
    view.forward = -glm::normalize(eyeDir);
    const glm::vec3 worldUp = std::abs(view.forward.y) > 0.999f ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
    view.right = glm::normalize(glm::cross(view.forward, worldUp));
    view.up = glm::cross(view.right, view.forward);
}

Json imageContent(const std::vector<uint8_t>& png) {
    Json image = Json::object();
    image.set("type", "image");
    image.set("data", ai::base64Encode(png.data(), png.size()));
    image.set("mimeType", "image/png");
    return image;
}

Json textContent(const std::string& text) {
    Json note = Json::object();
    note.set("type", "text");
    note.set("text", text);
    return note;
}

// Every visible Part, drawn as `view` sees it, as a PNG.
bool drawPng(const Document& doc, const View& view, int size, std::vector<uint8_t>& png, size_t& triangles,
             std::string& error) {
    const size_t pixels = size_t(size) * size_t(size);
    std::vector<float> depth(pixels, -std::numeric_limits<float>::max());
    std::vector<glm::vec3> color(pixels);
    for (int y = 0; y < size; ++y) // background: a quiet vertical gradient
        std::fill_n(color.begin() + std::ptrdiff_t(y) * size, size,
                    glm::mix(glm::vec3(0.20f, 0.21f, 0.23f), glm::vec3(0.11f, 0.11f, 0.12f), float(y) / float(size)));
    triangles = 0;
    for (int i = 0; i < doc.partCount(); ++i) {
        if (!doc.parts[size_t(i)].visible) continue;
        drawMesh(doc.partMesh(i), doc.parts[size_t(i)].transform, view, size, depth, color);
        triangles += doc.partMesh(i).triangles.size();
    }

    Image img;
    img.width = img.height = size;
    img.rgba.resize(pixels * 4);
    for (size_t p = 0; p < pixels; ++p) {
        const glm::vec3 c = glm::clamp(color[p], 0.0f, 1.0f);
        img.rgba[p * 4 + 0] = uint8_t(std::lround(std::pow(c.x, 1.0f / 2.2f) * 255.0f)); // linear-ish to sRGB
        img.rgba[p * 4 + 1] = uint8_t(std::lround(std::pow(c.y, 1.0f / 2.2f) * 255.0f));
        img.rgba[p * 4 + 2] = uint8_t(std::lround(std::pow(c.z, 1.0f / 2.2f) * 255.0f));
        img.rgba[p * 4 + 3] = 255;
    }
    if (!img.encodePng(png)) {
        error = "Couldn't encode the picture.";
        return false;
    }
    return true;
}

} // namespace

Json mcpPromptList() {
    Json prompts = Json::array();
    for (const Recipe& r : recipes()) {
        Json args = Json::array();
        for (const RecipeArg& a : r.args) {
            Json arg = Json::object();
            arg.set("name", a.name);
            arg.set("description", a.description);
            arg.set("required", a.required);
            args.push(arg);
        }
        Json p = Json::object();
        p.set("name", r.name);
        p.set("title", r.title);
        p.set("description", r.description);
        p.set("arguments", args);
        prompts.push(p);
    }
    Json result = Json::object();
    result.set("prompts", prompts);
    return result;
}

bool mcpPromptGet(const std::string& name, const Json& args, Json& result, std::string& error) {
    const Recipe* r = findRecipe(name);
    if (!r) {
        error = "Unknown prompt: " + name;
        return false;
    }
    for (const RecipeArg& a : r->args)
        if (a.required && argText(args, a.name).empty()) {
            error = std::string("Missing argument: ") + a.name;
            return false;
        }
    Json text = Json::object();
    text.set("type", "text");
    text.set("text", fillRecipe(*r, args));
    Json message = Json::object();
    message.set("role", "user");
    message.set("content", text);
    Json messages = Json::array();
    messages.push(message);
    result = Json::object();
    result.set("description", r->description);
    result.set("messages", messages);
    return true;
}

Json viewportImageTool() {
    auto prop = [](const char* type, const char* description) {
        Json p = Json::object();
        p.set("type", type);
        p.set("description", description);
        return p;
    };
    Json props = Json::object();
    props.set("size", prop("number", "Image width and height in pixels (256 to 1536, default 768)."));
    props.set("yaw", prop("number", "Degrees around the model: 0 = front, 90 = the model's right side, 180 = back."));
    props.set("elevation", prop("number", "Degrees above the model, -89 to 89; positive looks down on it."));
    props.set("frame", prop("string", "What to fit in the picture: \"all\" visible Parts (default), \"active\", or a Part's name."));
    props.set("preset", prop("string", "A named view instead of yaw/elevation: front, back, left, right, top, bottom or "
                                       "three_quarter (the default when no angle is given)."));
    props.set("zoom", prop("number", "Magnification of the framed Parts (default 1; 2 shows half as much)."));
    props.set("ortho", prop("boolean", "Orthographic (no perspective). The headless server always draws orthographic."));
    Json schema = Json::object();
    schema.set("type", "object");
    schema.set("properties", props);
    schema.set("required", Json::array());
    Json tool = Json::object();
    tool.set("name", "viewport_image");
    tool.set("description",
             "A picture of the scene as a PNG, to check the result of a step by eye. Clay-shaded, vertex colours shown, "
             "orthographic; call it after each step.");
    tool.set("inputSchema", schema);
    return tool;
}

bool renderViewportImage(const Document& doc, const Json& args, Json& content, std::string& error) {
    const int size = std::clamp(int(args["size"].asNumber(kDefaultSize)), kMinSize, kMaxSize);
    // A preset, then explicit angles over it; nothing given: three-quarter.
    float yawDeg = 35.0f, elevationDeg = 20.0f;
    if (args["preset"].isString()) {
        static const struct { const char* name; float yaw, elevation; } kPresets[] = {
            {"front", 0, 0}, {"back", 180, 0}, {"left", -90, 0}, {"right", 90, 0},
            {"top", 0, 89}, {"bottom", 0, -89}, {"three_quarter", 35, 20}};
        bool known = false;
        for (const auto& p : kPresets)
            if (args["preset"].asString() == p.name) yawDeg = p.yaw, elevationDeg = p.elevation, known = true;
        if (!known) {
            error = "Unknown preset \"" + args["preset"].asString() + "\" (front, back, left, right, top, bottom, three_quarter).";
            return false;
        }
    }
    yawDeg = float(args["yaw"].asNumber(yawDeg));
    elevationDeg = std::clamp(float(args["elevation"].asNumber(elevationDeg)), -89.0f, 89.0f);
    const float yaw = glm::radians(yawDeg), pitch = glm::radians(elevationDeg);
    const float zoom = std::clamp(float(args["zoom"].asNumber(1.0)), 0.1f, 20.0f);
    const std::string frame = args["frame"].isString() ? args["frame"].asString() : "all";

    // The Parts drawn (every visible one) and the ones framed.
    std::vector<int> drawn, framed;
    for (int i = 0; i < doc.partCount(); ++i) {
        if (!doc.parts[size_t(i)].visible) continue;
        drawn.push_back(i);
        if (frame == "all" || (frame == "active" && i == doc.activePart) || frame == doc.parts[size_t(i)].name)
            framed.push_back(i);
    }
    if (frame != "all" && frame != "active" && framed.empty()) {
        error = "No visible Part named \"" + frame + "\" (part_list names them).";
        return false;
    }
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
    for (const int i : framed)
        for (const glm::vec3& p : doc.partMesh(i).positions) {
            const glm::vec3 w = glm::vec3(doc.parts[size_t(i)].transform * glm::vec4(p, 1.0f)); // (a moved linked copy)
            lo = glm::min(lo, w), hi = glm::max(hi, w);
        }
    if (lo.x > hi.x) {
        error = "Nothing to show: the scene has no visible mesh (start with new_sphere or open).";
        return false;
    }

    View view;
    view.center = 0.5f * (lo + hi);
    view.halfExtent = std::max(1e-6f, 0.55f * glm::length(hi - lo)) / zoom; // the bounding sphere, with a margin
    // The model faces +Z, so the front view looks down -Z; yaw 90 looks at the model's right side (-X), elevation
    // raises the camera.
    aim(view, glm::vec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)));
    std::vector<uint8_t> png;
    size_t triangles = 0;
    if (!drawPng(doc, view, size, png, triangles, error)) return false;
    content = Json::array();
    content.push(imageContent(png));
    content.push(textContent(std::to_string(size) + " px, yaw " + std::to_string(int(std::lround(glm::degrees(yaw)))) +
                             ", elevation " + std::to_string(int(std::lround(glm::degrees(pitch)))) + ", " +
                             std::to_string(drawn.size()) + " visible Part(s), " + std::to_string(triangles) +
                             " triangles (headless clay view)."));
    return true;
}

Json viewCloseupTool() {
    auto prop = [](const char* type, const char* description) {
        Json p = Json::object();
        p.set("type", type);
        p.set("description", description);
        return p;
    };
    Json props = Json::object();
    props.set("point", prop("string", "\"x,y,z\" in scene units: the spot to look at (the nearest surface point is "
                                      "used; scene_overview and measure give the model's centre and size)."));
    props.set("span", prop("number", "How wide the picture is, in scene units (default 10% of the model's size; "
                                     "about 0.05-0.15 of a head's height shows pores and wrinkles)."));
    props.set("direction", prop("string", "\"x,y,z\": look from this side (pointing from the surface toward the "
                                          "camera). Default: straight at the surface, along its normal."));
    props.set("size", prop("number", "Image width and height in pixels (256 to 1536, default 768)."));
    Json schema = Json::object();
    schema.set("type", "object");
    schema.set("properties", props);
    Json required = Json::array();
    required.push("point");
    schema.set("required", required);
    Json tool = Json::object();
    tool.set("name", "view_closeup");
    tool.set("description",
             "A close-up picture of one spot of the model, lit low from the side so fine relief shows, to judge "
             "detail (pores, wrinkles, creases, scales) the way an artist zooms in. It also says how many triangles "
             "are in view and the finest detail the mesh can hold there. Doesn't move the view.");
    tool.set("inputSchema", schema);
    return tool;
}

bool closeupArgs(const Document& doc, const Json& args, Closeup& c, float& span, int& size, std::string& error) {
    glm::vec3 point(0.0f), eye(0.0f);
    if (!args["point"].isString() || !parseVec3(args["point"].asString(), point)) {
        error = "point must be \"x,y,z\" (scene units)";
        return false;
    }
    if (args["direction"].isString() && !parseVec3(args["direction"].asString(), eye)) {
        error = "direction must be \"x,y,z\"";
        return false;
    }
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
    for (int i = 0; i < doc.partCount(); ++i)
        if (doc.parts[size_t(i)].visible)
            for (const glm::vec3& p : doc.partMesh(i).positions) {
                const glm::vec3 w = glm::vec3(doc.parts[size_t(i)].transform * glm::vec4(p, 1.0f));
                lo = glm::min(lo, w), hi = glm::max(hi, w);
            }
    if (lo.x > hi.x) {
        error = "Nothing to show: the scene has no visible mesh.";
        return false;
    }
    const float modelSize = std::max(glm::length(hi - lo), 1e-6f);
    span = float(args["span"].asNumber(0.1 * modelSize));
    if (!(span > modelSize * 1e-4f && span <= modelSize * 2.0f)) {
        error = "span must be between " + std::to_string(modelSize * 1e-4f) + " and " + std::to_string(modelSize * 2.0f);
        return false;
    }
    size = std::clamp(int(args["size"].asNumber(kDefaultSize)), kMinSize, kMaxSize);
    return closeupFraming(doc, point, span, eye, c, error);
}

bool renderCloseup(const Document& doc, const Json& args, Json& content, std::string& error) {
    Closeup c;
    float span = 0.0f;
    int size = kDefaultSize;
    if (!closeupArgs(doc, args, c, span, size, error)) return false;
    View view;
    view.center = c.point;
    view.halfExtent = 0.5f * span;
    view.nearClip = span; // (what stands between: the nose in front of a cheek)
    view.raking = true;
    aim(view, c.eye);
    std::vector<uint8_t> png;
    size_t triangles = 0;
    if (!drawPng(doc, view, size, png, triangles, error)) return false;
    content = Json::array();
    content.push(imageContent(png));
    content.push(textContent(closeupText(c, span) + " (headless clay view, " + std::to_string(size) + " px)"));
    return true;
}

} // namespace sf
