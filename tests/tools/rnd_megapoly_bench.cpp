// SculptForge R&D spike rnd/mega-poly (never merged): adaptive view-dependent patch tessellation + sparse
// displacement tiles, GL 3.3 core only (floor tier), one brush, scripted camera + strokes, timings and stills.
//
// Build: see tests/tools/rnd_megapoly_build.sh. Run: sf_rnd_megapoly_bench [--side N] [--target PX] [--size WxH]
//        [--budget QUADS] [--threads T] [--maxdraw L] [--maxdepth D] [--csv F] [--shots DIR] [--heat]
#include "render/GL.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace gl;
namespace {
constexpr GLenum K_TEXTURE_BUFFER = 0x8C2A, K_R16I = 0x8233, K_RGBA32I = 0x8D82, K_RGBA32UI = 0x8D70, K_RGBA32F = 0x8814,
                 K_R32I = 0x8235, K_R8UI = 0x8232, K_TIME_ELAPSED = 0x88BF, K_QUERY_RESULT = 0x8866, K_RGB = 0x1907,
                 K_UNSIGNED_BYTE = 0x1401, K_TRIANGLES = 0x0004, K_CULL_FACE = 0x0B44, K_BACK = 0x0405, K_LESS = 0x0201,
                 K_FRAGMENT_SHADER = 0x8B30, K_MAX_TEXTURE_BUFFER_SIZE = 0x8C2B, K_TEXTURE0 = 0x84C0, K_FRAMEBUFFER = 0x8D40,
                 K_RENDERBUFFER = 0x8D41, K_RGBA8 = 0x8058, K_DEPTH_COMPONENT24 = 0x81A6, K_COLOR_ATTACHMENT0 = 0x8CE0,
                 K_DEPTH_ATTACHMENT = 0x8D00, K_FRAMEBUFFER_COMPLETE = 0x8CD5, K_PACK_ALIGNMENT = 0x0D05;
constexpr GLbitfield K_COLOR_BIT = 0x4000, K_DEPTH_BIT = 0x0100;

using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

struct V3 {
    float x = 0, y = 0, z = 0;
    V3() = default;
    V3(float a, float b, float c) : x(a), y(b), z(c) {}
    V3 operator+(V3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(V3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    V3& operator+=(V3 o) { x += o.x, y += o.y, z += o.z; return *this; }
};
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(V3 a) { return std::sqrt(dot(a, a)); }
V3 norm(V3 a) { float l = len(a); return l > 0 ? a * (1.0f / l) : V3(0, 0, 1); }
struct M4 { float m[16] = {}; }; // column major
M4 mul(const M4& a, const M4& b) {
    M4 r;
    for (int c = 0; c < 4; ++c)
        for (int rw = 0; rw < 4; ++rw) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}
M4 perspective(float fovy, float aspect, float n, float f) {
    M4 r; float t = 1.0f / std::tan(fovy / 2);
    r.m[0] = t / aspect; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1; r.m[14] = 2 * f * n / (n - f);
    return r;
}
M4 lookAt(V3 eye, V3 at, V3 up) {
    V3 f = norm(at - eye), s = norm(cross(f, up)), u = cross(s, f);
    M4 r;
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z; r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[15] = 1;
    r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
    return r;
}

// ---- the procedural "model": a lumpy sphere, r(dir) ----
float radiusAt(V3 d) {
    return 1.0f + 0.06f * std::sin(4 * d.x + 1) * std::sin(5 * d.y) + 0.04f * std::cos(6 * d.z + 2) + 0.03f * std::sin(9 * d.x) * std::cos(7 * d.y);
}
V3 surfaceAt(V3 d) { d = norm(d); return d * radiusAt(d); }
float implicit(V3 p) { return len(p) - radiusAt(norm(p)); }
V3 normalAt(V3 p) {
    const float h = 1e-4f;
    return norm(V3(implicit(p + V3(h, 0, 0)) - implicit(p - V3(h, 0, 0)), implicit(p + V3(0, h, 0)) - implicit(p - V3(0, h, 0)),
                   implicit(p + V3(0, 0, h)) - implicit(p - V3(0, 0, h))));
}

template <class F> void parallelFor(size_t n, int threads, F f) {
    std::vector<std::thread> ts;
    size_t chunk = (n + threads - 1) / threads;
    for (int t = 0; t < threads; ++t) {
        size_t b = size_t(t) * chunk, e = std::min(n, b + chunk);
        if (b >= e) break;
        ts.emplace_back(f, b, e, t);
    }
    for (auto& t : ts) t.join();
}

GLuint compile(GLenum type, const char* src) {
    GLuint s = CreateShader(type);
    ShaderSource(s, 1, &src, nullptr);
    CompileShader(s);
    GLint ok = 0; GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; GetShaderInfoLog(s, sizeof log, nullptr, log); std::fprintf(stderr, "shader: %s\n", log); std::exit(2); }
    return s;
}

constexpr int kT = 16;            // tile: 16 x 16 cells, 17 x 17 samples
constexpr int kTS = (kT + 1) * (kT + 1);

const char* kVS = R"(#version 330 core
uniform int uN; uniform int uLevel; uniform int uMaxDepth; uniform int uWalkCap; uniform float uStep; uniform mat4 uViewProj;
uniform usamplerBuffer uCorners, uNeighbors, uLevels; uniform samplerBuffer uPos, uNrm;
uniform isamplerBuffer uRoot, uNodes, uTiles;
layout(location = 0) in uint aPatch;
out vec3 vWorld; flat out float vDepth; flat out float vLevel;
int snapK(int k, uint nb) {
    if (nb == 0xffffffffu) return k;
    int ln = int(texelFetch(uLevels, int(nb)).r);
    if (ln >= uLevel || ln > 200) return k;
    int s = uLevel - ln; return (k >> s) << s;
}
float sampleDisp(uint patch, vec2 uv, out float depth) {
    int node = texelFetch(uRoot, int(patch)).r; depth = 0.0;
    if (node < 0) return 0.0;
    float sum = 0.0, scale = 1.0;
    int cap = (uWalkCap >= 0) ? min(uMaxDepth, max(uLevel - 4 + uWalkCap, 0)) : uMaxDepth;
    for (int l = 0; l <= cap; ++l) {
        ivec4 a = texelFetch(uNodes, node * 2); ivec4 b = texelFetch(uNodes, node * 2 + 1);
        vec2 g = uv * 16.0; ivec2 g0 = min(ivec2(g), ivec2(15)); vec2 f = g - vec2(g0);
        int b0 = a.r * 289 + g0.y * 17 + g0.x;
        float d00 = float(texelFetch(uTiles, b0).r), d10 = float(texelFetch(uTiles, b0 + 1).r);
        float d01 = float(texelFetch(uTiles, b0 + 17).r), d11 = float(texelFetch(uTiles, b0 + 18).r);
        sum += mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y) * scale;
        ivec2 q = min(ivec2(uv * 2.0), ivec2(1));
        int ch = (q.x + 2 * q.y == 0) ? a.g : (q.x + 2 * q.y == 1) ? a.b : (q.x + 2 * q.y == 2) ? a.a : b.r;
        if (ch < 0) break;
        node = ch; uv = uv * 2.0 - vec2(q); depth += 1.0; scale *= 0.5;
    }
    return sum;
}
void main() {
    int i = gl_VertexID % (uN + 1), j = gl_VertexID / (uN + 1);
    uvec4 nb = texelFetch(uNeighbors, int(aPatch));
    if (j == 0) i = snapK(i, nb.x); if (i == uN) j = snapK(j, nb.y); if (j == uN) i = snapK(i, nb.z); if (i == 0) j = snapK(j, nb.w);
    vec2 uv = vec2(i, j) / float(uN);
    uvec4 c = texelFetch(uCorners, int(aPatch));
    vec3 p0 = texelFetch(uPos, int(c.x)).xyz, p1 = texelFetch(uPos, int(c.y)).xyz, p2 = texelFetch(uPos, int(c.z)).xyz, p3 = texelFetch(uPos, int(c.w)).xyz;
    vec3 n0 = texelFetch(uNrm, int(c.x)).xyz, n1 = texelFetch(uNrm, int(c.y)).xyz, n2 = texelFetch(uNrm, int(c.z)).xyz, n3 = texelFetch(uNrm, int(c.w)).xyz;
    float w0 = (1.0 - uv.x) * (1.0 - uv.y), w1 = uv.x * (1.0 - uv.y), w2 = uv.x * uv.y, w3 = (1.0 - uv.x) * uv.y;
    vec3 p = p0 * w0 + p1 * w1 + p2 * w2 + p3 * w3;
    vec3 q = (p - dot(p - p0, n0) * n0) * w0 + (p - dot(p - p1, n1) * n1) * w1 + (p - dot(p - p2, n2) * n2) * w2 + (p - dot(p - p3, n3) * n3) * w3;
    vec3 S = mix(p, q, 0.75);
    vec3 N = normalize(n0 * w0 + n1 * w1 + n2 * w2 + n3 * w3);
    float depth; float d = sampleDisp(aPatch, uv, depth);
    vWorld = S + N * (d * uStep); vDepth = depth; vLevel = float(uLevel);
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
)";
const char* kFS = R"(#version 330 core
in vec3 vWorld; flat in float vDepth; flat in float vLevel; uniform vec3 uEye; uniform int uHeat; out vec4 oColor;
vec3 heat(float t) { return clamp(vec3(t * 3.0 - 1.0, 1.0 - abs(t * 3.0 - 1.5), 2.0 - t * 3.0), 0.0, 1.0); }
void main() {
    vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    vec3 v = normalize(uEye - vWorld);
    float diff = max(dot(n, v), 0.0);
    vec3 base = vec3(0.62, 0.60, 0.58);
    if (uHeat == 1) base = heat(vLevel / 8.0); else if (uHeat == 2) base = heat(vDepth / 8.0);
    vec3 col = base * (0.18 + 0.82 * diff) + vec3(0.25) * pow(max(dot(reflect(-v, n), v), 0.0), 24.0);
    oColor = vec4(pow(col, vec3(1.0 / 2.2)), 1.0);
}
)";

struct Node { int32_t tile = -1; int32_t child[4] = {-1, -1, -1, -1}; };

struct Harness {
    // options
    int side = 418, W = 1600, H = 960, threads = 4, maxDraw = 8, maxDepth = 10, heat = 0;
    float targetPx = 12.0f; size_t budgetQuads = 1'000'000; std::string csv, shots; int walkCap = -1;
    // base
    std::vector<V3> pos, nrm; std::vector<uint32_t> corners, neighbors; size_t patches = 0;
    std::vector<uint32_t> vpOff, vp; // vertex -> patches (CSR)
    std::vector<uint32_t> dirtyNrm; std::vector<uint32_t> stamp; uint32_t stampNo = 0;
    std::vector<float> pcx, pcy, pcz, prad, pnx, pny, pnz, pcone, pedge, prange;
    // tiles
    std::vector<Node> nodes; std::vector<int32_t> root; std::vector<int16_t> tiles; size_t tileCount = 0, tileCap = 0;
    float step = 0.0f;
    std::vector<uint32_t> dirtyTiles, dirtyNodes, dirtyRoots, dirtyVerts;
    // GPU
    GLuint prog = 0, bPos = 0, tPos = 0, bNrm = 0, tNrm = 0, bCor = 0, tCor = 0, bNb = 0, tNb = 0, bRoot = 0, tRoot = 0, bNodes = 0,
           tNodes = 0, bTiles = 0, tTiles = 0, bLev = 0, tLev = 0, bInst = 0, fbo = 0, rbC = 0, rbD = 0;
    std::vector<GLuint> vaos, ibos; std::vector<GLsizei> iboCount; size_t gpuStatic = 0;
    // per frame
    std::vector<uint8_t> levels; std::vector<uint32_t> inst; std::vector<size_t> levelOff, levelCnt; std::vector<int8_t> rawLevel;
    // timer
    void (*GenQueries)(GLsizei, GLuint*) = nullptr; void (*BeginQuery)(GLenum, GLuint) = nullptr; void (*EndQuery)(GLenum) = nullptr;
    void (*GetQueryObjectui64v)(GLuint, GLenum, uint64_t*) = nullptr; GLuint query = 0;
    FILE* csvf = nullptr;

    // ---------------- base mesh ----------------
    void buildBase() {
        const int N = side; const int64_t K = 2 * N + 1;
        std::vector<int64_t> keyOf; std::vector<uint32_t> faceVert(size_t(6) * (N + 1) * (N + 1));
        // unique vertices by integer cube coordinate
        std::vector<std::pair<int64_t, uint32_t>> keys; keys.reserve(faceVert.size());
        for (int f = 0; f < 6; ++f) {
            int axis = f / 2, sign = (f % 2) ? -1 : 1;
            for (int t = 0; t <= N; ++t)
                for (int s = 0; s <= N; ++s) {
                    int c[3]; c[axis] = sign * N; c[(axis + 1) % 3] = -N + 2 * s; c[(axis + 2) % 3] = -N + 2 * t;
                    int64_t key = (c[0] + N) + (c[1] + N) * K + (c[2] + N) * K * K;
                    keys.push_back({key, uint32_t(keys.size())});
                }
        }
        std::vector<std::pair<int64_t, uint32_t>> sorted = keys;
        std::sort(sorted.begin(), sorted.end());
        std::vector<uint32_t> remap(keys.size());
        for (size_t i = 0; i < sorted.size(); ++i) {
            if (i == 0 || sorted[i].first != sorted[i - 1].first) {
                int64_t key = sorted[i].first; int cx = int(key % K) - N, cy = int((key / K) % K) - N, cz = int(key / (K * K)) - N;
                V3 d = norm(V3(float(cx) / N, float(cy) / N, float(cz) / N));
                V3 p = surfaceAt(d);
                pos.push_back(p); nrm.push_back(normalAt(p));
            }
            remap[sorted[i].second] = uint32_t(pos.size() - 1);
        }
        // patches
        patches = size_t(6) * N * N; corners.resize(patches * 4);
        size_t pi = 0;
        for (int f = 0; f < 6; ++f) {
            auto vid = [&](int s, int t) { return remap[size_t(f) * (N + 1) * (N + 1) + size_t(t) * (N + 1) + s]; };
            // orientation: outward
            bool flip = false;
            { V3 a = pos[vid(0, 0)], b = pos[vid(1, 0)], c = pos[vid(0, 1)]; flip = dot(cross(b - a, c - a), a) < 0; }
            for (int t = 0; t < N; ++t)
                for (int s = 0; s < N; ++s, ++pi) {
                    uint32_t v0 = vid(s, t), v1 = vid(s + 1, t), v2 = vid(s + 1, t + 1), v3 = vid(s, t + 1);
                    if (flip) std::swap(v1, v3);
                    corners[pi * 4] = v0; corners[pi * 4 + 1] = v1; corners[pi * 4 + 2] = v2; corners[pi * 4 + 3] = v3;
                }
        }
        // neighbours by edge
        std::vector<std::pair<uint64_t, uint32_t>> edges(patches * 4);
        for (size_t p = 0; p < patches; ++p)
            for (int e = 0; e < 4; ++e) {
                uint32_t a = corners[p * 4 + e], b = corners[p * 4 + (e + 1) % 4];
                edges[p * 4 + e] = {uint64_t(std::min(a, b)) << 32 | std::max(a, b), uint32_t(p << 2 | e)};
            }
        std::sort(edges.begin(), edges.end());
        neighbors.assign(patches * 4, 0xffffffffu);
        for (size_t i = 0; i + 1 < edges.size(); ++i)
            if (edges[i].first == edges[i + 1].first) {
                uint32_t x = edges[i].second, y = edges[i + 1].second;
                neighbors[(x >> 2) * 4 + (x & 3)] = y >> 2; neighbors[(y >> 2) * 4 + (y & 3)] = x >> 2;
            }
        vpOff.assign(pos.size() + 1, 0);
        for (uint32_t c : corners) vpOff[c + 1]++;
        for (size_t i = 0; i < pos.size(); ++i) vpOff[i + 1] += vpOff[i];
        vp.resize(corners.size()); { std::vector<uint32_t> fill(vpOff.begin(), vpOff.end() - 1); for (size_t p = 0; p < patches; ++p) for (int k = 0; k < 4; ++k) vp[fill[corners[p * 4 + k]]++] = uint32_t(p); }
        stamp.assign(patches, 0);
        parallelFor(pos.size(), threads, [&](size_t b, size_t e, int) { for (size_t v = b; v < e; ++v) nrm[v] = fanNormal(v); });
        pcx.resize(patches); pcy.resize(patches); pcz.resize(patches); prad.resize(patches); pnx.resize(patches); pny.resize(patches);
        pnz.resize(patches); pcone.resize(patches); pedge.resize(patches); prange.assign(patches, 0.0f);
        parallelFor(patches, threads, [&](size_t b, size_t e, int) { for (size_t p = b; p < e; ++p) patchBounds(p); });
        root.assign(patches, -1); levels.assign(patches, 255); rawLevel.assign(patches, -1);
        float edge = 0; for (size_t p = 0; p < patches; ++p) edge += pedge[p]; edge /= float(patches);
        step = 0.3f / 32767.0f; // +-0.3 world units of displacement in 16 bits (9e-6 per unit)
        std::printf("base: %zu patches, %zu vertices, mean edge %.3e, step %.3e\n", patches, pos.size(), edge, step);
    }
    V3 fanNormal(size_t v) const { // the normalised sum of the adjacent patches' normals (cross of the diagonals)
        V3 n;
        for (uint32_t k = vpOff[v]; k < vpOff[v + 1]; ++k) { const uint32_t* c = &corners[size_t(vp[k]) * 4]; n += cross(pos[c[2]] - pos[c[0]], pos[c[3]] - pos[c[1]]); }
        return norm(n);
    }
    void patchBounds(size_t p) {
        V3 c[4], n[4]; for (int k = 0; k < 4; ++k) c[k] = pos[corners[p * 4 + k]], n[k] = nrm[corners[p * 4 + k]];
        V3 ctr = (c[0] + c[1] + c[2] + c[3]) * 0.25f; float r = 0; for (auto& q : c) r = std::max(r, len(q - ctr));
        V3 nn = norm(n[0] + n[1] + n[2] + n[3]); float cs = 1; for (auto& q : n) cs = std::min(cs, dot(nn, q));
        float e = 0; for (int k = 0; k < 4; ++k) e = std::max(e, len(c[k] - c[(k + 1) % 4]));
        pcx[p] = ctr.x, pcy[p] = ctr.y, pcz[p] = ctr.z, prad[p] = r * 1.05f + 0.02f * e, pnx[p] = nn.x, pny[p] = nn.y, pnz[p] = nn.z;
        pcone[p] = std::sqrt(std::max(0.0f, 1 - cs * cs)), pedge[p] = e;
    }
    // surface S(uv) and N(uv) of a patch, as the shader does
    void evalPatch(size_t p, float u, float v, V3& S, V3& N) const {
        const uint32_t* c = &corners[p * 4];
        float w[4] = {(1 - u) * (1 - v), u * (1 - v), u * v, (1 - u) * v};
        V3 pp, q, nn;
        for (int k = 0; k < 4; ++k) pp += pos[c[k]] * w[k];
        for (int k = 0; k < 4; ++k) { V3 pk = pos[c[k]], nk = nrm[c[k]]; q += (pp - nk * dot(pp - pk, nk)) * w[k]; nn += nk * w[k]; }
        S = pp + (q - pp) * 0.75f; N = norm(nn);
    }

    size_t surfacePoint(V3 dir, V3& T, V3& N) const {
        V3 a = surfaceAt(dir); size_t best = 0; float bd = 1e9f;
        for (size_t p = 0; p < patches; ++p) { float dx = pcx[p] - a.x, dy = pcy[p] - a.y, dz = pcz[p] - a.z, d = dx * dx + dy * dy + dz * dz; if (d < bd) bd = d, best = p; }
        pointOn(best, T, N); return best;
    }
    void pointOn(size_t p, V3& T, V3& N) const { evalPatch(p, 0.5f, 0.5f, T, N); T += N * dispAt(p, 0.5f, 0.5f); }
    // ---------------- GPU ----------------
    GLuint tbo(GLuint& buf, GLenum fmt, const void* data, size_t bytes, GLenum usage) {
        GLuint tex; GenBuffers(1, &buf); BindBuffer(K_TEXTURE_BUFFER, buf); BufferData(K_TEXTURE_BUFFER, GLsizeiptr(bytes), data, usage);
        GenTextures(1, &tex); BindTexture(K_TEXTURE_BUFFER, tex); TexBuffer(K_TEXTURE_BUFFER, fmt, buf); gpuStatic += bytes; return tex;
    }
    void initGL() {
        GLint maxTbo = 0; GetIntegerv(K_MAX_TEXTURE_BUFFER_SIZE, &maxTbo);
        prog = CreateProgram(); AttachShader(prog, compile(GL_VERTEX_SHADER, kVS)); AttachShader(prog, compile(K_FRAGMENT_SHADER, kFS)); LinkProgram(prog);
        GLint ok = 0; GetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) { char log[4096]; GetProgramInfoLog(prog, sizeof log, nullptr, log); std::fprintf(stderr, "link: %s\n", log); std::exit(2); }
        std::vector<float> p4(pos.size() * 4), n4(pos.size() * 4);
        for (size_t i = 0; i < pos.size(); ++i) { p4[i * 4] = pos[i].x, p4[i * 4 + 1] = pos[i].y, p4[i * 4 + 2] = pos[i].z; n4[i * 4] = nrm[i].x, n4[i * 4 + 1] = nrm[i].y, n4[i * 4 + 2] = nrm[i].z; }
        tPos = tbo(bPos, K_RGBA32F, p4.data(), p4.size() * 4, GL_DYNAMIC_DRAW);
        tNrm = tbo(bNrm, K_RGBA32F, n4.data(), n4.size() * 4, GL_DYNAMIC_DRAW);
        tCor = tbo(bCor, K_RGBA32UI, corners.data(), corners.size() * 4, GL_STATIC_DRAW);
        tNb = tbo(bNb, K_RGBA32UI, neighbors.data(), neighbors.size() * 4, GL_STATIC_DRAW);
        tRoot = tbo(bRoot, K_R32I, root.data(), root.size() * 4, GL_DYNAMIC_DRAW);
        tLev = tbo(bLev, K_R8UI, levels.data(), levels.size(), GL_DYNAMIC_DRAW);
        size_t nodeCap = 1 << 20; nodes.reserve(nodeCap);
        tNodes = tbo(bNodes, K_RGBA32I, nullptr, nodeCap * 32, GL_DYNAMIC_DRAW);
        tileCap = std::min<size_t>(200000, size_t(maxTbo) / kTS); tiles.reserve(tileCap * kTS);
        tTiles = tbo(bTiles, K_R16I, nullptr, tileCap * kTS * 2, GL_DYNAMIC_DRAW);
        std::printf("gpu: max texture buffer %d texels, tile pool %zu tiles (%.0f MB), static buffers %.0f MB\n", maxTbo, tileCap,
                    tileCap * kTS * 2 / 1e6, (gpuStatic - tileCap * kTS * 2 - nodeCap * 32) / 1e6);
        // grids per level
        GenBuffers(1, &bInst); BindBuffer(GL_ARRAY_BUFFER, bInst); BufferData(GL_ARRAY_BUFFER, GLsizeiptr(patches * 4), nullptr, GL_DYNAMIC_DRAW);
        for (int L = 0; L <= maxDraw; ++L) {
            int n = 1 << L; std::vector<uint32_t> idx; idx.reserve(size_t(n) * n * 6);
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i) {
                    uint32_t a = j * (n + 1) + i, b = a + 1, c = a + n + 1, d = c + 1;
                    idx.insert(idx.end(), {a, b, d, a, d, c});
                }
            GLuint vao, ibo; GenVertexArrays(1, &vao); BindVertexArray(vao); GenBuffers(1, &ibo);
            BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
            BindBuffer(GL_ARRAY_BUFFER, bInst); EnableVertexAttribArray(0); VertexAttribIPointer(0, 1, GL_UNSIGNED_INT, 0, nullptr); VertexAttribDivisor(0, 1);
            vaos.push_back(vao); ibos.push_back(ibo); iboCount.push_back(GLsizei(idx.size()));
        }
        BindVertexArray(0);
        GenFramebuffers(1, &fbo); BindFramebuffer(K_FRAMEBUFFER, fbo);
        GenRenderbuffers(1, &rbC); BindRenderbuffer(K_RENDERBUFFER, rbC); RenderbufferStorage(K_RENDERBUFFER, K_RGBA8, W, H);
        FramebufferRenderbuffer(K_FRAMEBUFFER, K_COLOR_ATTACHMENT0, K_RENDERBUFFER, rbC);
        GenRenderbuffers(1, &rbD); BindRenderbuffer(K_RENDERBUFFER, rbD); RenderbufferStorage(K_RENDERBUFFER, K_DEPTH_COMPONENT24, W, H);
        FramebufferRenderbuffer(K_FRAMEBUFFER, K_DEPTH_ATTACHMENT, K_RENDERBUFFER, rbD);
        if (CheckFramebufferStatus(K_FRAMEBUFFER) != K_FRAMEBUFFER_COMPLETE) { std::fprintf(stderr, "fbo incomplete\n"); std::exit(2); }
        Enable(GL_DEPTH_TEST); DepthFunc(K_LESS); Enable(K_CULL_FACE); CullFace(K_BACK); Viewport(0, 0, W, H);
        if (GenQueries) GenQueries(1, &query);
        inst.resize(patches); levelOff.assign(maxDraw + 2, 0); levelCnt.assign(maxDraw + 1, 0);
    }

    // ---------------- sparse tiles ----------------
    // Tiles hold DELTAS from the sum of their ancestors (bilinear), at a step that halves per level: a new child is all
    // zeros (the surface doesn't change), coarse edits move the fine detail with them, and deep tiles keep precision.
    int32_t newNode() {
        if (tileCount >= tileCap) { std::fprintf(stderr, "tile pool full\n"); std::exit(3); }
        int32_t t = int32_t(tileCount++); tiles.resize(tileCount * kTS); std::memset(&tiles[size_t(t) * kTS], 0, kTS * 2);
        nodes.push_back(Node{}); nodes.back().tile = t; dirtyTiles.push_back(uint32_t(t)); dirtyNodes.push_back(uint32_t(nodes.size() - 1));
        return int32_t(nodes.size() - 1);
    }
    float stepAt(int depth) const { return step / float(1 << depth); }
    float bilinear(int32_t tile, float u, float v) const { // u, v in tile units [0, 1]
        const int16_t* t = &tiles[size_t(tile) * kTS]; float g = u * kT, gg = v * kT; int x = std::min(int(g), kT - 1), y = std::min(int(gg), kT - 1); float fx = g - x, fy = gg - y;
        float d00 = t[y * 17 + x], d10 = t[y * 17 + x + 1], d01 = t[(y + 1) * 17 + x], d11 = t[(y + 1) * 17 + x + 1];
        return (d00 * (1 - fx) + d10 * fx) * (1 - fy) + (d01 * (1 - fx) + d11 * fx) * fy;
    }
    struct Anc { int32_t tile; float u0, v0, sz; int depth; };
    // Total displacement (world units) at (u, v) of a patch: the sum down the quadtree, as the shader does.
    float dispAt(size_t p, float u, float v) const {
        int32_t node = root[p]; if (node < 0) return 0; float sum = 0; int depth = 0;
        for (;;) {
            sum += bilinear(nodes[node].tile, u, v) * stepAt(depth);
            int qx = std::min(int(u * 2), 1), qy = std::min(int(v * 2), 1); int32_t ch = nodes[node].child[qx + 2 * qy];
            if (ch < 0) return sum;
            node = ch; u = u * 2 - qx; v = v * 2 - qy; ++depth;
        }
    }
    // Does the brush sphere reach this sub-square of the patch? AABB of a 3 x 3 grid of displaced points, grown by a Phong
    // bulge slack and the node's own delta range (node < 0: a child not yet allocated, its deltas are 0).
    bool regionHit(size_t p, int32_t node, int depth, float u0, float v0, float sz, V3 C, float r) const {
        V3 S, N; V3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) {
                float u = u0 + sz * i / 2, v = v0 + sz * j / 2; evalPatch(p, u, v, S, N); S += N * dispAt(p, u, v);
                mn = V3(std::min(mn.x, S.x), std::min(mn.y, S.y), std::min(mn.z, S.z)); mx = V3(std::max(mx.x, S.x), std::max(mx.y, S.y), std::max(mx.z, S.z));
            }
        float m = 0.15f * sz * pedge[p];
        if (node >= 0) { const int16_t* t = &tiles[size_t(nodes[node].tile) * kTS]; int mxv = 0; for (int k = 0; k < kTS; ++k) mxv = std::max(mxv, std::abs(int(t[k]))); m += mxv * stepAt(depth); }
        float d2 = 0;
        for (int k = 0; k < 3; ++k) {
            float c = k == 0 ? C.x : k == 1 ? C.y : C.z, lo = (k == 0 ? mn.x : k == 1 ? mn.y : mn.z) - m, hi = (k == 0 ? mx.x : k == 1 ? mx.y : mx.z) + m;
            float dd = c < lo ? lo - c : c > hi ? c - hi : 0; d2 += dd * dd;
        }
        return d2 < r * r;
    }
    int existingDepth(size_t p, int32_t node, int depth, float u0, float v0, float sz, V3 C, float r) const {
        if (node < 0 || !regionHit(p, node, depth, u0, v0, sz, C, r)) return depth - 1;
        int best = depth; float h = sz / 2;
        for (int q = 0; q < 4; ++q)
            if (nodes[node].child[q] >= 0) best = std::max(best, existingDepth(p, nodes[node].child[q], depth + 1, u0 + (q & 1) * h, v0 + (q >> 1) * h, h, C, r));
        return best;
    }
    struct DabStats { double findMs = 0, allocMs = 0, kernelMs = 0, uploadMs = 0; size_t patches = 0, samples = 0, changed = 0, newTiles = 0, baseVerts = 0; int depth = -1; bool base = false; };
    void kernelTile(size_t p, int32_t node, const Anc* anc, int nAnc, float u0, float v0, float sz, V3 C, float r, float strength, DabStats& st) {
        int16_t* t = &tiles[size_t(nodes[node].tile) * kTS]; bool changed = false; float range = prange[p]; const float st_ = stepAt(nAnc);
        for (int j = 0; j <= kT; ++j)
            for (int i = 0; i <= kT; ++i) {
                float u = u0 + sz * i / kT, v = v0 + sz * j / kT; V3 S, N; evalPatch(p, u, v, S, N);
                float base = 0; for (int k = 0; k < nAnc; ++k) base += bilinear(anc[k].tile, (u - anc[k].u0) / anc[k].sz, (v - anc[k].v0) / anc[k].sz) * stepAt(anc[k].depth);
                float d = base + float(t[j * 17 + i]) * st_; V3 w = S + N * d; float dist = len(w - C); ++st.samples;
                if (dist >= r) continue;
                float x = dist / r, f = 1 - 3 * x * x + 2 * x * x * x; d += strength * r * f;
                int val = int(std::lround((d - base) / st_)); val = std::clamp(val, -32767, 32767); t[j * 17 + i] = int16_t(val); changed = true; ++st.changed;
                range = std::max(range, std::fabs(base + val * st_));
            }
        if (changed) { dirtyTiles.push_back(uint32_t(nodes[node].tile)); prange[p] = range; }
    }
    void visit(size_t p, int32_t node, int depth, int D, Anc* anc, float u0, float v0, float sz, V3 C, float r, float strength, DabStats& st) {
        if (!regionHit(p, node, depth, u0, v0, sz, C, r)) return;
        if (depth == D) { kernelTile(p, node, anc, depth, u0, v0, sz, C, r, strength, st); return; }
        anc[depth] = Anc{nodes[node].tile, u0, v0, sz, depth};
        float h = sz / 2;
        for (int q = 0; q < 4; ++q) {
            float cu = u0 + (q & 1) * h, cv = v0 + (q >> 1) * h;
            if (nodes[node].child[q] < 0) {
                if (!regionHit(p, -1, depth + 1, cu, cv, h, C, r)) continue;
                int32_t ch = newNode(); nodes[node].child[q] = ch; dirtyNodes.push_back(uint32_t(node)); ++st.newTiles;
            }
            visit(p, nodes[node].child[q], depth + 1, D, anc, cu, cv, h, C, r, strength, st);
        }
    }
    // One Standard dab. autoDepth: tiles at least as fine as what the view draws (depth >= draw level - 4).
    DabStats dab(V3 C, float r, float strength, bool autoDepth) {
        DabStats st; auto t0 = Clock::now();
        std::vector<std::vector<uint32_t>> found(threads);
        parallelFor(patches, threads, [&](size_t b, size_t e, int t) {
            for (size_t p = b; p < e; ++p) {
                float dx = pcx[p] - C.x, dy = pcy[p] - C.y, dz = pcz[p] - C.z, rr = r + prad[p] + prange[p];
                if (dx * dx + dy * dy + dz * dz < rr * rr) found[t].push_back(uint32_t(p));
            }
        });
        std::vector<uint32_t> hit; for (auto& f : found) hit.insert(hit.end(), f.begin(), f.end());
        st.patches = hit.size(); auto t1 = Clock::now(); st.findMs = ms(t0, t1);
        if (hit.empty()) return st;
        // depth: cell <= r / 24, else the base cage
        int D = -100;
        for (uint32_t p : hit) {
            float cell0 = pedge[p] / kT; int l = int(std::ceil(std::log2(cell0 * 24 / r)));
            if (autoDepth && levels[p] < 200) l = std::max(l, int(levels[p]) - 4);
            D = std::max(D, l);
            if (root[p] >= 0) D = std::max(D, existingDepth(p, root[p], 0, 0, 0, 1, C, r));
        }
        if (D < 0) { // base cage: move vertices along their normals (normals kept: spike)
            st.base = true; std::vector<std::vector<uint32_t>> mv(threads);
            parallelFor(pos.size(), threads, [&](size_t b, size_t e, int t) {
                for (size_t v = b; v < e; ++v) {
                    float dist = len(pos[v] - C); if (dist >= r) continue;
                    float x = dist / r, f = 1 - 3 * x * x + 2 * x * x * x; pos[v] += nrm[v] * (strength * r * f); mv[t].push_back(uint32_t(v));
                }
            });
            for (auto& m : mv) dirtyVerts.insert(dirtyVerts.end(), m.begin(), m.end());
            st.baseVerts = dirtyVerts.size(); st.samples = pos.size();
            // patches with a moved corner, their corners' normals (the fan changed), then their bounds
            ++stampNo; std::vector<uint32_t> touched;
            for (uint32_t v : dirtyVerts) for (uint32_t k = vpOff[v]; k < vpOff[v + 1]; ++k) if (stamp[vp[k]] != stampNo) { stamp[vp[k]] = stampNo; touched.push_back(vp[k]); }
            std::vector<uint32_t> ring; for (uint32_t p : touched) for (int k = 0; k < 4; ++k) ring.push_back(corners[p * 4 + k]);
            std::sort(ring.begin(), ring.end()); ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
            for (uint32_t v : ring) nrm[v] = fanNormal(v);
            dirtyNrm.insert(dirtyNrm.end(), ring.begin(), ring.end());
            ++stampNo; for (uint32_t v : ring) for (uint32_t k = vpOff[v]; k < vpOff[v + 1]; ++k) if (stamp[vp[k]] != stampNo) { stamp[vp[k]] = stampNo; patchBounds(vp[k]); }
            auto t2 = Clock::now(); st.kernelMs = ms(t1, t2); upload(st); return st;
        }
        D = std::min(D, maxDepth); st.depth = D;
        size_t tilesBefore = tileCount;
        for (uint32_t p : hit) {
            if (root[p] < 0) { if (!regionHit(p, -1, 0, 0, 0, 1, C, r)) continue; root[p] = newNode(); dirtyRoots.push_back(p); ++st.newTiles; }
            Anc anc[16]; visit(p, root[p], 0, D, anc, 0, 0, 1, C, r, strength, st);
        }
        st.newTiles = tileCount - tilesBefore;
        if (std::getenv("SF_MEGA_DEBUG")) {
            std::printf("dab C (%.6f %.6f %.6f) r %.2e D %d patches %zu samples %zu changed %zu newTiles %zu\n", C.x, C.y, C.z, r, D, hit.size(), st.samples, st.changed, st.newTiles);
            if (st.samples == 0) { // trace the path under C: uv by a tangent-frame projection from the patch centre, then the hit test per depth
                size_t p = hit[0]; V3 S, N, Su, Sv, S1, S2; evalPatch(p, 0.5f, 0.5f, S, N); evalPatch(p, 0.51f, 0.5f, S1, N); evalPatch(p, 0.5f, 0.51f, S2, N);
                Su = (S1 - S) * 100.0f; Sv = (S2 - S) * 100.0f; V3 d = C - S; float u = 0.5f + dot(d, Su) / dot(Su, Su), v = 0.5f + dot(d, Sv) / dot(Sv, Sv);
                V3 Sc, Nc; evalPatch(p, u, v, Sc, Nc); Sc += Nc * dispAt(p, u, v);
                std::printf("  trace: uv (%.4f %.4f), surface there is %.2e from C (disp %.2e)\n", u, v, len(Sc - C), dispAt(p, u, v));
                int32_t node = root[p]; float u0 = 0, v0 = 0, sz = 1;
                for (int depth = 0; depth <= D && node >= 0; ++depth) {
                    bool hitq = regionHit(p, node, depth, u0, v0, sz, C, r);
                    std::printf("  depth %d node %d tile %d region (%.4f %.4f + %.4f) hit %d\n", depth, node, nodes[node].tile, u0, v0, sz, int(hitq));
                    float h = sz / 2; int qx = (u >= u0 + h) ? 1 : 0, qy = (v >= v0 + h) ? 1 : 0; node = nodes[node].child[qx + 2 * qy]; u0 += qx * h; v0 += qy * h; sz = h;
                }
            }
        }
        auto t2 = Clock::now(); st.kernelMs = ms(t1, t2); upload(st); return st;
    }
    template <class T> void uploadRuns(GLuint buf, std::vector<uint32_t>& ids, const T* data, size_t stride) {
        if (ids.empty()) return;
        std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        BindBuffer(K_TEXTURE_BUFFER, buf);
        for (size_t i = 0; i < ids.size();) {
            size_t j = i; while (j + 1 < ids.size() && ids[j + 1] == ids[j] + 1) ++j;
            BufferSubData(K_TEXTURE_BUFFER, GLintptr(ids[i] * stride), GLsizeiptr((j - i + 1) * stride), reinterpret_cast<const char*>(data) + ids[i] * stride);
            i = j + 1;
        }
        ids.clear();
    }
    std::vector<int32_t> nodeScratch;
    void upload(DabStats& st) {
        auto t0 = Clock::now();
        uploadRuns(bTiles, dirtyTiles, tiles.data(), kTS * 2);
        if (!dirtyNodes.empty()) {
            nodeScratch.resize(nodes.size() * 8);
            for (uint32_t n : dirtyNodes) { int32_t* d = &nodeScratch[size_t(n) * 8]; d[0] = nodes[n].tile; for (int q = 0; q < 4; ++q) d[1 + q] = nodes[n].child[q]; d[5] = d[6] = d[7] = 0; }
            uploadRuns(bNodes, dirtyNodes, nodeScratch.data(), 32);
        }
        uploadRuns(bRoot, dirtyRoots, root.data(), 4);
        if (!dirtyVerts.empty()) {
            std::vector<float> p4(pos.size() * 4);
            for (uint32_t v : dirtyVerts) p4[v * 4] = pos[v].x, p4[v * 4 + 1] = pos[v].y, p4[v * 4 + 2] = pos[v].z;
            uploadRuns(bPos, dirtyVerts, p4.data(), 16);
        }
        if (!dirtyNrm.empty()) {
            std::vector<float> n4(pos.size() * 4);
            for (uint32_t v : dirtyNrm) n4[v * 4] = nrm[v].x, n4[v * 4 + 1] = nrm[v].y, n4[v * 4 + 2] = nrm[v].z;
            uploadRuns(bNrm, dirtyNrm, n4.data(), 16);
        }
        st.uploadMs = ms(t0, Clock::now());
    }

    // ---------------- frame ----------------
    struct Cam { V3 eye, at; M4 viewProj; float focal; float planes[6][4]; };
    Cam camera(V3 eye, V3 at) {
        Cam c; c.eye = eye; c.at = at; float fov = 40.0f * 3.14159265f / 180; c.focal = H / 2.0f / std::tan(fov / 2);
        float ds = std::max(len(eye) - 1.12f, 1e-5f); float nearP = std::max(ds * 0.2f, 1e-6f), farP = len(eye) + 2;
        V3 up = std::fabs(norm(at - eye).y) > 0.99f ? V3(1, 0, 0) : V3(0, 1, 0);
        c.viewProj = mul(perspective(fov, float(W) / H, nearP, farP), lookAt(eye, at, up));
        const float* m = c.viewProj.m;
        for (int i = 0; i < 6; ++i) {
            int row = i / 2; float s = (i % 2) ? -1.0f : 1.0f;
            for (int k = 0; k < 4; ++k) c.planes[i][k] = m[k * 4 + 3] + s * m[k * 4 + row];
            float l = std::sqrt(c.planes[i][0] * c.planes[i][0] + c.planes[i][1] * c.planes[i][1] + c.planes[i][2] * c.planes[i][2]);
            for (int k = 0; k < 4; ++k) c.planes[i][k] /= l;
        }
        return c;
    }
    struct FrameStats { double cpuMs = 0, gpuMs = 0, totalMs = 0; size_t drawn = 0, quads = 0; int shift = 0, maxLevel = 0; };
    FrameStats frame(const Cam& cam) {
        FrameStats fs; auto t0 = Clock::now();
        std::vector<std::vector<size_t>> cnt(threads, std::vector<size_t>(maxDraw + 1, 0));
        parallelFor(patches, threads, [&](size_t b, size_t e, int t) {
            for (size_t p = b; p < e; ++p) {
                float cx = pcx[p], cy = pcy[p], cz = pcz[p], rr = prad[p] + prange[p]; bool out = false;
                for (int i = 0; i < 6 && !out; ++i) out = cam.planes[i][0] * cx + cam.planes[i][1] * cy + cam.planes[i][2] * cz + cam.planes[i][3] < -rr;
                if (!out) {
                    V3 toEye = cam.eye - V3(cx, cy, cz); float dist = len(toEye); toEye = toEye * (1 / dist);
                    float facing = toEye.x * pnx[p] + toEye.y * pny[p] + toEye.z * pnz[p];
                    if (facing < -(pcone[p] + rr / dist + 0.05f)) out = true;
                    else {
                        float px = cam.focal * pedge[p] / std::max(dist - rr, 1e-6f);
                        int L = px <= targetPx ? 0 : int(std::ceil(std::log2(px / targetPx))); L = std::min(L, maxDraw);
                        rawLevel[p] = int8_t(L); cnt[t][L]++;
                    }
                }
                if (out) rawLevel[p] = -1;
            }
        });
        std::vector<size_t> total(maxDraw + 1, 0);
        for (auto& c : cnt) for (int L = 0; L <= maxDraw; ++L) total[L] += c[L];
        int shift = 0;
        auto quadsAt = [&](int sh) { size_t q = 0; for (int L = 0; L <= maxDraw; ++L) q += total[L] * (size_t(1) << (2 * std::max(L - sh, 0))); return q; };
        while (quadsAt(shift) > budgetQuads && shift < maxDraw) ++shift;
        fs.shift = shift; fs.quads = quadsAt(shift);
        std::fill(levelCnt.begin(), levelCnt.end(), 0);
        for (int L = 0; L <= maxDraw; ++L) levelCnt[std::max(L - shift, 0)] += total[L];
        levelOff[0] = 0; for (int L = 0; L <= maxDraw; ++L) levelOff[L + 1] = levelOff[L] + levelCnt[L];
        // per-thread write cursors
        std::vector<std::vector<size_t>> cur(threads, std::vector<size_t>(maxDraw + 1, 0));
        for (int L = 0; L <= maxDraw; ++L) { size_t o = levelOff[L]; for (int t = 0; t < threads; ++t) { cur[t][L] = o; for (int R = 0; R <= maxDraw; ++R) if (std::max(R - shift, 0) == L) o += cnt[t][R]; } }
        parallelFor(patches, threads, [&](size_t b, size_t e, int t) {
            for (size_t p = b; p < e; ++p) {
                if (rawLevel[p] < 0) { levels[p] = 255; continue; }
                int L = std::max(int(rawLevel[p]) - shift, 0); levels[p] = uint8_t(L); inst[cur[t][L]++] = uint32_t(p);
            }
        });
        fs.drawn = levelOff[maxDraw + 1]; for (int L = maxDraw; L >= 0; --L) if (levelCnt[L]) { fs.maxLevel = L; break; }
        BindBuffer(GL_ARRAY_BUFFER, bInst); BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(fs.drawn * 4), inst.data());
        BindBuffer(K_TEXTURE_BUFFER, bLev); BufferSubData(K_TEXTURE_BUFFER, 0, GLsizeiptr(patches), levels.data());
        auto t1 = Clock::now(); fs.cpuMs = ms(t0, t1);
        // draw
        BindFramebuffer(K_FRAMEBUFFER, fbo); ClearColor(0.16f, 0.17f, 0.19f, 1); Clear(K_COLOR_BIT | K_DEPTH_BIT);
        UseProgram(prog);
        auto bind = [&](const char* name, int unit, GLuint tex) { ActiveTexture(K_TEXTURE0 + unit); BindTexture(K_TEXTURE_BUFFER, tex); Uniform1i(GetUniformLocation(prog, name), unit); };
        bind("uCorners", 0, tCor); bind("uNeighbors", 1, tNb); bind("uLevels", 2, tLev); bind("uPos", 3, tPos); bind("uNrm", 4, tNrm);
        bind("uRoot", 5, tRoot); bind("uNodes", 6, tNodes); bind("uTiles", 7, tTiles);
        UniformMatrix4fv(GetUniformLocation(prog, "uViewProj"), 1, 0, cam.viewProj.m);
        Uniform1i(GetUniformLocation(prog, "uMaxDepth"), maxDepth); Uniform1f(GetUniformLocation(prog, "uStep"), step); Uniform1i(GetUniformLocation(prog, "uWalkCap"), walkCap);
        Uniform3fv(GetUniformLocation(prog, "uEye"), 1, &cam.eye.x); Uniform1i(GetUniformLocation(prog, "uHeat"), heat);
        if (query) BeginQuery(K_TIME_ELAPSED, query);
        for (int L = 0; L <= maxDraw; ++L) {
            if (!levelCnt[L]) continue;
            BindVertexArray(vaos[L]); BindBuffer(GL_ARRAY_BUFFER, bInst);
            VertexAttribIPointer(0, 1, GL_UNSIGNED_INT, 0, reinterpret_cast<const void*>(levelOff[L] * 4));
            Uniform1i(GetUniformLocation(prog, "uN"), 1 << L); Uniform1i(GetUniformLocation(prog, "uLevel"), L);
            DrawElementsInstanced(K_TRIANGLES, iboCount[L], GL_UNSIGNED_INT, nullptr, GLsizei(levelCnt[L]));
        }
        if (query) EndQuery(K_TIME_ELAPSED);
        Finish();
        auto t2 = Clock::now(); fs.totalMs = ms(t0, t2);
        if (query) { uint64_t ns = 0; GetQueryObjectui64v(query, K_QUERY_RESULT, &ns); fs.gpuMs = ns / 1e6; }
        return fs;
    }
    void shot(const std::string& name) {
        if (shots.empty()) return;
        std::vector<unsigned char> px(size_t(W) * H * 3); PixelStorei(K_PACK_ALIGNMENT, 1);
        BindFramebuffer(K_FRAMEBUFFER, fbo); ReadPixels(0, 0, W, H, K_RGB, K_UNSIGNED_BYTE, px.data());
        std::string ppm = shots + "/" + name + ".ppm"; FILE* f = std::fopen(ppm.c_str(), "wb"); if (!f) return;
        std::fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int y = H - 1; y >= 0; --y) std::fwrite(&px[size_t(y) * W * 3], 1, size_t(W) * 3, f);
        std::fclose(f);
    }
    size_t gpuBytes() const { return gpuStatic + patches * 4 + tileCount * kTS * 2 + nodes.size() * 32; }
};

struct Acc { std::vector<double> t; double cpu = 0, gpu = 0, cpuMax = 0, gpuMax = 0; size_t drawn = 0, quads = 0, drawnMax = 0, quadsMax = 0; int maxL = 0;
    void add(const Harness::FrameStats& f, double extra) { t.push_back(f.totalMs + extra); cpu += f.cpuMs; gpu += f.gpuMs; cpuMax = std::max(cpuMax, f.cpuMs); gpuMax = std::max(gpuMax, f.gpuMs);
        drawn += f.drawn; quads += f.quads; drawnMax = std::max(drawnMax, f.drawn); quadsMax = std::max(quadsMax, f.quads); maxL = std::max(maxL, f.maxLevel); }
    void row(const char* name) { if (t.empty()) return; std::vector<double> s = t; std::sort(s.begin(), s.end()); double mean = 0; for (double x : s) mean += x; mean /= s.size();
        std::printf("| %s | %zu | %.1f | %.1f | %.1f | %.1f / %.1f | %.1f / %.1f | %.0fk / %.0fk | %.2fM / %.2fM | %d |\n", name, s.size(), mean, s[size_t(s.size() * 0.99) < s.size() ? size_t(s.size() * 0.99) : s.size() - 1], s.back(),
            cpu / s.size(), cpuMax, gpu / s.size(), gpuMax, double(drawn) / s.size() / 1e3, drawnMax / 1e3, double(quads) / s.size() / 1e6, quadsMax / 1e6, maxL); }
};
struct DabAcc { std::vector<double> t; double find = 0, kern = 0, up = 0; size_t samples = 0, tiles = 0, verts = 0, patches = 0; int depthMin = 99, depthMax = -1; int base = 0;
    void add(const Harness::DabStats& d) { double tot = d.findMs + d.kernelMs + d.uploadMs; t.push_back(tot); find += d.findMs; kern += d.kernelMs; up += d.uploadMs; samples += d.samples; tiles += d.newTiles; verts += d.baseVerts; patches += d.patches;
        if (d.base) ++base; else { depthMin = std::min(depthMin, d.depth); depthMax = std::max(depthMax, d.depth); } }
    void row(const char* name) { if (t.empty()) return; std::vector<double> s = t; std::sort(s.begin(), s.end()); double mean = 0; for (double x : s) mean += x; mean /= s.size();
        std::string depthStr = std::to_string(depthMin) + ".." + std::to_string(depthMax);
        std::printf("| %s | %zu | %.1f | %.1f | %.1f / %.1f / %.1f | %.0fk | %zu | %zu | %d | %s |\n", name, s.size(), mean, s.back(), find / s.size(), kern / s.size(), up / s.size(), double(samples) / s.size() / 1e3, patches / s.size(), tiles, base, depthMax < 0 ? "-" : depthStr.c_str()); }
};
} // namespace

int main(int argc, char** argv) {
    Harness h;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i]; auto next = [&]() { return std::string(i + 1 < argc ? argv[++i] : ""); };
        if (a == "--side") h.side = std::atoi(next().c_str()); else if (a == "--target") h.targetPx = float(std::atof(next().c_str()));
        else if (a == "--size") { std::string s = next(); std::sscanf(s.c_str(), "%dx%d", &h.W, &h.H); } else if (a == "--budget") h.budgetQuads = size_t(std::atof(next().c_str()));
        else if (a == "--threads") h.threads = std::atoi(next().c_str()); else if (a == "--maxdraw") h.maxDraw = std::atoi(next().c_str());
        else if (a == "--maxdepth") h.maxDepth = std::atoi(next().c_str()); else if (a == "--csv") h.csv = next(); else if (a == "--shots") h.shots = next();
        else if (a == "--heat") h.heat = std::atoi(next().c_str()); else if (a == "--walkcap") h.walkCap = std::atoi(next().c_str());
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE); SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_Window* win = SDL_CreateWindow("mega-poly spike", 320, 200, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win); if (!ctx) { std::fprintf(stderr, "GL 3.3: %s\n", SDL_GetError()); return 1; }
    SDL_GL_MakeCurrent(win, ctx);
    if (const char* missing = gl::load([](const char* n) { return reinterpret_cast<gl::GenericProc>(SDL_GL_GetProcAddress(n)); })) { std::fprintf(stderr, "missing %s\n", missing); return 1; }
    h.GenQueries = reinterpret_cast<void (*)(GLsizei, GLuint*)>(SDL_GL_GetProcAddress("glGenQueries"));
    h.BeginQuery = reinterpret_cast<void (*)(GLenum, GLuint)>(SDL_GL_GetProcAddress("glBeginQuery"));
    h.EndQuery = reinterpret_cast<void (*)(GLenum)>(SDL_GL_GetProcAddress("glEndQuery"));
    h.GetQueryObjectui64v = reinterpret_cast<void (*)(GLuint, GLenum, uint64_t*)>(SDL_GL_GetProcAddress("glGetQueryObjectui64v"));
    std::printf("gl: %s, %s; %dx%d, %d threads, target %.0f px, budget %.1fM quads, draw levels <= %d, tile depth <= %d\n", reinterpret_cast<const char*>(GetString(GL_RENDERER)), reinterpret_cast<const char*>(GetString(GL_VERSION)), h.W, h.H, h.threads, h.targetPx, h.budgetQuads / 1e6, h.maxDraw, h.maxDepth);
    auto tb = Clock::now(); h.buildBase(); std::printf("build base: %.0f ms\n", ms(tb, Clock::now()));
    h.initGL();
    if (!h.csv.empty()) { h.csvf = std::fopen(h.csv.c_str(), "w"); if (h.csvf) std::fprintf(h.csvf, "frame,phase,total_ms,cpu_ms,gpu_ms,dab_ms,drawn,quads,shift,max_level,tiles,gpu_mb\n"); }
    int heatShots = h.heat; h.heat = 0;
    int frameNo = 0; double worst = 0; std::string worstWhere;
    auto rotate = [](V3 v, V3 axis, float ang) { axis = norm(axis); return v * std::cos(ang) + cross(axis, v) * std::sin(ang) + axis * (dot(axis, v) * (1 - std::cos(ang))); };
    auto run = [&](const char* phase, Acc& acc, const Harness::Cam& cam, double dabMs) {
        auto fs = h.frame(cam); acc.add(fs, dabMs);
        if (fs.totalMs + dabMs > worst) { worst = fs.totalMs + dabMs; worstWhere = std::string(phase) + " frame " + std::to_string(frameNo) + (dabMs > 0 ? " (dab " + std::to_string(int(dabMs)) + " ms, frame " + std::to_string(int(fs.totalMs)) + ")" : ""); }
        if (h.csvf) std::fprintf(h.csvf, "%d,%s,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%d,%d,%zu,%.1f\n", frameNo, phase, fs.totalMs, fs.cpuMs, fs.gpuMs, dabMs, fs.drawn, fs.quads, fs.shift, fs.maxLevel, h.tileCount, h.gpuBytes() / 1e6);
        ++frameNo; return fs;
    };
    const V3 front = norm(V3(0.3f, 0.25f, 1.0f));
    Acc aOrbit, aFar, aZoom, aClose, aOut; DabAcc dFar, dZoom, dClose;
    // A: orbit, whole model
    for (int f = 0; f < 90; ++f) { float az = f * 6.2832f / 90; V3 eye = V3(std::sin(az) * std::cos(0.3f), std::sin(0.3f), std::cos(az) * std::cos(0.3f)) * 3.2f; run("orbit", aOrbit, h.camera(eye, V3()), 0); }
    // B: far stroke, 240 px, 40 dabs round a circle (the view of frame 0 of the orbit from the front)
    auto farCam = h.camera(front * 3.2f, V3());
    for (int k = 0; k < 40; ++k) {
        float ang = k * 6.2832f / 40; V3 d = rotate(front, V3(0, 1, 0), 0.45f * std::cos(ang)); d = rotate(d, cross(front, V3(0, 1, 0)), 0.45f * std::sin(ang));
        V3 C, Cn; h.surfacePoint(d, C, Cn); float r = 240.0f / farCam.focal * len(farCam.eye - C);
        auto ds = h.dab(C, r, 0.08f, true); dFar.add(ds); run("far-stroke", aFar, farCam, ds.findMs + ds.kernelMs + ds.uploadMs);
    }
    h.shot("far"); if (heatShots) { h.heat = 1; run("far-stroke", aFar, farCam, 0); h.shot("far-heat"); h.heat = 0; }
    // C: zoom in to a close-up of a point, sculpting 120 px dabs every 3 frames (auto subdivide)
    V3 T, up; size_t pT = h.surfacePoint(rotate(front, V3(0, 1, 0), 0.2f), T, up);
    const float dsFar = 2.2f, dsNear = 2e-3f; // 2e-4 is past float32: world coordinates near 1.0 step by 1.2e-7 (one pixel there): a local origin is needed for closer views
    for (int f = 0; f < 120; ++f) {
        float a = f / 119.0f; float ds = dsFar * std::pow(dsNear / dsFar, a); auto cam = h.camera(T + up * ds, T);
        double dm = 0;
        if (f % 3 == 2) { // push / pull alternately round a ring of 1.5 r about T, so the surface under the camera stays put
            float r = 120.0f / cam.focal * ds; V3 s1 = norm(cross(up, V3(0, 1, 0))), s2 = cross(up, s1); float ang = f * 0.9f;
            V3 C = T + s1 * (2.5f * r * std::cos(ang)) + s2 * (2.5f * r * std::sin(ang)); // never reaches T: the close-up stays flat
            auto st = h.dab(C, r, (f / 3) % 2 ? -0.03f : 0.03f, true); dZoom.add(st); dm = st.findMs + st.kernelMs + st.uploadMs; h.pointOn(pT, T, up); }
        run("zoom-in", aZoom, cam, dm);
        if (f == 40 || f == 80 || f == 100 || f == 119) h.shot("zoom-" + std::to_string(f));
    }
    // D: close stroke, 60 dabs of 120 px round a 300 px circle at the closest zoom
    auto closeCam = h.camera(T + up * dsNear, T); V3 side = norm(cross(up, V3(0, 1, 0))), side2 = cross(up, side);
    for (int k = 0; k < 60; ++k) {
        float ang = k * 6.2832f / 60; float rr = 300.0f / closeCam.focal * dsNear, r = 120.0f / closeCam.focal * dsNear;
        V3 C = T + side * (rr * std::cos(ang)) + side2 * (rr * std::sin(ang));
        auto st = h.dab(C, r, 0.3f, true); dClose.add(st); run("close-stroke", aClose, closeCam, st.findMs + st.kernelMs + st.uploadMs);
    }
    h.shot("close"); if (heatShots) { h.heat = 2; run("close-stroke", aClose, closeCam, 0); h.shot("close-depth"); h.heat = 0; }
    if (std::getenv("SF_MEGA_DEBUG")) { // the displacement field round the close-up centre, in steps of 1/8 of the close brush radius
        float rr = 120.0f / closeCam.focal * dsNear / h.pedge[pT] / 8; std::printf("disp field (uv step %.2e, rows v, cols u; units 1e-6):\n", rr);
        for (int j = -8; j <= 8; ++j) { for (int i = -8; i <= 8; ++i) std::printf("%7.1f", h.dispAt(pT, 0.5f + i * rr, 0.5f + j * rr) * 1e6); std::printf("\n"); } }
    { V3 S, N; h.evalPatch(pT, 0.5f, 0.5f, S, N); std::printf("close camera: T (%.5f %.5f %.5f) up (%.3f %.3f %.3f) eye-T %.2e; S (%.5f %.5f %.5f) disp %.2e; patch %zu level %d root %d\n", T.x, T.y, T.z, up.x, up.y, up.z, len(closeCam.eye - T), S.x, S.y, S.z, h.dispAt(pT, 0.5f, 0.5f), pT, int(h.levels[pT]), h.root[pT]); }
    // E: zoom out and orbit (fine tiles seen from afar)
    for (int f = 0; f < 60; ++f) { float a = f / 59.0f; float ds = dsNear * std::pow(dsFar / dsNear, a); run("zoom-out", aOut, h.camera(T + up * ds, T), 0); }
    for (int f = 0; f < 60; ++f) { float az = f * 6.2832f / 60; V3 eye = V3(std::sin(az) * std::cos(0.3f), std::sin(0.3f), std::cos(az) * std::cos(0.3f)) * 3.2f; run("zoom-out", aOut, h.camera(eye, V3()), 0); }
    std::printf("\n| phase | frames | mean ms | p99 ms | worst ms | cpu mean / max | gpu mean / max | patches drawn mean / max | quads mean / max | max draw level |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    aOrbit.row("orbit"); aFar.row("far stroke (240 px, frame + dab)"); aZoom.row("zoom in (120 px dabs every 3rd frame)"); aClose.row("close stroke (120 px)"); aOut.row("zoom out + orbit");
    std::printf("\n| stroke | dabs | mean ms | worst ms | find / kernel / upload ms | samples per dab | patches per dab | new tiles | base-cage dabs | tile depth |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    dFar.row("far 240 px"); dZoom.row("zoom 120 px"); dClose.row("close 120 px");
    std::printf("\nworst frame: %.1f ms at %s\nmemory: tiles %zu (%.1f MB), nodes %zu (%.1f MB), GPU total %.0f MB (static %.0f MB, of which tile pool reserved %.0f MB), CPU base %.0f MB\n",
                worst, worstWhere.c_str(), h.tileCount, h.tileCount * kTS * 2 / 1e6, h.nodes.size(), h.nodes.size() * 32 / 1e6, h.gpuBytes() / 1e6, h.gpuStatic / 1e6, h.tileCap * kTS * 2 / 1e6,
                (h.pos.size() * 24 + h.patches * 44.0) / 1e6);
    if (h.csvf) std::fclose(h.csvf);
    SDL_GL_DestroyContext(ctx); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
