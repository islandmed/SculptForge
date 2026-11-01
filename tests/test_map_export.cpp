// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Map export (core/MapExport.h): the preset format, every built-in preset's files and channel packing
// (Unreal BC / N in DirectX / ORM, Unity URP and HDRP mask maps, glTF / Godot ORM, Blender and USD separate
// maps), optional maps, 16-bit height, user preset files and the export_maps command.
#include "TestHarness.h"

#include "core/BakeImage.h"
#include "core/Commands.h"
#include "core/MapExport.h"
#include "core/Primitives.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"
#include "link/UnrealLink.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace sf;

namespace {

namespace fs = std::filesystem;

fs::path freshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir;
}

// A 64 texture: base colour (0.8, 0.4, 0.2) display, roughness 0.25, metallic 1, AO 0.5, a height ramp along x,
// and a normal tilted towards +y.
std::shared_ptr<PaintTexture> sampleTexture(bool height = true) {
    auto t = makeTexture(64, 64);
    for (size_t i = 0; i < t->pixels.size(); i += 4) t->pixels[i] = 204, t->pixels[i + 1] = 102, t->pixels[i + 2] = 51;
    for (Channel c : {Channel::Roughness, Channel::Metallic, Channel::AO, Channel::Normal}) setChannelEnabled(*t, c, true);
    if (height) setChannelEnabled(*t, Channel::Height, true);
    t->heightNormalStrength = 0.0f; // the ramp shouldn't tilt the normals checked below
    const glm::vec3 n = glm::normalize(glm::vec3(0, 0.5f, 1));
    const float r[1] = {0.25f}, m[1] = {1.0f}, ao[1] = {0.5f}, nv[3] = {n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f, n.z * 0.5f + 0.5f};
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            t->channels[size_t(Channel::Roughness)].base.setValues(x, y, r);
            t->channels[size_t(Channel::Metallic)].base.setValues(x, y, m);
            t->channels[size_t(Channel::AO)].base.setValues(x, y, ao);
            t->channels[size_t(Channel::Normal)].base.setValues(x, y, nv);
            if (height) {
                const float h[1] = {float(x) / 63.0f};
                t->channels[size_t(Channel::Height)].base.setValues(x, y, h);
            }
        }
    return t;
}

int px(const Image& img, int x, int y, int c) { return img.rgba[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + size_t(c)]; }
bool near(int a, int b, int tol = 1) { return std::abs(a - b) <= tol; }

} // namespace

TEST(map_export_sources_parse) {
    MapSource s;
    std::string err;
    CHECK(parseMapSource("roughness", s, err) && s.kind == MapSource::Kind::Channel && s.channel == int(Channel::Roughness));
    CHECK(parseMapSource("smoothness", s, err) && s.channel == int(Channel::Roughness) && s.invert);
    CHECK(parseMapSource("1-metallic", s, err) && s.invert && s.channel == int(Channel::Metallic));
    CHECK(parseMapSource("base_color.g", s, err) && s.channel == 0 && s.component == 1);
    CHECK(parseMapSource("base_color.a", s, err) && s.channel == int(Channel::Opacity));
    CHECK(parseMapSource("normal.b", s, err) && s.component == 2);
    CHECK(parseMapSource("opacity?", s, err) && s.dropIfMissing);
    CHECK(parseMapSource("0.5", s, err) && s.kind == MapSource::Kind::Constant && s.constant == 0.5f);
    CHECK(parseMapSource("1-0.25", s, err) && s.constant == 0.75f);
    CHECK(!parseMapSource("sparkle", s, err) && !err.empty());
    CHECK(!parseMapSource("roughness.g", s, err));
    CHECK(!parseMapSource("", s, err));
}

TEST(map_export_builtin_presets) {
    const auto& presets = builtinExportPresets();
    CHECK(presets.size() == 7);
    for (const char* id : {"unreal", "unity_urp", "unity_hdrp", "gltf", "godot", "blender", "usd"}) {
        ExportPreset p;
        std::string err;
        CHECK(findExportPreset(id, p, err));
        CHECK(p.id == id && !p.maps.empty() && !p.label.empty());
    }
    ExportPreset p;
    std::string err;
    CHECK(findExportPreset("UE", p, err) && p.id == "unreal" && p.normalDirectX);
    CHECK(findExportPreset("hdrp", p, err) && p.id == "unity_hdrp");
    CHECK(!findExportPreset("no_such_preset", p, err) && err.find("unreal") != std::string::npos);
}

TEST(map_export_unreal_packs_bc_n_orm) {
    auto t = sampleTexture();
    ExportPreset unreal, gltf;
    std::string err;
    CHECK(findExportPreset("unreal", unreal, err) && findExportPreset("gltf", gltf, err));
    const fs::path dir = freshDir("sf_map_export_unreal");
    std::vector<ExportedMap> files;
    CHECK(exportMaps(*t, unreal, dir.string(), "Rock", files, err));
    // BC, N, ORM and H (height is on); E is optional and emissive is off.
    CHECK(files.size() == 4);
    CHECK(fs::exists(dir / "T_Rock_BC.png") && fs::exists(dir / "T_Rock_N.png") && fs::exists(dir / "T_Rock_ORM.png"));
    CHECK(fs::exists(dir / "T_Rock_H.png") && !fs::exists(dir / "T_Rock_E.png"));
    Image bc, n, orm;
    CHECK(Image::load((dir / "T_Rock_BC.png").string(), bc, err));
    CHECK(near(px(bc, 5, 5, 0), 204) && near(px(bc, 5, 5, 1), 102) && px(bc, 5, 5, 3) == 255);
    CHECK(Image::load((dir / "T_Rock_ORM.png").string(), orm, err));
    CHECK(near(px(orm, 3, 3, 0), 128) && near(px(orm, 3, 3, 1), 64) && near(px(orm, 3, 3, 2), 255));
    // DirectX normal: green flipped compared with glTF's OpenGL normal.
    CHECK(Image::load((dir / "T_Rock_N.png").string(), n, err));
    Image glN;
    int ch = 0;
    CHECK(renderMap(*t, gltf.maps[1], gltf.normalDirectX, glN, ch) && ch == 3);
    CHECK(px(n, 7, 7, 1) < 128 && px(glN, 7, 7, 1) > 128);
    CHECK(near(px(n, 7, 7, 1) + px(glN, 7, 7, 1), 255, 2));
    CHECK(near(px(n, 7, 7, 2), px(glN, 7, 7, 2)));
    // Height: 16-bit grey, the ramp kept to 16-bit precision.
    int w = 0, h = 0, c = 0;
    std::vector<uint16_t> hp;
    CHECK(loadPng16((dir / "T_Rock_H.png").string(), w, h, c, hp, err));
    CHECK(w == 64 && h == 64 && c == 1);
    CHECK(hp[0] == 0 && hp[63] == 65535);
    CHECK(std::abs(int(hp[31]) - int(std::lround(31.0 / 63.0 * 65535.0))) <= 2);
    fs::remove_all(dir);
}

TEST(map_export_unity_godot_blender_usd) {
    auto t = sampleTexture(false);
    std::string err;
    const fs::path dir = freshDir("sf_map_export_engines");
    auto run = [&](const char* id, std::vector<ExportedMap>& files) {
        ExportPreset p;
        CHECK(findExportPreset(id, p, err));
        CHECK(exportMaps(*t, p, (dir / id).string(), "Prop", files, err));
    };
    std::vector<ExportedMap> files;
    // URP: metallic in R, smoothness (1 - roughness) in A.
    run("unity_urp", files);
    Image ms;
    CHECK(Image::load((dir / "unity_urp" / "Prop_MetallicSmoothness.png").string(), ms, err));
    CHECK(near(px(ms, 1, 1, 0), 255) && near(px(ms, 1, 1, 3), 191));
    CHECK(fs::exists(dir / "unity_urp" / "Prop_Occlusion.png"));      // AO is on
    CHECK(!fs::exists(dir / "unity_urp" / "Prop_Height.png"));        // height is off
    // HDRP mask map: metallic, AO, detail mask (white), smoothness.
    run("unity_hdrp", files);
    Image mask;
    CHECK(Image::load((dir / "unity_hdrp" / "Prop_MaskMap.png").string(), mask, err));
    CHECK(near(px(mask, 2, 2, 0), 255) && near(px(mask, 2, 2, 1), 128) && px(mask, 2, 2, 2) == 255 && near(px(mask, 2, 2, 3), 191));
    // Godot ORM.
    run("godot", files);
    CHECK(files.size() == 3 && fs::exists(dir / "godot" / "Prop_orm.png"));
    // Blender: one file per channel, optional ones only when enabled.
    run("blender", files);
    CHECK(fs::exists(dir / "blender" / "Prop_Roughness.png") && fs::exists(dir / "blender" / "Prop_AO.png"));
    CHECK(!fs::exists(dir / "blender" / "Prop_Emission.png") && !fs::exists(dir / "blender" / "Prop_Alpha.png"));
    Image rough;
    CHECK(Image::load((dir / "blender" / "Prop_Roughness.png").string(), rough, err));
    CHECK(near(px(rough, 0, 0, 0), 64) && px(rough, 0, 0, 0) == px(rough, 0, 0, 2));
    // USD names follow the UsdPreviewSurface inputs.
    run("usd", files);
    CHECK(fs::exists(dir / "usd" / "Prop_diffuseColor.png") && fs::exists(dir / "usd" / "Prop_occlusion.png"));
    // A texture without material channels still exports, with the defaults (roughness 0.65, metallic 0).
    auto plain = makeTexture(16, 16);
    ExportPreset gltf;
    CHECK(findExportPreset("gltf", gltf, err));
    CHECK(exportMaps(*plain, gltf, (dir / "plain").string(), "P", files, err));
    Image orm;
    CHECK(Image::load((dir / "plain" / "P_occlusionRoughnessMetallic.png").string(), orm, err));
    CHECK(px(orm, 0, 0, 0) == 255 && near(px(orm, 0, 0, 1), 166) && px(orm, 0, 0, 2) == 0);
    fs::remove_all(dir);
}

TEST(map_export_opacity_goes_into_alpha_only_when_painted) {
    auto t = sampleTexture(false);
    ExportPreset unreal;
    std::string err;
    CHECK(findExportPreset("unreal", unreal, err));
    Image bc;
    int ch = 0;
    CHECK(renderMap(*t, unreal.maps[0], true, bc, ch) && ch == 3);
    setChannelEnabled(*t, Channel::Opacity, true);
    const float a[1] = {0.25f};
    t->channels[size_t(Channel::Opacity)].base.setValues(0, 63, a); // top row of the texture = first image row
    CHECK(renderMap(*t, unreal.maps[0], true, bc, ch) && ch == 4);
    CHECK(near(px(bc, 0, 0, 3), 64) && px(bc, 1, 0, 3) == 255);
}

TEST(map_export_user_preset_files) {
    const fs::path dir = freshDir("sf_map_export_user");
    fs::create_directories(dir);
    const fs::path presetPath = dir / "studio.json";
    {
        std::ofstream f(presetPath);
        f << R"({"label": "Studio", "file": "{name}-{map}", "normal": "directx",
                 "maps": [{"map": "rma", "channels": ["roughness", "metallic", "ao"]},
                          {"map": "gloss", "channels": ["1-roughness"]}]})";
    }
    ExportPreset p;
    std::string err;
    CHECK(findExportPreset(presetPath.string(), p, err));
    CHECK(p.id == "studio" && p.maps.size() == 2 && p.normalDirectX);
    auto t = sampleTexture(false);
    std::vector<ExportedMap> files;
    CHECK(exportMaps(*t, p, (dir / "out").string(), "My Rock", files, err));
    CHECK(fs::exists(dir / "out" / "My_Rock-rma.png") && fs::exists(dir / "out" / "My_Rock-gloss.png"));
    // Broken presets say what is wrong.
    auto bad = [&](const char* json) {
        ExportPreset q;
        std::string why;
        return !parseExportPreset(json, q, why) && !why.empty();
    };
    CHECK(bad("not json"));
    CHECK(bad(R"({"maps": []})"));
    CHECK(bad(R"({"file": "{name}", "maps": [{"map": "a", "channels": ["roughness"]}]})"));
    CHECK(bad(R"({"maps": [{"map": "a", "channels": ["roughness", "metallic"]}]})"));
    CHECK(bad(R"({"maps": [{"map": "a/b", "channels": ["roughness"]}]})"));
    CHECK(bad(R"({"maps": [{"map": "a", "channels": ["glitter"]}]})"));
    CHECK(bad(R"({"maps": [{"map": "a", "channels": ["height"], "bits": 12}]})"));
    CHECK(bad(R"({"normal": "sideways", "maps": [{"map": "a", "channels": ["height"]}]})"));
    fs::remove_all(dir);
}

TEST(map_export_command) {
    Mesh m = makePlane(4);
    m.uvs.assign(m.triangleCount() * 3, glm::vec2(0.5f));
    m.texture = makeTexture(1024, 1024);
    m.paintToTexture = true;
    Document doc = makeDoc(std::move(m));
    CommandRegistry reg;
    const fs::path dir = freshDir("sf_map_export_cmd");
    CHECK(reg.execute(doc, "texture_channels", {{"enable", std::string("roughness,metallic,normal")}}).ok);
    const CommandResult r = reg.execute(doc, "export_maps", {{"preset", std::string("unreal")}, {"folder", dir.string()}, {"name", std::string("Lion")}});
    CHECK(r.ok);
    CHECK(r.message.find("3 Unreal Engine maps") != std::string::npos);
    CHECK(fs::exists(dir / "T_Lion_BC.png") && fs::exists(dir / "T_Lion_N.png") && fs::exists(dir / "T_Lion_ORM.png"));
    CHECK(reg.execute(doc, "export_maps", {{"list", true}}).message.find("unity_hdrp") != std::string::npos);
    CHECK(!reg.execute(doc, "export_maps", {{"preset", std::string("nope")}, {"folder", dir.string()}}).ok);
    CHECK(!reg.execute(doc, "export_maps", {}).ok);
    Document plain = makeDoc(makePlane(2));
    CHECK(!reg.execute(plain, "export_maps", {{"folder", dir.string()}}).ok);
    fs::remove_all(dir);
}

TEST(map_export_unreal_bridge_builds_a_material) {
    // addUnrealMaterialMaps exports T_<name>_BC / _N / _ORM and the import script brings them in and builds M_<name>.
    auto t = sampleTexture(false);
    const fs::path dir = freshDir("sf_map_export_bridge");
    link::UnrealImport im;
    im.fbxPath = "/tmp/SM_Lion.fbx";
    im.assetName = "SM_Lion";
    std::string err;
    CHECK(link::addUnrealMaterialMaps(*t, dir.string(), im, err));
    CHECK(im.buildMaterial && im.normalMapDirectX);
    CHECK(fs::exists(im.baseColorPath) && fs::exists(im.ormPath) && fs::exists(im.normalMapPath));
    CHECK(fs::path(im.ormPath).filename() == "T_Lion_ORM.png");
    CHECK(im.emissivePath.empty());
    const std::string py = link::unrealImportScript(im);
    CHECK(py.find("'T_Lion_BC'") != std::string::npos && py.find("'T_Lion_ORM'") != std::string::npos);
    CHECK(py.find("'T_Lion_N'") != std::string::npos);
    CHECK(py.find("TC_MASKS") != std::string::npos && py.find("flip_green_channel', False") != std::string::npos);
    CHECK(py.find("'/Game/SculptForge/M_Lion'") != std::string::npos);
    CHECK(py.find("MP_ROUGHNESS") != std::string::npos && py.find("MP_AMBIENT_OCCLUSION") != std::string::npos &&
          py.find("MP_METALLIC") != std::string::npos && py.find("MP_NORMAL") != std::string::npos);
    CHECK(py.find("SCULPTFORGE_IMPORTED") < py.find("SCULPTFORGE_MATERIAL"));
    // A remote editor can't read our files: no maps, no material.
    im.downloadUrl = "http://10.0.0.5:5000/t/SM_Lion.fbx";
    CHECK(link::unrealImportScript(im).find("T_Lion_ORM") == std::string::npos);
    CHECK(link::unrealImportScript(im).find("SCULPTFORGE_MATERIAL") == std::string::npos);
    // No material channels: nothing added.
    link::UnrealImport plain;
    auto colourOnly = makeTexture(16, 16);
    CHECK(link::addUnrealMaterialMaps(*colourOnly, dir.string(), plain, err));
    CHECK(!plain.buildMaterial && plain.baseColorPath.empty());
    fs::remove_all(dir);
}
