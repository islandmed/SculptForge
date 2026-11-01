// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MaterialRecipe.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/LayerStack.h"
#include "core/Texture.h"
#include "core/Undo.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

namespace sf {

namespace assets {
extern const unsigned char recipeWornMetal[];
extern const unsigned long long recipeWornMetal_size;
extern const unsigned char recipePaintedEdges[];
extern const unsigned long long recipePaintedEdges_size;
extern const unsigned char recipeDirtAndDust[];
extern const unsigned long long recipeDirtAndDust_size;
extern const unsigned char recipeRustedIron[];
extern const unsigned long long recipeRustedIron_size;
} // namespace assets

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = c == ' ' || c == '-' ? '_' : char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::filesystem::path fsPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
} // namespace

bool parseMaterialRecipe(const std::string& text, MaterialRecipe& out, std::string& error) {
    Json j;
    if (!Json::parse(text, j, error)) return false;
    if (!j.isObject() || j["format"].asString() != "sculptforge-material-recipe") {
        error = "Not a SculptForge Material Recipe";
        return false;
    }
    if (j["version"].asNumber(0) > 1) {
        error = "The Material Recipe is from a newer SculptForge";
        return false;
    }
    out = MaterialRecipe();
    out.name = j["name"].isString() ? j["name"].asString() : std::string("Material");
    out.id = j["id"].isString() ? j["id"].asString() : lower(out.name);
    out.description = j["description"].isString() ? j["description"].asString() : std::string();
    if (!j["layers"].isArray() || j["layers"].size() == 0 || j["layers"].size() > 16) {
        error = "A Material Recipe has 1 to 16 layers";
        return false;
    }
    for (const Json& l : j["layers"].items()) { // (checked now, so applying can't fail half way)
        FillLayer f;
        if (!fillLayerFromJson(l["fill"], f, error)) return false;
    }
    out.layers = j["layers"];
    return true;
}

const std::vector<MaterialRecipe>& builtinMaterialRecipes() {
    static std::vector<MaterialRecipe> list;
    static std::once_flag once;
    std::call_once(once, [] {
        const std::pair<const unsigned char*, unsigned long long> files[] = {
            {assets::recipeWornMetal, assets::recipeWornMetal_size},
            {assets::recipePaintedEdges, assets::recipePaintedEdges_size},
            {assets::recipeDirtAndDust, assets::recipeDirtAndDust_size},
            {assets::recipeRustedIron, assets::recipeRustedIron_size}};
        for (const auto& [data, size] : files) {
            MaterialRecipe r;
            std::string err;
            if (parseMaterialRecipe(std::string(reinterpret_cast<const char*>(data), size_t(size)), r, err)) list.push_back(std::move(r));
        }
    });
    return list;
}

const MaterialRecipe* findMaterialRecipe(const std::string& text) {
    const std::string s = lower(text);
    for (const MaterialRecipe& r : builtinMaterialRecipes())
        if (s == lower(r.id) || s == lower(r.name)) return &r;
    return nullptr;
}

std::string materialRecipeText(const PaintTexture& t, const std::string& name) {
    Json j = Json::object();
    j.set("format", "sculptforge-material-recipe");
    j.set("version", 1);
    j.set("id", lower(name));
    j.set("name", name);
    Json layers = Json::array();
    for (const TextureLayer& l : t.layers) {
        if (!l.fill) continue;
        Json o = Json::object();
        o.set("name", l.name);
        o.set("fill", fillLayerJson(*l.fill));
        layers.push(std::move(o));
    }
    j.set("layers", std::move(layers));
    return j.dump(true);
}

bool applyMaterialRecipe(Document& doc, const MaterialRecipe& recipe, std::string& message) {
    Mesh& m = doc.mesh;
    if (!hasTexture(m)) {
        message = "The Part has no texture: Tool > Texture Map > New Texture";
        return false;
    }
    UndoRecord r = captureTexture(m, "Material Recipe");
    PaintTexture& t = *m.texture;
    std::vector<int> added;
    for (const Json& l : recipe.layers.items()) {
        auto f = std::make_shared<FillLayer>();
        std::string err;
        if (!fillLayerFromJson(l["fill"], *f, err)) {
            m.texture = r.texture->texture;
            message = err;
            return false;
        }
        for (int c = 1; c < kChannelCount; ++c)
            if (f->material.channels & (1u << c)) setChannelEnabled(t, Channel(c), true);
        const std::string name = l["name"].isString() ? l["name"].asString() : recipe.name;
        const int i = addTextureLayer(t, uniqueTextureLayerName(t, name), -2);
        t.layers[size_t(i)].fill = std::move(f);
        t.layers[size_t(i)].mask.assign(t.texelCount() * 4, 0);
        for (size_t k = 0; k < t.layers[size_t(i)].mask.size(); k += 4)
            t.layers[size_t(i)].mask[k] = t.layers[size_t(i)].mask[k + 1] = t.layers[size_t(i)].mask[k + 2] = 255;
        added.push_back(i);
    }
    std::string err, note;
    if (!ensureFillMeshMaps(doc, err)) note = err.empty() ? " (baking Mesh Maps...)" : " (Mesh Maps: " + err + ")";
    PaintTexture& tt = *m.texture;
    for (int i : added) evaluateFillLayer(m, tt, i);
    compositeTextureAll(tt);
    finishTextureUndo(r, m);
    doc.undo.push(std::move(r));
    doc.modified = true;
    message = "Material Recipe " + recipe.name + ": " + std::to_string(added.size()) + " Fill layers" + note;
    return true;
}

void registerMaterialRecipeCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"material_recipe",
             "Material Recipes: stacks of Fill layers that read the Part's Mesh Maps, so they wear each model where it "
             "would wear. apply lays one on the active Part (worn_metal, painted_edges, dirt_and_dust, rusted_iron, or a "
             ".sfmaterial file), list names the built-in ones, save writes the Part's Fill layers as a recipe file.",
             {{"action", T::String, "apply (default), list or save.", false},
              {"name", T::String, "apply: a built-in recipe; save: the recipe's name.", false},
              {"path", T::String, "apply: a .sfmaterial file; save: where to write it.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string action = argString(a, "action", "apply");
                 if (action == "list") {
                     std::string s;
                     for (const MaterialRecipe& r : builtinMaterialRecipes())
                         s += (s.empty() ? "" : "\n") + r.id + ": " + r.name + " - " + r.description;
                     return {true, s};
                 }
                 if (action == "save") {
                     if (!hasTexture(doc.mesh)) return {false, "The Part has no texture"};
                     if (fillMapsNeeded(*doc.mesh.texture) == 0 &&
                         std::none_of(doc.mesh.texture->layers.begin(), doc.mesh.texture->layers.end(),
                                      [](const TextureLayer& l) { return l.fill != nullptr; }))
                         return {false, "The Part has no Fill layers to save"};
                     const std::string path = argString(a, "path");
                     if (path.empty()) return {false, "path is required"};
                     std::ofstream f(fsPath(path), std::ios::binary);
                     f << materialRecipeText(*doc.mesh.texture, argString(a, "name", "My Material"));
                     if (!f) return {false, "Can't write " + path};
                     return {true, "Saved the Material Recipe to " + path};
                 }
                 if (action != "apply") return {false, "action must be apply, list or save"};
                 MaterialRecipe fromFile;
                 const MaterialRecipe* recipe = nullptr;
                 if (a.contains("path")) {
                     std::ifstream f(fsPath(argString(a, "path")), std::ios::binary);
                     if (!f) return {false, "Can't open " + argString(a, "path")};
                     const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                     if (text.size() > (1u << 20)) return {false, "The recipe file is too large"};
                     std::string err;
                     if (!parseMaterialRecipe(text, fromFile, err)) return {false, err};
                     recipe = &fromFile;
                 } else {
                     recipe = findMaterialRecipe(argString(a, "name"));
                     if (!recipe) {
                         std::string names;
                         for (const MaterialRecipe& r : builtinMaterialRecipes()) names += (names.empty() ? "" : ", ") + r.id;
                         return {false, "Unknown Material Recipe '" + argString(a, "name") + "' (" + names + ")"};
                     }
                 }
                 std::string msg;
                 const bool ok = applyMaterialRecipe(doc, *recipe, msg);
                 return {ok, msg};
             }});
}

} // namespace sf
