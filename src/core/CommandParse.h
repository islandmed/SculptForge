// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plain words into a command: "clay 40", "intensity 30", "mirror x", "symmetry off", "remesh 200k", "hide all but head".
// Local and offline: it only matches the words against a catalog the app builds (action names in every shipped language,
// their other names, a few synonyms, brush and Part names), reads numbers with their units, forgives typos and ranks
// the readings. It runs nothing: the app turns the top reading into calls of its existing actions, with the numbers
// clamped to each entry's range. Kept apart from the widget so a shelf button or a bottom-docked command line reuses it.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sf::cmd {

// What the words after a name can mean for an entry.
enum class Role {
    Plain,      // runs as it is ("ortho", "turntable"); a number or a switch word makes it not this entry
    Brush,      // picks a brush; a number is its Size (px), a number with % its Intensity ("clay 40", "smooth 30%")
    Value,      // sets one number ("intensity 30", "size 40"); lo..hi, the number is clamped into it
    Switch,     // on / off / flip, with an optional axis ("mirror x", "symmetry off", "wireframe on")
    Amount,     // an action with an optional amount ("remesh 200k" = about 200 000 triangles, "remesh 300" = 300 voxels)
    Visibility, // "hide all but head", "show all", "isolate arm": names Parts of the catalog
};

// How a number was written.
enum class Unit { None, Percent, Px, Triangles };

struct Entry {
    std::string id;                  // the app's key: an action id, or "@intensity" for what no action covers
    std::string label;               // how the preview names it, in the interface language
    std::vector<std::string> names;  // every name that finds it: the label in each language, other names, synonyms
    Role role = Role::Plain;
    double lo = 0.0, hi = 0.0;       // Brush: the Size range; Value / Amount: the number's range
    int priority = 0;                // breaks a tie in score (a built-in brush before a custom one)
};

struct Catalog {
    std::vector<Entry> entries;
    std::vector<std::string> parts;  // the scene's Part names, for Visibility entries
};

struct Interpretation {
    size_t entry = 0;                // index into Catalog::entries
    double score = 0.0;              // 0..100
    bool exact = false;              // the words are the entry's label or another of its names, letter for letter
    // Brush: size (px) and intensity (percent); Value / Amount: `number`.
    bool hasSize = false, hasIntensity = false, hasNumber = false;
    double size = 0.0, intensity = 0.0, number = 0.0;
    Unit unit = Unit::None;          // Amount: Triangles for "200k" or any number from 5000, else the number is voxels
    bool clamped = false;            // a number was outside the range and moved into it
    bool needsNumber = false;        // Value: the words name it but give no number yet
    int state = -1;                  // Switch: 1 on, 0 off, -1 flip
    char axis = 0;                   // Switch: 'x', 'y' or 'z' when the words say one
    bool all = false, except = false, only = false; // Visibility: "all", "but / except", "only"
    std::vector<size_t> parts;       // Visibility: indices into Catalog::parts
    std::string target;              // Visibility: the words that named them, as typed (kept for the canonical line)
    bool needsTarget = false;        // Visibility: the verb with no Part named
    bool parametrised() const {
        return hasSize || hasIntensity || hasNumber || state >= 0 || axis || all || !parts.empty();
    }
};

// The readings of QUERY, best first, at most LIMIT; empty when nothing in the catalog is close (the query is then a
// normal search). A reading is dropped when the query has words or numbers it cannot use.
std::vector<Interpretation> interpret(const Catalog& catalog, std::string_view query, size_t limit = 8);

// A number written as the query writes it: "40", "200k", "1.5m", "30%", "12px". False when TEXT is not one.
bool parseNumber(std::string_view text, double& value, Unit& unit);

// The edit distance between two UTF-8 words (letters swapped next to each other count once), case and accents ignored.
int editDistance(std::string_view a, std::string_view b);

// Other names for an action or setting, one per entry ("view.ortho" -> "orthographic", "flat view", ...). Generic words only.
std::vector<std::string> synonymNames(std::string_view id);

// The line for an interpretation as the user would type it again: re-parsing it gives the same reading
// ("Clay 40 30%", "Symmetry X off", "Hide all but Head"). Names come from the entry's label and the catalog's Parts.
std::string canonicalLine(const Catalog& catalog, const Interpretation& reading);

// The last commands that ran (newest first), as canonical lines. A line that ran again moves to the front.
class CommandHistory {
public:
    static constexpr size_t kMax = 50;
    void push(std::string line);
    const std::vector<std::string>& lines() const { return lines_; }
    void clear() { lines_.clear(); cursor_ = -1; }
    // Up arrow: the next older line (the newest first); "" when there is none. Down: back toward the newest, then "".
    std::string older();
    std::string newer();
    void resetCursor() { cursor_ = -1; }
    bool browsing() const { return cursor_ >= 0; }
    std::string toText() const;                      // one line each, newest first
    void fromText(std::string_view text);

private:
    std::vector<std::string> lines_;
    int cursor_ = -1;
};

} // namespace sf::cmd
