// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cloth: an in-house cloth solver for the Cloth brush, Drape and Garments (docs/design/cloth.md).
//
// Extended position-based dynamics (XPBD: Macklin, Mueller, Chentanez, "XPBD: Position-Based Simulation of
// Compliant Constrained Dynamics", 2016) in the "small steps" form (Macklin et al. 2019): many substeps per frame,
// one constraint pass each, so every Lagrange multiplier starts at zero and a constraint's correction is
//     dlambda = -C / (sum_i w_i |grad_i C|^2 + alpha / h^2),   dx_i = w_i grad_i C dlambda
// (alpha = compliance, the inverse stiffness; h = substep). Corrections are gathered per particle (each particle
// sums what its own constraints ask of it, Jacobi style, relaxed) so the pass runs in parallel with no atomics and
// gives the same result on any number of threads.
//
// Constraints: stretch (each edge keeps its rest length; compression is softer than stretching, so cloth buckles
// into folds instead of resisting like rubber), bend (the distance across each pair of triangles sharing an edge,
// the far corners, Provot-style: cheap, and soft), seams (two particles pulled to rest length 0), pins (a weight
// toward a fixed point). Contact: particles are pushed out of collider surfaces (other Parts) to a thickness, with
// friction, through a triangle BVH. Self-collision (optional): particles keep a distance apart, pairs found once a
// frame in a spatial hash, pushed apart after each substep's constraints (gathered per particle, deterministic).
#pragma once

#include <glm/glm.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sf {

class Mesh;

// A fabric: how it stretches, bends, weighs and slides. Compliances are XPBD alphas (m/N), mass per area in kg/m^2.
struct Fabric {
    const char* id = "cotton";       // stored name ("silk", "cotton", "denim", "leather")
    float stretchCompliance = 1e-8f; // pulling an edge longer
    float compressCompliance = 1e-5f; // pushing it shorter (much softer: cloth buckles)
    float bendCompliance = 1e-3f;    // across two triangles (higher = drapes more)
    float density = 0.15f;           // kg/m^2
    float friction = 0.4f;           // against colliders, 0..1
    float damping = 0.02f;           // velocity lost per frame, 0..1
    float thickness = 0.002f;        // m: how far it keeps off colliders
};

enum class FabricPreset { Silk, Cotton, Denim, Leather, Count };
const Fabric& fabricPreset(FabricPreset preset);
// The preset with this id or label (any case); false when none.
bool fabricFromName(std::string_view name, FabricPreset& out);
const char* fabricLabel(FabricPreset preset); // "Silk", ...

// Two particles sewn together (Garments): pulled to rest length 0 by the solver.
struct ClothSeam {
    uint32_t a = 0, b = 0;
};

// What a simulation starts from. Garments fills it from its placed panels; Drape from a Part.
struct ClothSetup {
    std::vector<glm::vec3> positions;  // start positions, world (metres)
    std::vector<glm::uvec3> triangles;
    // Rest shape for stretch and bend: empty = `positions`. Garments passes the flat panels here (in 3D, any
    // placement), so rest lengths are the 2D pattern's and rest bending is flat.
    std::vector<glm::vec3> rest;
    // Empty, or one per particle, 0..1: 1 = held at its start position, between = pulled there softly.
    std::vector<float> pin;
    std::vector<ClothSeam> seams;
    float seamCompliance = 0.0f;       // 0 = rigid once closed
    // Empty, or one per particle: the side it was put on from (Garments: the panel's facing, out of the body). A
    // particle that starts inside a collider leaves it that way on the first frame, not toward the nearest surface
    // (which, for a sleeve's hem inside a bent elbow, is half the time the far side of the arm).
    std::vector<glm::vec3> outward;
};

// Surfaces the cloth collides with (other Parts). Holds its own copy of the triangles (built once per Drape).
class ClothColliders {
public:
    ClothColliders();
    ~ClothColliders();
    ClothColliders(ClothColliders&&) noexcept;
    ClothColliders& operator=(ClothColliders&&) noexcept;
    void add(const Mesh& mesh, const glm::mat4& toHere = glm::mat4(1.0f)); // (toHere: its positions into this space)
    void add(std::span<const glm::vec3> positions, std::span<const glm::uvec3> triangles);
    void build() const; // after the last add (drapeToRest calls it; queries before it find nothing)
    bool empty() const;
    size_t triangles() const;
    // The nearest surface point within `maxDistance` of `p` and the surface's outward normal there (false: none);
    // `triangle`: in, the one to keep (~0u: none) unless another is nearer by more than `keep` (m); out, the one used.
    bool closest(const glm::vec3& p, float maxDistance, glm::vec3& point, glm::vec3& normal, uint32_t* triangle = nullptr,
                 float keep = 0.0f) const;
    // The first surface along origin + t * dir (dir unit length, t <= maxT): where, and that triangle's normal.
    bool raycast(const glm::vec3& origin, const glm::vec3& dir, float maxT, glm::vec3& point, glm::vec3& normal) const;
    // The vertices in the box [lo, hi] on sharp features: the surface turns round them tighter than `radius` (a
    // strand, a curl's ridge, a needle, a fingertip, a shelf's edge). In index order, for vertex() / vertexNormal().
    void sharpVertices(const glm::vec3& lo, const glm::vec3& hi, float radius, std::vector<uint32_t>& out) const;
    const glm::vec3& vertex(uint32_t i) const;
    const glm::vec3& vertexNormal(uint32_t i) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct ClothSettings {
    glm::vec3 gravity{0.0f, -9.81f, 0.0f}; // m/s^2
    glm::vec3 wind{0.0f};                  // m/s, air velocity (drag pulls the cloth toward it)
    float frameSeconds = 1.0f / 60.0f;
    int substeps = 12;
    int iterations = 1; // constraint passes per substep (the brush uses more: its soft pins pull every substep)
    // At rest: every particle's mean position over `restFrames` frames moved from the last such window's mean less than
    // half as far as this speed (m/s) would take it, and none went faster than ten times it over any two frames.
    float restSpeed = 0.01f;
    int restFrames = 10;
    // > 0: self-collision, particles keep at least this far apart (m; pairs closer than it at rest are skipped).
    float selfDistance = 0.0f;
    // Thin features (a beard strand, a finger, an edge sharper than the particle spacing) and fast particles:
    // collider vertices keep to their side of the cloth's triangles, and a particle's path through the frame is
    // checked against the surface (it never ends up past a face it crossed). Drape; the brush leaves it off.
    bool thinFeatures = true;
};

// One simulation: particles, constraints, contact. Deterministic: the same setup gives the same positions on
// every machine thread count (gathered corrections, fixed order).
class ClothSim {
public:
    // False (and empty) when the setup is unusable (no triangles, an index out of range, mismatched sizes).
    bool init(const ClothSetup& setup, const Fabric& fabric, const ClothSettings& settings = {});
    void setColliders(const ClothColliders* colliders) {
        colliders_ = colliders;
        thinLo_ = glm::vec3(0.0f), thinHi_ = glm::vec3(-1.0f); // (its sharp vertices are queried again)
        thinVerts_.clear(), thinGrid_.clear();
    }
    void setFabric(const Fabric& fabric);
    void setSettings(const ClothSettings& settings) { settings_ = settings; }
    // Changes a particle's pin (0..1) and its pinned target (default: where it is now).
    void pin(uint32_t i, float weight);
    void pinTo(uint32_t i, float weight, const glm::vec3& target);
    // Grows (> 1) or shrinks a particle's share of its edges' rest lengths: an edge rests at its start length times
    // the mean of its two ends' scales (the Cloth brush's Inflate adds fabric so the surface balloons and folds).
    void setRestScale(uint32_t i, float scale);
    float restScale(uint32_t i) const { return i < restScale_.size() ? restScale_[i] : 1.0f; }
    size_t selfPairs() const { return selfPairs_; } // last frame's self-collision pairs (tests, stats)

    // One frame (settings.substeps substeps). Returns true while moving, false once at rest.
    bool step();
    bool atRest() const { return rested_; }
    float maxSpeed() const { return maxSpeed_; }    // last frame's, m/s
    double kineticEnergy() const;                   // J
    // The worst relative edge stretch, |l - rest| / rest, over all edges (tests, the stats readout).
    float maxStrain() const;

    size_t particles() const { return x_.size(); }
    const std::vector<glm::vec3>& positions() const { return x_; }
    std::vector<glm::vec3>& positionsMutable() { return x_; } // (the Cloth brush moves its driven particles)
    const std::vector<glm::uvec3>& triangles() const { return tris_; }

    // Internals the brush and tests read.
    struct Edge {
        uint32_t a, b;
        float rest;
    };
    const std::vector<Edge>& stretchEdges() const { return stretch_; }

private:
    void buildConstraints(const ClothSetup& setup);
    void project(uint32_t id, float h2inv);
    void findContacts(float frame);
    void findSelfPairs(float frame);
    void separateSelf();
    void findThinPairs(float span); // (span: seconds until the next look)
    void separateThin();
    void sweepParticles();
    void leaveColliders(); // (the first frame: particles that start inside go out along outward_)
    void substep(float h);
    void holdSeamsOnContact(float h);
    // How far off a collider the cloth is held: the fabric's, and at least an eighth of the particle spacing (a
    // triangle between particles sags into a curved body: 1.9 mm at 15 mm spacing; a third (and a fifth to a seventh, unevenly) made a 20 mm bag over a ball
    // flutter and never rest, and a fifth cost the 2M drape bench 25% more frames).
    float contactThickness() const { return std::max(fabric_.thickness, meanEdge_ / 8.0f); }

    Fabric fabric_;
    ClothSettings settings_;
    const ClothColliders* colliders_ = nullptr;
    std::vector<glm::uvec3> tris_;
    std::vector<glm::vec3> x_, prev_, v_, target_, dx_;
    std::vector<float> invMass_, w_, pinW_; // w_: inverse mass, 0 while held (pin 1)
    std::vector<glm::vec3> rest_;            // rest positions (self-collision skips pairs close at rest)
    std::vector<float> restScale_;           // empty = all 1
    // Self-collision: per particle up to kSelfSlots neighbours within reach this frame (~0u = none).
    static constexpr uint32_t kSelfSlots = 8;
    std::vector<uint32_t> selfNbr_;
    size_t selfPairs_ = 0;
    std::vector<Edge> stretch_, bend_, seam_;
    // Every constraint (kind << 30 | index), grouped by colour: no two of a colour share a particle.
    std::vector<uint32_t> order_, colourOffset_;
    uint32_t serialBegin_ = 0;
    // Per particle, this frame's nearest collider point and normal (contact_ = 0: none in reach).
    std::vector<uint8_t> contact_;
    // contactTri_: its triangle, kept from frame to frame while it is slow unless another is clearly nearer
    // (kContactKeep).
    std::vector<glm::vec3> contactPoint_, contactNormal_;
    std::vector<uint32_t> contactTri_;
    std::vector<uint8_t> contact2_;                   // a second surface in a concave corner (an armpit, a crotch)
    std::vector<glm::vec3> contactPoint2_, contactNormal2_;
    std::vector<uint32_t> contactTri2_;
    std::vector<uint8_t> hadContact2_;                // last frame's contact2_ (looked for again every frame)
    std::vector<float> contactReach_; // (how far findContacts looked: a move shorter than that crossed nothing)
    // Thin features: a collider vertex near a cloth triangle and the side of it the vertex keeps (+1 / -1).
    struct ThinPair {
        uint32_t tri, vertex;
        float side;
    };
    std::vector<ThinPair> thin_, thinLast_; // this frame's, in triangle order; last frame's
    std::vector<uint32_t> thinLastAt_;      // per triangle + 1: its range in thinLast_
    // Per collider vertex, last frame's nearest pair: a new triangle next to it (sharing a particle) takes its side.
    std::unordered_map<uint32_t, ThinPair> thinNearest_;
    // The collider's sharp vertices in a box around the cloth (queried again once the cloth leaves it), gridded by
    // an edge length: (cell key, vertex), sorted.
    std::vector<uint32_t> thinVerts_;
    std::vector<std::pair<uint64_t, uint32_t>> thinGrid_;
    glm::vec3 thinLo_{0.0f}, thinHi_{-1.0f};
    std::vector<glm::vec3> start_; // positions at the frame's start (the swept-path check)
    std::vector<glm::vec3> outward_; // ClothSetup::outward (unit), for the first frame
    std::vector<glm::vec3> thinDx_;
    std::vector<float> thinCount_;
    float meanEdge_ = 0.01f;       // rest edge length, mean
    float seamCompliance_ = 0.0f, seamAlpha_ = 0.0f;
    int frame_ = 0;
    float maxSpeed_ = 0.0f;
    std::vector<glm::vec3> restSum_, restMean_; // each particle's sum of positions over the window, the last window's mean
    std::vector<glm::vec3> twoBack_;  // positions two frames ago (the window's peak is over two frames)
    int restFromFrame_ = 0;
    float restPeak_ = 0.0f;           // the fastest frame in the window, m/s
    bool rested_ = false;
};

// The Cloth brush's settings (Brush > Cloth; saved with the brush settings).
// Drag: the cloth follows the stroke and folds; Push: along the normal (Alt pulls); Settle: only gravity, the region
// sags and drapes; Grab: the cloth under the stroke's start follows the brush, held (Move-like); Expand / Pinch: along
// the surface away from / toward the brush centre (Alt swaps); Inflate: the fabric under the brush grows and
// balloons out along its normals (Alt shrinks and pulls in).
enum class ClothBrushMode { Drag, Push, Settle, Grab, Expand, Pinch, Inflate, Count };
struct ClothBrushSettings {
    ClothBrushMode mode = ClothBrushMode::Drag;
    FabricPreset fabric = FabricPreset::Cotton;
    float gravity = 0.0f;  // 0..1 (Settle uses at least 0.5)
    float reach = 2.5f;    // the simulated region, in brush radii around the stroke (1.5..4)
    int steps = 2;         // simulation frames per dab (1..8): more = softer, slower
    size_t vertexBudget = 30000; // the region's most vertices (a bigger one shrinks its reach)
    float softBody = 0.0f; // 0..1: 0 = the cloth stays where it is moved; higher springs back toward the stroke-start shape
    bool selfCollide = true; // folds don't pass through each other
};
const char* clothModeName(ClothBrushMode mode); // "drag", "push", "settle", "grab", "expand", "pinch", "inflate"
const char* clothModeLabel(ClothBrushMode mode); // "Drag", ...
bool clothModeFromName(std::string_view name, ClothBrushMode& out);

// Drape: steps a setup until it rests (or `maxFrames`, or `cancel`). `progress(frame, maxSpeed)` after each frame
// (any thread; return false to stop). The entry point Garments and the Drape panel call.
struct DrapeOptions {
    ClothSettings settings;
    int maxFrames = 600;
    std::function<bool(int, float)> progress;
};
struct DrapeResult {
    std::vector<glm::vec3> positions;
    int frames = 0;
    bool rested = false;
    double milliseconds = 0.0;
};
DrapeResult drapeToRest(const ClothSetup& setup, const ClothColliders& colliders, const Fabric& fabric,
                        const DrapeOptions& options = {}, const std::atomic<bool>* cancel = nullptr);

} // namespace sf
