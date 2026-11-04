// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BrushPacks.h"

#include <utility>

namespace sf {

namespace {

using Dir = BrushRecipe::Direction;
using Plane = BrushRecipe::Plane;
using Pat = BrushRecipe::Pattern;

struct PackInfo {
    const char* name;
    const char* help;
};

const PackInfo kPacks[] = {
    {"Clay & Form", "Block in, build up and refine big forms: clay strokes, planes, and smooth blends."},
    {"Hard Surface", "Crisp planes, panel lines, bevels, rivets and bolts for props, armour and machines."},
    {"Organic & Skin", "Pores, wrinkles, folds, veins and scales for creatures and portraits."},
    {"Cloth & Folds", "Folds, seams, stitches, weave and knit for clothing and soft goods."},
    {"Damage & Wear", "Cracks, chips, dents, scratches and erosion for weathered stone, wood and metal."},
    {"Ornament", "Beads, ropes, filigree, studs and carved patterns for jewellery, trims and decoration."},
};

// Adds brushes to `out` for one pack: b("Name", "One line.", [](BrushRecipe& r) { ... }).
struct PackBuilder {
    std::vector<BrushRecipe>& out;
    const char* pack;
    template <class F>
    void operator()(const char* name, const char* description, F&& tune) {
        BrushRecipe r;
        r.name = name;
        r.description = description;
        r.origin.pack = pack;
        tune(r);
        r.clamp();
        out.push_back(std::move(r));
    }
};

void clayAndForm(PackBuilder b) {
    b("Block Former", "Big, flat-topped clay strokes that stack fast, for blocking in primary forms.", [](BrushRecipe& r) {
        r.depth = 0.35f; r.plane = Plane::Clay; r.planeAmount = 0.7f; r.hardness = 0.25f; r.alpha = "Square";
        r.spacing = 0.15f; r.smooth = 0.1f;
    });
    b("Soft Clay", "Gentle round clay that builds in thin layers and blends into the surface.", [](BrushRecipe& r) {
        r.depth = 0.22f; r.plane = Plane::Clay; r.planeAmount = 0.5f; r.smooth = 0.15f; r.pressureCurve = 0.8f;
    });
    b("Clay Strip", "Narrow, crisp strips of clay with a hard edge, for muscle fibres and planes.", [](BrushRecipe& r) {
        r.depth = 0.25f; r.plane = Plane::Clay; r.planeAmount = 0.7f; r.hardness = 0.3f; r.alpha = "Square";
        r.alphaFollow = true; r.spacing = 0.12f; r.smooth = 0.08f;
    });
    b("Plane Cutter", "Shaves the surface down to flat planes, for stylised forms and stone.", [](BrushRecipe& r) {
        r.depth = -0.1f; r.plane = Plane::Scrape; r.planeAmount = 1.0f; r.hardness = 0.7f; r.spacing = 0.08f;
    });
    b("Volume Fill", "Fills low areas up to the stroke's plane and leaves the high points alone.", [](BrushRecipe& r) {
        r.depth = 0.1f; r.plane = Plane::Fill; r.planeAmount = 0.9f; r.hardness = 0.3f; r.smooth = 0.1f;
    });
    b("Level Out", "Presses hills and valleys onto one plane and softens the result, to calm busy areas.",
      [](BrushRecipe& r) {
          r.depth = 0.0f; r.plane = Plane::Flatten; r.planeAmount = 0.8f; r.smooth = 0.35f; r.hardness = 0.3f;
      });
    b("Swell", "Puffs the surface out along its own normals, for cheeks, bellies and soft volumes.", [](BrushRecipe& r) {
        r.direction = Dir::Inflate; r.depth = 0.35f; r.smooth = 0.1f; r.hardness = 0.1f;
    });
    b("Form Smoother", "Relaxes lumps while keeping the big shape, for a clean pass after blocking.", [](BrushRecipe& r) {
        r.depth = 0.0f; r.smooth = 0.75f; r.plane = Plane::Flatten; r.planeAmount = 0.8f; r.hardness = 0.2f;
    });
    b("Clay Drag", "Smears soft clay along the stroke like a thumb pushing it, for blending transitions.",
      [](BrushRecipe& r) {
          r.depth = 0.12f; r.drag = 0.7f; r.plane = Plane::Clay; r.planeAmount = 0.4f; r.smooth = 0.1f;
      });
    b("Edge Definer", "Pinches a crisp edge into the surface where two planes meet.", [](BrushRecipe& r) {
        r.depth = 0.2f; r.pinch = 0.45f; r.hardness = 0.5f; r.tipSharpness = 0.3f; r.spacing = 0.1f;
    });
}

void hardSurface(PackBuilder b) {
    b("Panel Line", "Cuts a clean, narrow groove for panel gaps and seams.", [](BrushRecipe& r) {
        r.depth = -0.4f; r.pinch = 0.25f; r.hardness = 0.4f; r.tipSharpness = 0.3f; r.spacing = 0.08f;
        r.frontFacesOnly = true;
    });
    b("Flat Plane", "Presses a perfectly flat face with a hard edge, for machined surfaces.", [](BrushRecipe& r) {
        r.depth = 0.0f; r.plane = Plane::Flatten; r.planeAmount = 1.0f; r.hardness = 0.95f; r.alpha = "Square";
        r.spacing = 0.06f;
    });
    b("Bevel Scrape", "Scrapes high points into a bevel along an edge, for chamfers.", [](BrushRecipe& r) {
        r.depth = -0.05f; r.plane = Plane::Scrape; r.planeAmount = 1.0f; r.hardness = 0.7f; r.pinch = 0.0f;
        r.spacing = 0.06f;
    });
    b("Raised Plate", "Builds a flat, square-edged plate on the surface, for armour and hull panels.",
      [](BrushRecipe& r) {
          r.depth = 0.2f; r.plane = Plane::Clay; r.planeAmount = 0.6f; r.hardness = 0.35f; r.alpha = "Square";
          r.spacing = 0.12f; r.smooth = 0.08f;
    });
    b("Rivet Row", "Stamps rows of round rivet heads along the stroke.", [](BrushRecipe& r) {
        r.depth = 0.45f; r.alpha = "Rivets"; r.alphaFollow = true; r.hardness = 0.9f; r.spacing = 0.95f;
        r.frontFacesOnly = true;
    });
    b("Bolt Head", "Stamps a hexagonal bolt on a washer; space the dabs out or click once.", [](BrushRecipe& r) {
        r.depth = 0.8f; r.alpha = "Bolt"; r.hardness = 0.95f; r.spacing = 1.6f; r.frontFacesOnly = true;
    });
    b("Hex Armour", "Raises a honeycomb of hexagonal plates, for sci-fi armour and shells.", [](BrushRecipe& r) {
        r.depth = 0.3f; r.alpha = "Hex Plates"; r.hardness = 0.9f; r.spacing = 0.7f; r.frontFacesOnly = true;
    });
    b("Vent Slots", "Cuts parallel slots like a vent grille; drag across the slots.", [](BrushRecipe& r) {
        r.depth = -0.7f; r.alpha = "Wrinkles"; r.alphaFollow = true; r.hardness = 0.95f; r.pinch = 0.4f;
        r.spacing = 0.5f; r.frontFacesOnly = true;
    });
    b("Grid Studs", "An even grid of small studs, for grip textures and tread plates.", [](BrushRecipe& r) {
        r.depth = 0.6f; r.alpha = "Dot Grid"; r.hardness = 0.9f; r.spacing = 0.8f; r.frontFacesOnly = true;
    });
    b("Hard Cut", "Cuts the surface flat below the stroke, for sharp silhouettes and clean cuts.", [](BrushRecipe& r) {
        r.depth = -0.1f; r.plane = Plane::Scrape; r.planeAmount = 0.8f; r.hardness = 0.6f; r.alpha = "Square";
        r.spacing = 0.05f;
    });
}

void organicAndSkin(PackBuilder b) {
    b("Fine Pores", "Small, evenly scattered pores for faces and close-ups.", [](BrushRecipe& r) {
        r.depth = -0.5f; r.alpha = "Pores"; r.spacing = 0.45f; r.jitter = 0.35f; r.frontFacesOnly = true;
    });
    b("Deep Pores", "Bigger, deeper pores for noses, cheeks and rough skin.", [](BrushRecipe& r) {
        r.depth = -0.6f; r.alpha = "Pores"; r.pattern = Pat::Pores; r.patternScale = 0.3f; r.patternAmount = 0.5f;
        r.spacing = 0.3f; r.jitter = 0.3f; r.frontFacesOnly = true;
    });
    b("Crow's Feet", "Fine fanned wrinkle lines, for eye corners and knuckles.", [](BrushRecipe& r) {
        r.depth = -0.6f; r.alpha = "Wrinkles"; r.alphaFollow = true; r.spacing = 0.4f; r.frontFacesOnly = true;
    });
    b("Skin Fold", "Soft rolls of heavy skin, for necks, bellies and brows.", [](BrushRecipe& r) {
        r.depth = 0.6f; r.alpha = "Skin Folds"; r.alphaFollow = true; r.smooth = 0.1f; r.spacing = 0.35f;
    });
    b("Crease Line", "A sharp, deep crease with pinched sides, for eyelids, lips and nasal folds.", [](BrushRecipe& r) {
        r.depth = -0.6f; r.pinch = 0.45f; r.tipSharpness = 0.5f; r.hardness = 0.2f; r.spacing = 0.08f;
    });
    b("Vein", "Raises thin, soft-edged veins along the stroke.", [](BrushRecipe& r) {
        r.direction = Dir::Inflate; r.depth = 0.4f; r.pinch = 0.5f; r.tipSharpness = 0.5f; r.smooth = 0.15f;
        r.spacing = 0.06f;
    });
    b("Reptile Scales", "Overlapping rows of rounded scales, for lizards, dragons and fish.", [](BrushRecipe& r) {
        r.depth = 0.4f; r.alpha = "Scales"; r.alphaFollow = true; r.hardness = 0.5f; r.spacing = 0.7f;
        r.frontFacesOnly = true;
    });
    b("Leather Hide", "Pebbled grain with fine creases, for leather, elephant and rhino skin.", [](BrushRecipe& r) {
        r.depth = 0.5f; r.alpha = "Leather Grain"; r.spacing = 0.6f; r.jitter = 0.2f; r.frontFacesOnly = true;
    });
    b("Muscle Fibre", "Long soft ridges along the stroke, for muscle striation and sinew.", [](BrushRecipe& r) {
        r.depth = 0.5f; r.alpha = "Hair Strands"; r.alphaFollow = true; r.smooth = 0.1f; r.spacing = 0.3f;
    });
    b("Flesh Pinch", "Pulls soft flesh together into a ridge, for lips, knuckles and wrinkles that bunch.",
      [](BrushRecipe& r) {
          r.depth = 0.1f; r.pinch = 0.9f; r.smooth = 0.1f; r.hardness = 0.1f;
      });
}

void clothAndFolds(PackBuilder b) {
    b("Fold Ridge", "Raises a rounded fold of cloth with soft sides.", [](BrushRecipe& r) {
        r.depth = 0.45f; r.pinch = 0.45f; r.tipSharpness = 0.2f; r.smooth = 0.1f; r.spacing = 0.06f;
    });
    b("Fold Valley", "Carves the soft valley between two folds.", [](BrushRecipe& r) {
        r.depth = -0.4f; r.pinch = 0.3f; r.smooth = 0.15f; r.spacing = 0.06f;
    });
    b("Tension Crease", "A sharp crease where cloth pulls tight, for armpits, elbows and seams under strain.",
      [](BrushRecipe& r) {
          r.depth = -0.4f; r.pinch = 0.45f; r.tipSharpness = 0.5f; r.hardness = 0.3f; r.spacing = 0.08f;
      });
    b("Seam Stitch", "A sewn seam with cross stitches; drag along the seam.", [](BrushRecipe& r) {
        r.depth = 0.55f; r.alpha = "Stitches"; r.alphaFollow = true; r.hardness = 0.8f; r.spacing = 0.85f;
        r.frontFacesOnly = true;
    });
    b("Woven Cloth", "Threads woven over and under, for canvas, denim and sacking.", [](BrushRecipe& r) {
        r.depth = 0.45f; r.alpha = "Fabric Weave"; r.spacing = 0.6f; r.frontFacesOnly = true;
    });
    b("Knitted Wool", "Rows of V-shaped loops, for jumpers, scarves and chunky knits.", [](BrushRecipe& r) {
        r.depth = 0.55f; r.alpha = "Knit"; r.alphaFollow = true; r.spacing = 0.6f; r.frontFacesOnly = true;
    });
    b("Hem Edge", "A flat, square-edged band along the stroke, for hems, cuffs and straps.", [](BrushRecipe& r) {
        r.depth = 0.15f; r.plane = Plane::Clay; r.planeAmount = 0.6f; r.hardness = 0.3f; r.alpha = "Square";
        r.alphaFollow = true; r.spacing = 0.12f; r.smooth = 0.08f;
    });
    b("Cloth Drape", "Drags the surface along the stroke so folds flow and hang, for drapery.", [](BrushRecipe& r) {
        r.depth = 0.05f; r.drag = 0.4f; r.smooth = 0.2f; r.hardness = 0.1f;
    });
    b("Wrinkle Bunch", "Small compressed wrinkles where cloth gathers, at waists and wrists.", [](BrushRecipe& r) {
        r.depth = -0.3f; r.pattern = Pat::Ridges; r.patternScale = 0.3f; r.patternAmount = 0.9f; r.pinch = 0.4f;
        r.spacing = 0.1f;
    });
    b("Padded Quilt", "Puffs soft panels up between the stroke's edges, for quilting and padding.", [](BrushRecipe& r) {
        r.direction = Dir::Inflate; r.depth = 0.3f; r.alpha = "Square"; r.hardness = 0.2f; r.smooth = 0.2f;
        r.spacing = 0.15f;
    });
}

void damageAndWear(PackBuilder b) {
    b("Dry Cracks", "A web of thin cracks, for dry earth, old paint and stone.", [](BrushRecipe& r) {
        r.depth = -0.7f; r.alpha = "Cracks"; r.spacing = 0.6f; r.jitter = 0.2f; r.frontFacesOnly = true;
    });
    b("Deep Fissure", "One deep, jagged split, for broken rock and ice.", [](BrushRecipe& r) {
        r.depth = -0.7f; r.pinch = 0.6f; r.pattern = Pat::Cracks; r.patternScale = 0.6f; r.patternAmount = 0.5f;
        r.tipSharpness = 0.6f; r.jitter = 0.15f; r.spacing = 0.05f;
    });
    b("Edge Chips", "Knocks angular chips out of edges only; flat areas stay whole.", [](BrushRecipe& r) {
        r.depth = -0.8f; r.alpha = "Chips"; r.cavityMask = -0.5f; r.spacing = 0.5f; r.jitter = 0.3f;
    });
    b("Dents", "Soft, scattered dents, for beaten metal and hammered surfaces.", [](BrushRecipe& r) {
        r.depth = -0.4f; r.hardness = 0.1f; r.spacing = 0.9f; r.jitter = 0.6f; r.pressureCurve = 0.8f;
    });
    b("Scratches", "Thin, sharp scratches along the stroke, for scuffed metal and wood.", [](BrushRecipe& r) {
        r.depth = -0.35f; r.pinch = 0.3f; r.tipSharpness = 0.6f; r.hardness = 0.35f; r.jitter = 0.1f;
        r.spacing = 0.08f;
    });
    b("Eroded Stone", "Breaks the surface up with noise, for weathered rock and sandstone.", [](BrushRecipe& r) {
        r.depth = -0.3f; r.pattern = Pat::Noise; r.patternScale = 0.35f; r.patternAmount = 0.9f;
        r.plane = Plane::Scrape; r.planeAmount = 0.3f; r.spacing = 0.2f;
    });
    b("Rock Layers", "Stepped, broken strata, for cliffs and layered stone.", [](BrushRecipe& r) {
        r.depth = 0.55f; r.alpha = "Rock Strata"; r.hardness = 0.6f; r.spacing = 0.5f;
    });
    b("Cobbles", "Packed rounded stones with grooves between, for paths and old walls.", [](BrushRecipe& r) {
        r.depth = 0.55f; r.alpha = "Cobblestone"; r.spacing = 0.7f; r.frontFacesOnly = true;
    });
    b("Wood Grain", "Rough furrows and ridges, for bark and weathered planks; drag along the grain.",
      [](BrushRecipe& r) {
          r.depth = -0.55f; r.alpha = "Bark"; r.alphaFollow = true; r.spacing = 0.4f; r.frontFacesOnly = true;
      });
    b("Worn Edges", "Softens and rounds only the sharp ridges, as years of handling do.", [](BrushRecipe& r) {
        r.depth = -0.4f; r.smooth = 0.4f; r.cavityMask = -0.6f; r.hardness = 0.2f;
    });
}

void ornament(PackBuilder b) {
    b("Bead Chain", "A row of round beads along the stroke, for jewellery and trims.", [](BrushRecipe& r) {
        r.direction = Dir::Inflate; r.depth = 0.6f; r.hardness = 0.6f; r.tipSharpness = 0.0f; r.spacing = 0.9f;
        r.frontFacesOnly = true;
    });
    b("Rope Twist", "A twisted rope or cord, for borders and rigging.", [](BrushRecipe& r) {
        r.depth = 0.6f; r.alpha = "Hair Strands"; r.alphaFollow = true; r.pinch = 0.3f; r.spacing = 0.12f;
    });
    b("Filigree Line", "A thin, raised wire line, for filigree and engraved borders.", [](BrushRecipe& r) {
        r.depth = 0.45f; r.pinch = 0.3f; r.tipSharpness = 0.3f; r.hardness = 0.4f; r.spacing = 0.08f;
    });
    b("Engrave", "Cuts a fine, crisp line, for engraving, runes and lettering.", [](BrushRecipe& r) {
        r.depth = -0.35f; r.pinch = 0.3f; r.tipSharpness = 0.5f; r.hardness = 0.4f; r.spacing = 0.08f;
        r.frontFacesOnly = true;
    });
    b("Stud Row", "Rounded studs spaced along the stroke, for belts, doors and armour trims.", [](BrushRecipe& r) {
        r.depth = 0.5f; r.hardness = 0.85f; r.spacing = 1.3f; r.frontFacesOnly = true;
    });
    b("Raised Band", "A flat, raised band with clean edges, for bezels, rims and mouldings.", [](BrushRecipe& r) {
        r.depth = 0.25f; r.plane = Plane::Clay; r.planeAmount = 0.6f; r.hardness = 0.3f; r.pinch = 0.0f;
        r.spacing = 0.1f; r.smooth = 0.08f;
    });
    b("Scroll Relief", "Soft, rounded relief that builds up slowly, for carved scrolls and leaves.", [](BrushRecipe& r) {
        r.depth = 0.3f; r.plane = Plane::Clay; r.planeAmount = 0.6f; r.smooth = 0.25f; r.hardness = 0.25f;
        r.pressureCurve = 1.3f;
    });
    b("Scale Mail", "Rows of small scales, for armour, roof tiles and feathers.", [](BrushRecipe& r) {
        r.depth = 0.3f; r.pattern = Pat::Scales; r.patternScale = 0.2f; r.patternAmount = 1.0f; r.hardness = 0.6f;
        r.spacing = 0.25f; r.frontFacesOnly = true;
    });
    b("Gem Setting", "A raised round setting with a crisp rim, for cabochons and gem seats.", [](BrushRecipe& r) {
        r.direction = Dir::Inflate; r.depth = 0.7f; r.plane = Plane::Flatten; r.planeAmount = 0.4f;
        r.hardness = 0.95f; r.spacing = 2.0f; r.frontFacesOnly = true;
    });
    b("Dot Texture", "Fine even dots, for stippling, punched leather and decorative fields.", [](BrushRecipe& r) {
        r.depth = -0.55f; r.alpha = "Dot Grid"; r.spacing = 0.7f; r.frontFacesOnly = true;
    });
}

} // namespace

const std::vector<std::string>& builtinBrushPacks() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v;
        for (const PackInfo& p : kPacks) v.emplace_back(p.name);
        return v;
    }();
    return names;
}

std::string builtinBrushPackHelp(const std::string& pack) {
    for (const PackInfo& p : kPacks)
        if (pack == p.name) return p.help;
    return {};
}

std::vector<BrushRecipe> builtinPackRecipes() {
    std::vector<BrushRecipe> out;
    out.reserve(60);
    clayAndForm({out, kPacks[0].name});
    hardSurface({out, kPacks[1].name});
    organicAndSkin({out, kPacks[2].name});
    clothAndFolds({out, kPacks[3].name});
    damageAndWear({out, kPacks[4].name});
    ornament({out, kPacks[5].name});
    return out;
}

} // namespace sf
