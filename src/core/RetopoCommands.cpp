// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// qremesh and project_all: the scriptable side of Geometry > QRemesh and SubTool > Project (core/QRemesh.h,
// core/Projection.h). The app runs QRemesh on a worker thread and applies the result with applyQRemesh.
#include "core/Commands.h"
#include "core/KeepMasked.h"
#include "core/Projection.h"
#include "core/QRemesh.h"
#include "core/TextureTransfer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {

using T = ParamSpec::Type;

int symmetryAxisArg(const std::string& s) {
    if (s == "x" || s == "X") return 0;
    if (s == "y" || s == "Y") return 1;
    if (s == "z" || s == "Z") return 2;
    return -1;
}

} // namespace

std::string qualitySummary(const QuadQuality& q) {
    if (q.quads == 0) return {};
    char s[256];
    std::snprintf(s, sizeof s, ". Quality: %zu poles (%zu x3, %zu x5, %zu other), corners %.1f deg off square", q.poles,
                  q.valence[3], q.valence[5], q.poles - q.valence[3] - q.valence[5], double(q.angleDeviation));
    std::string out = s;
    if (q.measured) {
        std::snprintf(s, sizeof s, ", distance to the sculpt %.3f%% mean / %.2f%% max of its size", double(q.meanDistance),
                      double(q.hausdorff));
        out += s;
    }
    return out;
}

std::string applyQRemesh(Document& doc, Mesh result, bool keepOriginal, const QRemeshStats& st) {
    const size_t before = doc.mesh.triangleCount();
    if (keepOriginal) {
        const SubTool& active = doc.activeSubtoolInfo();
        const int at = doc.activeSubtool + 1;
        doc.addSubtool(doc.mesh, active.name + " Original", false, at);
        doc.subtools[size_t(at)].visible = false;
        doc.subtools[size_t(at)].qremeshGuides = doc.qremeshGuides; // to remesh it again with them
    }
    doc.replaceMeshUndoable(std::move(result), "Quad Remesh");
    const size_t faces = st.quads + st.triangles;
    std::string sym;
    if (st.symmetryAxis >= 0) {
        const char axis = char('X' + st.symmetryAxis);
        char s[160];
        if (!st.symmetryUsed)
            std::snprintf(s, sizeof s, "; Symmetry off: the model isn't mirror-symmetric across %c", axis);
        else if (std::abs(st.symmetryCenter) > 1e-6f)
            std::snprintf(s, sizeof s, "; mirrored across %c = %.3g (the model's middle)", axis, double(st.symmetryCenter));
        else
            std::snprintf(s, sizeof s, "; mirrored across %c", axis);
        sym = s;
    }
    if (st.guides) sym += "; followed " + std::to_string(st.guides) + (st.guides == 1 ? " guide" : " guides");
    if (st.guidesSkipped)
        sym += "; " + std::to_string(st.guidesSkipped) + (st.guidesSkipped == 1 ? " guide is" : " guides are") +
               " off the surface and were ignored (draw them again after moving the Part)";
    if (st.method == QRemeshMethod::Legacy) sym += "; legacy method";
    else if (st.fallbackPieces)
        sym += "; " + std::to_string(st.fallbackPieces) + (st.fallbackPieces == 1 ? " piece" : " pieces") + " used the legacy method";
    sym += qualitySummary(st.quality);
    char buf[768];
    std::snprintf(buf, sizeof buf, "Quad Remesh: %zu quads%s from %zu triangles (%.0f%% quads%s) in %.1f s%s%s", st.quads,
                  st.triangles ? (" + " + std::to_string(st.triangles) + " triangles").c_str() : "", before,
                  faces ? 100.0 * double(st.quads) / double(faces) : 0.0,
                  st.nonManifoldEdges ? (", " + std::to_string(st.nonManifoldEdges) + " non-manifold edges").c_str() : "",
                  st.seconds, sym.c_str(), keepOriginal ? "; the original is kept as a hidden Part" : "");
    return buf;
}

void registerRetopoCommands(CommandRegistry& reg) {
    reg.add({"qremesh",
             "Quad Remesh (like ZBrush's ZRemesher): automatic quad retopology of the active Part. Builds clean, "
             "all-quad topology with edge loops that follow the form, close to the target polygon count. Sculpt "
             "layers are baked; mask and vertex paint carry over. Undoable. Use project_all afterwards (from a kept "
             "original) to bring fine detail back onto a divided copy.",
             {{"target", T::Number, "Target polygon (quad) count, 16-2000000. Default 5000.", false},
              {"method", T::String, "'v2' (default): Quad Remesh 2, a curvature-aligned integer-grid layout with poles only "
                                    "where the form needs them, guides as exact edge loops and a quality report. "
                                    "'legacy': the 0.9 method.", false},
              {"quality", T::Bool, "Measure the result (poles, valences, corner angles, distance to the sculpt) and "
                                   "report it. Default true.", false},
              {"adaptive", T::Number, "Adaptive Size 0-1: 0 = even quads, 1 = small quads in curved areas, big "
                                      "ones on flat areas. Default 0.5.", false},
              {"keep_groups", T::Bool, "Keep Groups: edge loops follow the face group borders and the groups are kept.", false},
              {"detect_edges", T::Bool, "Detect Edges: hard edges get an edge loop (v2: faces meeting at more than about "
                                        "60 degrees along a line a few quads long; legacy: over 50 degrees). Default true.", false},
              {"use_polypaint", T::Bool, "Use the vertex paint as a density guide: red areas get up to 4x the quads, blue "
                                         "areas down to a quarter.", false},
              {"symmetry", T::String, "Mirror the topology across x, y or z. The mirror plane is found from the model (the "
                                      "world's centre plane, or the model's own middle when it sits off-centre); a model "
                                      "that isn't mirror-symmetric is remeshed without symmetry. Default: none.", false},
              {"symmetry_center", T::Number, "Use this mirror plane instead (its coordinate along the symmetry axis, e.g. 0 "
                                             "for the world's centre).", false},
              {"keep_original", T::Bool, "Keep the original as a hidden Part below (for project_all). Default false.", false},
              {"smooth", T::Number, "Relaxation passes on the surface (0-30). Default 6.", false},
              {"seed", T::Number, "Legacy method: random start of the direction field (results are repeatable per seed). "
                                  "Quad Remesh 2 has no random start.", false},
              {"keep_texture", T::Bool, "Keep a painted texture: Auto UV the result and project the texture onto it. "
                                        "Default true.", false},
              {"guides", T::Bool, "Follow the Part's Quad Remesh Guides (curves drawn with qremesh_guide): an edge loop "
                                  "runs along each. Default true.", false},
              {"curves_strength", T::Number, "Curves Strength 0-100: how far around each guide the edge flow follows "
                                             "it (0 = just the loop on the curve, 100 = about six quads to each side). "
                                             "Default 50.", false},
              {"keep_masked", T::Bool, "Keep masked: masked areas keep their triangles exactly as a fixed piece; the "
                                       "rest is remeshed and joined to them watertight. Default false.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.triangleCount() == 0) return {false, "Quad Remesh: the Part is empty"};
                 const std::vector<uint8_t> keep =
                     argBool(a, "keep_masked", false) ? keepMaskedTriangles(doc.mesh) : std::vector<uint8_t>();
                 const bool keeping = std::find(keep.begin(), keep.end(), 1) != keep.end();
                 if (keeping && std::find(keep.begin(), keep.end(), 0) == keep.end())
                     return {false, "Quad Remesh: everything is masked, so nothing would change"};
                 QRemeshOptions o;
                 const std::string method = argString(a, "method");
                 if (method == "legacy" || method == "Legacy") o.method = QRemeshMethod::Legacy;
                 else if (!method.empty() && method != "v2" && method != "V2")
                     return {false, "Quad Remesh: method must be v2 or legacy"};
                 o.measureQuality = argBool(a, "quality", true);
                 o.targetQuads = int(std::clamp(argNumber(a, "target", 5000), 16.0, 2'000'000.0));
                 o.adaptive = float(std::clamp(argNumber(a, "adaptive", 0.5), 0.0, 1.0));
                 o.keepGroups = argBool(a, "keep_groups", false);
                 o.detectEdges = argBool(a, "detect_edges", true);
                 o.usePolypaint = argBool(a, "use_polypaint", false);
                 o.symmetryAxis = symmetryAxisArg(argString(a, "symmetry"));
                 if (a.contains("symmetry_center")) {
                     o.symmetryAuto = false;
                     o.symmetryCenter = float(argNumber(a, "symmetry_center", 0.0));
                 }
                 o.smoothIterations = int(std::clamp(argNumber(a, "smooth", 6), 0.0, 30.0));
                 o.seed = uint32_t(std::max(0.0, argNumber(a, "seed", 1)));
                 if (argBool(a, "guides", true)) o.guides = doc.qremeshGuides;
                 o.guideStrength = float(std::clamp(argNumber(a, "curves_strength", 50.0), 0.0, 100.0) / 100.0);
                 Mesh out;
                 std::string err, keepNote;
                 QRemeshStats st;
                 if (keeping) { // remesh the unmasked part on its own, then put the masked part back beside it
                     if (!qremesh(unmaskedPart(doc.mesh, keep), o, out, err, {}, &st)) return {false, err};
                     Mesh stitched;
                     if (!stitchKeptTriangles(doc.mesh, keep, out, stitched, err, &keepNote)) return {false, "Quad Remesh (Keep masked): " + err};
                     out = std::move(stitched);
                 } else if (!qremesh(doc.mesh, o, out, err, {}, &st)) return {false, err};
                 const std::string texture = carryTextureNote(doc.mesh, out, argBool(a, "keep_texture", true));
                 return {true, applyQRemesh(doc, std::move(out), argBool(a, "keep_original", false), st) + texture +
                                   (keepNote.empty() ? "" : "; Keep masked: " + keepNote)};
             }});

    reg.add({"project_all",
             "Project All (Part palette): move the active Part's vertices onto the surface of the other visible "
             "Parts and take their vertex paint, e.g. after Quad Remesh and a few subdivisions, to get the sculpted detail "
             "back. Respects the mask (masked vertices stay). One undo step.",
             {{"distance", T::Number, "Dist: how far each vertex searches along its normal, as a fraction of the "
                                      "Part's bounding-box diagonal (0.001-1). Default 0.02.", false},
              {"mean", T::Bool, "Mean: with surface on both sides, settle halfway between them. Default false.", false},
              {"blur", T::Number, "PA Blur: smoothing passes over the projected offsets (0-20). Default 0.", false},
              {"geometry", T::Bool, "Project the shape. Default true.", false},
              {"polypaint", T::Bool, "Project the vertex paint (when a source has any). Default true.", false},
              {"source", T::String, "Project only from the Part with this name (default: all visible ones).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 ProjectOptions o;
                 o.distance = float(std::clamp(argNumber(a, "distance", 0.02), 0.001, 1.0));
                 o.mean = argBool(a, "mean", false);
                 o.blur = int(std::clamp(argNumber(a, "blur", 0), 0.0, 20.0));
                 o.geometry = argBool(a, "geometry", true);
                 o.polypaint = argBool(a, "polypaint", true);
                 int source = -1;
                 if (a.contains("source")) {
                     source = doc.findSubtool(argString(a, "source"));
                     if (source < 0) return {false, "No Part called '" + argString(a, "source") + "'"};
                     if (source == doc.activeSubtool) return {false, "Project All can't project a Part onto itself"};
                 }
                 std::string msg;
                 const bool ok = projectAll(doc, o, msg, source);
                 return {ok, msg};
             }});
}

} // namespace sf
