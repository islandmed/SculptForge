// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SelectionSets.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Multires.h"
#include "core/PaintLayer.h"
#include "core/Sculpt.h"
#include "io/Zip.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <sstream>

namespace sf {

namespace {

constexpr uint32_t kPackMagic = 0x53534653u; // "SFSS"
constexpr uint32_t kPackVersion = 1;
constexpr size_t kMaxNameBytes = 256;
constexpr const char* kSetsFormat = "SculptForge selection sets";
constexpr int kSetsFileVersion = 1;
constexpr size_t kMaxSets = 100000;
const glm::vec3 kWhite(1.0f);

struct ChannelName {
    uint32_t bit;
    const char* name;
};
constexpr ChannelName kChannelNames[] = {{kSetMask, "mask"},       {kSetPolygroups, "polygroups"}, {kSetPaint, "paint"},
                                         {kSetVisibility, "visibility"}, {kSetUvs, "uvs"},        {kSetMorph, "morph"}};

// --- Little-endian bytes ---

struct Out {
    std::vector<uint8_t> b;
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i)));
    }
    void words(const void* p, size_t n) { // n 32-bit values
        if constexpr (std::endian::native == std::endian::little) {
            const size_t at = b.size();
            b.resize(at + n * 4);
            if (n) std::memcpy(b.data() + at, p, n * 4);
        } else {
            const auto* w = static_cast<const uint32_t*>(p);
            for (size_t i = 0; i < n; ++i) u32(w[i]);
        }
    }
    void runs(const std::vector<SetRun>& r) {
        u64(r.size());
        static_assert(sizeof(SetRun) == 8);
        words(r.data(), r.size() * 2);
    }
    void section(uint32_t tag, const Out& payload) {
        u32(tag);
        u64(payload.b.size());
        b.insert(b.end(), payload.b.begin(), payload.b.end());
    }
};

struct In {
    const uint8_t* p = nullptr;
    size_t n = 0, at = 0;
    bool left(size_t bytes) const { return bytes <= n - at; }
    bool u32(uint32_t& v) {
        if (!left(4)) return false;
        v = uint32_t(p[at]) | uint32_t(p[at + 1]) << 8 | uint32_t(p[at + 2]) << 16 | uint32_t(p[at + 3]) << 24;
        at += 4;
        return true;
    }
    bool u64(uint64_t& v) {
        uint32_t lo = 0, hi = 0;
        if (!u32(lo) || !u32(hi)) return false;
        v = uint64_t(lo) | uint64_t(hi) << 32;
        return true;
    }
    // `count` 32-bit values into `out` (resized), refusing counts the remaining bytes can't hold.
    template <class T>
    bool words(uint64_t count, size_t perElement, std::vector<T>& out) {
        static_assert(sizeof(T) % 4 == 0);
        if (perElement * 4 != sizeof(T) || count > (n - at) / sizeof(T)) return false;
        out.resize(size_t(count));
        const size_t bytes = size_t(count) * sizeof(T);
        if constexpr (std::endian::native == std::endian::little) {
            if (bytes) std::memcpy(out.data(), p + at, bytes);
            at += bytes;
        } else {
            auto* w = reinterpret_cast<uint32_t*>(out.data());
            for (size_t i = 0; i < size_t(count) * perElement; ++i) u32(w[i]);
        }
        return true;
    }
    bool runs(std::vector<SetRun>& r) {
        uint64_t count = 0;
        return u64(count) && words(count, 2, r);
    }
};

// Runs of the elements [0, n) for which on(i) holds.
template <class On>
std::vector<SetRun> runsOf(size_t n, On on) {
    std::vector<SetRun> out;
    for (size_t i = 0; i < n;) {
        if (!on(i)) {
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < n && on(j)) ++j;
        out.push_back({uint32_t(i), uint32_t(j - i)});
        i = j;
    }
    return out;
}

// Runs in ascending order, not overlapping, inside [0, count); returns how many elements they cover.
bool runsValid(const std::vector<SetRun>& runs, uint64_t count, uint64_t& covered) {
    uint64_t end = 0;
    covered = 0;
    for (const SetRun& r : runs) {
        if (r.length == 0 || r.start < end || uint64_t(r.start) + r.length > count) return false;
        end = uint64_t(r.start) + r.length;
        covered += r.length;
    }
    return true;
}

// Fills the set's channels from arrays (null = the default everywhere: unmasked, group 0, white, shown).
void encodeChannels(SelectionSet& s, const float* mask, const uint32_t* groups, const glm::vec3* colors,
                    const uint8_t* hidden, const std::vector<glm::vec2>* uvs, const std::vector<glm::vec3>* positions) {
    const size_t nv = s.vertexCount, nt = s.triangleCount;
    if (s.has(kSetMask) && mask) {
        s.maskRuns = runsOf(nv, [&](size_t v) { return mask[v] != 0.0f; });
        bool ones = true;
        for (const SetRun& r : s.maskRuns)
            for (uint32_t k = 0; k < r.length && ones; ++k) ones = mask[r.start + k] == 1.0f;
        if (!ones)
            for (const SetRun& r : s.maskRuns) s.maskValues.insert(s.maskValues.end(), mask + r.start, mask + r.start + r.length);
    }
    if (s.has(kSetPolygroups)) {
        for (size_t t = 0; t < nt;) {
            const uint32_t id = groups ? groups[t] : 0u;
            size_t u = t + 1;
            while (u < nt && (groups ? groups[u] : 0u) == id) ++u;
            s.groupRuns.push_back({id, uint32_t(u - t)});
            t = u;
        }
    }
    if (s.has(kSetPaint) && colors) {
        s.paintRuns = runsOf(nv, [&](size_t v) { return colors[v] != kWhite; });
        for (const SetRun& r : s.paintRuns) s.paintValues.insert(s.paintValues.end(), colors + r.start, colors + r.start + r.length);
    }
    if (s.has(kSetVisibility) && hidden) s.hiddenRuns = runsOf(nt, [&](size_t t) { return hidden[t] != 0; });
    if (s.has(kSetUvs) && uvs) s.uvs = *uvs;
    if (s.has(kSetMorph) && positions) s.positions = *positions;
}

bool validName(const std::string& name, std::string& error) {
    if (name.empty()) {
        error = "A selection set needs a name";
        return false;
    }
    if (name.size() > kMaxNameBytes) {
        error = "Selection set names are at most " + std::to_string(kMaxNameBytes) + " bytes";
        return false;
    }
    for (unsigned char c : name)
        if (c < 0x20 || c == 0x7f) {
            error = "Selection set names can't hold control characters";
            return false;
        }
    return true;
}

std::string quote(const std::string& name) { return "'" + name + "'"; }

// The level of the mesh's stack whose counts the set has: the current one when it fits, else the one it was
// stored at, else any. -1 = none (another mesh, or the topology changed).
int setLevel(const Mesh& mesh, const SelectionSet& s) {
    const int cur = subdivCurrentLevel(mesh), n = subdivLevelCount(mesh);
    if (mesh.vertexCount() == s.vertexCount && mesh.triangleCount() == s.triangleCount) return cur;
    auto fits = [&](int k) {
        return k != cur && subdivLevelData(mesh, k).vertexCount() == s.vertexCount &&
               subdivLevel(mesh, k).triangleCount() == s.triangleCount;
    };
    if (n == 0) return -1;
    if (s.level >= 0 && s.level < n && fits(s.level)) return s.level;
    for (int k = 0; k < n; ++k)
        if (fits(k)) return k;
    return -1;
}

std::shared_ptr<SelectionSetList> editableList(const Mesh& mesh) {
    return mesh.selectionSets ? std::make_shared<SelectionSetList>(*mesh.selectionSets) : std::make_shared<SelectionSetList>();
}

// Makes `list` the active Part's (one undo step).
void commitList(Document& doc, std::shared_ptr<SelectionSetList> list, std::string label) {
    UndoRecord r;
    r.label = std::move(label);
    r.sets = std::make_unique<SelectionSetsUndo>();
    r.sets->list = doc.mesh.selectionSets;
    if (list->sets.empty()) list->morphTarget.clear();
    doc.mesh.selectionSets = list->sets.empty() ? nullptr : std::shared_ptr<const SelectionSetList>(std::move(list));
    doc.undo.push(std::move(r));
}

const SelectionSet* findSet(const Document& doc, const std::string& name, std::string& error) {
    const SelectionSetList* list = doc.mesh.selectionSets.get();
    const int i = list ? list->find(name) : -1;
    if (i < 0) {
        error = "No selection set " + quote(name) + (list && !list->sets.empty() ? "" : " (this Part has no sets yet)");
        return nullptr;
    }
    return list->sets[size_t(i)].get();
}

std::string uniqueSetName(const Mesh& mesh, const std::string& wanted) {
    const SelectionSetList* list = mesh.selectionSets.get();
    if (!list || list->find(wanted) < 0) return wanted;
    for (int k = 2;; ++k) {
        std::string candidate = wanted + " " + std::to_string(k);
        if (list->find(candidate) < 0) return candidate;
    }
}

// Moves the active Part's surface `amount` of the way to `target` (scaled by vertexInfluence: mask and hidden
// faces), recording the old values in `r`. Returns the vertices moved.
size_t applyShape(Document& doc, const std::vector<glm::vec3>& target, float amount, UndoRecord& r, std::string& error) {
    Mesh& m = doc.mesh;
    if (target.size() != m.vertexCount()) return 0;
    const int layer = doc.activeLayer >= 0 && doc.activeLayer < int(m.layers.size()) ? doc.activeLayer : -1;
    const float weight = m.layerTargetWeight(layer);
    if (std::abs(weight) < 1e-6f) {
        error = "The active sculpt layer's weight is 0, so its offsets would not show";
        return 0;
    }
    const bool layered = !m.layers.empty();
    std::vector<glm::vec3>& channel = m.layerTarget(layer);
    r.layer = layer;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const float k = vertexInfluence(m, v) * amount;
        if (k == 0.0f) continue;
        const glm::vec3 d = (target[v] - m.positions[v]) * k;
        if (d == glm::vec3(0.0f)) continue;
        r.positions.add(v, channel[v]);
        if (layered) channel[v] += d / weight;
        else m.positions[v] += d;
    }
    const std::vector<uint32_t>& moved = r.positions.indices;
    if (moved.empty()) return 0;
    if (layered) m.evaluate(moved);
    m.updateNormals(moved);
    for (uint32_t v : moved) m.markDirty(v);
    if (moved.size() * 4 > m.vertexCount()) doc.bvh.refitAll(m);
    else doc.bvh.refit(m, moved);
    return moved.size();
}

} // namespace

// --- Channels and operations ---

bool parseSetChannels(const std::string& text, uint32_t& out, std::string& error) {
    out = 0;
    std::string word;
    auto take = [&]() {
        if (word.empty()) return true;
        std::string w;
        for (char c : word) w += char(std::tolower(static_cast<unsigned char>(c)));
        word.clear();
        if (w == "all") out |= kSetAllChannels;
        else if (w == "mask") out |= kSetMask;
        else if (w == "polygroups" || w == "groups" || w == "polygroup" || w == "face_groups" || w == "facegroups") out |= kSetPolygroups;
        else if (w == "paint" || w == "colors" || w == "colours" || w == "polypaint" || w == "vertex_paint") out |= kSetPaint;
        else if (w == "visibility" || w == "hidden" || w == "visible") out |= kSetVisibility;
        else if (w == "uvs" || w == "uv") out |= kSetUvs;
        else if (w == "morph" || w == "shape" || w == "positions") out |= kSetMorph;
        else {
            error = "Unknown channel '" + w + "' (use mask, polygroups, paint, visibility, uvs, morph or all)";
            return false;
        }
        return true;
    };
    for (char c : text) {
        if (c == ',' || c == ' ' || c == '+' || c == '|' || c == ';') {
            if (!take()) return false;
        } else {
            word += c;
        }
    }
    return take();
}

std::string setChannelNames(uint32_t channels) {
    std::string out;
    for (const auto& c : kChannelNames)
        if (channels & c.bit) out += (out.empty() ? "" : ", ") + std::string(c.name);
    return out.empty() ? "nothing" : out;
}

bool parseSetOp(const std::string& text, SetOp& out) {
    std::string w;
    for (char c : text) w += char(std::tolower(static_cast<unsigned char>(c)));
    if (w.empty() || w == "replace") out = SetOp::Replace;
    else if (w == "union" || w == "add" || w == "+") out = SetOp::Union;
    else if (w == "intersect" || w == "intersection" || w == "&") out = SetOp::Intersect;
    else if (w == "subtract" || w == "difference" || w == "-") out = SetOp::Subtract;
    else return false;
    return true;
}

const char* setOpName(SetOp op) {
    switch (op) {
    case SetOp::Union: return "union";
    case SetOp::Intersect: return "intersect";
    case SetOp::Subtract: return "subtract";
    default: return "replace";
    }
}

void combineMask(std::vector<float>& current, const std::vector<float>& other, SetOp op) {
    if (current.size() != other.size()) current.resize(other.size(), 0.0f);
    for (size_t v = 0; v < other.size(); ++v) {
        const float a = current[v], b = other[v];
        switch (op) {
        case SetOp::Replace: current[v] = b; break;
        case SetOp::Union: current[v] = std::max(a, b); break;
        case SetOp::Intersect: current[v] = std::min(a, b); break;
        case SetOp::Subtract: current[v] = std::min(a, 1.0f - b); break;
        }
    }
}

void combineHidden(std::vector<uint8_t>& current, const std::vector<uint8_t>& other, SetOp op) {
    if (current.size() != other.size()) current.resize(other.size(), 0);
    for (size_t t = 0; t < other.size(); ++t) {
        const bool a = current[t] == 0, b = other[t] == 0; // shown
        bool shown = b;
        switch (op) {
        case SetOp::Replace: shown = b; break;
        case SetOp::Union: shown = a || b; break;
        case SetOp::Intersect: shown = a && b; break;
        case SetOp::Subtract: shown = a && !b; break;
        }
        current[t] = shown ? 0 : 1;
    }
}

// --- Sets ---

size_t SelectionSet::bytes() const {
    return sizeof(SelectionSet) + name.size() +
           (maskRuns.size() + groupRuns.size() + paintRuns.size() + hiddenRuns.size()) * sizeof(SetRun) +
           maskValues.size() * sizeof(float) + paintValues.size() * sizeof(glm::vec3) + uvs.size() * sizeof(glm::vec2) +
           positions.size() * sizeof(glm::vec3);
}

int SelectionSetList::find(const std::string& name) const {
    for (size_t i = 0; i < sets.size(); ++i)
        if (sets[i]->name == name) return int(i);
    return -1;
}

size_t SelectionSetList::bytes() const {
    size_t total = 0;
    for (const auto& s : sets) total += s->bytes();
    return total;
}

SelectionSet captureSelectionSet(const Mesh& mesh, std::string name, uint32_t channels) {
    SelectionSet s;
    s.name = std::move(name);
    s.channels = channels & kSetAllChannels;
    if (!mesh.hasUvs() || mesh.uvs.size() != mesh.triangleCount() * 3) s.channels &= ~uint32_t(kSetUvs);
    s.level = subdivCurrentLevel(mesh);
    s.vertexCount = uint32_t(mesh.vertexCount());
    s.triangleCount = uint32_t(mesh.triangleCount());
    encodeChannels(s, mesh.hasMask() ? mesh.mask.data() : nullptr, mesh.hasPolygroups() ? mesh.polygroups.data() : nullptr,
                   mesh.hasColors() ? mesh.colors.data() : nullptr, mesh.hasHidden() ? mesh.hidden.data() : nullptr,
                   &mesh.uvs, &mesh.positions);
    return s;
}

bool makeSelectionSet(std::string name, const SelectionSetData& d, uint32_t vertexCount, uint32_t triangleCount, int level,
                      SelectionSet& out, std::string& error) {
    if (!validName(name, error)) return false;
    auto check = [&](uint32_t bit, size_t have, size_t want, const char* what) {
        if (!(d.channels & bit) || have == want) return true;
        error = std::string(what) + ": expected " + std::to_string(want) + " values, got " + std::to_string(have);
        return false;
    };
    if (!check(kSetMask, d.mask.size(), vertexCount, "mask") ||
        !check(kSetPolygroups, d.polygroups.size(), triangleCount, "polygroups") ||
        !check(kSetPaint, d.colors.size(), vertexCount, "paint") ||
        !check(kSetVisibility, d.hidden.size(), triangleCount, "hidden") ||
        !check(kSetUvs, d.uvs.size(), size_t(triangleCount) * 3, "uvs") ||
        !check(kSetMorph, d.positions.size(), vertexCount, "positions"))
        return false;
    out = SelectionSet{};
    out.name = std::move(name);
    out.channels = d.channels & kSetAllChannels;
    out.level = level;
    out.vertexCount = vertexCount;
    out.triangleCount = triangleCount;
    encodeChannels(out, d.mask.empty() ? nullptr : d.mask.data(), d.polygroups.empty() ? nullptr : d.polygroups.data(),
                   d.colors.empty() ? nullptr : d.colors.data(), d.hidden.empty() ? nullptr : d.hidden.data(), &d.uvs,
                   &d.positions);
    return true;
}

void decodeSelectionSet(const SelectionSet& s, uint32_t channels, SelectionSetData& out) {
    out = SelectionSetData{};
    out.channels = channels & s.channels;
    if (out.channels & kSetMask) {
        out.mask.assign(s.vertexCount, 0.0f);
        size_t k = 0;
        for (const SetRun& r : s.maskRuns)
            for (uint32_t i = 0; i < r.length; ++i) out.mask[r.start + i] = s.maskValues.empty() ? 1.0f : s.maskValues[k++];
    }
    if (out.channels & kSetPolygroups) {
        out.polygroups.reserve(s.triangleCount);
        for (const SetRun& r : s.groupRuns) out.polygroups.insert(out.polygroups.end(), r.length, r.start);
        out.polygroups.resize(s.triangleCount, 0u);
    }
    if (out.channels & kSetPaint) {
        out.colors.assign(s.vertexCount, kWhite);
        size_t k = 0;
        for (const SetRun& r : s.paintRuns)
            for (uint32_t i = 0; i < r.length; ++i) out.colors[r.start + i] = s.paintValues[k++];
    }
    if (out.channels & kSetVisibility) {
        out.hidden.assign(s.triangleCount, 0);
        for (const SetRun& r : s.hiddenRuns) std::fill_n(out.hidden.begin() + r.start, r.length, uint8_t(1));
    }
    if (out.channels & kSetUvs) out.uvs = s.uvs;
    if (out.channels & kSetMorph) out.positions = s.positions;
}

bool selectionSetFits(const Mesh& mesh, const SelectionSet& s) { return setLevel(mesh, s) >= 0; }

bool selectionSetAt(const Mesh& mesh, const SelectionSet& s, uint32_t channels, SelectionSetData& out, std::string& error) {
    const int from = setLevel(mesh, s), to = subdivCurrentLevel(mesh);
    if (from < 0) {
        error = "Selection set " + quote(s.name) + " was stored on a mesh with " + std::to_string(s.vertexCount) +
                " vertices and " + std::to_string(s.triangleCount) + " triangles; this Part has " +
                std::to_string(mesh.vertexCount()) + " and " + std::to_string(mesh.triangleCount()) +
                (subdivLevelCount(mesh) ? " at this level, and no level of it fits" : "");
        return false;
    }
    decodeSelectionSet(s, channels, out);
    if (from == to) return true;
    bool ok = true;
    if (!out.mask.empty()) ok &= subdivMapVertices(mesh, from, to, out.mask);
    if (!out.colors.empty()) ok &= subdivMapVertices(mesh, from, to, out.colors, kWhite, false);
    if (!out.polygroups.empty()) ok &= subdivMapTriangles(mesh, from, to, out.polygroups);
    if (!out.hidden.empty()) ok &= subdivMapTriangles(mesh, from, to, out.hidden);
    if (!out.uvs.empty()) ok &= subdivMapCorners(mesh, from, to, out.uvs);
    if (!out.positions.empty()) {
        // The shape as an offset from level `from`'s own shape, carried through the levels' stencils and added to
        // this level's: the detail of the levels in between stays.
        const Mesh at = subdivLevelMesh(mesh, from);
        if (at.vertexCount() != out.positions.size()) {
            ok = false;
        } else {
            for (size_t v = 0; v < out.positions.size(); ++v) out.positions[v] -= at.positions[v];
            ok &= subdivMapVertices(mesh, from, to, out.positions, glm::vec3(0.0f), true);
            if (out.positions.size() == mesh.vertexCount())
                for (size_t v = 0; v < out.positions.size(); ++v) out.positions[v] += mesh.positions[v];
        }
    }
    if (!ok || (!out.mask.empty() && out.mask.size() != mesh.vertexCount()) ||
        (!out.positions.empty() && out.positions.size() != mesh.vertexCount()) ||
        (!out.hidden.empty() && out.hidden.size() != mesh.triangleCount())) {
        error = "Selection set " + quote(s.name) + " could not be carried to this subdivision level";
        return false;
    }
    return true;
}

// --- Binary form ---

std::vector<uint8_t> packSelectionSet(const SelectionSet& s) {
    Out o;
    o.u32(kPackMagic);
    o.u32(kPackVersion);
    o.u32(s.channels);
    o.u32(s.vertexCount);
    o.u32(s.triangleCount);
    o.u32(uint32_t(s.level));
    o.u32(uint32_t(s.name.size()));
    o.b.insert(o.b.end(), s.name.begin(), s.name.end());
    if (s.has(kSetMask)) {
        Out p;
        p.runs(s.maskRuns);
        p.u32(s.maskValues.empty() ? 1u : 0u);
        p.words(s.maskValues.data(), s.maskValues.size());
        o.section(kSetMask, p);
    }
    if (s.has(kSetPolygroups)) {
        Out p;
        p.runs(s.groupRuns);
        o.section(kSetPolygroups, p);
    }
    if (s.has(kSetPaint)) {
        Out p;
        p.runs(s.paintRuns);
        p.words(s.paintValues.data(), s.paintValues.size() * 3);
        o.section(kSetPaint, p);
    }
    if (s.has(kSetVisibility)) {
        Out p;
        p.runs(s.hiddenRuns);
        o.section(kSetVisibility, p);
    }
    if (s.has(kSetUvs)) {
        Out p;
        p.u64(s.uvs.size());
        p.words(s.uvs.data(), s.uvs.size() * 2);
        o.section(kSetUvs, p);
    }
    if (s.has(kSetMorph)) {
        Out p;
        p.u64(s.positions.size());
        p.words(s.positions.data(), s.positions.size() * 3);
        o.section(kSetMorph, p);
    }
    return std::move(o.b);
}

bool unpackSelectionSet(const uint8_t* data, size_t size, SelectionSet& out, std::string& error) {
    In in{data, size, 0};
    uint32_t magic = 0, version = 0, channels = 0, nv = 0, nt = 0, level = 0, nameBytes = 0;
    if (!in.u32(magic) || magic != kPackMagic || !in.u32(version)) {
        error = "not a selection set";
        return false;
    }
    if (version > kPackVersion) {
        error = "saved by a newer SculptForge (set format " + std::to_string(version) + ")";
        return false;
    }
    if (!in.u32(channels) || !in.u32(nv) || !in.u32(nt) || !in.u32(level) || !in.u32(nameBytes) ||
        nameBytes > kMaxNameBytes || !in.left(nameBytes)) {
        error = "the set's header is damaged";
        return false;
    }
    SelectionSet s;
    s.name.assign(reinterpret_cast<const char*>(data + in.at), nameBytes);
    in.at += nameBytes;
    s.channels = channels & kSetAllChannels;
    s.vertexCount = nv;
    s.triangleCount = nt;
    s.level = int(std::min<uint32_t>(level, 64));
    if (!validName(s.name, error)) return false;
    uint32_t found = 0;
    auto bad = [&](const char* what) {
        error = std::string("the set's ") + what + " is damaged";
        return false;
    };
    while (in.at < in.n) {
        uint32_t tag = 0;
        uint64_t length = 0;
        if (!in.u32(tag) || !in.u64(length) || !in.left(size_t(std::min<uint64_t>(length, SIZE_MAX)))) return bad("data");
        In p{data + in.at, size_t(length), 0};
        in.at += size_t(length);
        if (!(s.channels & tag) || (found & tag)) continue; // unknown or repeated sections are skipped
        found |= tag;
        uint64_t covered = 0;
        if (tag == kSetMask) {
            uint32_t ones = 0;
            if (!p.runs(s.maskRuns) || !runsValid(s.maskRuns, nv, covered) || !p.u32(ones) ||
                (!ones && !p.words(covered, 1, s.maskValues)))
                return bad("mask");
        } else if (tag == kSetPolygroups) {
            if (!p.runs(s.groupRuns)) return bad("polygroups");
            uint64_t total = 0;
            for (const SetRun& r : s.groupRuns) total += r.length;
            if (total != nt) return bad("polygroups");
        } else if (tag == kSetPaint) {
            if (!p.runs(s.paintRuns) || !runsValid(s.paintRuns, nv, covered) || !p.words(covered, 3, s.paintValues))
                return bad("paint");
        } else if (tag == kSetVisibility) {
            if (!p.runs(s.hiddenRuns) || !runsValid(s.hiddenRuns, nt, covered)) return bad("visibility");
        } else if (tag == kSetUvs) {
            uint64_t count = 0;
            if (!p.u64(count) || count != uint64_t(nt) * 3 || !p.words(count, 2, s.uvs)) return bad("UVs");
        } else if (tag == kSetMorph) {
            uint64_t count = 0;
            if (!p.u64(count) || count != nv || !p.words(count, 3, s.positions)) return bad("shape");
        }
    }
    if (found != s.channels) return bad("channel list");
    out = std::move(s);
    return true;
}

Json selectionSetSummary(const SelectionSet& s) {
    Json j = Json::object();
    j.set("name", s.name);
    Json channels = Json::array();
    for (const auto& c : kChannelNames)
        if (s.channels & c.bit) channels.push(c.name);
    j.set("channels", std::move(channels));
    j.set("level", s.level);
    j.set("vertex_count", double(s.vertexCount));
    j.set("triangle_count", double(s.triangleCount));
    j.set("bytes", double(s.bytes()));
    return j;
}

bool saveSelectionSets(const std::string& path, const SelectionSetList& list, std::string& error) {
    Json manifest = Json::object();
    manifest.set("format", kSetsFormat);
    manifest.set("version", kSetsFileVersion);
    if (!list.morphTarget.empty()) manifest.set("morph_target", list.morphTarget);
    Json sets = Json::array();
    std::vector<ZipEntry> entries;
    entries.push_back({"manifest.json", {}});
    for (size_t i = 0; i < list.sets.size(); ++i) {
        Json j = selectionSetSummary(*list.sets[i]);
        const std::string file = "sets/" + std::to_string(i) + ".bin";
        j.set("data", file);
        sets.push(std::move(j));
        entries.push_back({file, packSelectionSet(*list.sets[i])});
    }
    manifest.set("sets", std::move(sets));
    const std::string text = manifest.dump(true);
    entries[0].data.assign(text.begin(), text.end());
    return writeStoredZip(path, entries, 4, error);
}

bool loadSelectionSets(const std::string& path, SelectionSetList& out, std::string& error) {
    std::vector<ZipEntry> entries;
    if (!readStoredZip(path, entries, error)) return false;
    auto entry = [&](const std::string& name) -> const ZipEntry* {
        for (const auto& e : entries)
            if (e.name == name) return &e;
        return nullptr;
    };
    const ZipEntry* m = entry("manifest.json");
    Json manifest;
    if (!m || !Json::parse(std::string(m->bytes(), m->bytes() + m->size()), manifest, error) || !manifest.isObject() ||
        manifest["format"].asString() != kSetsFormat) {
        error = path + " is not a SculptForge selection sets file";
        return false;
    }
    if (manifest["version"].asNumber(0) > kSetsFileVersion) {
        error = path + " was saved by a newer SculptForge; please update to open it";
        return false;
    }
    const Json& sets = manifest["sets"];
    if (!sets.isArray() || sets.size() > kMaxSets) {
        error = path + " has no valid set list";
        return false;
    }
    SelectionSetList list;
    for (const Json& j : sets.items()) {
        const ZipEntry* e = entry(j["data"].asString());
        SelectionSet s;
        std::string why;
        if (!e || !unpackSelectionSet(e->bytes(), e->size(), s, why)) {
            error = path + ": set " + quote(j["name"].asString()) + " is unreadable" + (why.empty() ? "" : " (" + why + ")");
            return false;
        }
        if (list.find(s.name) >= 0) continue;
        list.sets.push_back(std::make_shared<const SelectionSet>(std::move(s)));
    }
    list.morphTarget = manifest["morph_target"].asString();
    if (list.find(list.morphTarget) < 0) list.morphTarget.clear();
    out = std::move(list);
    return true;
}

// --- Document operations ---

bool addSelectionSet(Document& doc, SelectionSet set, const std::string& label, std::string& message) {
    if (!validName(set.name, message)) return false;
    auto list = editableList(doc.mesh);
    const std::string name = set.name;
    const uint32_t channels = set.channels;
    const size_t bytes = set.bytes();
    auto shared = std::make_shared<const SelectionSet>(std::move(set));
    const int at = list->find(name);
    if (at >= 0) list->sets[size_t(at)] = std::move(shared);
    else list->sets.push_back(std::move(shared));
    if (list->morphTarget == name && !(channels & kSetMorph)) list->morphTarget.clear();
    commitList(doc, std::move(list), label);
    std::ostringstream s;
    s << (at >= 0 ? "Replaced" : "Stored") << " selection set " << quote(name) << " (" << setChannelNames(channels);
    if (subdivLevelCount(doc.mesh)) s << ", at " << subdivLevelText(doc.mesh);
    s << ", " << (bytes + 1023) / 1024 << " KB)";
    message = s.str();
    return true;
}

bool storeSelectionSet(Document& doc, const std::string& name, uint32_t channels, std::string& message) {
    if (!validName(name, message)) return false;
    if (!(channels & kSetAllChannels)) {
        message = "Choose at least one channel to store";
        return false;
    }
    SelectionSet s = captureSelectionSet(doc.mesh, name, channels);
    std::string note;
    if ((channels & kSetUvs) && !s.has(kSetUvs)) note = "; this Part has no UVs, so none were stored";
    if (!s.channels) {
        message = "This Part has no UVs to store";
        return false;
    }
    if (!addSelectionSet(doc, std::move(s), "Store set " + name, message)) return false;
    message += note;
    return true;
}

bool restoreSelectionSet(Document& doc, const std::string& name, uint32_t channels, SetOp op, std::string& message) {
    const SelectionSet* s = findSet(doc, name, message);
    if (!s) return false;
    const uint32_t wanted = channels ? channels : s->channels & ~uint32_t(kSetMorph);
    const uint32_t use = wanted & s->channels;
    if (!use) {
        message = "Selection set " + quote(name) + " holds " + setChannelNames(s->channels) + ", not " + setChannelNames(wanted);
        return false;
    }
    SelectionSetData d;
    if (!selectionSetAt(doc.mesh, *s, use, d, message)) return false;
    Mesh& m = doc.mesh;
    UndoRecord r;
    r.label = "Restore set " + name;
    if (use & kSetMask) {
        std::vector<float> next = d.mask;
        if (op != SetOp::Replace) {
            next = m.hasMask() ? m.mask : std::vector<float>(m.vertexCount(), 0.0f);
            combineMask(next, d.mask, op);
        }
        for (uint32_t v = 0; v < m.vertexCount(); ++v)
            if (m.maskAt(v) != next[v]) r.mask.add(v, m.maskAt(v));
        if (!r.mask.empty()) {
            std::vector<float>& mask = m.ensureMask();
            for (uint32_t v : r.mask.indices) {
                mask[v] = next[v];
                m.markDirty(v);
            }
        }
    }
    if (use & kSetVisibility) {
        std::vector<uint8_t> next = d.hidden;
        if (op != SetOp::Replace) {
            next.assign(m.triangleCount(), 0);
            for (uint32_t t = 0; t < m.triangleCount(); ++t) next[t] = m.triangleHidden(t);
            combineHidden(next, d.hidden, op);
        }
        for (uint32_t t = 0; t < m.triangleCount(); ++t)
            if (uint8_t(m.triangleHidden(t)) != next[t]) r.hidden.add(t, uint8_t(m.triangleHidden(t)));
        if (!r.hidden.empty()) {
            std::vector<uint8_t>& hidden = m.ensureHidden();
            for (uint32_t t : r.hidden.indices) hidden[t] = next[t];
            m.facesDirty = true;
        }
    }
    if (use & kSetPolygroups) {
        for (uint32_t t = 0; t < m.triangleCount(); ++t)
            if (m.polygroupAt(t) != d.polygroups[t]) r.polygroups.add(t, m.polygroupAt(t));
        if (!r.polygroups.empty()) {
            std::vector<uint32_t>& groups = m.ensurePolygroups();
            for (uint32_t t : r.polygroups.indices) groups[t] = d.polygroups[t];
            m.facesDirty = true;
        }
    }
    if (use & kSetPaint) {
        // Without paint layers into the vertex paint itself; with them into the Base layer (undo: paintLayer -1).
        const bool layered = !m.paintLayers.empty();
        r.paintLayer = -1;
        for (uint32_t v = 0; v < m.vertexCount(); ++v) {
            const glm::vec3 old = layered ? m.paintBase[v] : m.colorAt(v);
            if (old != d.colors[v]) r.colors.add(v, old);
        }
        if (!r.colors.empty()) {
            std::vector<glm::vec3>& target = layered ? m.paintBase : m.ensureColors();
            for (uint32_t v : r.colors.indices) target[v] = d.colors[v];
            compositePaint(m, r.colors.indices);
        }
    }
    if (use & kSetUvs) {
        const bool had = m.uvs.size() == m.triangleCount() * 3;
        for (uint32_t c = 0; c < d.uvs.size(); ++c) {
            const glm::vec2 old = had ? m.uvs[c] : glm::vec2(0.0f);
            if (!had || old != d.uvs[c]) r.uvs.add(c, old);
        }
        if (!r.uvs.empty()) {
            m.uvs = d.uvs;
            m.facesDirty = true;
            m.topologyDirty = true;
        }
    }
    if (use & kSetMorph) {
        std::string err;
        applyShape(doc, d.positions, 1.0f, r, err);
        if (!err.empty()) {
            message = err;
            r.positions = {};
        }
    }
    const bool changed = !r.empty();
    if (changed) doc.undo.push(std::move(r));
    message = (changed ? "Restored selection set " : "Selection set ") + quote(name) + (changed ? " (" : " already matches (") +
              setChannelNames(use) + (op != SetOp::Replace && (use & (kSetMask | kSetVisibility)) ? ", " + std::string(setOpName(op)) : "") +
              (subdivLevelCount(m) && s->level != subdivCurrentLevel(m) ? ", carried to " + subdivLevelText(m) : "") + ")";
    if ((wanted & ~s->channels) && channels) message += "; the set holds no " + setChannelNames(wanted & ~s->channels);
    return true;
}

bool renameSelectionSet(Document& doc, const std::string& name, const std::string& to, std::string& message) {
    if (!findSet(doc, name, message) || !validName(to, message)) return false;
    if (name == to) {
        message = "Selection set " + quote(name) + " already has that name";
        return true;
    }
    auto list = editableList(doc.mesh);
    if (list->find(to) >= 0) {
        message = "There is already a selection set " + quote(to);
        return false;
    }
    const int i = list->find(name);
    auto renamed = std::make_shared<SelectionSet>(*list->sets[size_t(i)]);
    renamed->name = to;
    list->sets[size_t(i)] = std::move(renamed);
    if (list->morphTarget == name) list->morphTarget = to;
    commitList(doc, std::move(list), "Rename set " + name);
    message = "Renamed selection set " + quote(name) + " to " + quote(to);
    return true;
}

bool deleteSelectionSet(Document& doc, const std::string& name, std::string& message) {
    if (!findSet(doc, name, message)) return false;
    auto list = editableList(doc.mesh);
    list->sets.erase(list->sets.begin() + list->find(name));
    if (list->morphTarget == name) list->morphTarget.clear();
    commitList(doc, std::move(list), "Delete set " + name);
    message = "Deleted selection set " + quote(name);
    return true;
}

bool combineSelectionSets(Document& doc, const std::string& a, const std::string& b, SetOp op, const std::string& into,
                          std::string& message) {
    const SelectionSet* sa = findSet(doc, a, message);
    const SelectionSet* sb = sa ? findSet(doc, b, message) : nullptr;
    if (!sb) return false;
    if (op == SetOp::Replace) {
        message = "Combine with union, intersect or subtract";
        return false;
    }
    const uint32_t channels = sa->channels & sb->channels & (kSetMask | kSetVisibility);
    if (!channels) {
        message = "Sets combine by their mask and visibility, and " + quote(a) + " and " + quote(b) + " don't share either";
        return false;
    }
    SelectionSetData da, db;
    if (!selectionSetAt(doc.mesh, *sa, channels, da, message) || !selectionSetAt(doc.mesh, *sb, channels, db, message)) return false;
    if (channels & kSetMask) combineMask(da.mask, db.mask, op);
    if (channels & kSetVisibility) combineHidden(da.hidden, db.hidden, op);
    const char* sign = op == SetOp::Union ? " + " : op == SetOp::Intersect ? " & " : " - ";
    const std::string name = into.empty() ? uniqueSetName(doc.mesh, a + sign + b) : into;
    SelectionSet out;
    if (!makeSelectionSet(name, da, uint32_t(doc.mesh.vertexCount()), uint32_t(doc.mesh.triangleCount()),
                          subdivCurrentLevel(doc.mesh), out, message))
        return false;
    return addSelectionSet(doc, std::move(out), std::string("Combine sets (") + setOpName(op) + ")", message);
}

bool morphToSelectionSet(Document& doc, const std::string& name, float amount, std::string& message) {
    const SelectionSet* s = findSet(doc, name, message);
    if (!s) return false;
    if (!s->has(kSetMorph)) {
        message = "Selection set " + quote(name) + " holds no shape (store it with the morph channel)";
        return false;
    }
    SelectionSetData d;
    if (!selectionSetAt(doc.mesh, *s, kSetMorph, d, message)) return false;
    UndoRecord r;
    r.label = "Morph to set " + name;
    std::string err;
    const size_t moved = applyShape(doc, d.positions, std::clamp(amount, -1.0f, 1.0f), r, err);
    if (!err.empty()) {
        message = err;
        return false;
    }
    if (!moved) {
        message = "The surface already matches selection set " + quote(name) + " (or it is all masked)";
        return true;
    }
    doc.undo.push(std::move(r));
    char buf[160];
    std::snprintf(buf, sizeof buf, "Morphed %zu vertices %.0f%% of the way to selection set ", moved, double(amount) * 100.0);
    message = buf + quote(name);
    return true;
}

bool setMorphTarget(Document& doc, const std::string& name, std::string& message) {
    if (name.empty()) {
        if (!doc.mesh.selectionSets || doc.mesh.selectionSets->morphTarget.empty()) {
            message = "No morph target to clear";
            return true;
        }
        auto list = editableList(doc.mesh);
        list->morphTarget.clear();
        commitList(doc, std::move(list), "Clear morph target");
        message = "The Morph brush has no target now";
        return true;
    }
    const SelectionSet* s = findSet(doc, name, message);
    if (!s) return false;
    if (!s->has(kSetMorph)) {
        message = "Selection set " + quote(name) + " holds no shape (store it with the morph channel)";
        return false;
    }
    if (!selectionSetFits(doc.mesh, *s)) {
        SelectionSetData d;
        selectionSetAt(doc.mesh, *s, kSetMorph, d, message);
        return false;
    }
    if (doc.mesh.selectionSets->morphTarget == name) {
        message = "Selection set " + quote(name) + " is already the morph target";
        return true;
    }
    auto list = editableList(doc.mesh);
    list->morphTarget = name;
    commitList(doc, std::move(list), "Morph target " + name);
    message = "The Morph brush now pulls toward selection set " + quote(name);
    return true;
}

// --- The Morph brush's target (Sculptor, core/Sculpt.h) ---

const std::vector<glm::vec3>* Sculptor::morphTarget(const Mesh& mesh) {
    const SelectionSetList* list = mesh.selectionSets.get();
    const int i = list && !list->morphTarget.empty() ? list->find(list->morphTarget) : -1;
    if (i < 0 || !list->sets[size_t(i)]->has(kSetMorph)) {
        morphSet_.reset();
        morphMapped_.clear();
        return nullptr;
    }
    const std::shared_ptr<const SelectionSet>& set = list->sets[size_t(i)];
    const int level = subdivCurrentLevel(mesh);
    if (set != morphSet_ || level != morphLevel_ || mesh.vertexCount() != morphVertices_) {
        morphSet_ = set;
        morphLevel_ = level;
        morphVertices_ = mesh.vertexCount();
        morphMapped_.clear();
        morphFits_ = false;
        if (set->vertexCount == mesh.vertexCount() && set->triangleCount == mesh.triangleCount()) {
            morphFits_ = true; // stored at this level: used as it is
        } else {
            SelectionSetData d;
            std::string error;
            if (selectionSetAt(mesh, *set, kSetMorph, d, error)) {
                morphMapped_ = std::move(d.positions);
                morphFits_ = true;
            }
        }
    }
    if (!morphFits_) return nullptr;
    return morphMapped_.empty() ? &set->positions : &morphMapped_;
}

// --- Commands ---

namespace {

bool channelsArg(const Args& a, const char* key, uint32_t fallback, uint32_t& out, std::string& error) {
    const std::string text = argString(a, key);
    if (text.empty()) {
        out = fallback;
        return true;
    }
    return parseSetChannels(text, out, error);
}

CommandResult result(bool ok, std::string message) { return {ok, std::move(message)}; }

} // namespace

void registerSelectionSetCommands(CommandRegistry& reg) {
    const std::string channelHelp = "Comma-separated channels: mask, polygroups, paint (vertex paint), visibility (hidden "
                                    "faces), uvs, morph (the shape), or all";
    reg.add({"selection_set_store",
             "Store a named selection set on the active Part: a snapshot of the channels you choose. Storing again with "
             "the same name replaces it. Sets restore at any subdivision level of the Part and are saved in projects.",
             {{"name", ParamSpec::Type::String, "Set name", true},
              {"channels", ParamSpec::Type::String, channelHelp + " (default mask,visibility)"}},
             [](Document& doc, const Args& a) {
                 std::string msg;
                 uint32_t channels = 0;
                 if (!channelsArg(a, "channels", kSetMask | kSetVisibility, channels, msg)) return result(false, msg);
                 const bool ok = storeSelectionSet(doc, argString(a, "name"), channels, msg);
                 return result(ok, msg);
             }});
    reg.add({"selection_set_restore",
             "Restore a selection set's channels on the active Part (one undo step), carried to the current subdivision "
             "level. mode combines the mask and visibility with the current ones.",
             {{"name", ParamSpec::Type::String, "Set name", true},
              {"channels", ParamSpec::Type::String,
               channelHelp + " (default: everything the set holds but the shape; include morph to restore the shape)"},
              {"mode", ParamSpec::Type::String, "replace (default), union, intersect or subtract (mask and visibility)"}},
             [](Document& doc, const Args& a) {
                 std::string msg;
                 uint32_t channels = 0;
                 SetOp op = SetOp::Replace;
                 if (!channelsArg(a, "channels", 0, channels, msg)) return result(false, msg);
                 if (!parseSetOp(argString(a, "mode"), op)) return result(false, "mode is replace, union, intersect or subtract");
                 const bool ok = restoreSelectionSet(doc, argString(a, "name"), channels, op, msg);
                 return result(ok, msg);
             }});
    reg.add({"selection_set_rename", "Rename a selection set of the active Part.",
             {{"name", ParamSpec::Type::String, "Set name", true}, {"to", ParamSpec::Type::String, "New name", true}},
             [](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok = renameSelectionSet(doc, argString(a, "name"), argString(a, "to"), msg);
                 return result(ok, msg);
             }});
    reg.add({"selection_set_delete", "Delete a selection set of the active Part (undoable).",
             {{"name", ParamSpec::Type::String, "Set name", true}},
             [](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok = deleteSelectionSet(doc, argString(a, "name"), msg);
                 return result(ok, msg);
             }});
    reg.add({"selection_set_list", "List the active Part's selection sets with their channels and levels.",
             {{"json", ParamSpec::Type::Bool, "Answer as JSON (for scripts)"}},
             [](Document& doc, const Args& a) {
                 const SelectionSetList* list = doc.mesh.selectionSets.get();
                 if (argBool(a, "json", false)) {
                     Json j = Json::object();
                     Json sets = Json::array();
                     if (list)
                         for (const auto& s : list->sets) {
                             Json one = selectionSetSummary(*s);
                             one.set("fits", selectionSetFits(doc.mesh, *s));
                             sets.push(std::move(one));
                         }
                     j.set("sets", std::move(sets));
                     j.set("morph_target", list ? list->morphTarget : std::string());
                     return result(true, j.dump());
                 }
                 if (!list || list->sets.empty()) return result(true, "No selection sets on this Part");
                 std::ostringstream s;
                 s << list->sets.size() << " selection set" << (list->sets.size() == 1 ? "" : "s") << ":";
                 for (const auto& set : list->sets) {
                     s << "\n  " << set->name << " (" << setChannelNames(set->channels);
                     if (subdivLevelCount(doc.mesh)) s << ", SDiv " << set->level + 1;
                     if (!selectionSetFits(doc.mesh, *set)) s << ", doesn't fit this mesh";
                     if (set->name == list->morphTarget) s << ", morph target";
                     s << ")";
                 }
                 return result(true, s.str());
             }});
    reg.add({"selection_set_combine",
             "Make a new selection set from two others by their mask and visibility: union, intersect or subtract.",
             {{"a", ParamSpec::Type::String, "First set", true},
              {"b", ParamSpec::Type::String, "Second set", true},
              {"op", ParamSpec::Type::String, "union, intersect or subtract (a minus b)", true},
              {"into", ParamSpec::Type::String, "Name of the new set (default \"a + b\", \"a & b\" or \"a - b\")"}},
             [](Document& doc, const Args& a) {
                 std::string msg;
                 SetOp op = SetOp::Replace;
                 if (!parseSetOp(argString(a, "op"), op) || op == SetOp::Replace)
                     return result(false, "op is union, intersect or subtract");
                 const bool ok = combineSelectionSets(doc, argString(a, "a"), argString(a, "b"), op, argString(a, "into"), msg);
                 return result(ok, msg);
             }});
    reg.add({"selection_set_from_visible",
             "Quick set: store the faces shown right now as a visibility set (named Visible, Visible 2, ... by default).",
             {{"name", ParamSpec::Type::String, "Set name (optional)"}},
             [](Document& doc, const Args& a) {
                 std::string name = argString(a, "name");
                 if (name.empty()) name = uniqueSetName(doc.mesh, "Visible");
                 std::string msg;
                 const bool ok = storeSelectionSet(doc, name, kSetVisibility, msg);
                 return result(ok, msg);
             }});
    reg.add({"selection_set_morph_target",
             "Make a selection set's shape the Morph brush's target (empty name: none). The set must hold the morph "
             "channel; it works at any subdivision level of the Part.",
             {{"name", ParamSpec::Type::String, "Set name, or empty to clear"}},
             [](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok = setMorphTarget(doc, argString(a, "name"), msg);
                 return result(ok, msg);
             }});
    reg.add({"selection_set_morph",
             "Morph to Set: move the whole surface toward a selection set's shape by amount (1 = all the way, negative "
             "pushes away), within the mask. One undo step.",
             {{"name", ParamSpec::Type::String, "Set name", true},
              {"amount", ParamSpec::Type::Number, "-1..1 (default 1)"}},
             [](Document& doc, const Args& a) {
                 std::string msg;
                 const bool ok = morphToSelectionSet(doc, argString(a, "name"), float(argNumber(a, "amount", 1.0)), msg);
                 return result(ok, msg);
             }});
    reg.add({"selection_sets_export",
             "Write the active Part's selection sets (or one) to a .sfsets file, for pipelines and other Parts or files.",
             {{"path", ParamSpec::Type::String, "Output .sfsets file", true},
              {"name", ParamSpec::Type::String, "Only this set (default: all)"}},
             [](Document& doc, const Args& a) {
                 const SelectionSetList* list = doc.mesh.selectionSets.get();
                 if (!list || list->sets.empty()) return result(false, "This Part has no selection sets to export");
                 SelectionSetList out = *list;
                 if (const std::string name = argString(a, "name"); !name.empty()) {
                     const int i = list->find(name);
                     if (i < 0) return result(false, "No selection set " + quote(name));
                     out.sets = {list->sets[size_t(i)]};
                     if (out.morphTarget != name) out.morphTarget.clear();
                 }
                 const std::string path = argString(a, "path");
                 std::string err;
                 if (!saveSelectionSets(path, out, err)) return result(false, err);
                 return result(true, "Exported " + std::to_string(out.sets.size()) + " selection set" +
                                         (out.sets.size() == 1 ? "" : "s") + " to " + path);
             }});
    reg.add({"selection_sets_import",
             "Read the selection sets of a .sfsets file into the active Part (one undo step). Sets with a name the Part "
             "has already replace it.",
             {{"path", ParamSpec::Type::String, ".sfsets file", true}},
             [](Document& doc, const Args& a) {
                 SelectionSetList in;
                 std::string err;
                 if (!loadSelectionSets(argString(a, "path"), in, err)) return result(false, err);
                 if (in.sets.empty()) return result(false, "The file holds no selection sets");
                 auto list = editableList(doc.mesh);
                 size_t fits = 0;
                 for (const auto& s : in.sets) {
                     fits += selectionSetFits(doc.mesh, *s);
                     const int at = list->find(s->name);
                     if (at >= 0) list->sets[size_t(at)] = s;
                     else list->sets.push_back(s);
                 }
                 if (list->morphTarget.empty()) list->morphTarget = in.morphTarget;
                 commitList(doc, std::move(list), "Import selection sets");
                 std::string msg = "Imported " + std::to_string(in.sets.size()) + " selection set" + (in.sets.size() == 1 ? "" : "s");
                 if (fits < in.sets.size()) msg += " (" + std::to_string(in.sets.size() - fits) + " don't fit this Part's mesh)";
                 return result(true, msg);
             }});
}

} // namespace sf
