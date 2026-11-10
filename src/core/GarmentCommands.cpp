// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// garment_* commands (core/Garment.h): make and edit the scene's garment pattern, sew it, write and read
// pattern files. The pattern lives in the project (Document::projectSettings["garment"]).
#include "core/Commands.h"
#include "core/Garment.h"
#include "core/GarmentPrint.h"
#include "core/GarmentTemplates.h"

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
    // garment_new and garment_size take every size chart measurement as <key>_mm.
    std::vector<ParamSpec> sizeParams;
    for (const MeasureSpec& m : measureSpecs())
        sizeParams.push_back({std::string(m.key) + "_mm", T::Number, m.description, false});
    // Measurements given are stored in the project's size chart (and used by the next template too).
    auto applySizes = [](Document& doc, const Args& a, std::string& changed) {
        SizeChart chart = documentSizeChart(doc);
        bool any = false;
        for (const MeasureSpec& m : measureSpecs()) {
            const std::string key = std::string(m.key) + "_mm";
            if (!a.count(key)) continue;
            chart.set(m.key, float(argNumber(a, key, m.fallback)));
            changed += (changed.empty() ? "" : ", ") + std::string(m.key);
            any = true;
        }
        if (any) setDocumentSizeChart(doc, chart);
        return chart;
    };
    std::string templateList = "empty";
    for (const TemplateSpec& t : templateSpecs()) templateList += "; " + std::string(t.description);
    std::vector<ParamSpec> newParams = {
        {"template", T::String, "tshirt (default), trousers, skirt, skirt_aline, hoodie, dress, cloak, glove or empty",
         false},
        {"length_mm", T::Number, "The template's main length in mm (see the template list; default from the size chart)",
         false},
        {"add", T::Bool, "Add the template beside the pattern already in the scene (an outfit) instead of replacing it",
         false},
        {"particle_mm", T::Number, "Distance between the cloth's particles in mm (default 15; gloves 3-6, from the hand)", false}};
    newParams.insert(newParams.end(), sizeParams.begin(), sizeParams.end());
    reg.add({"garment_new",
             "Garments: start a garment pattern in the scene (saved with the project) from a template, flat panels at "
             "real size made from the project's size chart (give any measurement as <name>_mm to change it), sewn "
             "the way the garment is made. Templates: " + templateList + ".",
             newParams,
             [applySizes](Document& doc, const Args& a) -> CommandResult {
                 const std::string t = lower(argString(a, "template", "tshirt"));
                 std::string changed;
                 const SizeChart chart = applySizes(doc, a, changed);
                 Pattern pat;
                 std::string error;
                 if (t != "empty" && !makeTemplate(t == "t-shirt" ? "tshirt" : t, chart,
                                                   float(argNumber(a, "length_mm", 0.0)), pat, error))
                     return {false, error};
                 const bool add = argBool(a, "add", false);
                 Pattern old;
                 if (add && t != "empty" && documentPattern(doc, old) && !old.empty()) {
                     appendPattern(old, pat);
                     pat = std::move(old);
                 }
                 if (a.count("particle_mm"))
                     pat.particleDistance = float(std::clamp(argNumber(a, "particle_mm", 15.0), 2.0, 200.0));
                 const std::string sizes = changed.empty() ? "" : " (size chart: " + changed + " set)";
                 if (pat.empty()) {
                     doc.projectSettings.set("garment", patternToJson(pat)); // an empty pattern to draw in
                     doc.modified = true;
                     return {true, "New empty garment pattern" + sizes};
                 }
                 setDocumentPattern(doc, pat);
                 return {true, std::string(add ? "Added " : "New ") + t + ": " + summary(pat) + sizes};
             }});
    std::vector<ParamSpec> chartParams = {{"reset", T::Bool, "Every measurement back to its default first", false}};
    chartParams.insert(chartParams.end(), sizeParams.begin(), sizeParams.end());
    reg.add({"garment_size",
             "Garments: the project's size chart (body measurements in mm that garment_new's templates are made from). "
             "Give any measurement as <name>_mm to set it; with none, lists the chart.",
             chartParams,
             [applySizes](Document& doc, const Args& a) -> CommandResult {
                 if (argBool(a, "reset", false)) setDocumentSizeChart(doc, SizeChart{});
                 std::string changed;
                 const SizeChart chart = applySizes(doc, a, changed);
                 std::ostringstream s;
                 s << (changed.empty() ? "Size chart" : "Size chart (" + changed + " set)");
                 const auto& specs = measureSpecs();
                 for (size_t i = 0; i < specs.size(); ++i) {
                     char buf[160];
                     std::snprintf(buf, sizeof buf, "\n%s_mm = %.0f  (%s)", specs[i].key, double(chart.mm[i]),
                                   specs[i].description);
                     s << buf;
                 }
                 return {true, s.str()};
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
             "Garments: add a mirrored copy of a panel (the other sleeve or side), placed beside it in the pattern.",
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
             "Garments: write the pattern at real size for sewing: pdf (tiled A4 or Letter pages to print at home: trim "
             "lines, matching triangles, page names, a 50 mm scale square on A1), svg_pages (the same pages, one SVG "
             "each: <name>-A1.svg, ...), svg (the whole sheet: sewing and cutting lines, notches, grain lines, names, "
             "the 50 mm square; for a plotter or print shop) or dxf (outlines in mm, for cutters).",
             {{"path", T::String, "File to write (.pdf, .svg or .dxf)", true},
              {"format", T::String, "pdf, svg_pages, svg or dxf (default: from the path)", false},
              {"paper", T::String, "pdf / svg_pages: a4 (default) or letter", false},
              {"landscape", T::Bool, "pdf / svg_pages: pages on their side", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 CommandResult r;
                 Pattern pat;
                 if (!loadOrFail(doc, pat, r)) return r;
                 if (pat.empty()) return {false, "The pattern has no panels"};
                 const std::string path = argString(a, "path");
                 const std::string lp = lower(path);
                 auto ends = [&](const char* ext) {
                     const std::string e(ext);
                     return lp.size() > e.size() && lp.compare(lp.size() - e.size(), e.size(), e) == 0;
                 };
                 std::string fmt = lower(argString(a, "format"));
                 if (fmt.empty()) fmt = ends(".dxf") ? "dxf" : ends(".pdf") ? "pdf" : "svg";
                 PrintOptions opt;
                 const std::string paper = lower(argString(a, "paper", "a4"));
                 if (paper != "a4" && paper != "letter") return {false, "Unknown paper \"" + paper + "\" (a4 or letter)"};
                 opt.paper = paper == "letter" ? Paper::Letter : Paper::A4;
                 opt.landscape = argBool(a, "landscape", false);
                 std::string error;
                 if (fmt == "pdf" || fmt == "svg_pages") {
                     const TileLayout t = tileLayout(pat, opt);
                     const std::string pages = std::to_string(t.pages()) + " " + (paper == "letter" ? "Letter" : "A4") +
                                               " pages (a grid of " + std::to_string(t.rows) + " rows x " + std::to_string(t.cols) + ", blank ones left out)";
                     if (fmt == "pdf") {
                         std::string pdf;
                         if (!patternToTiledPdf(pat, opt, pdf, error) || !writeText(path, pdf, error)) return {false, error};
                         return {true, "Wrote " + pages + " to " + path + ": print at 100%, check the 50 mm square on A1"};
                     }
                     std::vector<SvgPageFile> svgs;
                     if (!patternToTiledSvgs(pat, opt, svgs, error)) return {false, error};
                     const std::string stem = ends(".svg") ? path.substr(0, path.size() - 4) : path;
                     for (const SvgPageFile& page : svgs)
                         if (!writeText(stem + "-" + page.name + ".svg", page.svg, error)) return {false, error};
                     return {true, "Wrote " + pages + " as " + stem + "-A1.svg ..."};
                 }
                 if (fmt != "svg" && fmt != "dxf") return {false, "Unknown format \"" + fmt + "\" (pdf, svg_pages, svg or dxf)"};
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
    reg.add({"garment_edit",
             "Garments: edit a panel (the Pattern window's edits, one undo step each there). op: set_panel (its outline "
             "from points [x0, y0, ...] in mm and curves [[edge, c1x, c1y, c2x, c2y], ...] with 0-based edges; seams "
             "stay when the edge count does), split (a new point at t along an edge), remove_point, move (dx, dy mm), "
             "duplicate, mirror (in place), unfold (about a straight fold edge) or rename.",
             {{"op", T::String, "set_panel, split, remove_point, move, duplicate, mirror, unfold or rename", true},
              {"panel", T::String, "Panel name or number", true},
              {"points", T::String, "set_panel: outline as JSON [x0, y0, x1, y1, ...] in mm", false},
              {"curves", T::String, "set_panel: curved edges as JSON [[edge, c1x, c1y, c2x, c2y], ...]", false},
              {"edge", T::Number, "split, unfold: edge (1-based)", false},
              {"t", T::Number, "split: where along the edge (0-1, default 0.5)", false},
              {"point", T::Number, "remove_point: point (1-based)", false},
              {"dx", T::Number, "move: mm right", false},
              {"dy", T::Number, "move: mm up", false},
              {"name", T::String, "duplicate, rename: the name", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 CommandResult r;
                 Pattern pat;
                 if (!loadOrFail(doc, pat, r)) return r;
                 uint32_t i;
                 std::string error;
                 if (!findPanel(pat, argString(a, "panel"), i, error)) return {false, error};
                 const std::string op = lower(argString(a, "op"));
                 Panel& p = pat.panels[i];
                 const uint32_t edges = uint32_t(p.points.size());
                 auto edgeArg = [&](uint32_t& e) {
                     const double v = argNumber(a, "edge", 0);
                     if (!(v >= 1 && v <= double(edges))) return false;
                     e = uint32_t(v) - 1;
                     return true;
                 };
                 std::string done;
                 if (op == "set_panel") {
                     Json pj, cj;
                     if (!Json::parse(argString(a, "points"), pj, error) || !pj.isArray() || pj.size() % 2 || pj.size() < 6 ||
                         pj.size() > 40000)
                         return {false, "points must be a JSON array [x0, y0, x1, y1, ...] of 3 to 20,000 points"};
                     Panel q = p;
                     q.points.clear();
                     for (size_t k = 0; k + 1 < pj.size(); k += 2)
                         q.points.push_back({float(pj[k].asNumber()), float(pj[k + 1].asNumber())});
                     q.curves.assign(q.points.size(), EdgeCurve{});
                     if (const std::string cs = argString(a, "curves"); !cs.empty()) {
                         if (!Json::parse(cs, cj, error) || !cj.isArray()) return {false, "curves must be a JSON array"};
                         for (const Json& c : cj.items()) {
                             if (!c.isArray() || c.size() != 5) return {false, "Each curve is [edge, c1x, c1y, c2x, c2y]"};
                             const double e = c[0].asNumber(-1);
                             if (!(e >= 0 && e < double(q.points.size()))) return {false, "A curve's edge is out of range"};
                             q.curves[size_t(e)] = {true, {float(c[1].asNumber()), float(c[2].asNumber())},
                                                    {float(c[3].asNumber()), float(c[4].asNumber())}};
                         }
                     }
                     size_t budget = kOutlineBudget;
                     if (!validPanel(q, error, &budget)) return {false, error};
                     if (panelArea(q) < 0.0f) return {false, q.name + "'s outline must run counter-clockwise"};
                     const bool sameEdges = q.points.size() == edges;
                     if (!sameEdges) {
                         q.edgeNames.clear();
                         pat.seams.erase(std::remove_if(pat.seams.begin(), pat.seams.end(),
                                                        [&](const Seam& sm) { return sm.a.panel == i || sm.b.panel == i; }),
                                         pat.seams.end());
                     }
                     normalizePanel(q);
                     p = std::move(q);
                     done = p.name + (sameEdges ? " reshaped" : " reshaped (its seams were removed: the edges changed)");
                 } else if (op == "split") {
                     uint32_t e;
                     if (!edgeArg(e)) return {false, "No such edge on " + p.name};
                     splitEdge(pat, i, e, float(std::clamp(argNumber(a, "t", 0.5), 0.01, 0.99)));
                     done = "Split " + edgeName(pat, i, e);
                 } else if (op == "remove_point") {
                     const double v = argNumber(a, "point", 0);
                     if (!(v >= 1 && v <= double(edges))) return {false, "No such point on " + p.name};
                     if (!removePoint(pat, i, uint32_t(v) - 1)) return {false, p.name + " needs at least 3 points"};
                     if (!validPanel(pat.panels[i], error)) return {false, error};
                     done = "Removed point " + std::to_string(int(v)) + " of " + pat.panels[i].name;
                 } else if (op == "move") {
                     const glm::vec2 d(float(std::clamp(argNumber(a, "dx", 0), -1e5, 1e5)), float(std::clamp(argNumber(a, "dy", 0), -1e5, 1e5)));
                     for (glm::vec2& q : p.points) q += d;
                     for (EdgeCurve& c : p.curves) c.c1 += d, c.c2 += d;
                     if (!validPanel(p, error)) return {false, error};
                     done = "Moved " + p.name;
                 } else if (op == "duplicate") {
                     const uint32_t j = duplicatePanel(pat, i, {panelMax(p).x - panelMin(p).x + 60.0f, 0.0f}, argString(a, "name"));
                     done = "Added " + pat.panels[j].name;
                 } else if (op == "mirror") {
                     mirrorPanel(pat, i);
                     done = "Mirrored " + p.name;
                 } else if (op == "unfold") {
                     uint32_t e;
                     if (!edgeArg(e)) return {false, "No such edge on " + p.name};
                     if (!unfoldPanel(pat, i, e, error)) return {false, error};
                     done = "Unfolded " + pat.panels[i].name;
                 } else if (op == "rename") {
                     const std::string name = argString(a, "name").substr(0, 128);
                     if (name.empty()) return {false, "Give a name"};
                     p.name = name;
                     done = "Renamed to " + name;
                 } else {
                     return {false, "Unknown op \"" + op + "\""};
                 }
                 setDocumentPattern(doc, pat);
                 return {true, done};
             }});
    reg.add({"garment_clear", "Garments: remove the garment pattern from the scene.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 setDocumentPattern(doc, Pattern{});
                 return {true, "Garment pattern removed"};
             }});
}

} // namespace sf
