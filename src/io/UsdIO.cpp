// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/UsdIO.h"

#include "io/TextureExport.h"
#include "io/Zip.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string_view>

namespace sf {

namespace {

std::filesystem::path fsPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

std::string lowerExtension(const std::string& path) {
    std::string ext = fsPath(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

// ---------------------------------------------------------------- writing

struct Writer {
    std::string s;
    int depth = 0;

    void line(std::string_view text) {
        s.append(size_t(depth) * 4, ' ');
        s.append(text);
        s += '\n';
    }
    void indent() { s.append(size_t(depth) * 4, ' '); }
    void num(double v) {
        if (!std::isfinite(v)) v = 0.0;
        char b[32];
        const int n = std::snprintf(b, sizeof b, "%.9g", v);
        s.append(b, size_t(n));
    }
    void vec3(const glm::vec3& v) {
        s += '(';
        num(v.x);
        s += ", ";
        num(v.y);
        s += ", ";
        num(v.z);
        s += ')';
    }
    void vec2(const glm::vec2& v) {
        s += '(';
        num(v.x);
        s += ", ";
        num(v.y);
        s += ')';
    }
    void quat(const glm::quat& q) { // USD text order is (real, i, j, k)
        s += '(';
        num(q.w);
        s += ", ";
        num(q.x);
        s += ", ";
        num(q.y);
        s += ", ";
        num(q.z);
        s += ')';
    }
    template <class T, class F> void array(std::string_view decl, const std::vector<T>& values, F&& each) {
        indent();
        s.append(decl);
        s += " = [";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i) s += ", ";
            each(values[i]);
        }
        s += "]\n";
    }
    void open(std::string_view header) {
        line(header);
        line("{");
        ++depth;
    }
    void close() {
        --depth;
        line("}");
    }
};

std::string quoted(const std::string& t) { return "\"" + t + "\""; }

void writeExtent(Writer& w, const AABB& b) {
    w.indent();
    w.s += "float3[] extent = [";
    w.vec3(b.valid() ? b.min : glm::vec3(0.0f));
    w.s += ", ";
    w.vec3(b.valid() ? b.max : glm::vec3(0.0f));
    w.s += "]\n";
}

// One mesh prim `prim` under the root (a SkelRoot holding the Mesh when it has blend shapes).
void writeMesh(Writer& w, const Mesh& mesh, const std::string& root, const std::string& prim, const std::string& material,
               const MorphAnimation* animation) {
    const bool skinned = !mesh.layers.empty();
    const std::string meshPath = skinned ? "/" + root + "/" + prim + "/Mesh" : "/" + root + "/" + prim;
    // Unique, valid names for the blend shapes.
    std::vector<std::string> shapeNames;
    std::set<std::string> used;
    for (const auto& l : mesh.layers) {
        std::string n = usdIdentifier(l.name), unique = n;
        for (int k = 2; used.count(unique); ++k) unique = n + "_" + std::to_string(k);
        used.insert(unique);
        shapeNames.push_back(unique);
    }

    Mesh base;
    base.positions = mesh.basePositions();
    base.triangles = mesh.triangles;
    base.rebuildTopology();
    base.recomputeAllNormals();

    if (skinned) w.open("def SkelRoot \"" + prim + "\"");
    w.line("def Mesh \"" + (skinned ? std::string("Mesh") : prim) + "\" (");
    w.line(skinned ? "    prepend apiSchemas = [\"SkelBindingAPI\", \"MaterialBindingAPI\"]"
                   : "    prepend apiSchemas = [\"MaterialBindingAPI\"]");
    w.line(")");
    w.line("{");
    ++w.depth;
    writeExtent(w, base.bounds());
    // Quads where the mesh keeps them (Mesh::quadPartners), else triangles; UVs per face corner in the same
    // order (a quad split by a UV seam along its diagonal is written as two triangles).
    const bool uvs = hasCornerUvs(mesh);
    std::vector<glm::uvec4> faces;
    std::vector<int> counts;
    std::vector<glm::vec2> st;
    faces.reserve(mesh.triangleCount());
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        glm::uvec4 f;
        glm::vec2 fuv[4];
        const int corners = exportFaceUv(mesh, t, f, fuv);
        if (corners == 0) continue;
        faces.push_back(f);
        counts.push_back(corners);
        if (uvs) st.insert(st.end(), fuv, fuv + corners);
    }
    w.array("int[] faceVertexCounts", counts, [&](int n) { w.s += std::to_string(n); });
    size_t face = 0;
    w.array("int[] faceVertexIndices", faces, [&](const glm::uvec4& f) {
        w.s += std::to_string(f.x) + ", " + std::to_string(f.y) + ", " + std::to_string(f.z);
        if (counts[face++] == 4) w.s += ", " + std::to_string(f.w);
    });
    w.line("rel material:binding = <" + material + ">");
    w.array("normal3f[] normals", base.normals, [&](const glm::vec3& n) { w.vec3(n); });
    w.s.pop_back();
    w.s += " (\n";
    w.line("    interpolation = \"vertex\"");
    w.line(")");
    w.array("point3f[] points", base.positions, [&](const glm::vec3& p) { w.vec3(p); });
    if (uvs) {
        w.array("texCoord2f[] primvars:st", st, [&](const glm::vec2& uv) { w.vec2(uv); });
        w.s.pop_back();
        w.s += " (\n";
        w.line("    interpolation = \"faceVarying\"");
        w.line(")");
    }
    if (mesh.hasColors() && mesh.colors.size() == mesh.vertexCount()) {
        // Polypaint (linear, as USD expects); the material reads it through a primvar reader.
        w.array("color3f[] primvars:displayColor", mesh.colors, [&](const glm::vec3& c) { w.vec3(c); });
        w.s.pop_back();
        w.s += " (\n";
        w.line("    interpolation = \"vertex\"");
        w.line(")");
    }
    w.line("uniform token subdivisionScheme = \"none\"");
    if (skinned) {
        w.array("uniform token[] skel:blendShapes", shapeNames, [&](const std::string& n) { w.s += quoted(n); });
        w.array("rel skel:blendShapeTargets", shapeNames, [&](const std::string& n) { w.s += "<" + meshPath + "/" + n + ">"; });
        w.line("rel skel:skeleton = </" + root + "/" + prim + "/Skel>");
        for (size_t k = 0; k < mesh.layers.size(); ++k) {
            // Sparse: only the points the layer actually moves.
            std::vector<uint32_t> idx;
            const auto& d = mesh.layers[k].delta;
            for (uint32_t v = 0; v < d.size(); ++v)
                if (d[v] != glm::vec3(0.0f)) idx.push_back(v);
            if (idx.empty()) idx.push_back(0);
            w.s += '\n';
            w.open("def BlendShape \"" + shapeNames[k] + "\"");
            w.array("uniform vector3f[] offsets", idx, [&](uint32_t v) { w.vec3(d[v]); });
            w.array("uniform int[] pointIndices", idx, [&](uint32_t v) { w.s += std::to_string(v); });
            w.close();
        }
    }
    w.close();

    if (skinned) {
        // A joint-less skeleton is how UsdSkel drives pure blend-shape rigs.
        w.s += '\n';
        w.line("def Skeleton \"Skel\" (");
        w.line("    prepend apiSchemas = [\"SkelBindingAPI\"]");
        w.line(")");
        w.line("{");
        ++w.depth;
        w.line("uniform matrix4d[] bindTransforms = []");
        w.line("uniform token[] joints = []");
        w.line("uniform matrix4d[] restTransforms = []");
        w.line("rel skel:animationSource = </" + root + "/" + prim + "/Skel/Weights>");
        w.s += '\n';
        w.open("def SkelAnimation \"Weights\"");
        w.array("uniform token[] blendShapes", shapeNames, [&](const std::string& n) { w.s += quoted(n); });
        w.array("float[] blendShapeWeights", mesh.layers, [&](const SculptLayer& l) { w.num(l.weight); });
        if (animation && !animation->empty()) {
            // Keys for the shapes the animation names (the rest at their current weight), at time codes in frames.
            std::vector<int> channelOf;
            bool any = false;
            for (const auto& l : mesh.layers) {
                channelOf.push_back(animation->channel(l.name));
                any |= channelOf.back() >= 0;
            }
            if (any) {
                w.line("float[] blendShapeWeights.timeSamples = {");
                ++w.depth;
                for (size_t k = 0; k < animation->times.size(); ++k) {
                    w.indent();
                    w.num(std::round(animation->times[k] * animation->fps * 1000.0f) / 1000.0f);
                    w.s += ": [";
                    for (size_t i = 0; i < channelOf.size(); ++i) {
                        if (i) w.s += ", ";
                        w.num(channelOf[i] >= 0 ? animation->value(k, size_t(channelOf[i])) : mesh.layers[i].weight);
                    }
                    w.s += "],\n";
                }
                --w.depth;
                w.line("}");
            }
        }
        w.close();
        w.close();
        w.close();
    }
}

void writeSplats(Writer& w, const SplatCloud& c) {
    w.open("def ParticleField3DGaussianSplat \"Splats\""); // fixed name: never collides with the mesh prims
    AABB local;
    for (const auto& p : c.positions) local.expand(p);
    writeExtent(w, local);
    w.array("float[] opacities", c.opacities, [&](float o) { w.num(o); });
    w.array("quatf[] orientations", c.rotations, [&](const glm::quat& q) { w.quat(q); });
    w.array("point3f[] positions", c.positions, [&](const glm::vec3& p) { w.vec3(p); });
    w.line("uniform token projectionModeHint = \"perspective\"");
    w.array("float3[] radiance:sphericalHarmonicsCoefficients", c.sh, [&](const glm::vec3& v) { w.vec3(v); });
    w.line("uniform int radiance:sphericalHarmonicsDegree = " + std::to_string(c.shDegree));
    w.array("float3[] scales", c.scales, [&](const glm::vec3& v) { w.vec3(v); });
    w.line("uniform token sortingModeHint = \"zDepth\"");
    if (c.transform != glm::mat4(1.0f)) {
        w.indent();
        w.s += "matrix4d xformOp:transform = (";
        for (int col = 0; col < 4; ++col) { // USD rows are glm columns (row-vector convention)
            w.s += col ? ", (" : "(";
            for (int r = 0; r < 4; ++r) {
                if (r) w.s += ", ";
                w.num(c.transform[col][r]);
            }
            w.s += ')';
        }
        w.s += ")\n";
        w.line("uniform token[] xformOpOrder = [\"xformOp:transform\"]");
    }
    w.close();
}

// A UsdPreviewSurface clay material; `painted` reads the polypaint primvar for its colour.
void writeMaterial(Writer& w, const std::string& path, const std::string& name, bool painted) {
    w.open("def Material " + quoted(name));
    w.line("token outputs:surface.connect = <" + path + "/PreviewSurface.outputs:surface>");
    w.s += '\n';
    w.open("def Shader \"PreviewSurface\"");
    w.line("uniform token info:id = \"UsdPreviewSurface\"");
    if (painted) w.line("color3f inputs:diffuseColor.connect = <" + path + "/Polypaint.outputs:result>");
    else w.line("color3f inputs:diffuseColor = (0.72, 0.68, 0.62)");
    w.line("float inputs:metallic = 0");
    w.line("float inputs:roughness = 0.65");
    w.line("token outputs:surface");
    w.close();
    if (painted) {
        w.s += '\n';
        w.open("def Shader \"Polypaint\"");
        w.line("uniform token info:id = \"UsdPrimvarReader_float3\"");
        w.line("string inputs:varname = \"displayColor\"");
        w.line("float3 outputs:result");
        w.close();
    }
    w.close();
}

// A UsdPreviewSurface whose colour is the texture `file` (an asset path) on the "st" primvar.
void writeTextureMaterial(Writer& w, const std::string& path, const std::string& name, const std::string& file) {
    w.open("def Material " + quoted(name));
    w.line("token outputs:surface.connect = <" + path + "/PreviewSurface.outputs:surface>");
    w.s += '\n';
    w.open("def Shader \"PreviewSurface\"");
    w.line("uniform token info:id = \"UsdPreviewSurface\"");
    w.line("color3f inputs:diffuseColor.connect = <" + path + "/Texture.outputs:rgb>");
    w.line("float inputs:metallic = 0");
    w.line("float inputs:roughness = 0.65");
    w.line("token outputs:surface");
    w.close();
    w.s += '\n';
    w.open("def Shader \"Texture\"");
    w.line("uniform token info:id = \"UsdUVTexture\"");
    w.line("asset inputs:file = @" + file + "@");
    w.line("token inputs:sourceColorSpace = \"sRGB\"");
    w.line("float2 inputs:st.connect = <" + path + "/TexCoords.outputs:result>");
    w.line("token inputs:wrapS = \"repeat\"");
    w.line("token inputs:wrapT = \"repeat\"");
    w.line("float3 outputs:rgb");
    w.close();
    w.s += '\n';
    w.open("def Shader \"TexCoords\"");
    w.line("uniform token info:id = \"UsdPrimvarReader_float2\"");
    w.line("string inputs:varname = \"st\"");
    w.line("float2 outputs:result");
    w.close();
    w.close();
}

// `textures`: per mesh, the PNG asset path its material reads ("" = no texture).
std::string buildUsda(const std::vector<MeshObjectRef>& meshes, const std::vector<std::string>& textures,
                      const SplatCloud* splats, const std::string& name, const MorphAnimation* animation) {
    const std::string root = usdIdentifier(name.empty() ? "Sculpt" : name);
    // Textured meshes bind a material of their own. Of the others, polypainted ones bind a material that
    // reads their colours; with a mix, plain ones bind "Clay".
    size_t painted = 0, plain = 0;
    for (size_t i = 0; i < meshes.size(); ++i) {
        if (!textures[i].empty()) continue;
        ++plain;
        painted += meshes[i].mesh->hasColors();
    }
    const bool mixed = painted && painted < plain;
    const std::string clay = "/" + root + "/Materials/Clay", paintedClay = "/" + root + "/Materials/PaintedClay";
    Writer w;
    w.line("#usda 1.0");
    w.line("(");
    w.line("    defaultPrim = " + quoted(root));
    w.line("    doc = \"Created with SculptForge\"");
    w.line("    metersPerUnit = 1");
    w.line("    upAxis = \"Y\"");
    if (animation && !animation->empty()) {
        w.indent();
        w.s += "    startTimeCode = 0\n";
        w.indent();
        w.s += "    endTimeCode = ";
        w.num(std::round(animation->duration() * animation->fps * 1000.0f) / 1000.0f);
        w.s += "\n";
        w.indent();
        w.s += "    timeCodesPerSecond = ";
        w.num(animation->fps);
        w.s += "\n";
        w.indent();
        w.s += "    framesPerSecond = ";
        w.num(animation->fps);
        w.s += "\n";
    }
    w.line(")");
    w.s += '\n';
    w.line("def Xform " + quoted(root) + " (");
    w.line("    kind = \"component\"");
    w.line(")");
    w.line("{");
    ++w.depth;
    if (!meshes.empty()) {
        // One prim per object: "Sculpt" for a single one, else valid unique names from the objects.
        std::set<std::string> used = {"Materials", "Splats"};
        std::vector<std::pair<std::string, std::string>> textured; // material name, texture
        for (size_t i = 0; i < meshes.size(); ++i) {
            const MeshObjectRef& m = meshes[i];
            std::string prim = "Sculpt";
            if (meshes.size() > 1) {
                const std::string base = usdIdentifier(m.name.empty() ? "Mesh" : m.name);
                prim = base;
                for (int k = 2; used.count(prim); ++k) prim = base + "_" + std::to_string(k);
            }
            used.insert(prim);
            std::string material = mixed && m.mesh->hasColors() ? paintedClay : clay;
            if (!textures[i].empty()) {
                textured.push_back({prim + "_Material", textures[i]});
                material = "/" + root + "/Materials/" + textured.back().first;
            }
            writeMesh(w, *m.mesh, root, prim, material, animation);
            w.s += '\n';
        }
        w.open("def Scope \"Materials\"");
        if (plain) writeMaterial(w, clay, "Clay", painted && !mixed);
        if (mixed) {
            w.s += '\n';
            writeMaterial(w, paintedClay, "PaintedClay", true);
        }
        for (size_t k = 0; k < textured.size(); ++k) {
            if (plain || k) w.s += '\n';
            writeTextureMaterial(w, "/" + root + "/Materials/" + textured[k].first, textured[k].first, textured[k].second);
        }
        w.close();
    }
    if (splats && !splats->empty()) {
        if (!meshes.empty()) w.s += '\n';
        writeSplats(w, *splats);
    }
    w.close();
    return w.s;
}

// ---------------------------------------------------------------- reading

struct Token {
    enum Kind { Ident, Number, String, Path, Asset, Punct, End } kind = End;
    std::string_view text;
    double number = 0.0;
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : s_(src) {}

    Token next() {
        skipSpace();
        Token t;
        if (pos_ >= s_.size()) return t;
        const char c = s_[pos_];
        const size_t start = pos_;
        if (c == '"' || c == '\'') {
            const bool triple = s_.compare(pos_, 3, std::string(3, c)) == 0;
            pos_ += triple ? 3 : 1;
            while (pos_ < s_.size()) {
                if (s_[pos_] == '\\') {
                    pos_ += 2;
                    continue;
                }
                if (triple ? s_.compare(pos_, 3, std::string(3, c)) == 0 : s_[pos_] == c) break;
                ++pos_;
            }
            const size_t q = triple ? 3 : 1;
            t = {Token::String, std::string_view(s_).substr(start + q, std::min(pos_, s_.size()) - start - q)};
            pos_ = std::min(s_.size(), pos_ + q);
            return t;
        }
        if (c == '<') {
            const size_t end = s_.find('>', pos_);
            pos_ = end == std::string::npos ? s_.size() : end + 1;
            return {Token::Path, std::string_view(s_).substr(start + 1, pos_ - start - 2)};
        }
        if (c == '@') {
            const bool triple = s_.compare(pos_, 3, "@@@") == 0;
            const size_t end = s_.find(triple ? "@@@" : "@", pos_ + (triple ? 3 : 1));
            pos_ = end == std::string::npos ? s_.size() : end + (triple ? 3 : 1);
            return {Token::Asset, std::string_view(s_).substr(start, pos_ - start)};
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            ((c == '-' || c == '+' || c == '.') && pos_ + 1 < s_.size() &&
             (std::isdigit(static_cast<unsigned char>(s_[pos_ + 1])) || s_[pos_ + 1] == '.'))) {
            char* end = nullptr;
            t.number = std::strtod(s_.c_str() + pos_, &end);
            pos_ = size_t(end - s_.c_str());
            if (pos_ == start) ++pos_;
            t.kind = Token::Number;
            t.text = std::string_view(s_).substr(start, pos_ - start);
            return t;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            ++pos_;
            while (pos_ < s_.size()) {
                const char d = s_[pos_];
                if (std::isalnum(static_cast<unsigned char>(d)) || d == '_' || d == ':' || d == '.') ++pos_;
                else break;
            }
            t = {Token::Ident, std::string_view(s_).substr(start, pos_ - start)};
            if (t.text == "inf" || t.text == "-inf" || t.text == "nan") {
                t.kind = Token::Number;
                t.number = t.text == "-inf" ? -HUGE_VAL : t.text == "inf" ? HUGE_VAL : 0.0;
            }
            return t;
        }
        ++pos_;
        return {Token::Punct, std::string_view(s_).substr(start, 1)};
    }

private:
    void skipSpace() {
        while (pos_ < s_.size()) {
            if (std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
            else if (s_[pos_] == '#') {
                while (pos_ < s_.size() && s_[pos_] != '\n') ++pos_;
            } else break;
        }
    }
    const std::string& s_;
    size_t pos_ = 0;
};

struct Value {
    std::vector<double> nums;
    std::vector<std::string> strs;
    std::string type;          // declared type without "[]" ("texCoord2f", "point3f"...)
    std::string interpolation; // primvar metadata ("faceVarying", "vertex"...), if any
};

struct Prim {
    std::string type, path;
    bool abstract = false;
    int parent = -1;
    std::map<std::string, Value> attrs;
    const Value* get(const std::string& n) const {
        auto it = attrs.find(n);
        return it == attrs.end() ? nullptr : &it->second;
    }
};

class Parser {
public:
    explicit Parser(const std::string& text) : lex_(text) { advance(); }

    std::vector<Prim> prims;
    std::string upAxis = "Y";
    std::string error;

    bool parse() {
        if (isPunct("(")) parseLayerMetadata();
        while (tok_.kind != Token::End) {
            if (!parsePrim(-1, "")) return false;
        }
        return true;
    }

private:
    Lexer lex_;
    Token tok_;
    int guard_ = 0;

    void advance() { tok_ = lex_.next(); }
    bool isPunct(const char* p) const { return tok_.kind == Token::Punct && tok_.text == p; }
    bool isIdent(std::string_view s) const { return tok_.kind == Token::Ident && tok_.text == s; }
    bool fail(const std::string& msg) {
        if (error.empty()) error = msg;
        return false;
    }
    bool expect(const char* p) {
        if (!isPunct(p)) return fail(std::string("USD syntax error: expected '") + p + "' near '" + std::string(tok_.text) + "'");
        advance();
        return true;
    }

    void skipBalanced() { // at an opening bracket
        int level = 0;
        do {
            if (tok_.kind == Token::End) return;
            if (isPunct("(") || isPunct("[") || isPunct("{")) ++level;
            else if (isPunct(")") || isPunct("]") || isPunct("}")) --level;
            advance();
        } while (level > 0);
    }

    void parseLayerMetadata() {
        advance();
        while (tok_.kind != Token::End && !isPunct(")")) {
            if (isIdent("upAxis")) {
                advance();
                if (isPunct("=")) advance();
                if (tok_.kind == Token::String) upAxis = std::string(tok_.text);
                advance();
            } else if (isPunct("(") || isPunct("[") || isPunct("{")) {
                skipBalanced();
            } else {
                advance();
            }
        }
        if (isPunct(")")) advance();
    }

    bool parseValue(Value& v) {
        if (++guard_ > 256) return fail("USD value nested too deeply");
        bool ok = true;
        if (isPunct("[") || isPunct("(")) {
            const char* close = isPunct("[") ? "]" : ")";
            advance();
            while (ok && !isPunct(close)) {
                if (tok_.kind == Token::End) {
                    ok = fail("Unterminated USD array");
                    break;
                }
                ok = parseValue(v);
                if (isPunct(",")) advance();
            }
            if (ok) advance();
        } else if (isPunct("{")) { // timeSamples: use the first sample
            advance();
            bool first = true;
            while (ok && !isPunct("}")) {
                if (tok_.kind == Token::End) {
                    ok = fail("Unterminated USD dictionary");
                    break;
                }
                advance(); // key
                if (isPunct(":")) advance();
                Value sample;
                ok = parseValue(sample);
                if (first) v = std::move(sample);
                first = false;
                if (isPunct(",")) advance();
            }
            if (ok) advance();
        } else if (tok_.kind == Token::Number) {
            v.nums.push_back(tok_.number);
            advance();
        } else if (tok_.kind == Token::Ident) {
            if (tok_.text == "true") v.nums.push_back(1.0);
            else if (tok_.text == "false") v.nums.push_back(0.0);
            else if (tok_.text != "None") v.strs.emplace_back(tok_.text);
            advance();
        } else if (tok_.kind == Token::String || tok_.kind == Token::Path || tok_.kind == Token::Asset) {
            v.strs.emplace_back(tok_.text);
            advance();
        } else {
            ok = fail("Unexpected '" + std::string(tok_.text) + "' in USD value");
        }
        --guard_;
        return ok;
    }

    bool parsePrim(int parent, const std::string& parentPath) {
        if (!(isIdent("def") || isIdent("over") || isIdent("class")))
            return fail("USD syntax error: expected a prim near '" + std::string(tok_.text) + "'");
        const bool abstract = isIdent("class") || (parent >= 0 && prims[size_t(parent)].abstract);
        advance();
        Prim prim;
        if (tok_.kind == Token::Ident) {
            prim.type = std::string(tok_.text);
            advance();
        }
        if (tok_.kind != Token::String) return fail("USD syntax error: prim without a name");
        prim.path = parentPath + "/" + std::string(tok_.text);
        prim.parent = parent;
        prim.abstract = abstract;
        advance();
        if (isPunct("(")) skipBalanced();
        if (!expect("{")) return false;
        const int index = int(prims.size());
        prims.push_back(std::move(prim));
        const std::string path = prims.back().path;

        while (!isPunct("}")) {
            if (tok_.kind == Token::End) return fail("Unterminated prim " + path);
            if (isIdent("def") || isIdent("over") || isIdent("class")) {
                if (!parsePrim(index, path)) return false;
                continue;
            }
            if (isIdent("variantSet")) {
                while (tok_.kind != Token::End && !isPunct("{")) advance();
                skipBalanced();
                continue;
            }
            if (!parseProperty(index)) return false;
        }
        advance();
        return true;
    }

    bool parseProperty(int primIndex) {
        static const std::set<std::string_view> qualifiers = {"custom", "uniform", "varying", "prepend", "append",
                                                              "add", "delete", "reorder", "config"};
        while (tok_.kind == Token::Ident && qualifiers.count(tok_.text)) advance();
        if (tok_.kind != Token::Ident) {
            if (isPunct(";")) {
                advance();
                return true;
            }
            return fail("USD syntax error near '" + std::string(tok_.text) + "'");
        }
        const std::string type(tok_.text);
        advance(); // type name or 'rel'
        if (isPunct("[")) {
            advance();
            if (!expect("]")) return false;
        }
        if (tok_.kind != Token::Ident) return fail("USD syntax error: property without a name");
        std::string name(tok_.text);
        advance();
        bool timeSamples = false;
        if (name.size() > 12 && name.ends_with(".timeSamples")) {
            name.resize(name.size() - 12);
            timeSamples = true;
        }
        Value* stored = nullptr;
        if (isPunct("=")) {
            advance();
            Value v;
            if (!parseValue(v)) return false;
            v.type = type;
            auto& slot = prims[size_t(primIndex)].attrs[name];
            if (timeSamples || (slot.nums.empty() && slot.strs.empty())) {
                slot = std::move(v);
                stored = &slot;
            }
        }
        if (isPunct("(")) {
            // Metadata: only a primvar's interpolation is used.
            advance();
            int level = 1;
            while (tok_.kind != Token::End && level > 0) {
                if (level == 1 && isIdent("interpolation")) {
                    advance();
                    if (isPunct("=")) advance();
                    if (tok_.kind == Token::String && stored) stored->interpolation = std::string(tok_.text);
                    if (tok_.kind != Token::End) advance();
                    continue;
                }
                if (isPunct("(") || isPunct("[") || isPunct("{")) ++level;
                else if (isPunct(")") || isPunct("]") || isPunct("}")) --level;
                advance();
            }
        }
        return true;
    }
};

glm::mat4 axisRotation(char axis, float degrees) {
    const glm::vec3 a = axis == 'X' ? glm::vec3(1, 0, 0) : axis == 'Y' ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    return glm::rotate(glm::mat4(1.0f), glm::radians(degrees), a);
}

// Local transform from xformOpOrder (first op is outermost).
glm::mat4 localTransform(const Prim& p, bool& resetsStack) {
    resetsStack = false;
    glm::mat4 m(1.0f);
    const Value* order = p.get("xformOpOrder");
    if (!order) return m;
    for (std::string op : order->strs) {
        if (op == "!resetXformStack!") {
            resetsStack = true;
            m = glm::mat4(1.0f);
            continue;
        }
        bool invert = false;
        if (op.rfind("!invert!", 0) == 0) {
            invert = true;
            op = op.substr(8);
        }
        const Value* v = p.get(op);
        std::vector<double> n = v ? v->nums : std::vector<double>();
        const std::string kind = op.substr(0, op.find(':', 8)); // "xformOp:translate" without its suffix
        glm::mat4 x(1.0f);
        if (kind == "xformOp:translate" && n.size() >= 3) x = glm::translate(x, glm::vec3(n[0], n[1], n[2]));
        else if (kind == "xformOp:scale" && n.size() >= 3) x = glm::scale(x, glm::vec3(n[0], n[1], n[2]));
        else if (kind == "xformOp:scale" && n.size() == 1) x = glm::scale(x, glm::vec3(float(n[0])));
        else if (kind == "xformOp:orient" && n.size() >= 4)
            x = glm::mat4_cast(glm::normalize(glm::quat(float(n[0]), float(n[1]), float(n[2]), float(n[3]))));
        else if (kind == "xformOp:transform" && n.size() >= 16) {
            for (int i = 0; i < 16; ++i) glm::value_ptr(x)[i] = float(n[size_t(i)]);
        } else if (kind.rfind("xformOp:rotate", 0) == 0 && !n.empty()) {
            const std::string axes = kind.substr(14); // "X", "XYZ", "ZXY"...
            if (axes.size() == 1) x = axisRotation(axes[0], float(n[0]));
            else if (axes.size() == 3 && n.size() >= 3)
                for (int k = 2; k >= 0; --k) // first listed axis is applied first
                    x = x * axisRotation(axes[size_t(k)], float(n[size_t(axes[size_t(k)] - 'X')]));
        }
        m = m * (invert ? glm::inverse(x) : x);
    }
    return m;
}

std::vector<glm::vec3> vec3s(const Value* v) {
    std::vector<glm::vec3> out;
    if (!v) return out;
    out.reserve(v->nums.size() / 3);
    for (size_t i = 0; i + 2 < v->nums.size(); i += 3)
        out.emplace_back(float(v->nums[i]), float(v->nums[i + 1]), float(v->nums[i + 2]));
    return out;
}

// A point index from a USD array, or `size` when it is negative, fractional junk or too large.
size_t indexValue(double v, size_t size) { return v >= 0.0 && v < double(size) ? size_t(v) : size; }

// The SkelAnimation driving a mesh: skel:skeleton on the mesh or an ancestor, then its animationSource.
const Prim* skelAnimation(const std::vector<Prim>& prims, const std::map<std::string, size_t>& byPath, size_t mesh) {
    for (int i = int(mesh); i >= 0; i = prims[size_t(i)].parent) {
        const Value* skel = prims[size_t(i)].get("skel:skeleton");
        if (!skel || skel->strs.empty()) continue;
        auto s = byPath.find(skel->strs[0]);
        if (s == byPath.end()) return nullptr;
        const Value* anim = prims[s->second].get("skel:animationSource");
        if (!anim || anim->strs.empty()) return nullptr;
        auto a = byPath.find(anim->strs[0]);
        return a == byPath.end() ? nullptr : &prims[a->second];
    }
    return nullptr;
}

// Merges every Mesh prim into one object, or (split) makes one object per prim.
bool readUsdaText(const std::string& text, bool split, std::vector<MeshObject>& outMeshes, SplatCloud& outSplats,
                  std::string& error) {
    Parser parser(text);
    if (!parser.parse()) {
        error = parser.error;
        return false;
    }
    auto& prims = parser.prims;
    std::map<std::string, size_t> byPath;
    for (size_t i = 0; i < prims.size(); ++i) byPath[prims[i].path] = i;

    // World transforms (parents come first in `prims`).
    const glm::mat4 up = parser.upAxis == "Z" ? glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0))
                                              : glm::mat4(1.0f);
    std::vector<glm::mat4> world(prims.size());
    for (size_t i = 0; i < prims.size(); ++i) {
        bool reset = false;
        const glm::mat4 local = localTransform(prims[i], reset);
        world[i] = (prims[i].parent >= 0 && !reset ? world[size_t(prims[i].parent)] : up) * local;
    }

    // Blend-shape weights from any SkelAnimation.
    std::map<std::string, float> weights;
    for (const auto& p : prims) {
        if (p.type != "SkelAnimation") continue;
        const Value* names = p.get("blendShapes");
        const Value* w = p.get("blendShapeWeights");
        if (!names || !w) continue;
        for (size_t k = 0; k < names->strs.size() && k < w->nums.size(); ++k)
            weights.try_emplace(names->strs[k], float(w->nums[k]));
    }

    struct Gathered {
        std::string name;
        Mesh m;
        std::vector<SculptLayer> layers;
        bool uvs = false; // some prim had UVs (Mesh::uvs holds one per corner either way until the end)
    };
    std::vector<Gathered> objects(split ? 0 : 1);
    bool splatsFound = false;
    SplatCloud cloud;

    for (size_t i = 0; i < prims.size(); ++i) {
        const Prim& p = prims[i];
        if (p.abstract) continue;
        if (p.type == "Mesh") {
            const auto pts = vec3s(p.get("points"));
            const Value* counts = p.get("faceVertexCounts");
            const Value* indices = p.get("faceVertexIndices");
            if (pts.empty() || !counts || !indices) continue;
            const Value* orient = p.get("orientation");
            const bool leftHanded = orient && !orient->strs.empty() && orient->strs[0] == "leftHanded";
            if (split) {
                // Named after the prim, or after its SkelRoot (SculptForge writes <Name>/Mesh for blend shapes).
                const int parent = p.parent;
                const Prim& named = parent >= 0 && prims[size_t(parent)].type == "SkelRoot" ? prims[size_t(parent)] : p;
                objects.push_back({named.path.substr(named.path.rfind('/') + 1), {}, {}, false});
            }
            Mesh& m = objects.back().m;
            std::vector<SculptLayer>& layers = objects.back().layers;
            // Layer weights from the mesh's own SkelAnimation, else from any animation naming the shape.
            std::map<std::string, float> ownWeights;
            if (const Prim* anim = skelAnimation(prims, byPath, i)) {
                const Value* names = anim->get("blendShapes");
                const Value* w = anim->get("blendShapeWeights");
                for (size_t k = 0; names && w && k < names->strs.size() && k < w->nums.size(); ++k)
                    ownWeights.try_emplace(names->strs[k], float(w->nums[k]));
            }
            auto layerFor = [&](const std::string& name) -> SculptLayer& {
                for (auto& l : layers)
                    if (l.name == name) return l;
                auto own = ownWeights.find(name);
                auto it = weights.find(name);
                layers.push_back({name, std::vector<glm::vec3>(m.positions.size(), glm::vec3(0.0f)),
                                  own != ownWeights.end() ? own->second : it == weights.end() ? 0.0f : it->second});
                return layers.back();
            };
            const uint32_t baseIndex = uint32_t(m.positions.size());
            for (const auto& q : pts) m.positions.push_back(glm::vec3(world[i] * glm::vec4(q, 1.0f)));
            // Texture coordinates: primvars:st, else the first texCoord2 primvar; face-varying (per face corner)
            // or per point, optionally indexed (<name>:indices).
            std::string stName = "primvars:st";
            const Value* st = p.get(stName);
            if (!st)
                for (const auto& [n, v] : p.attrs)
                    if (n.rfind("primvars:", 0) == 0 && v.type.rfind("texCoord2", 0) == 0 && !n.ends_with(":indices")) {
                        st = &v;
                        stName = n;
                        break;
                    }
            const Value* stIndices = st ? p.get(stName + ":indices") : nullptr;
            const size_t stCount = stIndices ? stIndices->nums.size() : st ? st->nums.size() / 2 : 0;
            bool perCorner = false, perPoint = false;
            if (st) {
                if (st->interpolation == "faceVarying") perCorner = stCount >= indices->nums.size();
                else if (st->interpolation == "vertex" || st->interpolation == "varying") perPoint = stCount >= pts.size();
                else if (st->interpolation.empty()) {
                    perCorner = stCount == indices->nums.size();
                    perPoint = !perCorner && stCount == pts.size();
                }
            }
            objects.back().uvs |= perCorner || perPoint;
            auto uvAt = [&](size_t corner) {
                if (!perCorner && !perPoint) return glm::vec2(0.0f);
                size_t e = perCorner ? corner : indexValue(indices->nums[corner], pts.size());
                if (stIndices) e = e < stIndices->nums.size() ? indexValue(stIndices->nums[e], st->nums.size() / 2) : st->nums.size();
                return e < st->nums.size() / 2 ? glm::vec2(float(st->nums[e * 2]), float(st->nums[e * 2 + 1])) : glm::vec2(0.0f);
            };
            size_t cursor = 0;
            for (double c : counts->nums) {
                const size_t n = size_t(std::max(0.0, c));
                if (cursor + n > indices->nums.size()) {
                    error = "Mesh " + p.path + " has fewer indices than its face counts need";
                    return false;
                }
                const uint32_t firstTri = uint32_t(m.triangles.size());
                for (size_t k = 1; k + 1 < n; ++k) {
                    glm::uvec3 t(uint32_t(indexValue(indices->nums[cursor], pts.size())),
                                 uint32_t(indexValue(indices->nums[cursor + k], pts.size())),
                                 uint32_t(indexValue(indices->nums[cursor + k + 1], pts.size())));
                    if (t.x >= pts.size() || t.y >= pts.size() || t.z >= pts.size()) {
                        error = "Mesh " + p.path + " has a face index out of range";
                        return false;
                    }
                    glm::vec2 uv[3] = {uvAt(cursor), uvAt(cursor + k), uvAt(cursor + k + 1)};
                    if (leftHanded) {
                        std::swap(t.y, t.z);
                        std::swap(uv[1], uv[2]);
                    }
                    m.triangles.push_back(t + glm::uvec3(baseIndex));
                    m.uvs.insert(m.uvs.end(), uv, uv + 3);
                }
                if (n == 4) m.pairQuad(firstTri, firstTri + 1); // a quad stays paired (Mesh::quadPartners)
                cursor += n;
            }
            if (m.hasQuads()) m.quadPartners.resize(m.triangles.size(), Mesh::kNoQuad);
            for (auto& l : layers) l.delta.resize(m.positions.size(), glm::vec3(0.0f));
            // Blend shapes bound to this mesh.
            const Value* names = p.get("skel:blendShapes");
            const Value* targets = p.get("skel:blendShapeTargets");
            if (names && targets) {
                const glm::mat3 linear(world[i]);
                for (size_t k = 0; k < names->strs.size() && k < targets->strs.size(); ++k) {
                    std::string target = targets->strs[k];
                    if (!target.empty() && target[0] != '/') target = p.path + "/" + target;
                    auto it = byPath.find(target);
                    if (it == byPath.end()) continue;
                    const Prim& shape = prims[it->second];
                    const auto offsets = vec3s(shape.get("offsets"));
                    const Value* pointIndices = shape.get("pointIndices");
                    SculptLayer& layer = layerFor(names->strs[k]);
                    layer.delta.resize(m.positions.size(), glm::vec3(0.0f));
                    for (size_t o = 0; o < offsets.size(); ++o) {
                        const size_t v = pointIndices ? (o < pointIndices->nums.size() ? indexValue(pointIndices->nums[o], pts.size()) : pts.size())
                                                      : o;
                        if (v < pts.size()) layer.delta[baseIndex + v] = linear * offsets[o];
                    }
                }
            }
        } else if ((p.type == "ParticleField3DGaussianSplat" || p.type == "ParticleField") && !splatsFound) {
            const auto pos = vec3s(p.get("positions"));
            if (pos.empty()) continue;
            const size_t n = pos.size();
            const Value* degree = p.get("radiance:sphericalHarmonicsDegree");
            int d = degree && !degree->nums.empty() ? std::clamp(int(degree->nums[0]), 0, 3) : 0;
            auto coeffs = vec3s(p.get("radiance:sphericalHarmonicsCoefficients"));
            if (coeffs.size() < n * size_t((d + 1) * (d + 1))) d = 0;
            cloud.resize(n, d);
            cloud.positions = pos;
            const size_t K = size_t(cloud.shCount());
            if (coeffs.size() >= n * K) std::copy(coeffs.begin(), coeffs.begin() + std::ptrdiff_t(n * K), cloud.sh.begin());
            const auto sc = vec3s(p.get("scales"));
            if (sc.size() >= n) std::copy(sc.begin(), sc.begin() + std::ptrdiff_t(n), cloud.scales.begin());
            if (const Value* o = p.get("opacities"); o && o->nums.size() >= n)
                for (size_t k = 0; k < n; ++k) cloud.opacities[k] = float(o->nums[k]);
            if (const Value* q = p.get("orientations"); q && q->nums.size() >= n * 4)
                for (size_t k = 0; k < n; ++k) {
                    const glm::quat r(float(q->nums[k * 4]), float(q->nums[k * 4 + 1]), float(q->nums[k * 4 + 2]),
                                      float(q->nums[k * 4 + 3]));
                    const float len = glm::length(r);
                    cloud.rotations[k] = len > 0.0f ? r / len : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                }
            cloud.transform = world[i];
            cloud.name = p.path.substr(p.path.rfind('/') + 1);
            splatsFound = true;
        }
    }

    outMeshes.clear();
    for (auto& o : objects) {
        if (o.m.triangles.empty()) continue;
        if (!o.uvs) o.m.uvs.clear();
        for (auto& l : o.layers) l.delta.resize(o.m.positions.size(), glm::vec3(0.0f));
        outMeshes.push_back({o.name, weldImported(o.m, o.layers)});
    }
    if (outMeshes.empty() && !splatsFound) {
        error = "No meshes or Gaussian splats found (references, payloads and binary .usdc layers are not supported yet)";
        return false;
    }
    outSplats = splatsFound ? std::move(cloud) : SplatCloud();
    return true;
}

} // namespace

std::string usdIdentifier(const std::string& text) {
    std::string out;
    for (unsigned char c : text) out += std::isalnum(c) ? char(c) : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "_" + out;
    return out;
}

bool saveUsd(const std::string& path, const Mesh* mesh, const SplatCloud* splats, const std::string& name,
             std::string& error) {
    std::vector<MeshObjectRef> meshes;
    if (mesh && mesh->triangleCount() > 0) meshes.push_back({name, mesh});
    return saveUsd(path, meshes, splats, name, error);
}

bool saveUsd(const std::string& path, const std::vector<MeshObjectRef>& meshesIn, const SplatCloud* splats,
             const std::string& name, std::string& error, const MorphAnimation* animation) {
    std::vector<MeshObjectRef> meshes;
    for (const auto& m : meshesIn)
        if (m.mesh && m.mesh->triangleCount() > 0) meshes.push_back(m);
    const bool hasSplats = splats && !splats->empty();
    if (meshes.empty() && !hasSplats) {
        error = "Nothing to export";
        return false;
    }
    if (hasSplats) {
        if (std::string v = splats->validate(); !v.empty()) {
            error = v;
            return false;
        }
    }
    const std::string ext = lowerExtension(path);
    if (ext != ".usdz" && ext != ".usda" && ext != ".usd") {
        error = "USD export needs a .usda, .usd or .usdz file name";
        return false;
    }
    // Textures: PNGs inside a .usdz package, else beside the layer; asset paths are relative to the layer.
    const std::vector<std::string> pngs = textureFileNames(path, meshes);
    const std::string text = buildUsda(meshes, pngs, hasSplats ? splats : nullptr, name, animation);
    if (ext == ".usdz") {
        std::string inner = usdIdentifier(name.empty() ? "Sculpt" : name) + ".usda";
        std::vector<ZipEntry> entries = {{inner, std::vector<uint8_t>(text.begin(), text.end())}};
        for (size_t i = 0; i < meshes.size(); ++i) {
            if (pngs[i].empty()) continue;
            entries.push_back({pngs[i], {}});
            if (!encodeTexturePng(*meshes[i].mesh, entries.back().data)) {
                error = "Cannot encode the texture of " + meshes[i].name;
                return false;
            }
        }
        return writeStoredZip(path, entries, 64, error);
    }
    for (size_t i = 0; i < meshes.size(); ++i) {
        std::string pngPath;
        if (!pngs[i].empty() && !writeTextureBeside(path, pngs[i], *meshes[i].mesh, pngPath, error)) return false;
    }
    std::ofstream f(fsPath(path), std::ios::binary);
    if (!f || !f.write(text.data(), std::streamsize(text.size()))) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}

namespace {

bool readUsdFile(const std::string& path, bool split, std::vector<MeshObject>& meshes, SplatCloud& splats,
                 std::string& error) {
    std::string text;
    if (lowerExtension(path) == ".usdz") {
        std::vector<ZipEntry> entries;
        if (!readStoredZip(path, entries, error)) return false;
        if (entries.empty()) {
            error = "The USDZ package is empty";
            return false;
        }
        text.assign(entries[0].data.begin(), entries[0].data.end());
    } else {
        std::ifstream f(fsPath(path), std::ios::binary);
        if (!f) {
            error = "Cannot open " + path;
            return false;
        }
        text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (text.rfind("PXR-USDC", 0) == 0) {
        error = "Binary USD (usdc) is not supported yet - save as .usda (e.g. usdcat in.usd -o out.usda)";
        return false;
    }
    if (text.rfind("#usda", 0) != 0) {
        error = path + " is not a text USD layer";
        return false;
    }
    return readUsdaText(text, split, meshes, splats, error);
}

} // namespace

bool loadUsd(const std::string& path, Mesh& mesh, SplatCloud& splats, std::string& error) {
    std::vector<MeshObject> meshes;
    if (!readUsdFile(path, false, meshes, splats, error)) return false;
    mesh = meshes.empty() ? Mesh() : std::move(meshes.front().mesh);
    return true;
}

bool loadUsd(const std::string& path, std::vector<MeshObject>& meshes, SplatCloud& splats, std::string& error) {
    return readUsdFile(path, true, meshes, splats, error);
}

} // namespace sf
