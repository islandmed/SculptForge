// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sandboxed plugins (#20, docs/design/plugins.md): a .sfplugin is a WebAssembly module, installed inside a pack
// (plugins/<name>.sfplugin), that turns a mesh into a mesh. Phase 1 has two kinds:
//   deformer  - gets the active Part's mesh and returns new positions for its vertices (one undo step; the mask
//               and hidden faces hold their vertices in place, as every whole-mesh deformation does);
//   generator - gets the same mesh and returns a new one, added as a new Part.
// What a plugin can do is what the host hands it, and nothing else:
//   - no capabilities: the module may import nothing (no WASI, no host functions), so it has no files, network,
//     clock, environment or randomness; it sees only the copy of the mesh it is given and its parameter values;
//   - every run gets a fresh instance (no state survives a run), a fuel budget, a memory cap and a wall-clock
//     limit; a plugin that traps, runs out of fuel or time is turned off with a message saying why, and the mesh
//     is untouched (results are copied out and checked, then committed by the host);
//   - its parameters are declared in its manifest (a custom section, "sculptforge.plugin") and drawn by us;
//   - it runs only when the user runs it: never when a pack is installed or a project opens, never from a macro or
//     an AI agent (sculptforge-cli --mcp).
// The ABI the module implements (ABI 1, all little-endian, offsets are addresses in its memory "memory"):
//   export memory "memory"; export func sf_alloc(i32 bytes) -> i32; export func sf_run(i32 input) -> i32 output.
//   input  (48 bytes): u32 abi, kind, vertex_count, triangle_count, param_count, positions (f32 x3 per vertex),
//                      normals (f32 x3), mask (f32 per vertex, 1 = fully masked), triangles (u32 x3), params
//                      (f64 per parameter, manifest order), 2 reserved (0).
//   output (32 bytes): u32 abi, status (0 = ok), vertex_count, triangle_count, positions, triangles, message,
//                      message_len (UTF-8, shown when status != 0; at most 512 bytes).
// sdk/plugin/sculptforge_plugin.h is the same in C, with a sample plugin (sdk/plugin/examples/noise_displace.c).
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

namespace sf {

class CommandRegistry;
class Mesh;
struct Document;

constexpr const char* kPluginExtension = ".sfplugin";
constexpr const char* kPluginManifestSection = "sculptforge.plugin";
constexpr int kPluginFormatVersion = 1;
constexpr uint32_t kPluginAbi = 1;
constexpr uint64_t kMaxPluginBytes = 16ull << 20; // one .sfplugin
constexpr size_t kMaxPluginParams = 32;

enum class PluginKind { Deformer, Generator };
const char* pluginKindId(PluginKind k);    // "deformer"
const char* pluginKindLabel(PluginKind k); // "Deformer"

struct PluginParam {
    enum class Type { Number, Int, Bool };
    std::string id;    // lower-case letters, digits, '_'
    std::string label; // shown in the panel
    std::string help;  // tooltip ("" = none)
    Type type = Type::Number;
    double min = 0.0, max = 1.0, def = 0.0;
};

struct PluginManifest {
    std::string id, name, version, author, license, description;
    PluginKind kind = PluginKind::Deformer;
    std::vector<std::string> permissions; // phase 1 grants none: any entry is refused
    std::vector<PluginParam> params;
};

bool parsePluginManifest(const std::string& text, PluginManifest& out, std::string& error);

// Reads a .sfplugin without running it: a WebAssembly 1.0 module (not a component) whose sections are well formed,
// with exactly one "sculptforge.plugin" custom section holding a valid manifest and no imports at all. With the
// runtime built in, the module is also validated (wasmtime) and must export memory, sf_alloc and sf_run.
bool readPlugin(const std::vector<uint8_t>& wasm, PluginManifest& out, std::string& error);

// Tools for plugin authors and tests: appends the manifest as a "sculptforge.plugin" custom section, and (with the
// runtime built in) turns WebAssembly text into a module.
std::vector<uint8_t> pluginWithManifest(std::vector<uint8_t> wasm, const std::string& manifestJson);
bool pluginWatToWasm(const std::string& wat, std::vector<uint8_t>& out, std::string& error);

// The runtime is wasmtime, linked in when the platform has a build (cmake/Wasmtime.cmake).
bool pluginRuntimeAvailable();
std::string pluginRuntimeName(); // "wasmtime 50.0.1", or "" without

struct PluginLimits {
    uint64_t memoryBytes = 0; // the plugin's memory cap; 0 = 256 MB + 4x the input, at most 4 GB
    uint64_t fuel = 0;        // instructions, roughly; 0 = 2 billion + 20,000 per vertex and triangle
    double seconds = 20.0;    // wall clock
};

// What a plugin is given: a copy of the mesh (taken on the UI thread; the run can then go to a worker).
struct PluginInput {
    std::vector<glm::vec3> positions, normals;
    std::vector<float> mask; // per vertex, empty = none
    std::vector<glm::uvec3> triangles;
};
PluginInput pluginInputFrom(const Mesh& m);

struct PluginOutput {
    std::vector<glm::vec3> positions;  // deformer: one per input vertex; generator: the new mesh's
    std::vector<glm::uvec3> triangles; // generator only
};

enum class PluginFailure {
    None,
    NoRuntime,   // this build has no plugin runtime
    BadModule,   // won't compile or instantiate, misses an export
    Trap,        // crashed (out-of-bounds access, unreachable, ...)
    OutOfFuel,   // past its work budget
    Timeout,     // past its time
    Cancelled,   // the user stopped it
    PluginError, // it returned a non-zero status (with its message)
    BadOutput,   // its result breaks the rules (wrong counts, indices past the end, NaN, out-of-bounds pointers)
};
// Failures that turn the plugin off until the user turns it on again.
inline bool pluginFailureDisables(PluginFailure f) {
    return f == PluginFailure::Trap || f == PluginFailure::OutOfFuel || f == PluginFailure::Timeout;
}

struct PluginRunOptions {
    std::vector<double> params; // manifest order; missing ones take their defaults, values are clamped
    PluginLimits limits;
    const std::atomic<bool>* cancel = nullptr; // set to stop the run (checked about every 10 ms)
};

// Runs the module on a fresh instance. Thread safe; never touches a Document.
PluginFailure runPlugin(const std::vector<uint8_t>& wasm, const PluginManifest& manifest, const PluginInput& in,
                        const PluginRunOptions& opts, PluginOutput& out, std::string& error);

// --- Installed plugins: the plugins of enabled packs (core/Pack.h) ---

struct InstalledPlugin {
    std::string key;  // "<pack id>/<file name>"
    std::string file; // absolute path
    std::string packId, packName;
    std::string sha256;
    uint64_t bytes = 0;
    PluginManifest manifest;
    bool off = false;     // turned off after a failure (state in <packs>/plugins-off.json, by SHA-256)
    std::string offWhy;
};
std::vector<InstalledPlugin> installedPlugins(std::vector<std::string>* messages = nullptr);
bool findInstalledPlugin(const std::string& idOrKey, InstalledPlugin& out); // manifest id, name or key
// Turns a plugin off (with why) or on again.
bool setPluginOff(const InstalledPlugin& p, bool off, const std::string& why, std::string& error);

// Parameter values by id ("amount" -> 0.3); bools as 0/1. Unknown ids are an error.
bool pluginParamValues(const PluginManifest& m, const std::map<std::string, double>& byId, std::vector<double>& out,
                       std::string& error);

// Checks the active Part can take a plugin's result ("" when it can; the same rules as Tool > Reshape).
bool pluginCanRunOn(Document& doc, const InstalledPlugin& p, std::string& error);
// Applies a run's result: a deformer's positions as one undo step on the active Part (the mask and hidden faces
// hold their vertices), a generator's mesh as a new Part. `in` is the input the run was given (it must still match
// the active Part). Returns a status line, or false with the error.
bool commitPluginOutput(Document& doc, const InstalledPlugin& p, const PluginInput& in, PluginOutput&& out,
                        std::string& message);
// Runs and commits on the calling thread; a failure that disables the plugin turns it off.
bool runPluginOnDocument(Document& doc, const InstalledPlugin& p, const std::vector<double>& params, std::string& message,
                         const PluginLimits& limits = {});

// plugin_list, plugin_run, plugin_enable.
void registerPluginCommands(CommandRegistry& registry);

} // namespace sf
