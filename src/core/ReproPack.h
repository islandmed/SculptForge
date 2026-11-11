// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A bug report pack (Help > Create bug report pack..., docs/studios.md "Bug report packs"): one local .zip for a bug
// report that holds none of the artist's work, so a studio can send us a bug without sending its model. Nothing here
// reaches the network. Text is redacted by rules, not understood: the preview asks the artist to read it first.
//
// What it holds, each item a file the user sees in full and can leave out before saving:
//   system.txt      app version, OS, CPU, memory, GPU and driver
//   settings.json   the preferences that differ from the defaults
//   log.txt         the end of the log
//   actions.txt     the last commands and strokes, with only number and on/off parameters kept
//   mesh.json       the scene's topology class: per Part vertex and triangle counts, pieces, genus, open edges,
//                   subdivision levels and layers; the scene's overall size
//   proxy.ply       a generated stand-in with those counts: spheres, tori and sheets laid in a row inside the overall
//                   size; none of the sculpt's positions, so none of its shape
// and README.txt saying so. Never in it: vertex positions, textures or images. Paths, file names, URLs, e-mail
// addresses, quoted text and every name in Inputs::names are replaced (redactText); a name typed into free text that
// isn't known can remain, which is why the log and actions are shown before saving.
#pragma once

#include "core/Commands.h"
#include "core/Mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct Document;

namespace repro {

// Replaces anything that can name a person, a project or a place: file paths (/x/y, C:\x, ~/x, \\server),
// file names with an extension, URLs, e-mail addresses, quoted text and '=' values that aren't plain numbers or
// short lower-case words, with "<path>", "<file>", "<url>", "<email>" or "<text>".
std::string redactText(const std::string& text);

// The last commands and strokes (newest last, at most kActionCount), for actions.txt. Main thread only.
constexpr size_t kActionCount = 200;
// A command: its name and only its number and on/off parameters (a text value shows as <text>).
void noteCommand(const std::string& name, const Args& args);
// A stroke: the brush type, its dabs, and the vertices it touched.
void noteStroke(const char* brush, uint64_t dabs, uint64_t vertices);
std::vector<std::string> recentActions();
void clearActions(); // (tests)

// One Part's topology class.
struct PartStats {
    uint64_t vertices = 0, triangles = 0;
    int pieces = 0;          // connected pieces
    int genus = 0;           // handles, summed over the pieces (from V - E + F and the open edge loops)
    int openLoops = 0;       // boundary loops (holes)
    uint64_t openEdges = 0;  // edges with one triangle
    uint64_t nonManifoldEdges = 0; // edges with three or more
    int subdivLevels = 0;    // multires levels above the base
    int currentLevel = 0;    // the one shown (0 = the base)
    int layers = 0;          // sculpt layers
    bool uvs = false, colors = false, mask = false, faceGroups = false, hidden = false;
};
PartStats partStats(const Mesh& mesh);

// The whole scene: every Part's stats (in order; no names) and the size of the box around them all.
struct SceneStats {
    std::vector<PartStats> parts;
    glm::vec3 size{0.0f}; // of the scene's bounding box (its position is left out)
};
SceneStats sceneStats(const Document& doc);
std::string sceneStatsJson(const SceneStats& s);

// The stand-in: for each Part a lat-long sphere (genus 0, closed), a torus (genus 1 or more) or a grid sheet (open),
// with about the Part's vertex count, side by side inside s.size. At most MAX_VERTICES in all (bigger scenes are
// scaled down evenly; mesh.json keeps the true counts). Built from the counts alone.
constexpr uint64_t kProxyMaxVertices = 400000;
Mesh proxyMesh(const SceneStats& s, uint64_t maxVertices = kProxyMaxVertices);
std::vector<uint8_t> plyBytes(const Mesh& mesh); // binary little-endian PLY: positions and triangles only

struct Item {
    std::string id;      // "system", "settings", "log", "actions", "mesh", "proxy"
    std::string file;    // its name in the zip
    std::string label;   // for the preview
    std::string summary; // one line: what's in it
    std::vector<uint8_t> data;
    bool include = true;
};

struct Inputs {
    std::string system;              // already plain (no names): version, OS, GPU...
    std::string settingsJson;        // the preferences that differ from the defaults (values are redacted again)
    std::vector<std::string> log;    // the log's last lines (redacted here)
    std::vector<std::string> actions;
    std::vector<std::string> names;  // Part names, the project's name, the user and computer names: <name> wherever they appear
    const Document* doc = nullptr;
};
// Every name the artist gave something in DOC: Parts, Part folders, sculpt and paint layers, selection sets, brushes
// that aren't built in, files kept in the project, the project's own name. (The app adds packs, macros and accounts.)
std::vector<std::string> documentNames(const Document& doc);
std::vector<Item> buildItems(const Inputs& in);
// The zip: README.txt plus the included items. False with ERROR when it can't be written.
bool writePack(const std::string& path, const std::vector<Item>& items, std::string& error);
std::string readmeText(const std::vector<Item>& items);

// The last LINES lines of a text file ("" when it can't be read), at most 256 KB from its end.
std::vector<std::string> tailLines(const std::string& path, size_t lines);

} // namespace repro
} // namespace sf
