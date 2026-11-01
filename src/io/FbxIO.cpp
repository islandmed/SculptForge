// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/FbxIO.h"

#include "io/TextureExport.h"

#include "ufbx.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <filesystem>
#include <fstream>
#include <variant>

namespace sf {

namespace {

// ---------------------------------------------------------------- binary node tree

struct Property {
    char type;
    std::vector<uint8_t> bytes; // encoded payload (after the type code)
};

struct Node {
    std::string name;
    std::vector<Property> props;
    std::vector<Node> children;

    Node(std::string n = {}) : name(std::move(n)) {}

    template <class T> static void put(std::vector<uint8_t>& out, T v) {
        uint8_t b[sizeof(T)];
        std::memcpy(b, &v, sizeof(T)); // FBX is little-endian; so are all platforms we ship on
        out.insert(out.end(), b, b + sizeof(T));
    }
    Node& i16(int16_t v) { return scalar('Y', v); }
    Node& b(bool v) { return scalar('C', uint8_t(v ? 1 : 0)); }
    Node& i32(int32_t v) { return scalar('I', v); }
    Node& f64(double v) { return scalar('D', v); }
    Node& f32(float v) { return scalar('F', v); }
    Node& i64(int64_t v) { return scalar('L', v); }
    Node& str(const std::string& s) {
        Property p{'S', {}};
        put(p.bytes, uint32_t(s.size()));
        p.bytes.insert(p.bytes.end(), s.begin(), s.end());
        props.push_back(std::move(p));
        return *this;
    }
    // "Name\x00\x01Class": how objects carry their name and class in one string.
    Node& objName(const std::string& n, const std::string& cls) { return str(n + std::string("\x00\x01", 2) + cls); }
    Node& raw(const std::vector<uint8_t>& data) {
        Property p{'R', {}};
        put(p.bytes, uint32_t(data.size()));
        p.bytes.insert(p.bytes.end(), data.begin(), data.end());
        props.push_back(std::move(p));
        return *this;
    }
    template <class T> Node& array(char type, const std::vector<T>& v) {
        Property p{type, {}};
        put(p.bytes, uint32_t(v.size()));
        put(p.bytes, uint32_t(0)); // not compressed
        put(p.bytes, uint32_t(v.size() * sizeof(T)));
        const auto* d = reinterpret_cast<const uint8_t*>(v.data());
        p.bytes.insert(p.bytes.end(), d, d + v.size() * sizeof(T));
        props.push_back(std::move(p));
        return *this;
    }
    Node& f64s(const std::vector<double>& v) { return array('d', v); }
    Node& i32s(const std::vector<int32_t>& v) { return array('i', v); }

    Node& add(std::string childName) {
        children.emplace_back(std::move(childName));
        return children.back();
    }

    template <class T> Node& scalar(char type, T v) {
        Property p{type, {}};
        put(p.bytes, v);
        props.push_back(std::move(p));
        return *this;
    }

    // FBX 7.4 record: endOffset, propertyCount, propertyBytes (uint32), name length (uint8), name,
    // properties, children, and a 13-byte zero record closing the child list.
    // Null-record rule as in real-world files: always after children; for a bare node (no properties,
    // no children) unless it is the last in its list.
    void write(std::vector<uint8_t>& out, bool isLast = false) const {
        const size_t start = out.size();
        put(out, uint32_t(0)); // end offset, patched below
        put(out, uint32_t(props.size()));
        size_t propBytes = 0;
        for (const auto& p : props) propBytes += 1 + p.bytes.size();
        put(out, uint32_t(propBytes));
        out.push_back(uint8_t(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        for (const auto& p : props) {
            out.push_back(uint8_t(p.type));
            out.insert(out.end(), p.bytes.begin(), p.bytes.end());
        }
        if (!children.empty()) {
            for (size_t i = 0; i < children.size(); ++i) children[i].write(out, i + 1 == children.size());
            out.insert(out.end(), 13, 0);
        } else if (props.empty() && !isLast) {
            out.insert(out.end(), 13, 0);
        }
        const uint32_t end = uint32_t(out.size());
        std::memcpy(out.data() + start, &end, 4);
    }
};

// Properties70 entry: P: "Name", "Type", "Label", "Flags", values...
Node& prop70(Node& p70, const std::string& name, const std::string& type, const std::string& label,
             const std::string& flags) {
    return p70.add("P").str(name).str(type).str(label).str(flags);
}

// ---------------------------------------------------------------- scene

struct Ids {
    int64_t next = 1000000;
    int64_t operator()() { return next++; }
};

void addConnection(Node& connections, const char* kind, int64_t child, int64_t parent) {
    connections.add("C").str(kind).i64(child).i64(parent);
}
// An object connected to a property of its parent ("OP"), e.g. a texture to a material's DiffuseColor.
void addPropertyConnection(Node& connections, int64_t child, int64_t parent, const std::string& property) {
    connections.add("C").str("OP").i64(child).i64(parent).str(property);
}

void addHeader(Node& root) {
    Node& ext = root.add("FBXHeaderExtension");
    ext.add("FBXHeaderVersion").i32(1003);
    ext.add("FBXVersion").i32(7400);
    ext.add("EncryptionType").i32(0);
    Node& ts = ext.add("CreationTimeStamp");
    ts.add("Version").i32(1000);
    ts.add("Year").i32(1970);
    ts.add("Month").i32(1);
    ts.add("Day").i32(1);
    ts.add("Hour").i32(10);
    ts.add("Minute").i32(0);
    ts.add("Second").i32(0);
    ts.add("Millisecond").i32(0);
    ext.add("Creator").str("SculptForge");
    Node& info = ext.add("SceneInfo").objName("GlobalInfo", "SceneInfo").str("UserData");
    info.add("Type").str("UserData");
    info.add("Version").i32(100);
    Node& meta = info.add("MetaData");
    meta.add("Version").i32(100);
    meta.add("Title").str("");
    meta.add("Subject").str("");
    meta.add("Author").str("");
    meta.add("Keywords").str("");
    meta.add("Revision").str("");
    meta.add("Comment").str("");
    Node& p70 = info.add("Properties70");
    prop70(p70, "DocumentUrl", "KString", "Url", "").str("");
    prop70(p70, "SrcDocumentUrl", "KString", "Url", "").str("");
    prop70(p70, "Original", "Compound", "", "");
    prop70(p70, "Original|ApplicationVendor", "KString", "", "").str("SculptForge");
    prop70(p70, "Original|ApplicationName", "KString", "", "").str("SculptForge");
    prop70(p70, "Original|ApplicationVersion", "KString", "", "").str("0.1");

    // FileId and CreationTime must agree with the footer for some readers; these fixed
    // values are the ones widely used by open-source writers.
    static const uint8_t fileId[16] = {0x28, 0xb3, 0x2a, 0xeb, 0xb6, 0x24, 0xcc, 0xc2,
                                       0xbf, 0xc8, 0xb0, 0x2a, 0xa9, 0x2b, 0xfc, 0xf1};
    root.add("FileId").raw(std::vector<uint8_t>(fileId, fileId + 16));
    root.add("CreationTime").str("1970-01-01 10:00:00:000");
    root.add("Creator").str("SculptForge");
}

void addGlobalSettings(Node& root) {
    Node& gs = root.add("GlobalSettings");
    gs.add("Version").i32(1000);
    Node& p70 = gs.add("Properties70");
    prop70(p70, "UpAxis", "int", "Integer", "").i32(1);
    prop70(p70, "UpAxisSign", "int", "Integer", "").i32(1);
    prop70(p70, "FrontAxis", "int", "Integer", "").i32(2);
    prop70(p70, "FrontAxisSign", "int", "Integer", "").i32(1);
    prop70(p70, "CoordAxis", "int", "Integer", "").i32(0);
    prop70(p70, "CoordAxisSign", "int", "Integer", "").i32(1);
    prop70(p70, "OriginalUpAxis", "int", "Integer", "").i32(1);
    prop70(p70, "OriginalUpAxisSign", "int", "Integer", "").i32(1);
    prop70(p70, "UnitScaleFactor", "double", "Number", "").f64(1.0);
    prop70(p70, "OriginalUnitScaleFactor", "double", "Number", "").f64(1.0);
    prop70(p70, "AmbientColor", "ColorRGB", "Color", "").f64(0).f64(0).f64(0);
    prop70(p70, "DefaultCamera", "KString", "", "").str("Producer Perspective");
    prop70(p70, "TimeMode", "enum", "", "").i32(11);
    prop70(p70, "TimeSpanStart", "KTime", "Time", "").i64(0);
    prop70(p70, "TimeSpanStop", "KTime", "Time", "").i64(46186158000);
    prop70(p70, "CustomFrameRate", "double", "Number", "").f64(24.0);
}

// Geometry for one mesh: positions, polygons, per-corner normals, smoothing, one material.
Node makeGeometry(int64_t id, const std::string& name, const Mesh& mesh, float scale) {
    Node g("Geometry");
    g.i64(id).objName(name, "Geometry").str("Mesh");
    g.add("Properties70");
    g.add("GeometryVersion").i32(124);

    const auto& base = mesh.basePositions();
    std::vector<double> verts;
    verts.reserve(base.size() * 3);
    for (const auto& p : base) verts.insert(verts.end(), {double(p.x * scale), double(p.y * scale), double(p.z * scale)});
    g.add("Vertices").f64s(verts);

    // Polygons: quads where the mesh keeps them (Mesh::quadPartners), else triangles; the last corner is negated.
    // UVs per polygon corner (quads split by a UV seam along their diagonal are written as triangles).
    const bool uvs = hasCornerUvs(mesh);
    std::vector<int32_t> poly;
    std::vector<glm::vec2> cornerUvs;
    poly.reserve(mesh.triangleCount() * 3);
    size_t polygons = 0;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        glm::uvec4 f;
        glm::vec2 fuv[4];
        const int corners = exportFaceUv(mesh, t, f, fuv);
        if (corners == 0) continue;
        for (int c = 0; c < corners; ++c) poly.push_back(c + 1 < corners ? int32_t(f[c]) : ~int32_t(f[c]));
        if (uvs) cornerUvs.insert(cornerUvs.end(), fuv, fuv + corners);
        ++polygons;
    }
    g.add("PolygonVertexIndex").i32s(poly);

    // Normals of the base shape, per polygon corner.
    Mesh shape;
    shape.positions = base;
    shape.triangles = mesh.triangles;
    shape.rebuildTopology();
    shape.recomputeAllNormals();
    std::vector<double> normals;
    normals.reserve(poly.size() * 3);
    for (int32_t i : poly) {
        const glm::vec3& n = shape.normals[size_t(i < 0 ? ~i : i)];
        normals.insert(normals.end(), {double(n.x), double(n.y), double(n.z)});
    }
    Node& ln = g.add("LayerElementNormal").i32(0);
    ln.add("Version").i32(101);
    ln.add("Name").str("");
    ln.add("MappingInformationType").str("ByPolygonVertex");
    ln.add("ReferenceInformationType").str("Direct");
    ln.add("Normals").f64s(normals);

    Node& ls = g.add("LayerElementSmoothing").i32(0);
    ls.add("Version").i32(102);
    ls.add("Name").str("");
    ls.add("MappingInformationType").str("ByPolygon");
    ls.add("ReferenceInformationType").str("Direct");
    ls.add("Smoothing").i32s(std::vector<int32_t>(polygons, 1));

    if (uvs) {
        // Distinct UVs, indexed per polygon corner (as Blender and Maya write them).
        std::map<std::pair<float, float>, int32_t> index;
        std::vector<double> values;
        std::vector<int32_t> indices;
        indices.reserve(cornerUvs.size());
        for (const auto& uv : cornerUvs) {
            auto [it, inserted] = index.try_emplace({uv.x, uv.y}, int32_t(index.size()));
            if (inserted) values.insert(values.end(), {double(uv.x), double(uv.y)});
            indices.push_back(it->second);
        }
        Node& lu = g.add("LayerElementUV").i32(0);
        lu.add("Version").i32(101);
        lu.add("Name").str("UVMap");
        lu.add("MappingInformationType").str("ByPolygonVertex");
        lu.add("ReferenceInformationType").str("IndexToDirect");
        lu.add("UV").f64s(values);
        lu.add("UVIndex").i32s(indices);
    }

    Node& lm = g.add("LayerElementMaterial").i32(0);
    lm.add("Version").i32(101);
    lm.add("Name").str("");
    lm.add("MappingInformationType").str("AllSame");
    lm.add("ReferenceInformationType").str("IndexToDirect");
    lm.add("Materials").i32s({0});

    Node& layer = g.add("Layer").i32(0);
    layer.add("Version").i32(100);
    for (const char* type : {"LayerElementNormal", "LayerElementSmoothing", "LayerElementMaterial", "LayerElementUV"}) {
        if (!uvs && std::strcmp(type, "LayerElementUV") == 0) continue;
        Node& le = layer.add("LayerElement");
        le.add("Type").str(type);
        le.add("TypedIndex").i32(0);
    }
    return g;
}

Node makeModel(int64_t id, const std::string& name, const std::string& type) {
    Node m("Model");
    m.i64(id).objName(name, "Model").str(type);
    m.add("Version").i32(232);
    Node& p70 = m.add("Properties70");
    prop70(p70, "DefaultAttributeIndex", "int", "Integer", "").i32(0);
    prop70(p70, "InheritType", "enum", "", "").i32(1);
    m.add("MultiLayer").i32(0);
    m.add("MultiTake").i32(0);
    m.add("Shading").b(true);
    m.add("Culling").str("CullingOff");
    return m;
}

} // namespace

bool saveFbx(const std::string& path, const std::string& name, const std::vector<const Mesh*>& lods,
             const FbxExportOptions& o, std::string& error) {
    return saveFbx(path, std::vector<FbxObject>{{name, lods}}, o, error);
}

bool saveFbx(const std::string& path, const std::vector<FbxObject>& fbxObjects, const FbxExportOptions& o,
             std::string& error) {
    if (fbxObjects.empty()) {
        error = "The mesh is empty";
        return false;
    }
    for (const auto& obj : fbxObjects)
        if (obj.lods.empty() || !obj.lods[0] || obj.lods[0]->triangleCount() == 0 ||
            std::find(obj.lods.begin(), obj.lods.end(), nullptr) != obj.lods.end()) {
            error = "The mesh is empty";
            return false;
        }
    Ids ids;
    Node root;
    addHeader(root);
    addGlobalSettings(root);

    Node& documents = root.add("Documents");
    documents.add("Count").i32(1);
    const int64_t docId = ids();
    Node& document = documents.add("Document").i64(docId).str("Scene").str("Scene");
    prop70(document.add("Properties70"), "SourceObject", "object", "", "");
    document.add("RootNode").i64(0);
    root.add("References");

    Node definitions("Definitions");
    Node objects("Objects");
    Node connections("Connections");
    int models = 0, geometries = 0, deformers = 0, attributes = 0, materials = 1, textures = 0;

    // Material shared by every object and LOD.
    const int64_t materialId = ids();
    {
        Node& mat = objects.add("Material").i64(materialId).objName(o.materialName, "Material").str("");
        mat.add("Version").i32(102);
        mat.add("ShadingModel").str("Phong");
        mat.add("MultiLayer").i32(0);
        Node& p70 = mat.add("Properties70");
        prop70(p70, "DiffuseColor", "Color", "", "A").f64(o.color.x).f64(o.color.y).f64(o.color.z);
        prop70(p70, "Diffuse", "Vector3D", "Vector", "").f64(o.color.x).f64(o.color.y).f64(o.color.z);
        prop70(p70, "SpecularFactor", "Number", "", "A").f64(0.2);
        prop70(p70, "Shininess", "double", "Number", "").f64(20.0);
        prop70(p70, "ShininessExponent", "double", "Number", "").f64(20.0);
    }

    // Textured objects (core/Texture.h on LOD0): a PNG beside the file and a material of their own whose
    // DiffuseColor reads it (Texture + Video, as Blender and Maya write them).
    std::vector<MeshObjectRef> refs;
    for (const auto& obj : fbxObjects) refs.push_back({obj.name, obj.lods[0]});
    const std::vector<std::string> pngs = textureFileNames(path, refs);

    for (size_t oi = 0; oi < fbxObjects.size(); ++oi) {
        const FbxObject& object = fbxObjects[oi];
        const std::string& name = object.name;
        const std::vector<const Mesh*>& lods = object.lods;
        const bool grouped = lods.size() > 1;
        int64_t parentId = 0;
        int64_t objectMaterialId = materialId;
        if (!pngs[oi].empty()) {
            std::string pngPath;
            if (!writeTextureBeside(path, pngs[oi], *lods[0], pngPath, error)) return false;
            std::error_code ec;
            const std::filesystem::path absolute = std::filesystem::absolute(textureFsPath(pngPath), ec);
            const std::string file = ec ? pngPath : textureUtf8(absolute);
            objectMaterialId = ids();
            const int64_t textureId = ids(), videoId = ids();
            const std::string matName = "M_" + safeTextureName(name);
            Node& mat = objects.add("Material").i64(objectMaterialId).objName(matName, "Material").str("");
            mat.add("Version").i32(102);
            mat.add("ShadingModel").str("Phong");
            mat.add("MultiLayer").i32(0);
            Node& p70 = mat.add("Properties70");
            prop70(p70, "DiffuseColor", "Color", "", "A").f64(1.0).f64(1.0).f64(1.0);
            prop70(p70, "Diffuse", "Vector3D", "Vector", "").f64(1.0).f64(1.0).f64(1.0);
            prop70(p70, "SpecularFactor", "Number", "", "A").f64(0.2);
            prop70(p70, "Shininess", "double", "Number", "").f64(20.0);
            prop70(p70, "ShininessExponent", "double", "Number", "").f64(20.0);

            // One Texture + Video pair reading `fileAbs` (relative name `rel`), connected to the material's `property`.
            auto addTexture = [&](int64_t texId, int64_t vidId, const std::string& texName, const std::string& fileAbs,
                                  const std::string& rel, const std::string& property) {
                Node& tex = objects.add("Texture").i64(texId).objName(texName, "Texture").str("");
                tex.add("Type").str("TextureVideoClip");
                tex.add("Version").i32(202);
                tex.add("TextureName").objName(texName, "Texture");
                Node& tp70 = tex.add("Properties70");
                prop70(tp70, "UseMaterial", "bool", "", "").i32(1);
                prop70(tp70, "UseMipMap", "bool", "", "").i32(0);
                tex.add("Media").objName(texName, "Video");
                tex.add("FileName").str(fileAbs);
                tex.add("RelativeFilename").str(rel);
                tex.add("ModelUVTranslation").f64(0.0).f64(0.0);
                tex.add("ModelUVScaling").f64(1.0).f64(1.0);
                tex.add("Texture_Alpha_Source").str("None");
                tex.add("Cropping").i32(0).i32(0).i32(0).i32(0);

                Node& video = objects.add("Video").i64(vidId).objName(texName, "Video").str("Clip");
                video.add("Type").str("Clip");
                prop70(video.add("Properties70"), "Path", "KString", "XRefUrl", "").str(fileAbs);
                video.add("UseMipMap").i32(0);
                video.add("Filename").str(fileAbs);
                video.add("RelativeFilename").str(rel);

                addConnection(connections, "OO", vidId, texId);
                addPropertyConnection(connections, texId, objectMaterialId, property);
                ++textures;
            };
            addTexture(textureId, videoId, safeTextureName(name) + "_color", file, pngs[oi], "DiffuseColor");
            // Material channels (core/TextureChannels.h): their maps on the FBX material properties importers map
            // to PBR inputs (Blender: ShininessExponent -> roughness, ReflectionFactor -> metallic).
            std::vector<PbrPng> pbr;
            if (!encodePbrPngs(*lods[0], false, pngs[oi], pbr) || !writePbrPngsBeside(path, pbr, error)) {
                if (error.empty()) error = "Cannot encode the material maps of " + name;
                return false;
            }
            for (const PbrPng& p : pbr) {
                const char* property = p.slot == "normal"      ? "NormalMap"
                                       : p.slot == "roughness" ? "ShininessExponent"
                                       : p.slot == "metallic"  ? "ReflectionFactor"
                                       : p.slot == "emissive"  ? "EmissiveColor"
                                       : p.slot == "opacity"   ? "TransparencyFactor"
                                                               : nullptr;
                if (!property) continue; // occlusion: no FBX material input; the file is still written
                const std::string pngAbs = textureUtf8(textureFsPath(file).parent_path() / textureFsPath(p.name));
                addTexture(ids(), ids(), safeTextureName(name) + "_" + p.slot, pngAbs, p.name, property);
            }
            ++materials;
        }
        if (grouped) {
            const int64_t groupId = ids(), attrId = ids();
            Node attr("NodeAttribute");
            attr.i64(attrId).objName("", "NodeAttribute").str("LodGroup");
            Node& p70 = attr.add("Properties70");
            prop70(p70, "MinMaxDistance", "bool", "", "").i32(0);
            prop70(p70, "MinDistance", "double", "Number", "").f64(-100.0);
            prop70(p70, "MaxDistance", "double", "Number", "").f64(100.0);
            prop70(p70, "WorldSpace", "bool", "", "").i32(0);
            prop70(p70, "ThresholdsUsedAsPercentage", "bool", "", "").i32(1);
            for (size_t i = 1; i < lods.size(); ++i)
                prop70(p70, "Thresholds|Level" + std::to_string(i - 1), "Distance", "", "")
                    .f32(100.0f / float(1u << i))
                    .str("cm");
            for (size_t i = 0; i < lods.size(); ++i)
                prop70(p70, "DisplayLevels|Level" + std::to_string(i), "enum", "", "").i32(0);
            attr.add("TypeFlags").str("LodGroup");
            objects.children.push_back(std::move(attr));
            objects.children.push_back(makeModel(groupId, name, "LodGroup"));
            addConnection(connections, "OO", attrId, groupId);
            addConnection(connections, "OO", groupId, 0);
            parentId = groupId;
            ++models;
            ++attributes;
        }

        for (size_t level = 0; level < lods.size(); ++level) {
            const Mesh& mesh = *lods[level];
            const std::string meshName = grouped ? name + "_LOD" + std::to_string(level) : name;
            const int64_t modelId = ids(), geometryId = ids();
            objects.children.push_back(makeGeometry(geometryId, meshName, mesh, o.scale));
            objects.children.push_back(makeModel(modelId, meshName, "Mesh"));
            addConnection(connections, "OO", geometryId, modelId);
            addConnection(connections, "OO", objectMaterialId, modelId);
            if (grouped) {
                // Like Maya/FBX SDK files: LOD group -> one Null per level -> the level's mesh.
                const int64_t levelId = ids(), levelAttrId = ids();
                Node attr("NodeAttribute");
                attr.i64(levelAttrId).objName("", "NodeAttribute").str("Null");
                attr.add("TypeFlags").str("Null");
                objects.children.push_back(std::move(attr));
                objects.children.push_back(makeModel(levelId, "LOD_" + std::to_string(level), "Null"));
                addConnection(connections, "OO", levelAttrId, levelId);
                addConnection(connections, "OO", levelId, parentId);
                addConnection(connections, "OO", modelId, levelId);
                ++models;
                ++attributes;
            } else {
                addConnection(connections, "OO", modelId, parentId);
            }
            ++models;
            ++geometries;

            // Sculpt layers -> blend shape channels (LOD0 only).
            if (level == 0 && o.layers && !mesh.layers.empty()) {
                const int64_t blendId = ids();
                Node& blend = objects.add("Deformer").i64(blendId).objName(meshName, "Deformer").str("BlendShape");
                blend.add("Version").i32(100);
                addConnection(connections, "OO", blendId, geometryId);
                ++deformers;
                for (const auto& layer : mesh.layers) {
                    const int64_t channelId = ids(), shapeId = ids();
                    Node& channel = objects.add("Deformer").i64(channelId).objName(layer.name, "SubDeformer").str("BlendShapeChannel");
                    channel.add("Version").i32(100);
                    channel.add("DeformPercent").f64(double(layer.weight) * 100.0);

                    std::vector<int32_t> indexes;
                    std::vector<double> offsets;
                    for (uint32_t v = 0; v < layer.delta.size(); ++v) {
                        if (layer.delta[v] == glm::vec3(0.0f)) continue;
                        indexes.push_back(int32_t(v));
                        const glm::vec3 d = layer.delta[v] * o.scale;
                        offsets.insert(offsets.end(), {double(d.x), double(d.y), double(d.z)});
                    }
                    if (indexes.empty()) {
                        indexes.push_back(0);
                        offsets.insert(offsets.end(), {0.0, 0.0, 0.0});
                    }
                    channel.add("FullWeights").f64s(std::vector<double>(indexes.size(), 100.0));
                    Node& shape = objects.add("Geometry").i64(shapeId).objName(layer.name, "Geometry").str("Shape");
                    shape.add("Properties70");
                    shape.add("Version").i32(100);
                    shape.add("Indexes").i32s(indexes);
                    shape.add("Vertices").f64s(offsets);
                    std::vector<double> shapeNormals(offsets.size(), 0.0);
                    shapeNormals[0] = 1.1e-6; // all-zero shape normals break some importers (Unity)
                    shape.add("Normals").f64s(shapeNormals);
                    addConnection(connections, "OO", shapeId, channelId);
                    addConnection(connections, "OO", channelId, blendId);
                    ++deformers;
                    ++geometries;
                }
            }
        }
    }

    // Definitions: object type counts (property templates are optional).
    definitions.add("Version").i32(100);
    const int total = 1 + models + geometries + materials + deformers + attributes + textures * 2;
    definitions.add("Count").i32(total);
    auto defType = [&](const char* type, int count) {
        if (count == 0) return;
        Node& t = definitions.add("ObjectType").str(type);
        t.add("Count").i32(count);
    };
    defType("GlobalSettings", 1);
    defType("Model", models);
    defType("Geometry", geometries);
    defType("Material", materials);
    defType("Texture", textures);
    defType("Video", textures);
    defType("Deformer", deformers);
    defType("NodeAttribute", attributes);

    root.children.push_back(std::move(definitions));
    root.children.push_back(std::move(objects));
    root.children.push_back(std::move(connections));
    Node& takes = root.add("Takes");
    takes.add("Current").str("");

    // File: header, top-level records, closing null record, footer.
    std::vector<uint8_t> out;
    const char magic[] = "Kaydara FBX Binary  ";
    out.insert(out.end(), magic, magic + sizeof magic); // includes the terminating zero
    out.push_back(0x1A);
    out.push_back(0x00);
    Node::put(out, uint32_t(7400));
    for (const auto& n : root.children) n.write(out);
    out.insert(out.end(), 13, 0); // end of the top-level list
    static const uint8_t footerId[16] = {0xfa, 0xbc, 0xab, 0x09, 0xd0, 0xc8, 0xd4, 0x66,
                                         0xb1, 0x76, 0xfb, 0x83, 0x1c, 0xf7, 0x26, 0x7e};
    out.insert(out.end(), footerId, footerId + 16);
    out.insert(out.end(), 4, 0);
    size_t pad = ((out.size() + 15) & ~size_t(15)) - out.size();
    if (pad == 0) pad = 16;
    out.insert(out.end(), pad, 0);
    Node::put(out, uint32_t(7400));
    out.insert(out.end(), 120, 0);
    static const uint8_t footerMagic[16] = {0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e,
                                            0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29, 0x0b};
    out.insert(out.end(), footerMagic, footerMagic + 16);

    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f || !f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()))) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

namespace {

// True if `node` sits under a LOD group via any child other than the first (LOD0).
bool isLowerLod(const ufbx_node* node) {
    for (const ufbx_node* n = node; n && n->parent; n = n->parent) {
        const ufbx_node* p = n->parent;
        if (p->attrib_type != UFBX_ELEMENT_LOD_GROUP) continue;
        if (p->children.count > 0 && p->children.data[0] != n) return true;
    }
    return false;
}

glm::vec3 toVec(const ufbx_vec3& v) { return {float(v.x), float(v.y), float(v.z)}; }

// An object's name: its LOD group's for a LOD0 mesh, else the node's.
std::string objectName(const ufbx_node* node) {
    for (const ufbx_node* p = node->parent; p; p = p->parent)
        if (p->attrib_type == UFBX_ELEMENT_LOD_GROUP && p->name.length) return std::string(p->name.data, p->name.length);
    return std::string(node->name.data, node->name.length);
}

// Merges every mesh node into one object, or (split) makes one object per node.
bool readFbx(const std::string& path, bool split, std::vector<MeshObject>& out, std::string& error) {
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    ufbx_error err;
    ufbx_scene* scene = ufbx_load_file_len(path.data(), path.size(), &opts, &err);
    if (!scene) {
        char buf[512];
        ufbx_format_error(buf, sizeof buf, &err);
        error = "Cannot read FBX: " + std::string(buf);
        return false;
    }
    struct Gathered {
        std::string name;
        Mesh m;
        std::vector<SculptLayer> layers;
        std::map<std::string, size_t> layerIndex;
        bool uvs = false; // some mesh had UVs (Mesh::uvs holds one per corner either way until the end)
    };
    std::vector<Gathered> objects(split ? 0 : 1);
    std::vector<uint32_t> tri;
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node* node = scene->nodes.data[ni];
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || !node->visible || isLowerLod(node)) continue;
        if (split) objects.push_back({objectName(node), {}, {}, {}, false});
        Mesh& m = objects.back().m;
        std::vector<SculptLayer>& layers = objects.back().layers;
        std::map<std::string, size_t>& layerIndex = objects.back().layerIndex;
        const uint32_t base = uint32_t(m.positions.size());
        const ufbx_matrix& xf = node->geometry_to_world;
        const ufbx_vec3 zero = {};
        const ufbx_vec3 origin = ufbx_transform_position(&xf, zero);
        for (size_t v = 0; v < mesh->vertices.count; ++v) m.positions.push_back(toVec(ufbx_transform_position(&xf, mesh->vertices.data[v])));
        tri.resize(mesh->max_face_triangles * 3);
        // UVs of the first UV set, per face corner (V up, as in SculptForge).
        const bool meshUvs = mesh->vertex_uv.exists;
        objects.back().uvs |= meshUvs;
        for (size_t f = 0; f < mesh->faces.count; ++f) {
            const uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), mesh, mesh->faces.data[f]);
            for (uint32_t t = 0; t < n; ++t) {
                m.triangles.push_back(glm::uvec3(base + mesh->vertex_indices.data[tri[t * 3]], base + mesh->vertex_indices.data[tri[t * 3 + 1]],
                                                 base + mesh->vertex_indices.data[tri[t * 3 + 2]]));
                for (int c = 0; c < 3; ++c) {
                    const ufbx_vec2 uv = meshUvs ? ufbx_get_vertex_vec2(&mesh->vertex_uv, tri[t * 3 + size_t(c)]) : ufbx_vec2{};
                    m.uvs.push_back(glm::vec2(float(uv.x), float(uv.y)));
                }
            }
            // A quad's two triangles stay paired (Mesh::quadPartners); n-gons are left as triangles.
            if (mesh->faces.data[f].num_indices == 4 && n == 2)
                m.pairQuad(uint32_t(m.triangles.size() - 2), uint32_t(m.triangles.size() - 1));
        }
        if (m.hasQuads()) m.quadPartners.resize(m.triangles.size(), Mesh::kNoQuad);
        for (auto& l : layers) l.delta.resize(m.positions.size(), glm::vec3(0.0f));
        for (size_t d = 0; d < mesh->blend_deformers.count; ++d) {
            const ufbx_blend_deformer* blend = mesh->blend_deformers.data[d];
            for (size_t c = 0; c < blend->channels.count; ++c) {
                const ufbx_blend_channel* ch = blend->channels.data[c];
                const ufbx_blend_shape* shape = ch->target_shape;
                if (!shape) continue;
                const std::string name(ch->name.data, ch->name.length);
                auto [it, inserted] = layerIndex.try_emplace(name, layers.size());
                if (inserted) layers.push_back({name, std::vector<glm::vec3>(m.positions.size(), glm::vec3(0.0f)), float(ch->weight)});
                SculptLayer& layer = layers[it->second];
                for (size_t k = 0; k < shape->num_offsets; ++k) {
                    const uint32_t v = shape->offset_vertices.data[k];
                    if (v >= mesh->vertices.count) continue;
                    const ufbx_vec3 moved = ufbx_transform_position(&xf, shape->position_offsets.data[k]);
                    layer.delta[base + v] = toVec(moved) - toVec(origin);
                }
            }
        }
    }
    ufbx_free_scene(scene);
    out.clear();
    for (auto& o : objects) {
        if (o.m.triangles.empty()) continue;
        if (!o.uvs) o.m.uvs.clear();
        for (auto& l : o.layers) l.delta.resize(o.m.positions.size(), glm::vec3(0.0f));
        out.push_back({o.name, weldImported(o.m, o.layers)});
    }
    if (out.empty()) {
        error = "No meshes found in " + path;
        return false;
    }
    return true;
}

} // namespace

bool loadFbx(const std::string& path, Mesh& out, std::string& error) {
    std::vector<MeshObject> objects;
    if (!readFbx(path, false, objects, error)) return false;
    out = std::move(objects.front().mesh);
    return true;
}

bool loadFbx(const std::string& path, std::vector<MeshObject>& out, std::string& error) {
    return readFbx(path, true, out, error);
}

} // namespace sf
