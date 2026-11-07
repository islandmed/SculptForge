// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// garment_* commands (core/Garment.h): make and edit the scene's garment pattern, sew it, write and read
// pattern files. The pattern lives in the project (Document::projectSettings["garment"]).
#include "core/Commands.h"
#include "core/Garment.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <limits>
#include <fstream>
#include <sstream>

namespace sf {

namespace {

using namespace garment;
using T = ParamSpec::Type;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// A panel by name (any case) or 1-based number.
bool findPanel(const Pattern& pat, const std::string& ref, uint32_t& out, std::string& error) {
    for (uint32_t i = 0; i < pat.panels.size(); ++i)
        if (lower(pat.panels[i].name) == lower(ref)) {
            out = i;
            return true;
        }
    char* end = nullptr;
    const long n = std::strtol(ref.c_str(), &end, 10);
    if (!ref.empty() && end && *end == 0 && n >= 1 && size_t(n) <= pat.panels.size()) {
        out = uint32_t(n - 1);
        return true;
    }
    error = "No panel \"" + ref + "\" in the pattern";
    return false;
}

bool loadOrFail(const Document& doc, Pattern& pat, CommandResult& r) {
    if (documentPattern(doc, pat)) return true;
    r = {false, "No garment pattern: run garment_new first"};
    return false;
}

std::string summary(const Pattern& pat) {
    std::ostringstream s;
    s << pat.panels.size() << " panels, " << pat.seams.size() << " seams";
    GarmentMesh m;
    std::string error;
    if (!pat.panels.empty() && triangulatePattern(pat, m, error))
        s << ", " << m.vertexCount() << " particles at " << pat.particleDistance << " mm";
    return s.str();
}

bool writeText(const std::string& path, const std::string& text, std::string& error) {
    std::ofstream f(path, std::ios::binary);
    if (!f || !f.write(text.data(), std::streamsize(text.size()))) {
        error = "Could not write " + path;
        return false;
    }
    return true;
}

CommandResult seamCommand(Document& doc, const Args& a) {
    CommandResult r;
    Pattern pat;
    if (!loadOrFail(doc, pat, r)) return r;
    Seam s;
    std::string error;
    for (int side = 0; side < 2; ++side) {
        const std::string p = side ? "b_" : "a_";
        SeamSide& sd = side ? s.b : s.a;
        if (!findPanel(pat, argString(a, p + "panel"), sd.panel, error)) return {false, error};
        const double e = argNumber(a, p + "edge", 0);
        if (!(e >= 1 && e <= double(pat.panels[sd.panel].points.size())))
            return {false, "No edge " + std::to_string(int(e)) + " on " + pat.panels[sd.panel].name};
        sd.edge = uint32_t(e) - 1;
        sd.t0 = float(std::clamp(argNumber(a, p + "from", 0.0), 0.0, 1.0));
        sd.t1 = float(std::clamp(argNumber(a, p + "to", 1.0), 0.0, 1.0));
    }
    s.flipped = argBool(a, "flipped", false);
    if (!addSeam(pat, s, error)) return {false, error};
    setDocumentPattern(doc, pat);
    const SeamCheck c = checkSeam(pat, s);
    return {true, "Sewed " + c.text};
}

} // namespace

void registerGarmentCommands(CommandRegistry& reg) {
    reg.add({"garment_new",
             "Garments: start a garment pattern in the scene (saved with the project). template: tshirt (front, back "
             "and two sleeves, sewn and placed: ready to drape) or empty.",
             {{"template", T::String, "tshirt (default) or empty", false},
              {"chest_mm", T::Number, "T-shirt: the body's chest girth in mm (default 960)", false},
              {"length_mm", T::Number, "T-shirt: length from the shoulder in mm (default 700)", false},
              {"particle_mm", T::Number, "Distance between the cloth's particles in mm (default 15)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string t = lower(argString(a, "template", "tshirt"));
                 Pattern pat;
                 if (t == "tshirt" || t == "t-shirt")
                     pat = tshirtPattern(float(argNumber(a, "chest_mm", 960.0)), float(argNumber(a, "length_mm", 700.0)));
                 else if (t != "empty")
                     return {false, "Unknown template \"" + t + "\" (tshirt or empty)"};
                 pat.particleDistance = float(std::clamp(argNumber(a, "particle_mm", pat.particleDistance), 2.0, 200.0));
                 if (pat.empty()) {
                     doc.projectSettings.set("garment", patternToJson(pat)); // an empty pattern to draw in
                     doc.modified = true;
                     return {true, "New empty garment pattern"};
                 }
                 setDocumentPattern(doc, pat);
                 return {true, "New garment pattern: " + summary(pat)};
             }});
    reg.add({"garment_info", "Garments: the pattern's panels, seams (with their lengths) and any problems.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 CommandResult r;
                 Pattern pat;
                 if (!loadOrFail(doc, pat, r)) return r;
                 std::ostringstream s;
                 s << summary(pat);
                 for (size_t i = 0; i < pat.panels.size(); ++i) {
                     const Panel& p = pat.panels[i];
                     const glm::vec2 size = panelMax(p) - panelMin(p);
                     char buf[200];
                     std::snprintf(buf, sizeof buf, "\n%zu. %s: %.0f x %.0f mm, %zu edges", i + 1, p.name.c_str(),
                                   double(size.x), double(size.y), p.points.size());
                     s << buf;
                 }
                 for (const Seam& sm : pat.seams) s << "\nSeam " << checkSeam(pat, sm).text;
                 for (const std::string& pr : patternProblems(pat)) s << "\nProblem: " << pr;
                 return {true, s.str()};
             }});
    reg.add({"garment_add_panel",
             "Garments: add a pattern panel from its outline: points as JSON [x0, y0, x1, y1, ...] in mm (y up), "
             "or a rectangle from width_mm and height_mm.",
             {{"name", T::String, "Panel name", false},
              {"points", T::String, "Outline as JSON [x0, y0, x1, y1, ...] in mm, 3 points or more", false},
              {"width_mm", T::Number, "Rectangle width (when no points)", false},
              {"height_mm", T::Number, "Rectangle height (when no points)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Pattern pat;
                 documentPattern(doc, pat);
                 Panel p;
                 const std::string pts = argString(a, "points");
                 const std::string name = argString(a, "name", "Panel " + std::to_string(pat.panels.size() + 1));
                 if (!pts.empty()) {
                     Json j;
                     std::string error;
                     if (!Json::parse(pts, j, error) || !j.isArray() || j.size() % 2 || j.size() < 6)
                         return {false, "points must be a JSON array [x0, y0, x1, y1, ...] of 3 points or more"};
                     for (size_t k = 0; k + 1 < j.size() && k < 40000; k += 2)
                         p.points.push_back({float(j[k].asNumber()), float(j[k + 1].asNumber())});
                     p.curves.resize(p.points.size());
                     p.name = name;
                 } else {
                     const float w = float(argNumber(a, "width_mm", 0.0)), h = float(argNumber(a, "height_mm", 0.0));
                     if (!(w >= 1.0f && h >= 1.0f && w < 1e5f && h < 1e5f)) return {false, "Give points or width_mm and height_mm"};
                     float x = 0.0f;
                     for (const Panel& q : pat.panels) x = std::max(x, panelMax(q).x + 60.0f);
                     p = rectanglePanel(name, w, h, {x, 0.0f});
                 }
                 normalizePanel(p);
                 std::string error;
                 if (!validPanel(p, error)) return {false, error};
                 pat.panels.push_back(std::move(p));
                 setDocumentPattern(doc, pat);
                 return {true, "Added " + pat.panels.back().name + ": " + summary(pat)};
             }});
    reg.add({"garment_remove_panel", "Garments: remove a panel (and its seams).",
             {{"panel", T::String, "Panel name or number", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 CommandResult r;
                 Pattern pat;
                 if (!loadOrFail(doc, pat, r)) return r;
                 uint32_t i;
                 std::string error;
                 if (!findPanel(pat, argString(a, "panel"), i, error)) return {false, error};
                 const std::string name = pat.panels[i].name;
                 removePanel(pat, i);
                 setDocumentPattern(doc, pat);
                 return {true, "Removed " + name};
             }});
    reg.add({"garment_mirror_copy",
             "Garments: add a mirrored copy of a panel (the other sleeve or side), placed on the other side of the body.",
             {{"panel", T::String, "Panel name or number", true}, {"name", T::String, "The copy's name", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 CommandResult r;
                 Pattern pat;
                 if (!loadOrFail(doc, pat, r)) return r;
                 uint32_t i;
                 std::string error;
                 if (!findPanel(pat, argString(a, "panel"), i, error)) return {false, error};
                 const uint32_t j = mirroredCopy(pat, i, argString(a, "name"));
                 setDocumentPattern(doc, pat);
                 return {true, "Added " + pat.panels[j].name};
             }});
    reg.add({"garment_sew",
             "Garments: sew an edge range of one panel to an edge range of another (or the same) panel. Edges are "
             "numbered from 1 (edge 1 runs from point 1 to point 2); from/to are fractions of the edge (0-1). Sewn "
             "the right way, a's start meets b's end; flipped=true sews start to start (twists the fabric).",
             {{"a_panel", T::String, "First panel (name or number)", true},
              {"a_edge", T::Number, "Its edge (1-based)", true},
              {"a_from", T::Number, "Range start along it (default 0)", false},
              {"a_to", T::Number, "Range end (default 1)", false},
              {"b_panel", T::String, "Second panel", true},
              {"b_edge", T::Number, "Its edge (1-based)", true},
              {"b_from", T::Number, "Range start (default 0)", false},
              {"b_to", T::Number, "Range end (default 1)", false},
              {"flipped", T::Bool, "Sew start to start (default false)", false}},
             seamCommand});
    reg.add({"garment_unsew", "Garments: remove a seam.", {{"seam", T::Number, "Seam number (1-based, see garment_info)", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 CommandResult r;
                 Pattern pat;
                 if (!loadOrFail(doc, pat, r)) return r;
                 const double n = argNumber(a, "seam", 0);
                 if (!(n >= 1 && n <= double(pat.seams.size()))) return {false, "No such seam"};
                 pat.seams.erase(pat.seams.begin() + long(n - 1));
                 setDocumentPattern(doc, pat);
                 return {true, "Removed seam " + std::to_string(int(n))};
             }});
    reg.add({"garment_set", "Garments: the pattern's simulation settings.",
             {{"particle_mm", T::Number, "Distance between particles in mm (2-200; smaller = finer folds, slower)", false},
              {"fabric", T::String, "silk, cotton, denim or leather", false},
              {"thickness_mm", T::Number, "Thickness of the Part made from the garment (0 = a single surface)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 CommandResult r;
                 Pattern pat;
                 if (!loadOrFail(doc, pat, r)) return r;
                 if (a.count("particle_mm")) pat.particleDistance = float(std::clamp(argNumber(a, "particle_mm", 15), 2.0, 200.0));
                 if (a.count("thickness_mm")) pat.thickness = float(std::clamp(argNumber(a, "thickness_mm", 2), 0.0, 100.0));
                 if (a.count("fabric")) {
                     const std::string f = lower(argString(a, "fabric"));
                     if (f != "silk" && f != "cotton" && f != "denim" && f != "leather") return {false, "Unknown fabric \"" + f + "\""};
                     pat.fabric = f;
                 }
                 setDocumentPattern(doc, pat);
                 return {true, "Garment: " + summary(pat) + ", " + pat.fabric};
             }});
    reg.add({"garment_export",
             "Garments: write the pattern at real size for sewing: svg (sewing and cutting lines, notches, grain "
             "lines, names, a 50 mm scale square; print at 100%) or dxf (outlines in mm).",
             {{"path", T::String, "File to write (.svg or .dxf)", true}, {"format", T::String, "svg or dxf (default: from the path)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 CommandResult r;
                 Pattern pat;
                 if (!loadOrFail(doc, pat, r)) return r;
                 if (pat.empty()) return {false, "The pattern has no panels"};
                 const std::string path = argString(a, "path");
                 std::string fmt = lower(argString(a, "format"));
                 if (fmt.empty()) fmt = lower(path).size() > 4 && lower(path).substr(lower(path).size() - 4) == ".dxf" ? "dxf" : "svg";
                 std::string error;
                 if (!writeText(path, fmt == "dxf" ? patternToDxf(pat) : patternToSvg(pat), error)) return {false, error};
                 return {true, "Wrote " + std::to_string(pat.panels.size()) + " panels to " + path};
             }});
    reg.add({"garment_import", "Garments: add panels from an SVG (every closed shape, at the file's real size).",
             {{"path", T::String, "SVG file", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 std::ifstream f(path, std::ios::binary);
                 if (!f) return {false, "Could not open " + path};
                 f.seekg(0, std::ios::end);
                 const std::streamoff size = f.tellg();
                 if (size < 0 || size > 4 * 1024 * 1024) return {false, "The SVG is larger than 4 MB"};
                 f.seekg(0);
                 std::string text(size_t(size), '\0');
                 f.read(text.data(), size);
                 Pattern pat;
                 documentPattern(doc, pat);
                 std::vector<Panel> panels;
                 std::string error;
                 const int n = panelsFromSvg(text, panels, error);
                 if (!n) return {false, error};
                 // Beside what's there already.
                 float x = 0.0f;
                 for (const Panel& q : pat.panels) x = std::max(x, panelMax(q).x + 60.0f);
                 float minX = std::numeric_limits<float>::max(), minY = minX;
                 for (const Panel& p : panels) {
                     minX = std::min(minX, panelMin(p).x);
                     minY = std::min(minY, panelMin(p).y);
                 }
                 const glm::vec2 shift(x - minX, -minY);
                 for (Panel& p : panels) {
                     for (glm::vec2& q : p.points) q += shift;
                     for (EdgeCurve& c : p.curves) {
                         c.c1 += shift;
                         c.c2 += shift;
                     }
                 }
                 pat.panels.insert(pat.panels.end(), panels.begin(), panels.end());
                 setDocumentPattern(doc, pat);
                 return {true, "Imported " + std::to_string(n) + " panels" + (error.empty() ? "" : " (" + error + ")")};
             }});
    reg.add({"garment_clear", "Garments: remove the garment pattern from the scene.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 setDocumentPattern(doc, Pattern{});
                 return {true, "Garment pattern removed"};
             }});
}

} // namespace sf
