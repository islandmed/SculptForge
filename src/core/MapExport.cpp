// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MapExport.h"

#include "core/BakeImage.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace sf {

namespace {

// The built-in presets, in the same JSON a user preset file holds (so the parser is what defines them).
const char* const kBuiltinPresets[] = {
    R"PRESET({"id": "unreal", "label": "Unreal Engine",
        "description": "Base colour (opacity in alpha when painted), DirectX normal, packed occlusion / roughness / metallic. The Unreal bridge imports these and builds a material.",
        "file": "T_{name}_{map}", "normal": "directx",
        "maps": [{"map": "BC", "channels": ["base_color.r", "base_color.g", "base_color.b", "opacity?"]},
                 {"map": "N", "channels": ["normal.r", "normal.g", "normal.b"]},
                 {"map": "ORM", "channels": ["ao", "roughness", "metallic"]},
                 {"map": "E", "channels": ["emissive.r", "emissive.g", "emissive.b"], "optional": true},
                 {"map": "H", "channels": ["height"], "bits": 16, "optional": true}]})PRESET",
    R"PRESET({"id": "unity_urp", "label": "Unity (URP Lit)",
        "description": "Base map, OpenGL normal, metallic in red with smoothness in alpha, occlusion, emission and height.",
        "file": "{name}_{map}", "normal": "opengl",
        "maps": [{"map": "BaseMap", "channels": ["base_color.r", "base_color.g", "base_color.b", "opacity?"]},
                 {"map": "Normal", "channels": ["normal.r", "normal.g", "normal.b"]},
                 {"map": "MetallicSmoothness", "channels": ["metallic", "0", "0", "smoothness"]},
                 {"map": "Occlusion", "channels": ["ao"], "optional": true},
                 {"map": "Emission", "channels": ["emissive.r", "emissive.g", "emissive.b"], "optional": true},
                 {"map": "Height", "channels": ["height"], "bits": 16, "optional": true}]})PRESET",
    R"PRESET({"id": "unity_hdrp", "label": "Unity (HDRP Lit)",
        "description": "Base map, OpenGL normal and the mask map: metallic, occlusion, detail mask (white), smoothness.",
        "file": "{name}_{map}", "normal": "opengl",
        "maps": [{"map": "BaseMap", "channels": ["base_color.r", "base_color.g", "base_color.b", "opacity?"]},
                 {"map": "Normal", "channels": ["normal.r", "normal.g", "normal.b"]},
                 {"map": "MaskMap", "channels": ["metallic", "ao", "1", "smoothness"]},
                 {"map": "Emission", "channels": ["emissive.r", "emissive.g", "emissive.b"], "optional": true},
                 {"map": "Height", "channels": ["height"], "bits": 16, "optional": true}]})PRESET",
    R"PRESET({"id": "gltf", "label": "glTF",
        "description": "glTF 2.0 metal-rough: base colour, OpenGL normal, occlusion / roughness / metallic packed in R / G / B.",
        "file": "{name}_{map}", "normal": "opengl",
        "maps": [{"map": "baseColor", "channels": ["base_color.r", "base_color.g", "base_color.b", "opacity?"]},
                 {"map": "normal", "channels": ["normal.r", "normal.g", "normal.b"]},
                 {"map": "occlusionRoughnessMetallic", "channels": ["ao", "roughness", "metallic"]},
                 {"map": "emissive", "channels": ["emissive.r", "emissive.g", "emissive.b"], "optional": true}]})PRESET",
    R"PRESET({"id": "godot", "label": "Godot",
        "description": "Godot StandardMaterial3D: albedo, OpenGL normal, ORM (occlusion / roughness / metallic), emission, height.",
        "file": "{name}_{map}", "normal": "opengl",
        "maps": [{"map": "albedo", "channels": ["base_color.r", "base_color.g", "base_color.b", "opacity?"]},
                 {"map": "normal", "channels": ["normal.r", "normal.g", "normal.b"]},
                 {"map": "orm", "channels": ["ao", "roughness", "metallic"]},
                 {"map": "emission", "channels": ["emissive.r", "emissive.g", "emissive.b"], "optional": true},
                 {"map": "height", "channels": ["height"], "bits": 16, "optional": true}]})PRESET",
    R"PRESET({"id": "blender", "label": "Blender",
        "description": "One file per channel for the Principled BSDF: base colour, roughness, metallic, OpenGL normal, height (16-bit), AO, emission, alpha.",
        "file": "{name}_{map}", "normal": "opengl",
        "maps": [{"map": "BaseColor", "channels": ["base_color.r", "base_color.g", "base_color.b"]},
                 {"map": "Roughness", "channels": ["roughness"]},
                 {"map": "Metallic", "channels": ["metallic"]},
                 {"map": "Normal", "channels": ["normal.r", "normal.g", "normal.b"]},
                 {"map": "Height", "channels": ["height"], "bits": 16, "optional": true},
                 {"map": "AO", "channels": ["ao"], "optional": true},
                 {"map": "Emission", "channels": ["emissive.r", "emissive.g", "emissive.b"], "optional": true},
                 {"map": "Alpha", "channels": ["opacity"], "optional": true}]})PRESET",
    R"PRESET({"id": "usd", "label": "USD (UsdPreviewSurface)",
        "description": "One file per UsdPreviewSurface input: diffuseColor, roughness, metallic, normal, occlusion, emissiveColor, opacity, displacement.",
        "file": "{name}_{map}", "normal": "opengl",
        "maps": [{"map": "diffuseColor", "channels": ["base_color.r", "base_color.g", "base_color.b"]},
                 {"map": "roughness", "channels": ["roughness"]},
                 {"map": "metallic", "channels": ["metallic"]},
                 {"map": "normal", "channels": ["normal.r", "normal.g", "normal.b"]},
                 {"map": "occlusion", "channels": ["ao"], "optional": true},
                 {"map": "emissiveColor", "channels": ["emissive.r", "emissive.g", "emissive.b"], "optional": true},
                 {"map": "opacity", "channels": ["opacity"], "optional": true},
                 {"map": "displacement", "channels": ["height"], "bits": 16, "optional": true}]})PRESET",
};

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::filesystem::path fsPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string utf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Safe in file names: letters, digits, '-', '_' and '.'.
std::string safeName(const std::string& s) {
    std::string out;
    for (unsigned char c : s) out += std::isalnum(c) || c == '-' || c == '_' || c == '.' ? char(c) : '_';
    return out.empty() ? std::string("Object") : out;
}

// The sources a map keeps for this texture ('?' components dropped when their channel is off).
std::vector<MapSource> keptSources(const PaintTexture& t, const MapSpec& map) {
    std::vector<MapSource> out;
    for (const MapSource& s : map.sources)
        if (!(s.dropIfMissing && s.kind == MapSource::Kind::Channel && !channelEnabled(t, Channel(s.channel)))) out.push_back(s);
    if (out.size() == 2) out.resize(1); // grey + alpha isn't offered: keep grey
    return out;
}

// Evaluates the map's sources over the texture: fn(x, y, values) with values in 0..1, rows bottom-up.
template <class Fn>
void evaluate(const PaintTexture& t, const std::vector<MapSource>& srcs, bool directX, Fn&& fn) {
    bool needNormal = false;
    uint32_t needed = 0;
    for (const MapSource& s : srcs)
        if (s.kind == MapSource::Kind::Channel) {
            if (s.channel == int(Channel::Normal)) needNormal = true;
            else needed |= 1u << s.channel;
        }
    const TileImage* imgs[kChannelCount] = {};
    for (int c = 1; c < kChannelCount; ++c)
        if ((needed & (1u << c)) && channelEnabled(t, Channel(c))) imgs[c] = &channelImage(t, Channel(c));
    poolFor(size_t(t.height), [&](size_t b, size_t e) {
        float vals[4];
        for (int y = int(b); y < int(e); ++y)
            for (int x = 0; x < t.width; ++x) {
                float chan[kChannelCount][3];
                glm::vec3 n(0, 0, 1);
                if (needNormal) n = effectiveNormalAt(t, x, y);
                const uint8_t* px = &t.pixels[(size_t(y) * size_t(t.width) + size_t(x)) * 4];
                for (size_t k = 0; k < srcs.size(); ++k) {
                    const MapSource& s = srcs[k];
                    float v = s.constant;
                    if (s.kind == MapSource::Kind::Channel) {
                        if (s.channel == int(Channel::BaseColor)) {
                            v = float(px[std::clamp(s.component, 0, 2)]) / 255.0f;
                        } else if (s.channel == int(Channel::Normal)) {
                            float c = n[std::clamp(s.component, 0, 2)];
                            if (s.component == 1 && directX) c = -c;
                            v = c * 0.5f + 0.5f;
                        } else if (const TileImage* img = imgs[s.channel]) {
                            img->values(x, y, chan[s.channel]);
                            v = chan[s.channel][std::clamp(s.component, 0, img->components() - 1)];
                        } else {
                            v = channelFormat(s.channel).defaultValue[size_t(std::clamp(s.component, 0, 2))];
                        }
                    }
                    vals[k] = std::clamp(s.invert ? 1.0f - v : v, 0.0f, 1.0f);
                }
                fn(x, y, vals);
            }
    }, 8, 2);
}

} // namespace

bool parseMapSource(const std::string& textIn, MapSource& out, std::string& error) {
    out = MapSource();
    std::string text;
    for (char c : textIn)
        if (!std::isspace(static_cast<unsigned char>(c))) text += char(std::tolower(static_cast<unsigned char>(c)));
    if (!text.empty() && text.back() == '?') {
        out.dropIfMissing = true;
        text.pop_back();
    }
    if (text.rfind("1-", 0) == 0) {
        out.invert = true;
        text = text.substr(2);
    }
    if (text.empty()) {
        error = "Empty map source";
        return false;
    }
    if (std::isdigit(static_cast<unsigned char>(text[0])) || text[0] == '.') {
        char* end = nullptr;
        const float v = std::strtof(text.c_str(), &end);
        if (end && *end == '\0' && std::isfinite(v)) {
            out.kind = MapSource::Kind::Constant;
            out.constant = std::clamp(v, 0.0f, 1.0f);
            if (out.invert) out.constant = 1.0f - out.constant, out.invert = false;
            return true;
        }
    }
    if (text == "smoothness" || text == "gloss" || text == "glossiness") {
        out.kind = MapSource::Kind::Channel;
        out.channel = int(Channel::Roughness);
        out.invert = !out.invert;
        return true;
    }
    std::string name = text, comp;
    if (const size_t dot = text.find('.'); dot != std::string::npos) {
        name = text.substr(0, dot);
        comp = text.substr(dot + 1);
    }
    Channel c;
    if (!parseChannel(name, c)) {
        error = "Unknown map source '" + textIn + "'";
        return false;
    }
    out.kind = MapSource::Kind::Channel;
    out.channel = int(c);
    if (!comp.empty()) {
        static const char* names[] = {"r", "g", "b"};
        int found = -1;
        for (int i = 0; i < 3; ++i)
            if (comp == names[i] || comp == std::string(1, "xyz"[i])) found = i;
        if (comp == "a" && c == Channel::BaseColor) { // base colour's alpha is the opacity channel
            out.channel = int(Channel::Opacity);
            found = 0;
        }
        if (found < 0 || (found > 0 && channelFormat(out.channel).components < 3 && out.channel != int(Channel::BaseColor))) {
            error = "Unknown component in map source '" + textIn + "'";
            return false;
        }
        out.component = found;
    }
    return true;
}

bool parseExportPreset(const std::string& json, ExportPreset& out, std::string& error) {
    Json j;
    if (!Json::parse(json, j, error) || !j.isObject()) {
        error = "Export preset is not a JSON object" + (error.empty() ? "" : " (" + error + ")");
        return false;
    }
    out = ExportPreset();
    out.id = j["id"].asString();
    out.label = j["label"].isString() ? j["label"].asString() : out.id;
    out.description = j["description"].asString();
    out.file = j["file"].isString() ? j["file"].asString() : std::string("{name}_{map}");
    if (out.file.find("{map}") == std::string::npos) {
        error = "Export preset 'file' must contain {map}";
        return false;
    }
    const std::string normal = lower(j["normal"].isString() ? j["normal"].asString() : std::string("opengl"));
    if (normal != "opengl" && normal != "directx") {
        error = "Export preset 'normal' must be opengl or directx";
        return false;
    }
    out.normalDirectX = normal == "directx";
    if (!j["maps"].isArray() || j["maps"].size() == 0 || j["maps"].size() > 32) {
        error = "Export preset needs a list of 1-32 maps";
        return false;
    }
    for (size_t i = 0; i < j["maps"].size(); ++i) {
        const Json& mj = j["maps"][i];
        MapSpec m;
        m.map = mj["map"].asString();
        if (m.map.empty() || safeName(m.map) != m.map) {
            error = "Export preset map " + std::to_string(i + 1) + " needs a 'map' name of letters, digits, - and _";
            return false;
        }
        const size_t n = mj["channels"].size();
        if (!mj["channels"].isArray() || (n != 1 && n != 3 && n != 4)) {
            error = "Map '" + m.map + "' needs 1, 3 or 4 channels";
            return false;
        }
        for (size_t k = 0; k < n; ++k) {
            MapSource s;
            if (!parseMapSource(mj["channels"][k].asString(), s, error)) return false;
            m.sources.push_back(s);
        }
        m.bits = int(mj["bits"].asNumber(8));
        if (m.bits != 8 && m.bits != 16) {
            error = "Map '" + m.map + "': bits must be 8 or 16";
            return false;
        }
        m.optional = mj["optional"].asBool(false);
        out.maps.push_back(std::move(m));
    }
    return true;
}

const std::vector<ExportPreset>& builtinExportPresets() {
    static const std::vector<ExportPreset> presets = [] {
        std::vector<ExportPreset> v;
        for (const char* text : kBuiltinPresets) {
            ExportPreset p;
            std::string err;
            if (parseExportPreset(text, p, err)) v.push_back(std::move(p));
        }
        return v;
    }();
    return presets;
}

bool findExportPreset(const std::string& idOrPath, ExportPreset& out, std::string& error) {
    std::string id = lower(idOrPath);
    if (id == "ue" || id == "unreal_engine") id = "unreal";
    if (id == "unity") id = "unity_urp";
    if (id == "urp") id = "unity_urp";
    if (id == "hdrp") id = "unity_hdrp";
    if (id == "gltf2" || id == "glb") id = "gltf";
    for (const ExportPreset& p : builtinExportPresets())
        if (p.id == id) {
            out = p;
            return true;
        }
    std::ifstream f(fsPath(idOrPath), std::ios::binary);
    if (!f) {
        std::string ids;
        for (const ExportPreset& p : builtinExportPresets()) ids += (ids.empty() ? "" : ", ") + p.id;
        error = "No export preset '" + idOrPath + "' (built-in: " + ids + ", or a preset .json file)";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (!parseExportPreset(ss.str(), out, error)) return false;
    if (out.id.empty()) out.id = utf8(fsPath(idOrPath).stem());
    return true;
}

bool mapWanted(const PaintTexture& t, const MapSpec& map) {
    if (!map.optional) return true;
    for (const MapSource& s : map.sources)
        if (s.kind == MapSource::Kind::Channel && s.channel != int(Channel::BaseColor) && channelEnabled(t, Channel(s.channel)))
            return true;
    return false;
}

bool renderMap(const PaintTexture& t, const MapSpec& map, bool directX, Image& out, int& channels) {
    if (t.width <= 0 || t.pixels.size() != t.texelCount() * 4) return false;
    const std::vector<MapSource> srcs = keptSources(t, map);
    channels = int(srcs.size());
    out.width = t.width;
    out.height = t.height;
    out.rgba.assign(t.texelCount() * 4, 255);
    evaluate(t, srcs, directX, [&](int x, int y, const float* v) {
        uint8_t* o = &out.rgba[(size_t(t.height - 1 - y) * size_t(t.width) + size_t(x)) * 4];
        auto b = [](float f) { return uint8_t(std::lround(f * 255.0f)); };
        if (channels == 1) {
            o[0] = o[1] = o[2] = b(v[0]);
        } else {
            o[0] = b(v[0]), o[1] = b(v[1]), o[2] = b(v[2]);
            if (channels == 4) o[3] = b(v[3]);
        }
    });
    return true;
}

bool renderMap16(const PaintTexture& t, const MapSpec& map, bool directX, std::vector<uint16_t>& out, int& channels) {
    if (t.width <= 0 || t.pixels.size() != t.texelCount() * 4) return false;
    const std::vector<MapSource> srcs = keptSources(t, map);
    channels = int(srcs.size());
    out.assign(t.texelCount() * size_t(channels), 0);
    evaluate(t, srcs, directX, [&](int x, int y, const float* v) {
        uint16_t* o = &out[(size_t(t.height - 1 - y) * size_t(t.width) + size_t(x)) * size_t(channels)];
        for (int k = 0; k < channels; ++k) o[k] = uint16_t(std::lround(v[k] * 65535.0f));
    });
    return true;
}

std::string mapFileName(const ExportPreset& preset, const MapSpec& map, const std::string& name) {
    std::string f = preset.file;
    auto put = [&](const std::string& key, const std::string& value) {
        for (size_t at = f.find(key); at != std::string::npos; at = f.find(key, at + value.size())) f.replace(at, key.size(), value);
    };
    put("{name}", safeName(name));
    put("{map}", map.map);
    return safeName(f) + ".png";
}

bool exportMaps(const PaintTexture& t, const ExportPreset& preset, const std::string& folder, const std::string& name,
                std::vector<ExportedMap>& written, std::string& error) {
    written.clear();
    std::error_code ec;
    std::filesystem::create_directories(fsPath(folder), ec);
    if (!std::filesystem::is_directory(fsPath(folder))) {
        error = "Cannot create the folder " + folder;
        return false;
    }
    // PNG compression is single-threaded and dominates at 4K / 8K: maps are written on up to four threads at once.
    std::vector<const MapSpec*> maps;
    for (const MapSpec& map : preset.maps)
        if (mapWanted(t, map)) maps.push_back(&map);
    std::vector<std::string> paths(maps.size()), errors(maps.size());
    std::vector<char> ok(maps.size(), 0);
    auto writeOne = [&](size_t i) {
        const MapSpec& map = *maps[i];
        paths[i] = utf8(fsPath(folder) / fsPath(mapFileName(preset, map, name)));
        int channels = 0;
        if (map.bits == 16) {
            std::vector<uint16_t> px;
            ok[i] = renderMap16(t, map, preset.normalDirectX, px, channels) && savePng16(paths[i], t.width, t.height, channels, px, errors[i]);
        } else {
            Image img;
            ok[i] = renderMap(t, map, preset.normalDirectX, img, channels) && img.savePng(paths[i], errors[i]);
        }
    };
    constexpr size_t kAtOnce = 4;
    for (size_t first = 0; first < maps.size(); first += kAtOnce) {
        std::vector<std::thread> threads;
        for (size_t i = first + 1; i < std::min(maps.size(), first + kAtOnce); ++i) threads.emplace_back(writeOne, i);
        writeOne(first);
        for (auto& th : threads) th.join();
    }
    for (size_t i = 0; i < maps.size(); ++i) {
        if (!ok[i]) {
            error = errors[i].empty() ? "Cannot write " + paths[i] : errors[i];
            return false;
        }
        written.push_back({maps[i]->map, paths[i]});
    }
    return true;
}

bool encodeMapPng(const PaintTexture& t, const std::vector<std::string>& sources, bool directX, std::vector<uint8_t>& png) {
    MapSpec spec;
    spec.map = "map";
    for (const std::string& s : sources) {
        MapSource src;
        std::string err;
        if (!parseMapSource(s, src, err)) return false;
        spec.sources.push_back(src);
    }
    Image img;
    int channels = 0;
    return renderMap(t, spec, directX, img, channels) && img.encodePng(png);
}

std::vector<std::string> pbrSlots(const PaintTexture& t, bool packedOrm) {
    std::vector<std::string> out;
    if (!hasMaterialChannels(t)) return out;
    out.push_back("base_color");
    if (channelEnabled(t, Channel::Normal) || channelEnabled(t, Channel::Height)) out.push_back("normal");
    if (packedOrm) {
        out.push_back("orm");
    } else {
        out.push_back("roughness");
        out.push_back("metallic");
        if (channelEnabled(t, Channel::AO)) out.push_back("occlusion");
    }
    if (channelEnabled(t, Channel::Emissive)) out.push_back("emissive");
    if (!packedOrm && channelEnabled(t, Channel::Opacity)) out.push_back("opacity");
    return out;
}

std::vector<std::string> pbrSlotSources(const std::string& slot) {
    if (slot == "base_color") return {"base_color.r", "base_color.g", "base_color.b", "opacity?"};
    if (slot == "normal") return {"normal.r", "normal.g", "normal.b"};
    if (slot == "orm") return {"ao", "roughness", "metallic"};
    if (slot == "roughness") return {"roughness"};
    if (slot == "metallic") return {"metallic"};
    if (slot == "occlusion") return {"ao"};
    if (slot == "emissive") return {"emissive.r", "emissive.g", "emissive.b"};
    if (slot == "opacity") return {"opacity"};
    return {};
}

void registerMapExportCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"export_maps",
             "Export the active Part's texture channels as image files for an engine or DCC, using an export "
             "preset: unreal (T_<name>_BC / _N in DirectX / _ORM), unity_urp, unity_hdrp (mask map), gltf, godot, "
             "blender (one file per channel), usd, or your own preset .json file (docs/guide/export-presets.md). "
             "list=true lists the presets.",
             {{"preset", T::String, "Preset id or a preset .json path. Default gltf.", false},
              {"folder", T::String, "Output folder (created if missing).", false},
              {"name", T::String, "Object name in the file names. Default: the Part's name.", false},
              {"list", T::Bool, "List the built-in presets instead.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (argBool(a, "list", false)) {
                     std::string s = "Export presets:";
                     for (const ExportPreset& p : builtinExportPresets()) {
                         s += "\n  " + p.id + " (" + p.label + "):";
                         for (const MapSpec& m : p.maps) s += " " + m.map + (m.optional ? "?" : "");
                     }
                     return {true, s};
                 }
                 if (!hasTexture(doc.mesh))
                     return {false, "The Part has no texture: create one first (Tool > Texture Map > New, or texture_create)"};
                 const std::string folder = argString(a, "folder");
                 if (folder.empty()) return {false, "Give a folder"};
                 ExportPreset preset;
                 std::string err;
                 if (!findExportPreset(argString(a, "preset", "gltf"), preset, err)) return {false, err};
                 std::string name = argString(a, "name");
                 if (name.empty() && doc.activeSubtool >= 0 && doc.activeSubtool < doc.subtoolCount())
                     name = doc.subtools[size_t(doc.activeSubtool)].name;
                 if (name.empty()) name = "Sculpt";
                 std::vector<ExportedMap> written;
                 if (!exportMaps(*doc.mesh.texture, preset, folder, name, written, err)) return {false, err};
                 std::string files;
                 for (const ExportedMap& m : written) files += "\n  " + m.path;
                 return {true, "Exported " + std::to_string(written.size()) + " " + preset.label + " maps (" +
                                   std::to_string(doc.mesh.texture->width) + "x" + std::to_string(doc.mesh.texture->height) + "):" + files};
             }});
}

} // namespace sf
