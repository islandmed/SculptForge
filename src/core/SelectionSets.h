// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Selection Sets (Tool > Selection Sets): any number of named snapshots of a Part's mask, polygroups, vertex
// paint, visibility (hidden faces), UVs and shape (the morph target), each holding the channels it was stored
// with. A set is stored at the subdivision level the Part is at and restores at any level of the same stack: its
// values travel through the levels as the stack passes attributes on (core/Multires.h, subdivMapVertices ...),
// and the shape travels as an offset from the level's own shape, so the detail of the levels in between stays.
//
// Sets are compact: masks, vertex paint and visibility keep only runs of the elements that differ from the
// default (unmasked, white, shown), polygroups are run-length coded, UVs and shapes are kept as they are. A set is
// immutable once made and shared (std::shared_ptr) by every copy of the mesh, so undo steps cost a pointer.
//
// Beyond ZBrush: sets combine (union, intersect, subtract) into new sets or onto the current mask and visibility,
// a quick set takes the visible faces, the Morph brush and Morph to Set pull the surface toward any set's shape
// (at any level), and every operation is a command, so Python (`sculptforge`), sculptforge-cli, MCP and the AI
// assistant push and pull sets too. Sets travel with projects (.sfproj), as their own `.sfsets` files, and in
// USD exports (GeomSubsets for visibility and polygroups, primvars for the rest; read back on import).
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
struct Document;
class Json;

// What a set holds (bits).
enum SetChannel : uint32_t {
    kSetMask = 1,
    kSetPolygroups = 2,
    kSetPaint = 4,       // vertex paint (Mesh::colors)
    kSetVisibility = 8,  // hidden faces
    kSetUvs = 16,
    kSetMorph = 32,      // the shape (vertex positions): the Morph brush's target
    kSetAllChannels = 63,
};
// "mask,polygroups,paint,visibility,uvs,morph" (any order, also "all", "groups", "colors", "hidden", "shape").
bool parseSetChannels(const std::string& text, uint32_t& out, std::string& error);
std::string setChannelNames(uint32_t channels); // "mask, visibility"

// How a set's mask and visibility meet the current ones (restore) or another set's (combine). Visibility combines
// the shown faces: Union shows what either shows, Intersect what both show, Subtract what the first shows and the
// second doesn't.
enum class SetOp { Replace, Union, Intersect, Subtract };
bool parseSetOp(const std::string& text, SetOp& out);
const char* setOpName(SetOp op);

// Elements [start, start + length).
struct SetRun {
    uint32_t start = 0, length = 0;
};

struct SelectionSet {
    std::string name;
    uint32_t channels = 0;
    int level = 0;                              // the subdivision level it was stored at (0 without levels)
    uint32_t vertexCount = 0, triangleCount = 0; // of that level
    // Mask: runs of masked vertices and their values in run order (no values: every one is exactly 1).
    std::vector<SetRun> maskRuns;
    std::vector<float> maskValues;
    // Polygroups: runs of triangles with one id (start = the id, length = how many).
    std::vector<SetRun> groupRuns;
    // Vertex paint: runs of vertices that aren't white, and their colours (linear RGB).
    std::vector<SetRun> paintRuns;
    std::vector<glm::vec3> paintValues;
    // Visibility: runs of hidden triangles.
    std::vector<SetRun> hiddenRuns;
    std::vector<glm::vec2> uvs;       // per triangle corner
    std::vector<glm::vec3> positions; // the shape

    bool has(uint32_t c) const { return (channels & c) != 0; }
    size_t bytes() const;
};

struct SelectionSetList {
    std::vector<std::shared_ptr<const SelectionSet>> sets; // in the order they were made
    std::string morphTarget;                               // the set the Morph brush pulls toward ("" = none)
    int find(const std::string& name) const;               // index, or -1
    size_t bytes() const;
};

// A set's channels as plain arrays (only the channels in `channels`; the others stay empty).
struct SelectionSetData {
    uint32_t channels = 0;
    std::vector<float> mask;
    std::vector<uint32_t> polygroups;
    std::vector<glm::vec3> colors;
    std::vector<uint8_t> hidden;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> positions;
};

// The mesh's own channels (UVs only when it has them).
SelectionSet captureSelectionSet(const Mesh& mesh, std::string name, uint32_t channels);
// A set from arrays laid out for `vertexCount` / `triangleCount` (checked; error names the first that doesn't fit).
bool makeSelectionSet(std::string name, const SelectionSetData& data, uint32_t vertexCount, uint32_t triangleCount,
                      int level, SelectionSet& out, std::string& error);
// The set's arrays at the level it was stored at.
void decodeSelectionSet(const SelectionSet& s, uint32_t channels, SelectionSetData& out);
// The set's arrays at the mesh's current level (any level of its stack whose counts the set fits; see the file
// comment). False with `error` when it was stored on a different mesh.
bool selectionSetAt(const Mesh& mesh, const SelectionSet& s, uint32_t channels, SelectionSetData& out, std::string& error);
// True when selectionSetAt can place the set on this mesh.
bool selectionSetFits(const Mesh& mesh, const SelectionSet& s);

void combineMask(std::vector<float>& current, const std::vector<float>& other, SetOp op);
void combineHidden(std::vector<uint8_t>& current, const std::vector<uint8_t>& other, SetOp op);

// Binary form of one set (projects and .sfsets files): versioned, little-endian, with tagged sections that newer
// readers may add to (unknown ones are skipped). Unpacking checks every count and run against the set's counts.
std::vector<uint8_t> packSelectionSet(const SelectionSet& s);
bool unpackSelectionSet(const uint8_t* data, size_t size, SelectionSet& out, std::string& error);
// Name, channels, level and counts, for manifests (readers rely on the packed data, not on this).
Json selectionSetSummary(const SelectionSet& s);

// .sfsets: a stored ZIP with manifest.json and one packed set per entry, for pipelines and other files.
bool saveSelectionSets(const std::string& path, const SelectionSetList& list, std::string& error);
bool loadSelectionSets(const std::string& path, SelectionSetList& out, std::string& error);

// --- Undoable operations on the active Part (one undo step each; `message` for the status line) ---
bool storeSelectionSet(Document& doc, const std::string& name, uint32_t channels, std::string& message);
// Adds (or replaces, by name) a ready-made set.
bool addSelectionSet(Document& doc, SelectionSet set, const std::string& label, std::string& message);
// Restores the set's channels in `channels` (0 = all it holds but the shape). The shape is restored only when asked
// for (kSetMorph), within the mask. `op` applies to the mask and visibility.
bool restoreSelectionSet(Document& doc, const std::string& name, uint32_t channels, SetOp op, std::string& message);
bool renameSelectionSet(Document& doc, const std::string& name, const std::string& to, std::string& message);
bool deleteSelectionSet(Document& doc, const std::string& name, std::string& message);
bool combineSelectionSets(Document& doc, const std::string& a, const std::string& b, SetOp op, const std::string& into,
                          std::string& message);
// Moves the surface `amount` (0..1, negative pushes away) of the way to the set's shape, scaled by 1 - mask.
bool morphToSelectionSet(Document& doc, const std::string& name, float amount, std::string& message);
bool setMorphTarget(Document& doc, const std::string& name, std::string& message);

// selection_set_store, _restore, _rename, _delete, _list, _combine, _from_visible, _morph_target, _morph,
// selection_sets_export, selection_sets_import.
void registerSelectionSetCommands(CommandRegistry& registry);

} // namespace sf
