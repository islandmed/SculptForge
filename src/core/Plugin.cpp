// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sandboxed plugins: the .sfplugin reader, the wasmtime host and the installed-plugin state (see Plugin.h and
// docs/design/plugins.md for the rules and the threat model).
#include "core/Plugin.h"

#include "core/Commands.h"
#include "core/Deform.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Mesh.h"
#include "core/Pack.h"
#include "core/RadialMenu.h"
#include "core/Sha256.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#ifdef SF_HAVE_WASMTIME
#include <wasmtime.h>
#endif

namespace sf {

namespace fs = std::filesystem;

namespace {

fs::path u8path(const std::string& p) { return fs::path(std::u8string(p.begin(), p.end())); }
std::string u8str(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// --- The module's sections (WebAssembly 1.0 binary format) ---

bool leb32(const uint8_t*& p, const uint8_t* end, uint32_t& v) {
    v = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        if (p >= end) return false;
        const uint8_t b = *p++;
        if (shift == 28 && (b & 0x70)) return false; // past 32 bits
        v |= uint32_t(b & 0x7f) << shift;
        if (!(b & 0x80)) return true;
    }
    return false;
}

bool name(const uint8_t*& p, const uint8_t* end, std::string& out) {
    uint32_t n = 0;
    if (!leb32(p, end, n) || n > size_t(end - p)) return false;
    out.assign(reinterpret_cast<const char*>(p), n);
    p += n;
    return true;
}

struct Sections {
    std::string manifest;
    int manifests = 0;
    std::string firstImport; // "module.name"
    uint32_t imports = 0;
    std::map<std::string, uint8_t> exports; // name -> kind (0 func, 1 table, 2 memory, 3 global)
};

bool readSections(const std::vector<uint8_t>& wasm, Sections& s, std::string& error) {
    if (wasm.size() > kMaxPluginBytes) {
        error = "it is too big (at most " + std::to_string(kMaxPluginBytes >> 20) + " MB)";
        return false;
    }
    static const uint8_t kHead[8] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    if (wasm.size() < 8 || std::memcmp(wasm.data(), kHead, 4) != 0) {
        error = "it is not a WebAssembly module";
        return false;
    }
    if (std::memcmp(wasm.data(), kHead, 8) != 0) {
        error = "it is not a WebAssembly 1.0 core module (components and other versions aren't supported)";
        return false;
    }
    const uint8_t* p = wasm.data() + 8;
    const uint8_t* end = wasm.data() + wasm.size();
    while (p < end) {
        const uint8_t id = *p++;
        uint32_t size = 0;
        if (!leb32(p, end, size) || size > size_t(end - p)) {
            error = "it is damaged (a section runs past the end)";
            return false;
        }
        const uint8_t* q = p;
        const uint8_t* sectionEnd = p + size;
        if (id == 0) {
            std::string n;
            if (!name(q, sectionEnd, n)) {
                error = "it is damaged (a custom section's name)";
                return false;
            }
            if (n == kPluginManifestSection) {
                ++s.manifests;
                s.manifest.assign(reinterpret_cast<const char*>(q), size_t(sectionEnd - q));
            }
        } else if (id == 2) {
            if (!leb32(q, sectionEnd, s.imports)) {
                error = "it is damaged (the import section)";
                return false;
            }
            std::string mod, field;
            if (s.imports > 0 && name(q, sectionEnd, mod) && name(q, sectionEnd, field))
                s.firstImport = radialCleanText(mod, 40) + "." + radialCleanText(field, 40);
        } else if (id == 7) {
            uint32_t count = 0;
            if (!leb32(q, sectionEnd, count)) {
                error = "it is damaged (the export section)";
                return false;
            }
            for (uint32_t i = 0; i < count; ++i) {
                std::string n;
                uint32_t index = 0;
                if (!name(q, sectionEnd, n) || q >= sectionEnd) {
                    error = "it is damaged (the export section)";
                    return false;
                }
                const uint8_t kind = *q++;
                if (!leb32(q, sectionEnd, index)) {
                    error = "it is damaged (the export section)";
                    return false;
                }
                s.exports.emplace(n, kind);
            }
        }
        p = sectionEnd;
    }
    return true;
}

bool paramIdOk(const std::string& id) {
    if (id.empty() || id.size() > 32 || id == "id") return false;
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

// --- The runtime ---

#ifdef SF_HAVE_WASMTIME

std::string errorText(wasmtime_error_t* e) {
    wasm_name_t m;
    wasmtime_error_message(e, &m);
    std::string s(m.data, m.size);
    wasm_byte_vec_delete(&m);
    wasmtime_error_delete(e);
    return s;
}

std::string trapText(wasm_trap_t* t) {
    wasm_message_t m;
    wasm_trap_message(t, &m);
    std::string s(m.data, m.size);
    wasm_byte_vec_delete(&m);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// One engine for the process: no WASI, no threads, no signal handlers (explicit bounds checks, so the app's crash
// handler is never in wasmtime's way), fuel and epoch interruption on, deterministic NaNs.
wasm_engine_t* engine() {
    static wasm_engine_t* e = [] {
        wasm_config_t* c = wasm_config_new();
        wasmtime_config_consume_fuel_set(c, true);
        wasmtime_config_epoch_interruption_set(c, true);
        wasmtime_config_max_wasm_stack_set(c, 512 * 1024);
        wasmtime_config_wasm_threads_set(c, false);
        wasmtime_config_wasm_memory64_set(c, false);
        wasmtime_config_wasm_multi_memory_set(c, false);
        wasmtime_config_wasm_component_model_set(c, false);
        wasmtime_config_wasm_gc_set(c, false);
        wasmtime_config_cranelift_nan_canonicalization_set(c, true);
        wasmtime_config_signals_based_traps_set(c, false);
        return wasm_engine_new_with_config(c);
    }();
    return e;
}

using ModulePtr = std::shared_ptr<wasmtime_module_t>;

// Compiled modules by SHA-256 (a few; compiling is the slow part of a first run).
ModulePtr compiledModule(const std::vector<uint8_t>& wasm, std::string& error) {
    static std::mutex mu;
    static std::map<std::string, ModulePtr> cache;
    const std::string key = sha256Hex(wasm.data(), wasm.size());
    {
        std::lock_guard<std::mutex> lock(mu);
        if (auto it = cache.find(key); it != cache.end()) return it->second;
    }
    wasmtime_module_t* m = nullptr;
    if (wasmtime_error_t* e = wasmtime_module_new(engine(), wasm.data(), wasm.size(), &m)) {
        error = "it doesn't compile: " + radialCleanText(errorText(e), 300);
        return nullptr;
    }
    ModulePtr ptr(m, wasmtime_module_delete);
    std::lock_guard<std::mutex> lock(mu);
    if (cache.size() >= 8) cache.clear();
    cache[key] = ptr;
    return ptr;
}

// What the epoch callback checks: the run's own clock and cancel flag (the epoch is engine wide).
struct RunClock {
    std::chrono::steady_clock::time_point start;
    double seconds = 20.0;
    const std::atomic<bool>* cancel = nullptr;
    PluginFailure stop = PluginFailure::None;
};

wasmtime_error_t* onEpoch(const wasmtime_context_t*, void* data, uint64_t* delta, wasmtime_update_deadline_kind_t* kind) {
    RunClock& c = *static_cast<RunClock*>(data);
    if (c.cancel && c.cancel->load()) c.stop = PluginFailure::Cancelled;
    else if (std::chrono::duration<double>(std::chrono::steady_clock::now() - c.start).count() > c.seconds)
        c.stop = PluginFailure::Timeout;
    if (c.stop != PluginFailure::None) return wasmtime_error_new("stopped by SculptForge");
    *delta = 1;
    *kind = WASMTIME_UPDATE_DEADLINE_CONTINUE;
    return nullptr;
}

// Ticks the engine's epoch every 10 ms while a run is going.
class Ticker {
public:
    Ticker() : thread_([this] {
        std::unique_lock<std::mutex> lock(mu_);
        while (!cv_.wait_for(lock, std::chrono::milliseconds(10), [this] { return done_; }))
            wasmtime_engine_increment_epoch(engine());
    }) {}
    ~Ticker() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            done_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    bool done_ = false;
    std::thread thread_;
};

#endif // SF_HAVE_WASMTIME

uint32_t rdu32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v; // WebAssembly and every platform we build for are little-endian
}

} // namespace

const char* pluginKindId(PluginKind k) { return k == PluginKind::Generator ? "generator" : "deformer"; }
const char* pluginKindLabel(PluginKind k) { return k == PluginKind::Generator ? "Generator" : "Deformer"; }

bool parsePluginManifest(const std::string& textIn, PluginManifest& out, std::string& error) {
    out = {};
    if (textIn.size() > 64 * 1024) {
        error = "its manifest is too big (at most 64 KB)";
        return false;
    }
    Json j;
    std::string err;
    if (!Json::parseStrict(textIn, j, err) || !j.isObject()) {
        error = "its manifest is not a valid JSON object" + (err.empty() ? "" : ": " + err);
        return false;
    }
    static const std::set<std::string> kKeys = {"format", "formatVersion", "id",          "name",        "version", "author",
                                                "license", "description",  "kind",        "permissions", "params"};
    for (const auto& [key, v] : j.members())
        if (!kKeys.count(key)) {
            error = "its manifest has an unknown key \"" + radialCleanText(key, 40) + "\"";
            return false;
        }
    auto str = [&](const Json& o, const char* key, bool required, size_t maxBytes, bool multiline, std::string& to) {
        if (!o.has(key)) {
            if (required) error = std::string("its manifest is missing \"") + key + "\"";
            return !required;
        }
        const Json& v = o[key];
        if (!v.isString() || !packTextOk(v.asString(), maxBytes, multiline) || (required && v.asString().empty())) {
            error = std::string("its manifest's \"") + key + "\" must be text (at most " + std::to_string(maxBytes) +
                    " bytes, no control characters)";
            return false;
        }
        to = v.asString();
        return true;
    };
    std::string format, kind;
    if (!str(j, "format", true, 32, false, format)) return false;
    if (format != "sculptforge-plugin") {
        error = "its manifest's \"format\" must be \"sculptforge-plugin\"";
        return false;
    }
    if (!j["formatVersion"].isNumber() || j["formatVersion"].asNumber() != double(kPluginFormatVersion)) {
        error = "its manifest's \"formatVersion\" must be " + std::to_string(kPluginFormatVersion) +
                " (a newer SculptForge may run it)";
        return false;
    }
    if (!str(j, "id", true, 64, false, out.id) || !str(j, "name", true, 64, false, out.name) ||
        !str(j, "version", true, 32, false, out.version) || !str(j, "author", true, 64, false, out.author) ||
        !str(j, "license", true, 64, false, out.license) || !str(j, "description", false, 1000, true, out.description) ||
        !str(j, "kind", true, 16, false, kind))
        return false;
    if (!packIdValid(out.id)) {
        error = "its manifest's \"id\" must be 1-64 lower-case letters, digits, '.', '_' or '-'";
        return false;
    }
    if (kind == "deformer") out.kind = PluginKind::Deformer;
    else if (kind == "generator") out.kind = PluginKind::Generator;
    else {
        error = "its manifest's \"kind\" must be \"deformer\" or \"generator\"";
        return false;
    }
    if (j.has("permissions")) {
        const Json& ps = j["permissions"];
        if (!ps.isArray()) {
            error = "its manifest's \"permissions\" must be a list";
            return false;
        }
        for (const Json& p : ps.items()) {
            const std::string what = p.isString() ? radialCleanText(p.asString(), 40) : std::string("?");
            error = "it asks for the permission \"" + what +
                    "\", which this SculptForge doesn't grant (plugins get only the mesh they're given)";
            return false;
        }
    }
    if (j.has("params")) {
        const Json& ps = j["params"];
        if (!ps.isArray() || ps.size() > kMaxPluginParams) {
            error = "its manifest's \"params\" must be a list of at most " + std::to_string(kMaxPluginParams);
            return false;
        }
        std::set<std::string> seen;
        static const std::set<std::string> kParamKeys = {"id", "label", "help", "type", "min", "max", "default"};
        for (const Json& p : ps.items()) {
            if (!p.isObject()) {
                error = "its manifest's \"params\" must hold objects";
                return false;
            }
            for (const auto& [key, v] : p.members())
                if (!kParamKeys.count(key)) {
                    error = "a parameter has an unknown key \"" + radialCleanText(key, 40) + "\"";
                    return false;
                }
            PluginParam q;
            std::string type;
            if (!str(p, "id", true, 32, false, q.id) || !str(p, "label", true, 48, false, q.label) ||
                !str(p, "help", false, 300, false, q.help) || !str(p, "type", true, 8, false, type))
                return false;
            if (!paramIdOk(q.id) || !seen.insert(q.id).second) {
                error = "the parameter id \"" + radialCleanText(q.id, 32) +
                        "\" is not lower-case letters, digits and '_', or is used twice";
                return false;
            }
            if (type == "number") q.type = PluginParam::Type::Number;
            else if (type == "int") q.type = PluginParam::Type::Int;
            else if (type == "bool") q.type = PluginParam::Type::Bool;
            else {
                error = "parameter \"" + q.id + "\": \"type\" must be number, int or bool";
                return false;
            }
            auto num = [&](const char* key, double fallback, double& to) {
                if (!p.has(key)) {
                    to = fallback;
                    return true;
                }
                if (!p[key].isNumber() || !std::isfinite(p[key].asNumber()) || std::fabs(p[key].asNumber()) > 1e12) {
                    error = "parameter \"" + q.id + "\": \"" + key + "\" must be a number";
                    return false;
                }
                to = p[key].asNumber();
                return true;
            };
            if (q.type == PluginParam::Type::Bool) {
                q.min = 0.0;
                q.max = 1.0;
                if (p.has("default") && !p["default"].isBool()) {
                    error = "parameter \"" + q.id + "\": \"default\" must be true or false";
                    return false;
                }
                q.def = p["default"].asBool(false) ? 1.0 : 0.0;
            } else {
                if (!num("min", 0.0, q.min) || !num("max", 1.0, q.max) || !num("default", q.min, q.def)) return false;
                if (q.type == PluginParam::Type::Int) {
                    q.min = std::ceil(q.min);
                    q.max = std::floor(q.max);
                    q.def = std::round(q.def);
                }
                if (!(q.min <= q.max) || q.def < q.min || q.def > q.max) {
                    error = "parameter \"" + q.id + "\": needs min <= default <= max";
                    return false;
                }
            }
            out.params.push_back(std::move(q));
        }
    }
    return true;
}

bool readPlugin(const std::vector<uint8_t>& wasm, PluginManifest& out, std::string& error) {
    Sections s;
    if (!readSections(wasm, s, error)) return false;
    if (s.manifests != 1) {
        error = s.manifests ? "it has more than one manifest" : "it has no manifest (a \"sculptforge.plugin\" custom section)";
        return false;
    }
    if (!parsePluginManifest(s.manifest, out, error)) return false;
    if (s.imports) {
        error = "it asks for \"" + s.firstImport +
                "\" from outside; plugins can't import anything (no files, network, clock or system access)";
        return false;
    }
    for (const auto& [n, kind] : {std::pair<const char*, uint8_t>{"memory", 2}, {"sf_alloc", 0}, {"sf_run", 0}}) {
        auto it = s.exports.find(n);
        if (it == s.exports.end() || it->second != kind) {
            error = std::string("it doesn't export ") + (kind == 2 ? "its memory as \"" : "the function \"") + n + "\"";
            return false;
        }
    }
#ifdef SF_HAVE_WASMTIME
    if (wasmtime_error_t* e = wasmtime_module_validate(engine(), wasm.data(), wasm.size())) {
        error = "it is not a valid module: " + radialCleanText(errorText(e), 300);
        return false;
    }
#endif
    return true;
}

std::vector<uint8_t> pluginWithManifest(std::vector<uint8_t> wasm, const std::string& manifestJson) {
    auto leb = [](std::vector<uint8_t>& b, uint64_t v) {
        do {
            uint8_t c = v & 0x7f;
            v >>= 7;
            b.push_back(v ? uint8_t(c | 0x80) : c);
        } while (v);
    };
    std::vector<uint8_t> body;
    const std::string n = kPluginManifestSection;
    leb(body, n.size());
    body.insert(body.end(), n.begin(), n.end());
    body.insert(body.end(), manifestJson.begin(), manifestJson.end());
    wasm.push_back(0);
    leb(wasm, body.size());
    wasm.insert(wasm.end(), body.begin(), body.end());
    return wasm;
}

bool pluginWatToWasm(const std::string& wat, std::vector<uint8_t>& out, std::string& error) {
#ifdef SF_HAVE_WASMTIME
    wasm_byte_vec_t v;
    if (wasmtime_error_t* e = wasmtime_wat2wasm(wat.data(), wat.size(), &v)) {
        error = errorText(e);
        return false;
    }
    out.assign(reinterpret_cast<const uint8_t*>(v.data), reinterpret_cast<const uint8_t*>(v.data) + v.size);
    wasm_byte_vec_delete(&v);
    return true;
#else
    (void)wat;
    out.clear();
    error = "no plugin runtime in this build";
    return false;
#endif
}

bool pluginRuntimeAvailable() {
#ifdef SF_HAVE_WASMTIME
    return true;
#else
    return false;
#endif
}

std::string pluginRuntimeName() {
#ifdef SF_HAVE_WASMTIME
    return std::string("wasmtime ") + WASMTIME_VERSION;
#else
    return {};
#endif
}

PluginInput pluginInputFrom(const Mesh& m) {
    PluginInput in;
    in.positions = m.positions;
    in.normals = m.normals;
    in.normals.resize(in.positions.size(), glm::vec3(0.0f));
    if (m.hasMask()) in.mask = m.mask;
    in.triangles = m.triangles;
    return in;
}

PluginFailure runPlugin(const std::vector<uint8_t>& wasm, const PluginManifest& manifest, const PluginInput& in,
                        const PluginRunOptions& opts, PluginOutput& out, std::string& error) {
    out = {};
#ifndef SF_HAVE_WASMTIME
    (void)wasm;
    (void)manifest;
    (void)in;
    (void)opts;
    error = "this build of SculptForge can't run plugins (no plugin runtime for this platform)";
    return PluginFailure::NoRuntime;
#else
    const uint64_t nv = in.positions.size(), nt = in.triangles.size(), np = manifest.params.size();
    if (nv > 0xffffffffull / 64 || nt > 0xffffffffull / 64) {
        error = "the mesh is too big for a plugin (a plugin's memory holds at most 4 GB)";
        return PluginFailure::BadOutput;
    }
    // [header 48][params 8 np][positions 12 nv][normals 12 nv][mask 4 nv][triangles 12 nt]
    const uint64_t oParams = 48, oPos = oParams + 8 * np, oNrm = oPos + 12 * nv, oMask = oNrm + 12 * nv;
    const uint64_t oTri = oMask + (in.mask.empty() ? 0 : 4 * nv), inBytes = oTri + 12 * nt;
    if (inBytes >= (4ull << 30) - (64ull << 20)) {
        error = "the mesh is too big for a plugin (a plugin's memory holds at most 4 GB)";
        return PluginFailure::BadOutput;
    }
    const uint64_t cap = opts.limits.memoryBytes ? opts.limits.memoryBytes
                                                 : std::min<uint64_t>((256ull << 20) + 4 * inBytes, 4ull << 30);
    const uint64_t fuel = opts.limits.fuel ? opts.limits.fuel : 2000000000ull + 20000ull * (nv + nt);

    std::string err;
    ModulePtr module = compiledModule(wasm, err);
    if (!module) {
        error = err;
        return PluginFailure::BadModule;
    }
    {
        wasm_importtype_vec_t imports;
        wasmtime_module_imports(module.get(), &imports);
        const size_t n = imports.size;
        wasm_importtype_vec_delete(&imports);
        if (n) {
            error = "it imports from outside, which plugins can't";
            return PluginFailure::BadModule;
        }
    }
    RunClock clock;
    clock.seconds = opts.limits.seconds;
    clock.cancel = opts.cancel;
    std::unique_ptr<wasmtime_store_t, void (*)(wasmtime_store_t*)> store(wasmtime_store_new(engine(), nullptr, nullptr),
                                                                         wasmtime_store_delete);
    wasmtime_context_t* cx = wasmtime_store_context(store.get());
    wasmtime_store_limiter(store.get(), int64_t(cap), 100000, 1, 4, 1);
    if (wasmtime_error_t* e = wasmtime_context_set_fuel(cx, fuel)) {
        error = errorText(e);
        return PluginFailure::BadModule;
    }
    wasmtime_store_epoch_deadline_callback(store.get(), onEpoch, &clock, nullptr);
    wasmtime_context_set_epoch_deadline(cx, 1);
    clock.start = std::chrono::steady_clock::now();
    Ticker ticker;

    // A trap or an error from wasmtime: what it means for the user.
    auto failed = [&](wasmtime_error_t* e, wasm_trap_t* trap, const char* during) -> PluginFailure {
        std::string why;
        wasmtime_trap_code_t code = 0;
        bool haveCode = false;
        if (trap) {
            haveCode = wasmtime_trap_code(trap, &code);
            why = trapText(trap);
            wasm_trap_delete(trap);
        }
        if (e) why = errorText(e);
        if (clock.stop == PluginFailure::Cancelled) {
            error = "stopped";
            return PluginFailure::Cancelled;
        }
        if (clock.stop == PluginFailure::Timeout) {
            char t[64];
            std::snprintf(t, sizeof t, "it ran past its time (%.0f s)", clock.seconds);
            error = t;
            return PluginFailure::Timeout;
        }
        if (haveCode && code == WASMTIME_TRAP_CODE_OUT_OF_FUEL) {
            error = "it ran past its work limit";
            return PluginFailure::OutOfFuel;
        }
        if (haveCode && code == WASMTIME_TRAP_CODE_STACK_OVERFLOW) error = "it crashed (stack overflow)";
        else if (haveCode && code == WASMTIME_TRAP_CODE_MEMORY_OUT_OF_BOUNDS) error = "it crashed (memory access out of bounds)";
        else if (haveCode && code == WASMTIME_TRAP_CODE_UNREACHABLE_CODE_REACHED) error = "it crashed (unreachable code)";
        else error = std::string("it crashed ") + during + ": " + radialCleanText(why, 200);
        return trap || haveCode ? PluginFailure::Trap : PluginFailure::BadModule;
    };

    wasmtime_instance_t instance;
    wasm_trap_t* trap = nullptr;
    if (wasmtime_error_t* e = wasmtime_instance_new(cx, module.get(), nullptr, 0, &instance, &trap); e || trap) {
        // Starting it ran its start function, which may trap or loop: the same rules as a run.
        PluginFailure f = failed(e, trap, "while starting");
        if (f == PluginFailure::BadModule) error = "it can't start: " + error;
        return f;
    }
    wasmtime_extern_t memX, allocX, runX;
    if (!wasmtime_instance_export_get(cx, &instance, "memory", 6, &memX) || memX.kind != WASMTIME_EXTERN_MEMORY ||
        !wasmtime_instance_export_get(cx, &instance, "sf_alloc", 8, &allocX) || allocX.kind != WASMTIME_EXTERN_FUNC ||
        !wasmtime_instance_export_get(cx, &instance, "sf_run", 6, &runX) || runX.kind != WASMTIME_EXTERN_FUNC) {
        error = "it doesn't export memory, sf_alloc and sf_run";
        return PluginFailure::BadModule;
    }
    auto call = [&](const wasmtime_func_t& f, uint32_t arg, uint32_t& result, const char* during) -> PluginFailure {
        wasmtime_val_t a, r;
        a.kind = WASMTIME_I32;
        a.of.i32 = int32_t(arg);
        wasm_trap_t* t = nullptr;
        wasmtime_error_t* e = wasmtime_func_call(cx, &f, &a, 1, &r, 1, &t);
        if (e || t) {
            const PluginFailure fl = failed(e, t, during);
            // A wrong signature is an error before any code ran.
            if (fl == PluginFailure::BadModule) error = "sf_alloc and sf_run must take and return one i32: " + error;
            return fl;
        }
        if (r.kind != WASMTIME_I32) {
            error = std::string("its ") + during + " didn't return an i32";
            return PluginFailure::BadModule;
        }
        result = uint32_t(r.of.i32);
        return PluginFailure::None;
    };

    uint32_t base = 0;
    if (PluginFailure f = call(allocX.of.func, uint32_t(inBytes), base, "sf_alloc"); f != PluginFailure::None) return f;
    {
        const uint64_t size = wasmtime_memory_data_size(cx, &memX.of.memory);
        if (base == 0 || base % 8 || uint64_t(base) + inBytes > size) {
            error = "its sf_alloc returned memory it doesn't have (" + std::to_string(base) + " for " +
                    std::to_string(inBytes) + " bytes)";
            return PluginFailure::BadOutput;
        }
        uint8_t* mem = wasmtime_memory_data(cx, &memX.of.memory) + base;
        auto put = [&](uint64_t at, uint32_t v) { std::memcpy(mem + at, &v, 4); };
        auto addr = [&](uint64_t off) { return uint32_t(base + off); };
        put(0, kPluginAbi);
        put(4, manifest.kind == PluginKind::Generator ? 1u : 0u);
        put(8, uint32_t(nv));
        put(12, uint32_t(nt));
        put(16, uint32_t(np));
        put(20, addr(oPos));
        put(24, addr(oNrm));
        put(28, in.mask.empty() ? 0u : addr(oMask));
        put(32, addr(oTri));
        put(36, addr(oParams));
        put(40, 0);
        put(44, 0);
        for (size_t i = 0; i < np; ++i) {
            const PluginParam& q = manifest.params[i];
            double v = i < opts.params.size() && std::isfinite(opts.params[i]) ? opts.params[i] : q.def;
            v = std::clamp(v, q.min, q.max);
            if (q.type != PluginParam::Type::Number) v = std::round(v);
            std::memcpy(mem + oParams + 8 * i, &v, 8);
        }
        if (nv) {
            std::memcpy(mem + oPos, in.positions.data(), 12 * nv);
            const uint64_t nn = std::min<uint64_t>(nv, in.normals.size());
            std::memcpy(mem + oNrm, in.normals.data(), 12 * nn);
            std::memset(mem + oNrm + 12 * nn, 0, 12 * (nv - nn));
            if (!in.mask.empty()) std::memcpy(mem + oMask, in.mask.data(), 4 * nv);
        }
        if (nt) std::memcpy(mem + oTri, in.triangles.data(), 12 * nt);
    }
    uint32_t outAt = 0;
    if (PluginFailure f = call(runX.of.func, base, outAt, "sf_run"); f != PluginFailure::None) return f;

    // Everything below reads the plugin's memory as hostile data: every count and address is checked in 64 bits.
    const uint8_t* mem = wasmtime_memory_data(cx, &memX.of.memory);
    const uint64_t size = wasmtime_memory_data_size(cx, &memX.of.memory);
    auto bad = [&](const std::string& why) {
        error = "it returned a broken result: " + why;
        return PluginFailure::BadOutput;
    };
    if (outAt == 0 || outAt % 4 || uint64_t(outAt) + 32 > size) return bad("no result at " + std::to_string(outAt));
    const uint8_t* o = mem + outAt;
    const uint32_t abi = rdu32(o), status = rdu32(o + 4), ov = rdu32(o + 8), ot = rdu32(o + 12);
    const uint32_t oP = rdu32(o + 16), oT = rdu32(o + 20), oM = rdu32(o + 24), oML = rdu32(o + 28);
    if (abi != kPluginAbi) return bad("ABI " + std::to_string(abi) + ", expected " + std::to_string(kPluginAbi));
    if (status != 0) {
        std::string msg;
        if (oM && oML && uint64_t(oM) + std::min<uint32_t>(oML, 512) <= size)
            msg = radialCleanText(std::string(reinterpret_cast<const char*>(mem + oM), std::min<uint32_t>(oML, 512)), 300);
        error = msg.empty() ? "it reported an error (" + std::to_string(status) + ")" : msg;
        return PluginFailure::PluginError;
    }
    auto inside = [&](uint32_t at, uint64_t bytes) { return at % 4 == 0 && uint64_t(at) + bytes <= size; };
    if (manifest.kind == PluginKind::Deformer) {
        if (ov != nv) return bad(std::to_string(ov) + " vertices for a mesh of " + std::to_string(nv));
        if (ot != 0) return bad("a deformer can't change the triangles");
    } else {
        if (ov < 3 || ot < 1) return bad("a generator must return at least one triangle");
        if (ov > (64u << 20) || ot > (128u << 20)) return bad("too many vertices or triangles");
        if (!inside(oT, 12ull * ot)) return bad("its triangles are outside its memory");
    }
    if (!inside(oP, 12ull * ov)) return bad("its positions are outside its memory");
    out.positions.resize(ov);
    std::memcpy(out.positions.data(), mem + oP, 12ull * ov);
    for (uint32_t i = 0; i < ov; ++i) {
        const glm::vec3& p = out.positions[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            std::max({std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)}) > 1e9f)
            return bad("vertex " + std::to_string(i) + " is not a finite position");
    }
    if (manifest.kind == PluginKind::Generator) {
        out.triangles.resize(ot);
        std::memcpy(out.triangles.data(), mem + oT, 12ull * ot);
        for (uint32_t i = 0; i < ot; ++i) {
            const glm::uvec3& t = out.triangles[i];
            if (t.x >= ov || t.y >= ov || t.z >= ov)
                return bad("triangle " + std::to_string(i) + " uses a vertex past the end (" + std::to_string(ov) + ")");
            if (t.x == t.y || t.y == t.z || t.x == t.z) return bad("triangle " + std::to_string(i) + " repeats a vertex");
        }
    }
    return PluginFailure::None;
#endif
}

// --- Installed plugins ---

namespace {

fs::path offPath() { return u8path(packsRoot()) / "plugins-off.json"; }

std::map<std::string, std::string> readOff() { // sha256 -> why
    std::map<std::string, std::string> out;
    std::ifstream f(offPath(), std::ios::binary);
    if (!f) return out;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Json j;
    std::string err;
    if (text.size() > 256 * 1024 || !Json::parse(text, j, err) || !j.isObject()) return out;
    for (const auto& [sha, v] : j.members())
        if (v.isString()) out[sha] = radialCleanText(v.asString(), 300);
    return out;
}

bool writeOff(const std::map<std::string, std::string>& off, std::string& error) {
    Json j = Json::object();
    for (const auto& [sha, why] : off) j.set(sha, why);
    std::error_code ec;
    fs::create_directories(u8path(packsRoot()), ec);
    const fs::path tmp = offPath().string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(true) << "\n";
        if (!f) {
            error = "Cannot write " + u8str(tmp);
            return false;
        }
    }
    fs::rename(tmp, offPath(), ec);
    if (ec) {
        error = "Cannot write " + u8str(offPath()) + ": " + ec.message();
        return false;
    }
    return true;
}

bool readFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0 || uint64_t(n) > kMaxPluginBytes) return false;
    f.seekg(0);
    out.resize(size_t(n));
    return bool(f.read(reinterpret_cast<char*>(out.data()), n));
}

} // namespace

std::vector<InstalledPlugin> installedPlugins(std::vector<std::string>* messages) {
    std::vector<InstalledPlugin> out;
    const auto off = readOff();
    for (const PackContentItem& item : enabledPackItems(PackItemType::Plugin)) {
        InstalledPlugin p;
        std::vector<uint8_t> bytes;
        std::string err;
        if (!readFileBytes(item.file, bytes) || !readPlugin(bytes, p.manifest, err)) {
            if (messages) messages->push_back("Plugin " + item.label() + " left out: " + (err.empty() ? "unreadable" : err));
            continue;
        }
        p.file = item.file;
        p.packId = item.packId;
        p.packName = item.packName;
        p.key = item.packId + "/" + u8str(u8path(item.file).filename());
        p.sha256 = sha256Hex(bytes.data(), bytes.size());
        p.bytes = bytes.size();
        if (auto it = off.find(p.sha256); it != off.end()) {
            p.off = true;
            p.offWhy = it->second;
        }
        out.push_back(std::move(p));
    }
    return out;
}

bool findInstalledPlugin(const std::string& idOrKey, InstalledPlugin& out) {
    for (InstalledPlugin& p : installedPlugins())
        if (p.key == idOrKey || p.manifest.id == idOrKey || p.manifest.name == idOrKey) {
            out = std::move(p);
            return true;
        }
    return false;
}

bool setPluginOff(const InstalledPlugin& p, bool off, const std::string& why, std::string& error) {
    auto state = readOff();
    if (off) state[p.sha256] = radialCleanText(why, 300);
    else state.erase(p.sha256);
    return writeOff(state, error);
}

bool pluginParamValues(const PluginManifest& m, const std::map<std::string, double>& byId, std::vector<double>& out,
                       std::string& error) {
    out.clear();
    for (const auto& [id, v] : byId) {
        const bool known = std::any_of(m.params.begin(), m.params.end(), [&](const PluginParam& q) { return q.id == id; });
        if (!known) {
            error = m.name + " has no parameter \"" + radialCleanText(id, 32) + "\"";
            return false;
        }
    }
    for (const PluginParam& q : m.params) {
        auto it = byId.find(q.id);
        out.push_back(it == byId.end() ? q.def : std::clamp(it->second, q.min, q.max));
    }
    return true;
}

bool pluginCanRunOn(Document& doc, const InstalledPlugin& p, std::string& error) {
    if (p.manifest.kind == PluginKind::Deformer) return canDeformActivePart(doc, p.manifest.name, error);
    if (doc.sculptor.strokeActive()) {
        error = p.manifest.name + ": finish the stroke first";
        return false;
    }
    return true;
}

bool commitPluginOutput(Document& doc, const InstalledPlugin& p, const PluginInput& in, PluginOutput&& out,
                        std::string& message) {
    const std::string& name = p.manifest.name;
    if (p.manifest.kind == PluginKind::Generator) {
        Mesh m;
        m.positions = std::move(out.positions);
        m.triangles = std::move(out.triangles);
        m.rebuildTopology();
        m.recomputeAllNormals();
        const size_t v = m.vertexCount(), t = m.triangleCount();
        doc.addPart(std::move(m), name);
        message = name + ": added a Part (" + std::to_string(v) + " vertices, " + std::to_string(t) + " triangles)";
        return true;
    }
    std::string err;
    if (!pluginCanRunOn(doc, p, err)) {
        message = err;
        return false;
    }
    if (doc.mesh.vertexCount() != in.positions.size() || doc.mesh.triangleCount() != in.triangles.size()) {
        message = name + ": the Part changed while the plugin ran; run it again";
        return false;
    }
    // The mask and hidden faces hold their vertices, as every whole-mesh deformation does.
    const Mesh& m = doc.mesh;
    const bool hidden = m.hasHidden(), masked = m.hasMask();
    std::vector<glm::vec3>& P = out.positions;
    for (uint32_t v = 0; v < P.size(); ++v) {
        float w = masked ? 1.0f - m.maskAt(v) : 1.0f;
        if (hidden && m.vertexHidden(v)) w = 0.0f;
        P[v] = m.positions[v] + (P[v] - m.positions[v]) * w;
    }
    const size_t moved = commitDeformedPositions(doc, P, name, masked, err);
    if (!moved) {
        message = err;
        return false;
    }
    message = name + ": moved " + std::to_string(moved) + " vertices";
    return true;
}

bool runPluginOnDocument(Document& doc, const InstalledPlugin& p, const std::vector<double>& params, std::string& message,
                         const PluginLimits& limits) {
    if (p.off) {
        message = p.manifest.name + " is turned off (" + p.offWhy + "); turn it on again first";
        return false;
    }
    std::string err;
    if (!pluginCanRunOn(doc, p, err)) {
        message = err;
        return false;
    }
    std::vector<uint8_t> wasm;
    if (!readFileBytes(p.file, wasm) || sha256Hex(wasm.data(), wasm.size()) != p.sha256) {
        message = p.manifest.name + ": its file changed or can't be read; reinstall its pack";
        return false;
    }
    doc.syncBvh();
    const PluginInput in = pluginInputFrom(doc.mesh);
    PluginRunOptions opts;
    opts.params = params;
    opts.limits = limits;
    PluginOutput out;
    const PluginFailure f = runPlugin(wasm, p.manifest, in, opts, out, err);
    if (f != PluginFailure::None) {
        message = p.manifest.name + ": " + err + ". The mesh is unchanged.";
        if (pluginFailureDisables(f)) {
            std::string e2;
            setPluginOff(p, true, err, e2);
            message += " It is turned off until you turn it on again (Library > Packs, or plugin_enable).";
        }
        return false;
    }
    return commitPluginOutput(doc, p, in, std::move(out), message);
}

void registerPluginCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"plugin_list",
                  "List the installed plugins (from packs): id, kind, parameters and whether they are on. Plugins run "
                  "sandboxed with no access to files, network or the system.",
                  {},
                  [](Document&, const Args&) -> CommandResult {
                      std::vector<std::string> messages;
                      const auto plugins = installedPlugins(&messages);
                      std::string out = plugins.empty() ? "No plugins installed"
                                                        : std::to_string(plugins.size()) + (plugins.size() == 1 ? " plugin" : " plugins");
                      if (!pluginRuntimeAvailable()) out += " (this build can't run plugins)";
                      for (const InstalledPlugin& p : plugins) {
                          out += "\n" + p.manifest.id + "  " + p.manifest.name + " " + p.manifest.version + " (" +
                                 pluginKindId(p.manifest.kind) + ") by " + p.manifest.author + ", from " + p.packName;
                          for (const PluginParam& q : p.manifest.params) {
                              char range[96];
                              std::snprintf(range, sizeof range, "%g..%g, default %g", q.min, q.max, q.def);
                              out += "\n    " + q.id + " (" + (q.type == PluginParam::Type::Bool ? std::string("bool") : range) + ")";
                          }
                          if (p.off) out += "\n    off: " + p.offWhy;
                      }
                      for (const std::string& m : messages) out += "\n" + m;
                      return {true, out};
                  }});
    registry.add({"plugin_run",
                  "Run an installed plugin: a deformer moves the active Part's vertices (mask-aware, one undo step), a "
                  "generator adds a new Part. Give its parameters by id (plugin_list shows them), e.g. amount=0.3. It "
                  "runs sandboxed with a time, work and memory limit; one that crashes or runs too long is turned off.",
                  {{"id", T::String, "The plugin's id or name.", true}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      InstalledPlugin p;
                      if (!findInstalledPlugin(argString(a, "id"), p))
                          return {false, "No installed plugin \"" + argString(a, "id") + "\" (plugin_list)"};
                      std::map<std::string, double> byId;
                      for (const auto& [k, v] : a) {
                          if (k == "id") continue;
                          if (const double* d = std::get_if<double>(&v)) byId[k] = *d;
                          else if (const bool* b = std::get_if<bool>(&v)) byId[k] = *b ? 1.0 : 0.0;
                          else {
                              const std::string& s = std::get<std::string>(v);
                              char* end = nullptr;
                              const double d = std::strtod(s.c_str(), &end);
                              if (s == "true" || s == "false") byId[k] = s == "true" ? 1.0 : 0.0;
                              else if (!s.empty() && end && *end == 0) byId[k] = d;
                              else return {false, k + " must be a number"};
                          }
                      }
                      std::vector<double> params;
                      std::string msg;
                      if (!pluginParamValues(p.manifest, byId, params, msg)) return {false, msg};
                      const bool ok = runPluginOnDocument(doc, p, params, msg);
                      return {ok, msg};
                  }});
    registry.add({"plugin_enable", "Turn a plugin that was turned off after a failure on again.",
                  {{"id", T::String, "The plugin's id or name.", true}},
                  [](Document&, const Args& a) -> CommandResult {
                      InstalledPlugin p;
                      if (!findInstalledPlugin(argString(a, "id"), p))
                          return {false, "No installed plugin \"" + argString(a, "id") + "\" (plugin_list)"};
                      std::string err;
                      if (!setPluginOff(p, false, "", err)) return {false, err};
                      return {true, "Turned on plugin " + p.manifest.name};
                  }});
}

} // namespace sf
