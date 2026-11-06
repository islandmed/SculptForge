// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real-world size (#148): a part opened from a file comes back out at the size it went in, in every format, with
// the file's own numbers where the format has no units (STL millimetres, OBJ and PLY in the unit they were read
// in), USD stage units (metersPerUnit) read, projects keeping the unit, and the length formatting the UI shows.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Project.h"
#include "core/Units.h"
#include "io/GltfIO.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"
#include "io/UsdIO.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sf;

namespace {

struct Box {
    glm::vec3 lo, hi;
};

// Decimal sizes a CAD tool or slicer writes: a 50 x 50 x 199.5 box off the origin, in the file's own numbers.
constexpr Box kBox{{-12.7f, 0.4f, 0.0f}, {37.3f, 50.4f, 199.5f}};

std::vector<glm::vec3> boxTriangles(const Box& b) {
    const glm::vec3 c[8] = {{b.lo.x, b.lo.y, b.lo.z}, {b.hi.x, b.lo.y, b.lo.z}, {b.hi.x, b.hi.y, b.lo.z},
                            {b.lo.x, b.hi.y, b.lo.z}, {b.lo.x, b.lo.y, b.hi.z}, {b.hi.x, b.lo.y, b.hi.z},
                            {b.hi.x, b.hi.y, b.hi.z}, {b.lo.x, b.hi.y, b.hi.z}};
    const int f[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                          {2, 3, 7}, {2, 7, 6}, {1, 2, 6}, {1, 6, 5}, {0, 4, 7}, {0, 7, 3}};
    std::vector<glm::vec3> out;
    for (const auto& t : f)
        for (int k : t) out.push_back(c[k]);
    return out;
}

void writeStl(const std::string& path, const Box& b) {
    const auto tris = boxTriangles(b);
    std::string bytes(80, '\0');
    const uint32_t n = uint32_t(tris.size() / 3);
    bytes.append(reinterpret_cast<const char*>(&n), 4);
    for (uint32_t t = 0; t < n; ++t) {
        const float normal[3] = {0, 0, 0};
        bytes.append(reinterpret_cast<const char*>(normal), 12);
        for (int k = 0; k < 3; ++k) bytes.append(reinterpret_cast<const char*>(&tris[t * 3 + k]), 12);
        bytes.append(2, '\0');
    }
    std::ofstream(std::filesystem::path(path), std::ios::binary) << bytes;
}

void writeObj(const std::string& path, const Box& b) {
    std::ostringstream s;
    const auto tris = boxTriangles(b);
    for (const auto& p : tris) s << "v " << p.x << " " << p.y << " " << p.z << "\n";
    for (size_t i = 0; i < tris.size(); i += 3) s << "f " << i + 1 << " " << i + 2 << " " << i + 3 << "\n";
    std::ofstream(std::filesystem::path(path), std::ios::binary) << s.str();
}

std::string readFile(const std::string& path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

Box boundsOf(const std::vector<glm::vec3>& points) {
    Box b{glm::vec3(INFINITY), glm::vec3(-INFINITY)};
    for (const auto& p : points) b.lo = glm::min(b.lo, p), b.hi = glm::max(b.hi, p);
    return b;
}

// The numbers in the file, as a slicer reads them.
Box stlFileBounds(const std::string& path) {
    const std::string bytes = readFile(path);
    uint32_t n = 0;
    std::memcpy(&n, bytes.data() + 80, 4);
    std::vector<glm::vec3> pts;
    for (uint32_t t = 0; t < n; ++t)
        for (int k = 0; k < 3; ++k) {
            glm::vec3 p;
            std::memcpy(&p, bytes.data() + 84 + t * 50 + 12 + k * 12, 12);
            pts.push_back(p);
        }
    return boundsOf(pts);
}

Box objFileBounds(const std::string& path) {
    std::istringstream in(readFile(path));
    std::string line;
    std::vector<glm::vec3> pts;
    while (std::getline(in, line)) {
        if (line.rfind("v ", 0) != 0) continue;
        glm::vec3 p;
        std::istringstream(line.substr(2)) >> p.x >> p.y >> p.z;
        pts.push_back(p);
    }
    return boundsOf(pts);
}

Box meshBounds(const Mesh& m) { return boundsOf(m.positions); }

bool same(const Box& a, const Box& b) { return a.lo == b.lo && a.hi == b.hi; }
bool near(const Box& a, const Box& b, float rel = 1e-6f) {
    const float tol = rel * glm::length(b.hi - b.lo);
    return glm::length(a.lo - b.lo) <= tol && glm::length(a.hi - b.hi) <= tol;
}

// The box in the file, Z up, as SculptForge holds it (Y up): x, z, -y.
Box yUp(const Box& b) { return {{b.lo.x, b.lo.z, -b.hi.y}, {b.hi.x, b.hi.z, -b.lo.y}}; }
Box scaled(const Box& b, float s) { return {b.lo * s, b.hi * s}; }

} // namespace

TEST(units_stl_round_trip_exact) {
    const std::string in = tempPath("sf_units_in.stl"), out = tempPath("sf_units_out.stl");
    writeStl(in, kBox);
    CommandRegistry reg;
    Document doc;
    CHECK(reg.execute(doc, "open", {{"path", in}}).ok);
    CHECK(near(meshBounds(doc.mesh), scaled(yUp(kBox), 0.001f)));
    const CommandResult r = reg.execute(doc, "export_stl", {{"path", out}});
    CHECK(r.ok);
    CHECK(same(stlFileBounds(out), kBox)); // the slicer sees exactly the numbers that went in
    CHECK(r.message.find("50.0 x 50.0 x 199.5 mm") != std::string::npos);
    std::filesystem::remove(in);
    std::filesystem::remove(out);
}

TEST(units_stl_in_obj_and_ply_out_keep_millimetres) {
    const std::string in = tempPath("sf_units_mm.stl"), obj = tempPath("sf_units_mm.obj"), ply = tempPath("sf_units_mm.ply");
    writeStl(in, kBox);
    CommandRegistry reg;
    Document doc;
    CHECK(reg.execute(doc, "open", {{"path", in}}).ok);
    CHECK(reg.execute(doc, "save_obj", {{"path", obj}}).ok);
    CHECK(same(objFileBounds(obj), yUp(kBox))); // was 0.001x: millimetres in, metres out
    CHECK(reg.execute(doc, "export_ply", {{"path", ply}}).ok);
    Mesh m;
    SplatCloud splats;
    PlyContent content;
    std::string err;
    CHECK(loadPly(ply, m, splats, content, err));
    CHECK(same(meshBounds(m), yUp(kBox)));
    // A slicer reading that OBJ (millimetres) back into an STL-world: open the OBJ in mm, export STL.
    Document again;
    CHECK(reg.execute(again, "open", {{"path", obj}, {"unit", std::string("mm")}}).ok);
    const std::string stl = tempPath("sf_units_mm_again.stl");
    CHECK(reg.execute(again, "export_stl", {{"path", stl}}).ok);
    CHECK(same(stlFileBounds(stl), kBox));
    for (const auto& p : {in, obj, ply, stl}) std::filesystem::remove(p);
}

TEST(units_obj_units_round_trip_exact) {
    // An OBJ of a 50 mm part read in mm: exports to OBJ keep the numbers, STL writes the same millimetres.
    const std::string in = tempPath("sf_units_part.obj"), obj = tempPath("sf_units_part_out.obj"), stl = tempPath("sf_units_part.stl");
    writeObj(in, kBox);
    CommandRegistry reg;
    for (const char* unit : {"mm", "cm", "m", "in"}) {
        Document doc;
        CHECK(reg.execute(doc, "open", {{"path", in}, {"unit", std::string(unit)}}).ok);
        const double perMetre = findLengthUnit(unit)->perMetre;
        CHECK(near(meshBounds(doc.mesh), scaled(kBox, float(1.0 / perMetre))));
        CHECK(reg.execute(doc, "save_obj", {{"path", obj}}).ok);
        CHECK(same(objFileBounds(obj), kBox));
        CHECK(reg.execute(doc, "export_stl", {{"path", stl}}).ok);
        // STL is millimetres: the OBJ's unit converted, Y up turned into Z up.
        const Box zUp{{kBox.lo.x, -kBox.hi.z, kBox.lo.y}, {kBox.hi.x, -kBox.lo.z, kBox.hi.y}};
        CHECK(near(stlFileBounds(stl), scaled(zUp, float(1000.0 / perMetre))));
        if (std::string(unit) == "mm") CHECK(same(stlFileBounds(stl), zUp));
    }
    // open_obj takes the unit too.
    Document doc;
    CHECK(reg.execute(doc, "open_obj", {{"path", in}, {"unit", std::string("mm")}}).ok);
    CHECK(near(meshBounds(doc.mesh), scaled(kBox, 0.001f)));
    CHECK(!reg.execute(doc, "open", {{"path", in}, {"unit", std::string("furlong")}}).ok);
    for (const auto& p : {in, obj, stl}) std::filesystem::remove(p);
}

TEST(units_inch_stl_stays_inches) {
    const std::string in = tempPath("sf_units_in_in.stl"), out = tempPath("sf_units_in_out.stl");
    writeStl(in, kBox);
    CommandRegistry reg;
    Document doc;
    CHECK(reg.execute(doc, "open", {{"path", in}, {"unit", std::string("in")}}).ok);
    CHECK(near(meshBounds(doc.mesh), scaled(yUp(kBox), 0.0254f)));
    CHECK(reg.execute(doc, "export_stl", {{"path", out}}).ok);
    CHECK(same(stlFileBounds(out), kBox));
    std::filesystem::remove(in);
    std::filesystem::remove(out);
}

TEST(units_gltf_fbx_usd_keep_the_size) {
    // Unit-carrying formats: metres in glTF and USD, centimetres in FBX; reopening gives the same size.
    const std::string in = tempPath("sf_units_carry.stl");
    writeStl(in, kBox);
    CommandRegistry reg;
    Document doc;
    CHECK(reg.execute(doc, "open", {{"path", in}}).ok);
    const Box metres = meshBounds(doc.mesh);
    for (const char* ext : {".glb", ".usda", ".fbx"}) {
        const std::string out = tempPath((std::string("sf_units_carry") + ext).c_str());
        const char* cmd = std::string(ext) == ".glb" ? "export_glb" : std::string(ext) == ".usda" ? "export_usd" : "export_fbx";
        CHECK(reg.execute(doc, cmd, {{"path", out}}).ok);
        Document back;
        CHECK(reg.execute(back, "open", {{"path", out}}).ok);
        if (std::string(ext) == ".fbx") CHECK(near(meshBounds(back.mesh), metres));
        else CHECK(same(meshBounds(back.mesh), metres));
        std::filesystem::remove(out);
    }
    std::filesystem::remove(in);
}

TEST(units_usd_meters_per_unit) {
    // Hand-written stages in centimetres (Maya, Houdini: metersPerUnit = 0.01) and in metres, with the same numbers:
    // -12.7 .. 199.5 cm comes in as exactly the floats nearest -0.127 .. 1.995 m (not 100x), metres as written, and
    // both come back the same after export_usd and reopening (#148, @NicTanghe).
    for (double metersPerUnit : {0.01, 1.0}) {
        const std::string path = tempPath(metersPerUnit == 1.0 ? "sf_units_m.usda" : "sf_units_cm.usda");
        const std::string out = tempPath("sf_units_usd_back.usda");
        std::ostringstream s;
        s << "#usda 1.0\n(\n    metersPerUnit = " << metersPerUnit << "\n    upAxis = \"Y\"\n)\n\ndef Mesh \"Box\"\n{\n"
          << "    point3f[] points = [";
        const auto tris = boxTriangles(kBox);
        for (size_t i = 0; i < tris.size(); ++i) s << (i ? ", " : "") << "(" << tris[i].x << ", " << tris[i].y << ", " << tris[i].z << ")";
        s << "]\n    int[] faceVertexCounts = [";
        for (size_t i = 0; i < tris.size() / 3; ++i) s << (i ? ", " : "") << 3;
        s << "]\n    int[] faceVertexIndices = [";
        for (size_t i = 0; i < tris.size(); ++i) s << (i ? ", " : "") << i;
        s << "]\n}\n";
        std::ofstream(std::filesystem::path(path), std::ios::binary) << s.str();
        const double per = 1.0 / metersPerUnit;
        const Box expected{{fromFileUnits(kBox.lo.x, per), fromFileUnits(kBox.lo.y, per), fromFileUnits(kBox.lo.z, per)},
                           {fromFileUnits(kBox.hi.x, per), fromFileUnits(kBox.hi.y, per), fromFileUnits(kBox.hi.z, per)}};
        CommandRegistry reg;
        Document doc;
        CHECK(reg.execute(doc, "open", {{"path", path}}).ok);
        CHECK(same(meshBounds(doc.mesh), expected));
        CHECK(reg.execute(doc, "export_usd", {{"path", out}}).ok);
        Document back;
        CHECK(reg.execute(back, "open", {{"path", out}}).ok);
        CHECK(same(meshBounds(back.mesh), expected));
        std::filesystem::remove(path);
        std::filesystem::remove(out);
    }
}

TEST(units_project_keeps_the_file_unit) {
    const std::string in = tempPath("sf_units_proj.stl"), proj = tempPath("sf_units_proj.sfproj"), obj = tempPath("sf_units_proj.obj");
    writeStl(in, kBox);
    CommandRegistry reg;
    Document doc;
    CHECK(reg.execute(doc, "open", {{"path", in}}).ok);
    CHECK(doc.unitlessScale == 1000.0 && doc.unitlessFormat == "stl");
    std::string err;
    CHECK(saveProject(proj, doc, err));
    Document back;
    CHECK(loadProject(proj, back, err));
    CHECK(back.unitlessScale == 1000.0 && back.unitlessFormat == "stl");
    CHECK(reg.execute(back, "save_obj", {{"path", obj}}).ok);
    CHECK(same(objFileBounds(obj), yUp(kBox)));
    // A new scene forgets it: OBJ is metres again.
    CHECK(reg.execute(back, "new_sphere", {}).ok);
    CHECK(back.unitlessScale == 0.0 && back.unitlessFormat.empty());
    CHECK(back.unitlessExportScale("obj") == 1.0 && back.unitlessExportScale("stl") == 1000.0);
    for (const auto& p : {in, proj, obj}) std::filesystem::remove(p);
}

TEST(units_format_and_parse) {
    CHECK(findLengthUnit("MM") == &millimetres());
    CHECK(findLengthUnit("inches") && std::string(findLengthUnit("inches")->id) == "in");
    CHECK(!findLengthUnit("ft"));
    CHECK(formatLength(0.05, millimetres()) == "50 mm");
    CHECK(formatLength(0.0254, *findLengthUnit("in")) == "1 in");
    CHECK(formatLength(1.8, metres()) == "1.8 m");
    CHECK(formatLength(0.0125, *findLengthUnit("cm")) == "1.25 cm");
    CHECK(formatSize(glm::vec3(0.05f, 0.02f, 0.0125f), millimetres()) == "50 x 20 x 12.5 mm");
    CHECK(unitForScale(1000.0) == &millimetres() && !unitForScale(7.0));
    // Writing back what was read: decimal file values come back exactly in every unit.
    for (const LengthUnit& u : lengthUnits())
        for (float v : {12.7f, 0.4f, 199.9f, -33.3f, 25.4f, 1234.567f, 0.001f})
            CHECK(toFileUnits(fromFileUnits(v, u.perMetre), u.perMetre) == v);
}
