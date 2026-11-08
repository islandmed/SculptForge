// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Garment templates (docs/design/garment-templates.md): flat pattern panels at real size made from a size chart the
// user types in (chest, waist, hip, inseam, ...). Every template is sewn the way the garment is made (the template
// is the user's pick, like the T-shirt; nothing is sewn from where panels happen to lie) and laid out flat in rows.
// The proportions are our own, written down in the design note; no commercial pattern is copied.
#pragma once

#include "core/Garment.h"
#include "core/Json.h"

#include <string>
#include <vector>

namespace sf {

struct Document;

namespace garment {

// One body measurement of the size chart, in mm.
struct MeasureSpec {
    const char* key;         // "chest" (the command argument is "chest_mm")
    const char* description; // "Chest girth, around the fullest part"
    float fallback, min, max;
};
const std::vector<MeasureSpec>& measureSpecs();

// The size chart: one value per measureSpecs() entry, clamped to its range.
struct SizeChart {
    std::vector<float> mm; // same order as measureSpecs()
    SizeChart();           // every measurement at its default (an adult medium)
    float get(const std::string& key) const;
    bool set(const std::string& key, float value); // clamped; false for an unknown key or a non-finite value
};

Json sizeChartToJson(const SizeChart& s);
SizeChart sizeChartFromJson(const Json& j); // unknown keys and bad values are ignored
// The document's size chart (projectSettings["garment_sizes"]; the defaults when there is none) and storing it.
SizeChart documentSizeChart(const Document& doc);
void setDocumentSizeChart(Document& doc, const SizeChart& s);

struct TemplateSpec {
    const char* id;          // "trousers"
    const char* description; // "Trousers: front and back of each leg, ..."
};
const std::vector<TemplateSpec>& templateSpecs();

// The template `id` at the chart's sizes, sewn and laid out with its lower-left corner at the origin. `lengthMm`
// > 0 overrides the template's main length (hem to shoulder, waist to hem, ...; see templateSpecs). False with
// `error` for an unknown id.
bool makeTemplate(const std::string& id, const SizeChart& size, float lengthMm, Pattern& out, std::string& error);

// Appends `add`'s panels and seams to `pat`, placed to the right of what is there (60 mm apart); `pat` keeps its
// particle distance, fabric and thickness (an empty `pat` becomes `add`).
void appendPattern(Pattern& pat, const Pattern& add);

// The panel mirrored left to right about x = 0 and still counter-clockwise; its edge i is the mirror of `p`'s edge
// mirrorEdge(n, i), run the other way (so the two sew together the right way round, unflipped).
Panel mirroredPanel(const Panel& p);
inline uint32_t mirrorEdge(uint32_t n, uint32_t e) { return (2 * n - 2 - e) % n; }

} // namespace garment
} // namespace sf
