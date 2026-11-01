// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BrushRecipe.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

using K = RecipeParam::Kind;

const char* kDirections[] = {"normal", "inflate", "view", "stroke"};
const char* kPlanes[] = {"none", "clay", "flatten", "fill", "scrape"};
const char* kPatterns[] = {"none", "noise", "scales", "ridges", "pores", "cracks"};

template <typename E, size_t N>
E enumFromString(const std::string& s, const char* (&names)[N], E fallback) {
    for (size_t i = 0; i < N; ++i)
        if (s == names[i]) return E(i);
    return fallback;
}

float* floatField(BrushRecipe& r, const std::string& key) {
    if (key == "depth") return &r.depth;
    if (key == "planeAmount") return &r.planeAmount;
    if (key == "pinch") return &r.pinch;
    if (key == "drag") return &r.drag;
    if (key == "smooth") return &r.smooth;
    if (key == "hardness") return &r.hardness;
    if (key == "tipSharpness") return &r.tipSharpness;
    if (key == "patternScale") return &r.patternScale;
    if (key == "patternAmount") return &r.patternAmount;
    if (key == "spacing") return &r.spacing;
    if (key == "jitter") return &r.jitter;
    if (key == "pressureCurve") return &r.pressureCurve;
    if (key == "cavityMask") return &r.cavityMask;
    return nullptr;
}

std::filesystem::path utf8Path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

} // namespace

const std::vector<RecipeParam>& recipeParams() {
    static const std::vector<RecipeParam> params = {
        {"direction", "Direction",
         "Which way the surface moves: 'normal' = out of the surface (like adding clay), 'inflate' = puffs "
         "outward in every direction, 'view' = toward the camera, 'stroke' = pushes along the stroke direction.",
         K::Enum, 0, 0, {kDirections[0], kDirections[1], kDirections[2], kDirections[3]}},
        {"depth", "Depth", "How much material is added (positive) or carved away (negative). Range -1 to 1.",
         K::Float, -1.0f, 1.0f, {}},
        {"plane", "Plane",
         "Planar behaviour: 'clay' builds up flat-topped layers, 'flatten' levels everything, 'fill' only raises "
         "low spots, 'scrape' only cuts down high spots, 'none' disables it.",
         K::Enum, 0, 0, {kPlanes[0], kPlanes[1], kPlanes[2], kPlanes[3], kPlanes[4]}},
        {"planeAmount", "Plane amount", "Strength of the plane behaviour. Range 0 to 1.", K::Float, 0.0f, 1.0f, {}},
        {"pinch", "Pinch",
         "Pulls the surface toward the centre of the stroke for crisp creases and edges (negative spreads it "
         "apart). Range -1 to 1.",
         K::Float, -1.0f, 1.0f, {}},
        {"drag", "Drag", "Smears and drags the surface along with the stroke, like a palette knife. Range 0 to 1.",
         K::Float, 0.0f, 1.0f, {}},
        {"smooth", "Auto smooth", "Relaxes the surface as you paint for softer, cleaner results. Range 0 to 1.",
         K::Float, 0.0f, 1.0f, {}},
        {"hardness", "Hardness", "Edge of the brush tip: 0 = soft airbrush falloff, 1 = hard-edged stamp. Range 0 to 1.",
         K::Float, 0.0f, 1.0f, {}},
        {"tipSharpness", "Tip sharpness",
         "Profile of the tip: 0 = round and broad, 1 = narrow needle point for fine lines. Range 0 to 1.", K::Float,
         0.0f, 1.0f, {}},
        {"pattern", "Pattern",
         "Surface texture stamped by the brush: 'noise' = organic bumps, 'scales' = rounded reptile scales, "
         "'ridges' = sinewy ridges, 'pores' = small skin pits, 'cracks' = cracked stone/dry earth lines, 'none'.",
         K::Enum, 0, 0, {kPatterns[0], kPatterns[1], kPatterns[2], kPatterns[3], kPatterns[4], kPatterns[5]}},
        {"patternScale", "Pattern size", "Size of texture features relative to the brush size. Range 0.03 to 2.",
         K::Float, 0.03f, 2.0f, {}},
        {"patternAmount", "Pattern amount", "How strongly the texture shows. Range 0 to 1.", K::Float, 0.0f, 1.0f, {}},
        {"spacing", "Spacing",
         "Distance between dabs relative to brush size: small = continuous line, large = separate stamps. Range "
         "0.02 to 2.",
         K::Float, 0.02f, 2.0f, {}},
        {"jitter", "Jitter", "Random scatter of dabs around the stroke. Range 0 to 1.", K::Float, 0.0f, 1.0f, {}},
        {"pressureCurve", "Pressure curve",
         "Tablet response: below 1 reacts to a light touch, above 1 needs a firm press. Range 0.3 to 3.", K::Float,
         0.3f, 3.0f, {}},
        {"frontFacesOnly", "Front faces only", "Only affect surfaces facing the camera (protects thin parts).",
         K::Bool, 0, 1, {}},
        {"cavityMask", "Cavity mask",
         "Limits the brush to cavities (positive, e.g. dirt in crevices) or to peaks and ridges (negative, e.g. "
         "wear on edges). 0 = everywhere. Range -1 to 1.",
         K::Float, -1.0f, 1.0f, {}},
    };
    return params;
}

const char* brushSourceName(BrushSource source) {
    switch (source) {
    case BrushSource::Session: return "session";
    case BrushSource::Builtin: return "built-in";
    case BrushSource::User: return "user";
    case BrushSource::Studio: return "studio";
    case BrushSource::Gallery: return "gallery";
    case BrushSource::Project: return "project";
    case BrushSource::Pack: return "pack";
    }
    return "session";
}

float* BrushRecipe::field(const std::string& key) { return floatField(*this, key); }

int BrushRecipe::enumIndex(const std::string& key) const {
    if (key == "direction") return int(direction);
    if (key == "plane") return int(plane);
    if (key == "pattern") return int(pattern);
    return 0;
}

void BrushRecipe::setEnumIndex(const std::string& key, int index) {
    if (key == "direction") direction = Direction(std::clamp(index, 0, 3));
    else if (key == "plane") plane = Plane(std::clamp(index, 0, 4));
    else if (key == "pattern") pattern = Pattern(std::clamp(index, 0, 5));
}

void BrushRecipe::clamp() {
    for (const auto& p : recipeParams())
        if (p.kind == K::Float)
            if (float* f = floatField(*this, p.key)) *f = std::clamp(*f, p.min, p.max);
    if (name.empty()) name = "Custom Brush";
    if (name.size() > 64) name.resize(64);
    if (description.size() > 500) description.resize(500);
}

Json BrushRecipe::toJson() const {
    BrushRecipe r = *this;
    Json j = Json::object();
    j.set("name", r.name);
    j.set("description", r.description);
    if (connectedOnly) j.set("connectedOnly", true);
    if (viewFalloff) j.set("viewFalloff", true);
    for (const auto& p : recipeParams()) {
        switch (p.kind) {
        case K::Float: j.set(p.key, std::round(double(*floatField(r, p.key)) * 1000.0) / 1000.0); break;
        case K::Bool: j.set(p.key, frontFacesOnly); break;
        case K::Enum:
            if (std::string(p.key) == "direction") j.set(p.key, kDirections[int(direction)]);
            else if (std::string(p.key) == "plane") j.set(p.key, kPlanes[int(plane)]);
            else j.set(p.key, kPatterns[int(pattern)]);
            break;
        }
    }
    return j;
}

void BrushRecipe::applyJson(const Json& j) {
    if (!j.isObject()) return;
    if (j["name"].isString()) name = j["name"].asString();
    if (j["description"].isString()) description = j["description"].asString();
    if (j["connectedOnly"].isBool()) connectedOnly = j["connectedOnly"].asBool();
    if (j["viewFalloff"].isBool()) viewFalloff = j["viewFalloff"].asBool();
    for (const auto& p : recipeParams()) {
        const Json& v = j[p.key];
        if (v.isNull()) continue;
        switch (p.kind) {
        case K::Float:
            if (v.isNumber()) *floatField(*this, p.key) = float(v.asNumber());
            break;
        case K::Bool:
            if (v.isBool()) frontFacesOnly = v.asBool();
            break;
        case K::Enum: {
            const std::string& s = v.asString();
            if (std::string(p.key) == "direction") direction = enumFromString(s, kDirections, direction);
            else if (std::string(p.key) == "plane") plane = enumFromString(s, kPlanes, plane);
            else pattern = enumFromString(s, kPatterns, pattern);
            break;
        }
        }
    }
    clamp();
}

BrushRecipe BrushRecipe::fromJson(const Json& j) {
    BrushRecipe r;
    r.applyJson(j);
    return r;
}

bool BrushRecipe::save(const std::string& path, std::string& error) const {
    Json j = toJson();
    j.set("format", "sfbrush");
    j.set("version", 1);
    std::ofstream f(utf8Path(path), std::ios::binary);
    if (!f) {
        error = "Cannot write " + path;
        return false;
    }
    f << j.dump(true) << "\n";
    return bool(f);
}

bool BrushRecipe::load(const std::string& path, BrushRecipe& out, std::string& error) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Json j;
    if (!Json::parse(text, j, error)) return false;
    if (!j.isObject()) {
        error = "Not a brush file";
        return false;
    }
    out = fromJson(j);
    return true;
}

Json recipeJsonSchema() {
    Json props = Json::object();
    Json required = Json::array();
    auto addString = [&](const char* key, const char* desc) {
        Json p = Json::object();
        p.set("type", "string");
        p.set("description", desc);
        props.set(key, p);
        required.push(key);
    };
    addString("name", "Short, evocative brush name an artist would recognise (max ~4 words).");
    addString("description", "One sentence describing what the brush does and when to use it.");
    for (const auto& rp : recipeParams()) {
        Json p = Json::object();
        p.set("description", rp.help);
        if (rp.kind == K::Float) {
            p.set("type", "number");
        } else if (rp.kind == K::Bool) {
            p.set("type", "boolean");
        } else {
            p.set("type", "string");
            Json e = Json::array();
            for (const char* o : rp.options) e.push(o);
            p.set("enum", e);
        }
        props.set(rp.key, p);
        required.push(rp.key);
    }
    Json schema = Json::object();
    schema.set("type", "object");
    schema.set("properties", props);
    schema.set("required", required);
    schema.set("additionalProperties", false);
    return schema;
}

std::vector<BrushRecipe> builtinBrushRecipes() {
    std::vector<BrushRecipe> out;
    auto add = [&](BrushRecipe r) {
        r.clamp();
        r.origin.source = BrushSource::Builtin;
        out.push_back(std::move(r));
    };
    {
        BrushRecipe r;
        r.name = "Wrinkle";
        r.description = "Fine, sharp creases for skin folds and fabric wrinkles.";
        r.depth = -0.55f;
        r.pinch = 0.7f;
        r.tipSharpness = 0.7f;
        r.hardness = 0.15f;
        r.spacing = 0.06f;
        add(r);
    }
    {
        BrushRecipe r;
        r.name = "Palette Knife";
        r.description = "Flattens and drags clay sideways like a sculptor's knife.";
        r.depth = 0.0f;
        r.plane = BrushRecipe::Plane::Flatten;
        r.planeAmount = 0.8f;
        r.drag = 0.6f;
        r.hardness = 0.75f;
        r.spacing = 0.1f;
        add(r);
    }
    {
        BrushRecipe r;
        r.name = "Dragon Scales";
        r.description = "Stamps rounded overlapping scales along the stroke.";
        r.depth = 0.45f;
        r.pattern = BrushRecipe::Pattern::Scales;
        r.patternScale = 0.35f;
        r.patternAmount = 0.95f;
        r.hardness = 0.35f;
        r.spacing = 0.35f;
        add(r);
    }
    {
        BrushRecipe r;
        r.name = "Stone Chisel";
        r.description = "Scrapes hard planes and breaks them up with cracks for rocks and ruins.";
        r.depth = -0.25f;
        r.plane = BrushRecipe::Plane::Scrape;
        r.planeAmount = 0.8f;
        r.pattern = BrushRecipe::Pattern::Cracks;
        r.patternScale = 0.45f;
        r.patternAmount = 0.6f;
        r.hardness = 0.85f;
        add(r);
    }
    {
        BrushRecipe r;
        r.name = "Skin Pores";
        r.description = "Sprinkles tiny pores for close-up skin detail.";
        r.depth = -0.2f;
        r.pattern = BrushRecipe::Pattern::Pores;
        r.patternScale = 0.08f;
        r.patternAmount = 1.0f;
        r.spacing = 0.3f;
        r.jitter = 0.3f;
        r.frontFacesOnly = true;
        add(r);
    }
    {
        BrushRecipe r;
        r.name = "Clay Slab";
        r.description = "Builds up flat-topped strokes of clay for blocking in forms.";
        r.depth = 0.3f;
        r.plane = BrushRecipe::Plane::Clay;
        r.planeAmount = 0.7f;
        r.hardness = 0.35f;
        r.smooth = 0.1f;
        add(r);
    }
    {
        BrushRecipe r;
        r.name = "Edge Wear";
        r.description = "Chips and softens only the sharp edges and ridges, for weathered props.";
        r.depth = -0.35f;
        r.pattern = BrushRecipe::Pattern::Noise;
        r.patternScale = 0.25f;
        r.patternAmount = 0.7f;
        r.cavityMask = -0.8f;
        r.hardness = 0.4f;
        add(r);
    }
    return out;
}

} // namespace sf
