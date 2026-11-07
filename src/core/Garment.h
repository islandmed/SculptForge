// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Garments (docs/design/cloth.md): clothing made the way it is sewn. Pattern panels are flat pieces of fabric drawn
// at real size (millimetres); seams say which edges are sewn together; each panel is placed around the body; the
// cloth solver (core/Cloth.h, Drape) pulls the seams closed and lets the garment settle; the result becomes a Part
// whose UVs are the flat panel layout.
//
//  - Panel: a closed outline of points, counter-clockwise as seen from the right side (outside) of the fabric.
//    Edge i runs from point i to point i + 1 (the last edge back to point 0), straight or a cubic curve. Notches
//    mark matching points for sewing, the grain line the fabric's warp direction, the seam allowance the margin
//    added around the outline when it is cut (export only: the simulation uses the outline, the sewing line).
//  - Seam: a range of one panel edge sewn to a range of another (or the same panel's). Positions along an edge are
//    fractions of its length (0 = its start point, 1 = its end). Sewn the right way the two ranges run in opposite
//    directions (as two neighbouring faces of a surface share an edge), so side a's start meets side b's end;
//    `flipped` sews start to start, which twists the fabric (seamReport warns).
//  - Triangulation (triangulatePattern): every panel becomes even, nearly equilateral triangles at the particle
//    distance (a lattice along the grain, a constrained Delaunay triangulation of it with the outline as
//    constraints, then smoothing); both sides of a seam get the same number of points, so the seam's particles pair
//    one to one and weld cleanly after the simulation.
//  - Arrangement (analyzeBody, placePattern): slots around the body (front, back, sides, arms) found from its
//    shape, each panel wrapped around the torso or an arm so the seams start close.
//
// The pattern is saved in the project (Document::projectSettings["garment"]); SVG export writes the panels at real
// size (with seam allowance, notches, grain lines) for printing and sewing, DXF the outlines for cutters.
#pragma once

#include "core/Json.h"
#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct Document;
class CommandRegistry;

// garment_new, garment_info, garment_sew, garment_export, ... (core/GarmentCommands.cpp)
void registerGarmentCommands(CommandRegistry& registry);

namespace garment {

// A panel edge's shape: straight, or a cubic Bezier from its start point through c1, c2 to its end point (mm).
struct EdgeCurve {
    bool curved = false;
    glm::vec2 c1{0.0f}, c2{0.0f};
};

// A matching mark on an edge, `t` (0..1) along it.
struct Notch {
    uint32_t edge = 0;
    float t = 0.5f;
};

// Where a panel goes around the body (placePattern).
enum class Slot : uint8_t { Front, Back, Left, Right, LeftArm, RightArm, Free, Count };
const char* slotId(Slot s);    // "front", "back", "left", "right", "left_arm", "right_arm", "free"
const char* slotLabel(Slot s); // "Front", ...
bool slotFromId(const std::string& id, Slot& out);

struct Placement {
    Slot slot = Slot::Front;
    // Metres in the slot's frame: x across the panel (around the body), y along it (+ up), z away from the body.
    glm::vec3 offset{0.0f};
    bool wrap = true; // bent around the torso or the arm (false: flat, facing out of the slot)
};

struct Panel {
    std::string name = "Panel";
    std::vector<glm::vec2> points; // mm, the sewing line, counter-clockwise from the right side
    std::vector<EdgeCurve> curves; // one per edge (same count as points)
    std::vector<Notch> notches;
    float grainDegrees = 90.0f;    // warp direction, degrees from +x (90 = up the panel)
    float seamAllowance = 10.0f;   // mm
    Placement place;
};

struct SeamSide {
    uint32_t panel = 0, edge = 0;
    float t0 = 0.0f, t1 = 1.0f; // the sewn range along the edge, t0 < t1
};

struct Seam {
    SeamSide a, b;
    bool flipped = false; // a's start meets b's start (twists the fabric)
};

struct Pattern {
    std::vector<Panel> panels;
    std::vector<Seam> seams;
    float particleDistance = 15.0f; // mm between neighbouring particles (triangle edge length)
    std::string fabric = "cotton";  // a core/Cloth.h fabric preset id
    float thickness = 2.0f;         // mm, given to the Part made by Convert (0 = a single surface)
    bool empty() const { return panels.empty(); }
};

// --- Panel geometry (all in mm) ---

// Fixes up a panel: one curve per point, counter-clockwise outline (reversing points, curves and notches if not),
// notches on real edges.
void normalizePanel(Panel& p);
// Edge `e` as a polyline from its start point to its end point (curves flattened to within `tolerance` mm).
std::vector<glm::vec2> edgePolyline(const Panel& p, uint32_t e, float tolerance = 0.2f);
float edgeLength(const Panel& p, uint32_t e);
// The point `t` (0..1 of its length) along edge `e`, and the edge's direction there.
glm::vec2 edgePoint(const Panel& p, uint32_t e, float t, glm::vec2* tangent = nullptr);
// The whole outline as one closed polyline (no repeated first point).
std::vector<glm::vec2> outline(const Panel& p, float tolerance = 0.2f);
float panelArea(const Panel& p); // mm^2, > 0 when counter-clockwise
float perimeter(const Panel& p);
glm::vec2 panelMin(const Panel& p);
glm::vec2 panelMax(const Panel& p);
// False (with a reason) for fewer than 3 points, a zero-area or self-crossing outline, or edges shorter than 1 mm.
bool validPanel(const Panel& p, std::string& error);
// The outline grown outward by `distance` mm (the cutting line), as a closed polyline.
std::vector<glm::vec2> offsetOutline(const Panel& p, float distance);

// --- Pattern edits (each keeps seams pointing at the right panels and edges) ---

// Flips a panel left to right about its own centre (still counter-clockwise; seams on it keep their edges).
void mirrorPanel(Pattern& pat, uint32_t panel);
// A copy of the panel `offsetMm` to the right, named "<name> 2" (or `name`); returns its index.
uint32_t duplicatePanel(Pattern& pat, uint32_t panel, glm::vec2 offsetMm, const std::string& name = {});
// A mirrored copy (the other sleeve, the other half): mirrored about its centre and moved beside it; its slot is
// the mirrored one (left arm <-> right arm, left <-> right). Returns its index.
uint32_t mirroredCopy(Pattern& pat, uint32_t panel, const std::string& name = {});
// Unfolds a panel drawn as a half about straight edge `edge` (the fold line): the panel becomes the whole shape,
// the fold edge disappears. False when the edge is curved.
bool unfoldPanel(Pattern& pat, uint32_t panel, uint32_t edge, std::string& error);
void removePanel(Pattern& pat, uint32_t panel); // and its seams
// Splits edge `e` at `t` with a new point (curves split exactly); seams and notches on the edge move with it.
void splitEdge(Pattern& pat, uint32_t panel, uint32_t edge, float t);
// Removes point `i` (the two edges beside it become one straight edge); false when the panel would have < 3.
bool removePoint(Pattern& pat, uint32_t panel, uint32_t point);

// --- Seams ---

// Adds a seam; false (with the reason) when a side's range is empty or overlaps another seam on the same edge.
bool addSeam(Pattern& pat, const Seam& seam, std::string& error);
struct SeamCheck {
    float lengthA = 0.0f, lengthB = 0.0f; // mm
    bool lengthsMatch = true;             // within 2% (and 3 mm): more is eased or gathered in
    bool twisted = false;                 // flipped: the fabric turns over along the seam
    std::string text;                     // "Front.right <-> Back.left: 412 / 412 mm"
};
SeamCheck checkSeam(const Pattern& pat, const Seam& seam);
float seamSideLength(const Pattern& pat, const SeamSide& s);
// "Front", or "Front edge 3" when the panel has no name for it.
std::string edgeName(const Pattern& pat, uint32_t panel, uint32_t edge);
// Problems the pattern has (invalid panels, overlapping or twisted seams, mismatched lengths), one line each.
std::vector<std::string> patternProblems(const Pattern& pat);

// --- Triangulation ---

struct SeamLink {
    uint32_t a = 0, b = 0; // particles (vertices) sewn together
};

struct GarmentMesh {
    std::vector<glm::vec2> flat;        // mm, pattern sheet coordinates
    std::vector<uint32_t> panelOf;      // per vertex
    std::vector<glm::uvec3> triangles;  // counter-clockwise in the sheet (seen from the right side)
    std::vector<uint8_t> border;        // per vertex: 1 on a panel outline
    std::vector<SeamLink> links;        // seam particle pairs (one per point along each seam)
    std::vector<uint32_t> panelFirstVertex, panelFirstTriangle; // where each panel starts (+ one past the end)
    size_t vertexCount() const { return flat.size(); }
};

// Triangulates every panel at the pattern's particle distance. False (with the reason) when a panel is invalid or
// a seam is unusable.
bool triangulatePattern(const Pattern& pat, GarmentMesh& out, std::string& error);
// Quality numbers for tests and the stats readout: the shortest/longest edge over the particle distance, the
// smallest angle in degrees.
struct MeshQuality {
    float minEdge = 0.0f, maxEdge = 0.0f, minAngle = 0.0f;
};
MeshQuality meshQuality(const GarmentMesh& m, float particleDistance);

// --- Arrangement ---

// What placePattern needs to know about the body, found from its vertices (Y up, facing +Z, metres).
struct BodyFrame {
    AABB bounds;
    float centerX = 0.0f, centerZ = 0.0f;
    float shoulderY = 0.0f;           // the top of the torso panels
    float torsoHalfWidth = 0.15f;     // x
    float torsoFrontZ = 0.1f, torsoBackZ = -0.1f;
    bool arms = false;                // arms held out (T or A pose) were found
    glm::vec3 leftShoulder{0.0f}, rightShoulder{0.0f}; // where each arm starts (character's left = +X)
    glm::vec3 leftArmDir{1.0f, 0.0f, 0.0f}, rightArmDir{-1.0f, 0.0f, 0.0f};
    float armRadius = 0.05f;
};
BodyFrame analyzeBody(const Mesh& body);
// A body frame for no body: a 1.75 m figure standing at the origin.
BodyFrame defaultBody();

// Gives panels without a chosen slot one from their names (front, back, sleeve/arm with left/right, side).
void autoSlots(Pattern& pat);
// The 3D position (metres) of pattern point `flat` (mm) of panel `p` placed on `body`; `gap` metres off it.
glm::vec3 placePoint(const Panel& p, glm::vec2 flat, const BodyFrame& body, float gap = 0.03f);
// Every vertex of `m` placed around the body (placed) and the flat rest shape in 3D (rest: sheet x, y, z = 0, in
// metres): what the cloth solver starts from.
void placePattern(const Pattern& pat, const GarmentMesh& m, const BodyFrame& body, std::vector<glm::vec3>& placed,
                  std::vector<glm::vec3>& rest, float gap = 0.03f);

// --- After the simulation ---

// The garment as a Part: seam pairs welded, the panel layout as UVs (the whole sheet fit into 0..1, aspect kept),
// one face group per panel (1, 2, ...), thickened by `thicknessMm` outward-inward (0 = a single surface: the
// inner side and rim get face group 0x8000 + panel).
Mesh garmentToMesh(const GarmentMesh& m, const std::vector<glm::vec3>& positions, float thicknessMm);

// --- Templates ---

// A T-shirt pattern (front, back, two sleeves, all seams sewn, slots set): `chestMm` the body's chest girth,
// `lengthMm` the body length from the shoulder.
Pattern tshirtPattern(float chestMm = 960.0f, float lengthMm = 700.0f);
// A simple rectangle panel (`w` x `h` mm, lower-left corner at `at`).
Panel rectanglePanel(const std::string& name, float w, float h, glm::vec2 at = {0.0f, 0.0f});

// --- Saving ---

Json patternToJson(const Pattern& pat);
// False (pattern untouched) when `j` is not a pattern; bad panels are left out with a note in `error`.
bool patternFromJson(const Json& j, Pattern& out, std::string& error);
// The document's pattern (projectSettings["garment"]); false when there is none.
bool documentPattern(const Document& doc, Pattern& out);
// Stores the pattern in the document (an empty pattern removes it) and marks the document modified.
void setDocumentPattern(Document& doc, const Pattern& pat);

// --- Files ---

// The pattern at real size: each panel's sewing line (solid), cutting line (seam allowance, dashed), notches,
// grain line and name, laid out as in the pattern sheet. Width and height in mm.
std::string patternToSvg(const Pattern& pat);
// The outlines (sewing and cutting lines) as an ASCII DXF in millimetres (LWPOLYLINE, layers SEW and CUT).
std::string patternToDxf(const Pattern& pat);
// Panels from SVG: every closed path, polygon, polyline or rect (in the document's real units: mm, cm, in, or px
// at 96 per inch; the y axis flipped up). Curves stay curves (quadratics as cubics). Returns the panel count
// added to `out`; 0 with `error` when nothing usable was found. At most 4 MB of text, 512 panels, 20,000 points.
int panelsFromSvg(const std::string& svg, std::vector<Panel>& out, std::string& error);

} // namespace garment
} // namespace sf
