// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The open cache (core/OpenCache.h): a big mesh's project carries its topology, BVH structure and micro edge
// neighbours; opening takes them only when they are this mesh's, intact and well-formed, and rebuilds otherwise.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Json.h"
#include "core/Micro.h"
#include "core/Primitives.h"
#include "core/OpenCache.h"
#include "core/Project.h"
#include "io/Zip.h"

#include <cstring>
#include <filesystem>
#include <map>
#include <string>

using namespace sf;

namespace {

struct SmallCacheThreshold { // (the test meshes are small)
    size_t before = openCacheMinTriangles;
    SmallCacheThreshold() { openCacheMinTriangles = 1000; }
    ~SmallCacheThreshold() { openCacheMinTriangles = before; }
};

Document cachedDoc() {
    Document doc = makeDoc(makeIcosphere(5)); // (plain: a subdivision level's adjacency is implicit, not saved)
    std::string err;
    CHECK(microDivide(doc.mesh, 2, err));
    return doc;
}

// The loaded mesh's topology, BVH and edge neighbours are those a rebuild makes.
void checkLikeRebuilt(Document& loaded, const Document& original) {
    Mesh fresh = loaded.mesh;
    fresh.rebuildTopology();
    bool same = true;
    for (uint32_t v = 0; v < loaded.mesh.vertexCount() && same; ++v) {
        const IndexRing a = loaded.mesh.neighbors(v), b = fresh.neighbors(v);
        const IndexRing c = loaded.mesh.vertexTriangles(v), d = fresh.vertexTriangles(v);
        same = a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()) && c.size() == d.size() &&
               std::equal(c.begin(), c.end(), d.begin());
    }
    CHECK(same);
    BVH built;
    built.build(loaded.mesh);
    int hits = 0;
    for (int k = 0; k < 64; ++k) {
        const float a = float(k) * 0.37f, b = float(k) * 0.11f;
        const glm::vec3 dir = glm::normalize(glm::vec3(std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b)));
        RayHit h1, h2;
        const bool r1 = loaded.bvh.raycast(loaded.mesh, dir * -5.0f, dir, h1), r2 = built.raycast(loaded.mesh, dir * -5.0f, dir, h2);
        CHECK(r1 == r2);
        if (r1 && r2) {
            CHECK(h1.t == h2.t); // (the same nearest hit; on a shared edge either triangle may hold it)
            ++hits;
        }
    }
    CHECK(hits > 32);
    CHECK(loaded.mesh.micro && original.mesh.micro);
    if (loaded.mesh.micro && original.mesh.micro) CHECK(loaded.mesh.micro->edgeNeighbor == original.mesh.micro->edgeNeighbor);
}

// Rewrites the saved project: `edit(manifest text, entries)`.
template <class F>
void rewrite(const std::string& path, F edit) {
    std::vector<ZipEntry> entries;
    std::string err;
    CHECK(readStoredZip(path, entries, err));
    std::string manifest(entries[0].data.begin(), entries[0].data.end());
    edit(manifest, entries);
    entries[0].data.assign(manifest.begin(), manifest.end());
    CHECK(writeStoredZip(path, entries, 64, err));
}

ZipEntry* entryNamed(std::vector<ZipEntry>& entries, const std::string& name) {
    for (ZipEntry& e : entries)
        if (e.name == name) return &e;
    return nullptr;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
}

std::string hex64(uint64_t v) {
    char buf[20];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

// Changes one value of a cache array, packs it again and puts its hash and size in the manifest (as a crafted
// file would): only the checks on the values can catch it.
constexpr uint32_t kCopyFirst = 0xFFFFFFFEu; // (craft: the array's first value)
void craft(std::string& manifest, std::vector<ZipEntry>& entries, const std::string& key, size_t index, uint32_t value) {
    Json m;
    std::string err;
    CHECK(Json::parse(manifest, m, err));
    Json parts = m["parts"], p0 = parts[0], oc = p0["open_cache"], d = oc[key];
    ZipEntry* e = entryNamed(entries, d["file"].asString());
    CHECK(e != nullptr);
    if (!e) return;
    std::vector<uint32_t> v;
    CHECK(openCacheUnpack(e->data.data(), e->data.size(), size_t(d["values"].asNumber()), v));
    if (index >= v.size()) return;
    if (value == kCopyFirst) value = v[0];
    v[index] = value;
    e->data = openCachePack(v);
    d.set("count", double(e->data.size()));
    d.set("hash", hex64(openCacheHash(v)));
    oc.set(key, d);
    p0.set("open_cache", oc);
    Json list = Json::array();
    list.push(p0);
    for (size_t i = 1; i < parts.size(); ++i) list.push(parts[i]);
    m.set("parts", list);
    manifest = m.dump();
}

} // namespace

TEST(open_cache_round_trip_uses_it) {
    SmallCacheThreshold small;
    Document doc = cachedDoc();
    const std::string path = tempPath("sf_open_cache.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document loaded;
    CHECK(loadProject(path, loaded, err));
    CHECK(lastOpenCacheUse.topology);
    CHECK(lastOpenCacheUse.bvh);
    CHECK(lastOpenCacheUse.microEdges);
    checkLikeRebuilt(loaded, doc);
    // Small meshes save none.
    openCacheMinTriangles = 1u << 30;
    CHECK(saveProject(path, doc, err));
    Document plain;
    CHECK(loadProject(path, plain, err));
    CHECK(!lastOpenCacheUse.topology && !lastOpenCacheUse.bvh && !lastOpenCacheUse.microEdges);
    std::filesystem::remove(path);
}

TEST(open_cache_of_another_mesh_is_ignored) {
    SmallCacheThreshold small;
    Document doc = cachedDoc();
    const std::string path = tempPath("sf_open_cache_stale.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    rewrite(path, [&](std::string& manifest, std::vector<ZipEntry>&) { replaceAll(manifest, hex64(openCacheKey(doc.mesh)), hex64(12345)); });
    Document loaded;
    CHECK(loadProject(path, loaded, err));
    CHECK(!lastOpenCacheUse.topology && !lastOpenCacheUse.bvh && !lastOpenCacheUse.microEdges);
    checkLikeRebuilt(loaded, doc);
    std::filesystem::remove(path);
}

TEST(open_cache_damaged_bytes_are_ignored) {
    SmallCacheThreshold small;
    Document doc = cachedDoc();
    const std::string path = tempPath("sf_open_cache_damaged.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    rewrite(path, [&](std::string&, std::vector<ZipEntry>& entries) { // (the hash in the manifest stays: a damaged file)
        ZipEntry* e = entryNamed(entries, "parts/0/cache/adj_list.pack");
        CHECK(e != nullptr);
        if (e) e->data[e->data.size() / 2] ^= 0x01; // (still decodes: only the hash catches it)
    });
    Document loaded;
    CHECK(loadProject(path, loaded, err));
    CHECK(!lastOpenCacheUse.topology);
    CHECK(lastOpenCacheUse.bvh); // (the other arrays are intact)
    checkLikeRebuilt(loaded, doc);
    std::filesystem::remove(path);
}

TEST(open_cache_crafted_tables_are_rejected) {
    SmallCacheThreshold small;
    Document doc = cachedDoc();
    const std::string path = tempPath("sf_open_cache_crafted.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    const uint32_t nt = uint32_t(doc.mesh.triangleCount());
    struct Case {
        const char* file;
        size_t index;
        uint32_t value;
    };
    for (const Case c : {Case{"vt_list", 5, nt + 7},          // out of range
                         Case{"vt_list", nt * 3 / 2, 0},      // in range, not that vertex's triangle
                         Case{"adj_offsets", 3, 0xFFFFFFF0u}, // offsets past the list
                         Case{"adj_list", 4, 0xFFFFFFFFu},    // a vertex that doesn't exist
                         Case{"bvh_tris", 1, kCopyFirst},     // a triangle twice
                         Case{"bvh_nodes", 0, 0},             // the root its own child
                         Case{"micro_edges", 2, 5u << 2}}) {  // an edge it doesn't share
        CHECK(saveProject(path, doc, err));
        rewrite(path, [&](std::string& manifest, std::vector<ZipEntry>& entries) { craft(manifest, entries, c.file, c.index, c.value); });
        Document loaded;
        CHECK(loadProject(path, loaded, err));
        const std::string f = c.file;
        if (f.find("bvh") != std::string::npos) CHECK(!lastOpenCacheUse.bvh);
        else if (f.find("micro") != std::string::npos) CHECK(!lastOpenCacheUse.microEdges);
        else CHECK(!lastOpenCacheUse.topology);
        checkLikeRebuilt(loaded, doc);
    }
    std::filesystem::remove(path);
}

TEST(open_cache_packing_round_trips_and_rejects_bad_bytes) {
    std::vector<uint32_t> v;
    for (uint32_t i = 0; i < 200000; ++i) v.push_back(i % 7 == 0 ? 0xFFFFFFFFu : i * 3 + (i & 5)); // (jumps both ways)
    const std::vector<uint8_t> packed = openCachePack(v);
    CHECK(packed.size() < v.size() * 3);
    std::vector<uint32_t> back;
    CHECK(openCacheUnpack(packed.data(), packed.size(), v.size(), back));
    CHECK(back == v);
    CHECK(!openCacheUnpack(packed.data(), packed.size() - 1, v.size(), back)); // truncated
    CHECK(!openCacheUnpack(packed.data(), packed.size(), v.size() - 1, back)); // another count
    CHECK(!openCacheUnpack(packed.data(), 7, v.size(), back));                 // no header
    std::vector<uint8_t> bad = packed;
    bad[bad.size() - 1] |= 0x80; // the last value runs past its block
    CHECK(!openCacheUnpack(bad.data(), bad.size(), v.size(), back));
    std::vector<uint8_t> empty = openCachePack({});
    CHECK(openCacheUnpack(empty.data(), empty.size(), 0, back) && back.empty());
}

TEST(open_cache_slot_order_is_the_renderers) {
    SmallCacheThreshold small;
    Document doc = cachedDoc();
    const size_t nt = doc.mesh.triangleCount();
    const std::string path = tempPath("sf_open_cache_order.sfproj");
    std::string err;
    // None made yet (no renderer): the canonical one.
    CHECK(saveProject(path, doc, err));
    Document first;
    CHECK(loadProject(path, first, err));
    CHECK(first.mesh.micro && first.mesh.micro->slotOrder && *first.mesh.micro->slotOrder == microSlotOrder(doc.mesh));
    // The renderer's, as it is (buildLayout takes it back exactly), even after the base moved.
    auto order = std::make_shared<std::vector<uint32_t>>(nt);
    for (uint32_t t = 0; t < nt; ++t) (*order)[t] = uint32_t(nt - 1 - t);
    doc.mesh.micro->slotOrder = order;
    doc.mesh.positions[0] += glm::vec3(0.01f);
    doc.mesh.openCacheSource.reset(); // (the save above made it the source: compute this time)
    CHECK(saveProject(path, doc, err));
    Document loaded;
    CHECK(loadProject(path, loaded, err));
    CHECK(loaded.mesh.micro && loaded.mesh.micro->slotOrder && *loaded.mesh.micro->slotOrder == *order);
    std::filesystem::remove(path);
}

namespace {
std::map<std::string, std::vector<uint8_t>> cacheEntries(const std::string& path) {
    std::vector<ZipEntry> entries;
    std::string err;
    std::map<std::string, std::vector<uint8_t>> out;
    if (readStoredZip(path, entries, err))
        for (ZipEntry& e : entries)
            if (e.name.find("/cache/") != std::string::npos) out[e.name] = e.data;
    return out;
}
} // namespace

TEST(open_cache_is_copied_on_save_until_the_topology_changes) {
    SmallCacheThreshold small;
    Document doc = cachedDoc();
    const std::string a = tempPath("sf_open_cache_a.sfproj"), b = tempPath("sf_open_cache_b.sfproj"),
                      c = tempPath("sf_open_cache_c.sfproj");
    std::string err;
    // An autosave of a mesh without a source writes none (it would be worked out now).
    CHECK(saveProject(a, doc, err, true));
    CHECK(cacheEntries(a).empty());
    CHECK(saveProject(a, doc, err));
    CHECK(!cacheEntries(a).empty());
    CHECK(doc.mesh.openCacheSource != nullptr);
    Document loaded;
    CHECK(loadProject(a, loaded, err));
    CHECK(loaded.mesh.openCacheSource != nullptr && loaded.mesh.openCacheSource->path == a);
    // Sculpting keeps the topology: the next save (an autosave too) copies the same bytes.
    loaded.mesh.positions[3] += glm::vec3(0.02f);
    CHECK(saveProject(b, loaded, err, true));
    CHECK(cacheEntries(b).size() == cacheEntries(a).size());
    bool same = true;
    const auto ea = cacheEntries(a), eb = cacheEntries(b);
    for (const auto& [name, bytes] : ea) same = same && eb.count(name) && eb.at(name) == bytes;
    CHECK(same);
    Document again;
    CHECK(loadProject(b, again, err));
    CHECK(lastOpenCacheUse.topology && lastOpenCacheUse.bvh && lastOpenCacheUse.microEdges);
    checkLikeRebuilt(again, loaded);
    // A topology change drops the source: an autosave then writes none, a save works it out again.
    again.mesh.rebuildTopology();
    CHECK(again.mesh.openCacheSource == nullptr);
    CHECK(saveProject(c, again, err, true));
    CHECK(cacheEntries(c).empty());
    // A source file changed since: not copied from (worked out instead).
    Document fromB;
    CHECK(loadProject(b, fromB, err));
    CHECK(fromB.mesh.openCacheSource != nullptr);
    rewrite(b, [](std::string&, std::vector<ZipEntry>&) {}); // (written again: another modification time)
    CHECK(saveProject(c, fromB, err, true));
    CHECK(cacheEntries(c).empty());
    for (const std::string& p : {a, b, c}) std::filesystem::remove(p);
}
