// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/EnginePresets.h"

#include "core/Decimate.h"
#include "core/Parts.h"
#include "io/FbxIO.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace sf {

namespace {

// Unreal asset names: letters, digits and underscores.
std::string unrealName(const std::string& text) {
    std::string out;
    for (unsigned char c : text) out += std::isalnum(c) ? char(c) : '_';
    return out.empty() ? std::string("Sculpt") : out;
}

} // namespace

bool exportForUnreal(const Document& doc, const UnrealExportOptions& o, UnrealExportResult& r, std::string& error) {
    const std::vector<MeshObjectRef> objects = visiblePartObjects(doc);
    if (objects.empty()) {
        error = "The mesh is empty";
        return false;
    }
    // Morph targets only exist on skeletal meshes in Unreal; a skeletal mesh can't use an FBX LOD group
    // the same way, so layered sculpts (any visible Part with layers) export LOD0 only.
    r.skeletal = false;
    AABB bounds;
    for (const auto& obj : objects) {
        r.skeletal |= !obj.mesh->layers.empty();
        bounds.expand(obj.mesh->bounds());
    }
    r.assetName = !o.assetName.empty() ? unrealName(o.assetName) : (r.skeletal ? "SK_" : "SM_") + unrealName(doc.assetName());
    const float height = bounds.extent().y;
    r.scale = o.heightCm > 0.0f && height > 0.0f ? o.heightCm / height : 100.0f;

    // Every visible Part is its own FBX model (Unreal: one material section each with "Combine Meshes",
    // or one asset each without). Static meshes get a LOD group per Part, each decimated on its own, so
    // small parts (eyes, teeth) keep their share of triangles; Unreal's combined import gathers level n of
    // every group into its LOD n.
    std::vector<std::vector<Mesh>> lods(objects.size());
    std::vector<FbxObject> fbxObjects;
    for (size_t i = 0; i < objects.size(); ++i) {
        FbxObject fo{objects.size() == 1 ? r.assetName : unrealName(objects[i].name), {objects[i].mesh}};
        if (!r.skeletal && o.lods > 1) {
            lods[i] = generateLods(*objects[i].mesh, std::clamp(o.lods, 2, 8), std::clamp(o.ratio, 0.1f, 0.9f));
            fo.lods.clear();
            for (const auto& l : lods[i]) fo.lods.push_back(&l);
        }
        fbxObjects.push_back(std::move(fo));
    }
    r.lodCount = int(fbxObjects.front().lods.size());

    std::filesystem::path folder(std::u8string(o.folder.begin(), o.folder.end()));
    if (folder.empty()) folder = std::filesystem::temp_directory_path();
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto file = (folder / std::u8string(reinterpret_cast<const char8_t*>(r.assetName.data()), r.assetName.size())).u8string() + u8".fbx";
    r.path.assign(file.begin(), file.end());

    FbxExportOptions fo;
    fo.scale = r.scale;
    return saveFbx(r.path, fbxObjects, fo, error);
}

} // namespace sf
