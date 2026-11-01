// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A brush recipe describes a sculpting brush as data: how the surface moves,
// the shape of the tip, surface texture, stroke behaviour and masking. Recipes
// are what artists (or the AI brush assistant) create; they are saved as
// shareable .sfbrush JSON files and never contain executable code.
#pragma once

#include "core/Json.h"

#include <string>
#include <vector>

namespace sf {

// Where a brush in the library came from (see core/BrushLibrary.h). Precedence
// for brushes of the same name: user library > studio folders > built-in.
enum class BrushSource {
    Session, // made this session and not in the library (brush_create, brush_load, an AI reply not yet saved)
    Builtin, // ships with SculptForge
    User,    // the artist's own library (<app data>/brushes), saved automatically
    Studio,  // a read-only shared folder from SCULPTFORGE_BRUSH_PATH
    Gallery, // installed from the community gallery (lives in the user library)
    Project, // came with an opened .sfproj and differs from (or is not in) the library
    Pack,    // from an installed pack (core/Pack.h): read-only, like a studio brush
};
const char* brushSourceName(BrushSource source); // "session", "built-in", "user", "studio", "gallery", "project"

struct BrushOrigin {
    BrushSource source = BrushSource::Session;
    std::string path; // the .sfbrush file it was loaded from or is saved to (user, gallery, studio)
    // A built-in or studio brush of the same name this user/gallery brush hides (Session = none).
    BrushSource overrides = BrushSource::Session;
    std::string pack; // Pack: the pack's name (the lists show it as the brush's tag)
};

struct BrushRecipe {
    enum class Direction { Normal, Inflate, View, Stroke };
    enum class Plane { None, Clay, Flatten, Fill, Scrape };
    enum class Pattern { None, Noise, Scales, Ridges, Pores, Cracks };

    std::string name = "Custom Brush";
    std::string description;

    // Surface motion
    Direction direction = Direction::Normal;
    float depth = 0.5f;       // -1..1: positive builds up, negative carves
    Plane plane = Plane::None;
    float planeAmount = 0.5f; // 0..1
    float pinch = 0.0f;       // -1..1: pulls surface toward the stroke centre line (crisp edges)
    float drag = 0.0f;        // 0..1: smears the surface along the stroke
    float smooth = 0.0f;      // 0..1: relaxes the surface after each dab

    // Tip shape
    float hardness = 0.0f;     // 0 = soft airbrush edge, 1 = hard stamp edge
    float tipSharpness = 0.0f; // 0 = rounded, 1 = needle-like point

    // Surface texture
    Pattern pattern = Pattern::None;
    float patternScale = 0.3f;  // feature size as a fraction of brush radius (0.03..2)
    float patternAmount = 0.0f; // 0..1

    // Stroke
    float spacing = 0.15f;      // dab spacing as a fraction of brush size (0.02..2)
    float jitter = 0.0f;        // 0..1 random scatter of dabs
    float pressureCurve = 1.0f; // 0.3..3: >1 needs a harder press, <1 responds to light touch

    // Masking
    bool frontFacesOnly = false;
    // Falloff options (DabParams::connectedOnly / viewFalloff), on with the app's own or on their own. Written to
    // the recipe's JSON only when on, so other recipes' files stay as they were.
    bool connectedOnly = false;
    bool viewFalloff = false;
    float cavityMask = 0.0f; // -1..1: >0 only affects cavities, <0 only ridges/peaks

    // Library bookkeeping, not part of the recipe: never written by toJson().
    BrushOrigin origin;

    void clamp();
    // Numeric parameter by key (see recipeParams()), or nullptr.
    float* field(const std::string& key);
    // Enum parameters as indices into RecipeParam::options.
    int enumIndex(const std::string& key) const;
    void setEnumIndex(const std::string& key, int index);
    Json toJson() const;
    // Lenient: missing keys keep their current values, values are clamped.
    void applyJson(const Json& j);
    static BrushRecipe fromJson(const Json& j);

    bool save(const std::string& path, std::string& error) const;
    static bool load(const std::string& path, BrushRecipe& out, std::string& error);
};

// Describes each recipe parameter once, for UI, AI schema and commands.
struct RecipeParam {
    const char* key;
    const char* label;
    const char* help; // written for artists
    enum class Kind { Float, Enum, Bool } kind;
    float min = 0.0f, max = 1.0f;
    std::vector<const char*> options; // Enum values
};
const std::vector<RecipeParam>& recipeParams();

// JSON Schema (structured-output compatible: all keys required, no
// additionalProperties, ranges given in descriptions).
Json recipeJsonSchema();

std::vector<BrushRecipe> builtinBrushRecipes();

} // namespace sf
