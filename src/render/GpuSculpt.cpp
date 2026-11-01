// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/GpuSculpt.h"

#include "core/Micro.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace sf {

namespace {

// OpenGL 4.3 entry points and enums GL.h (3.3 core) doesn't have.
constexpr GLenum GL_SHADER_STORAGE_BUFFER = 0x90D2;
constexpr GLenum GL_COMPUTE_SHADER = 0x91B9;
constexpr GLbitfield GL_SHADER_STORAGE_BARRIER_BIT = 0x00002000;
constexpr GLbitfield GL_BUFFER_UPDATE_BARRIER_BIT = 0x00000200;
constexpr GLenum GL_MAJOR_VERSION = 0x821B;
constexpr GLenum GL_MINOR_VERSION = 0x821C;
constexpr GLenum GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS = 0x90DD;
constexpr GLenum GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS = 0x90DB;
constexpr GLenum GL_STREAM_READ = 0x88E1;

using PFN_DispatchCompute = void(SF_GLAPI*)(GLuint, GLuint, GLuint);
using PFN_MemoryBarrier = void(SF_GLAPI*)(GLbitfield);
using PFN_BindBufferBase = void(SF_GLAPI*)(GLenum, GLuint, GLuint);
using PFN_GetBufferSubData = void(SF_GLAPI*)(GLenum, GLintptr, GLsizeiptr, void*);
using PFN_Uniform1ui = void(SF_GLAPI*)(GLint, GLuint);
PFN_DispatchCompute DispatchCompute = nullptr;
PFN_MemoryBarrier MemoryBarrier = nullptr;
PFN_BindBufferBase BindBufferBase = nullptr;
PFN_GetBufferSubData GetBufferSubData = nullptr;
PFN_Uniform1ui Uniform1ui = nullptr;

double msSince(std::chrono::steady_clock::time_point& t) {
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - t).count();
    t = now;
    return ms;
}

// --- Kernels (GLSL 4.30). The arithmetic follows core/Sculpt.cpp (mesh levels) and core/MicroSculpt.cpp (micro
// levels) operation for operation, so both paths agree within float rounding. ---

const char* kCommon = R"(#version 430 core
layout(local_size_x = LOCAL) in;
uniform uint uCount;
uint invocation() { return (gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x) * uint(LOCAL) + gl_LocalInvocationID.x; }
float falloff(float dist, float radius, float focal) {
    float t = clamp(1.0 - dist / radius, 0.0, 1.0);
    if (focal != 0.0) t = pow(t, exp2(2.0 * clamp(focal, -1.0, 1.0)));
    return t * t * (3.0 - 2.0 * t);
}
vec3 projectOnPlane(vec3 v, vec3 n) { return v - n * dot(v, n); }
)";

// Mesh levels, pass 1: the area plane's sums, one partial (7 floats) per workgroup.
const char* kMeshSums = R"(
layout(std430, binding = 0) readonly buffer Pos { float pos[]; };
layout(std430, binding = 1) readonly buffer Nrm { float nrm[]; };
layout(std430, binding = 2) writeonly buffer Partial { float partial[]; };
uniform vec3 uCenter;
uniform float uRadius, uFocal;
shared float sums[7][LOCAL];
void main() {
    uint i = invocation(), l = gl_LocalInvocationID.x;
    float v[7] = float[7](0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    if (i < uCount) {
        vec3 x = vec3(pos[3u * i], pos[3u * i + 1u], pos[3u * i + 2u]);
        vec3 n = vec3(nrm[3u * i], nrm[3u * i + 1u], nrm[3u * i + 2u]);
        float w = falloff(distance(x, uCenter), uRadius, uFocal);
        v = float[7](n.x * w, n.y * w, n.z * w, x.x * w, x.y * w, x.z * w, w);
    }
    for (int k = 0; k < 7; ++k) sums[k][l] = v[k];
    barrier();
    for (uint s = uint(LOCAL) / 2u; s > 0u; s >>= 1) {
        if (l < s)
            for (int k = 0; k < 7; ++k) sums[k][l] += sums[k][l + s];
        barrier();
    }
    if (l == 0u) {
        uint g = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
        for (int k = 0; k < 7; ++k) partial[7u * g + uint(k)] = sums[k][0];
    }
}
)";

// Mesh levels, pass 2: each vertex's delta in units of uUnit.
const char* kMeshDeltas = R"(
layout(std430, binding = 0) readonly buffer Pos { float pos[]; };
layout(std430, binding = 1) readonly buffer Nrm { float nrm[]; };
layout(std430, binding = 3) writeonly buffer Out { int outd[]; };
uniform vec3 uCenter, uAreaNormal, uAreaCenter;
uniform float uRadius, uFocal, uStrength, uSign, uInvert, uUnit;
uniform int uBrush;
void main() {
    uint i = invocation();
    if (i >= uCount) return;
    vec3 x = vec3(pos[3u * i], pos[3u * i + 1u], pos[3u * i + 2u]);
    vec3 o = x - uCenter;
    float f = falloff(length(o), uRadius, uFocal);
    float s = uStrength, r = uRadius, sign = uSign;
    vec3 an = uAreaNormal;
    vec3 d = vec3(0.0);
    if (uBrush == 0) d = an * (sign * s * r * 0.06 * f);
    else if (uBrush == 1) {
        vec3 pp = uAreaCenter + an * (sign * r * 0.05 * s);
        float h = dot(x - pp, an);
        if (sign * h < 0.0) d = -an * (h * f);
    } else if (uBrush == 2) {
        vec3 n = vec3(nrm[3u * i], nrm[3u * i + 1u], nrm[3u * i + 2u]);
        d = n * (sign * s * r * 0.05 * f);
    } else if (uBrush == 4) {
        float h = dot(x - uAreaCenter, an);
        d = -an * (h * s * f * (uInvert > 0.5 ? -0.5 : 1.0));
    } else if (uBrush == 5) d = projectOnPlane(uCenter - x, an) * (sign * 0.2 * s * f);
    else if (uBrush == 6) {
        d = an * (-sign * s * r * 0.05 * (f * f));
        d += projectOnPlane(uCenter - x, an) * (0.25 * s * f);
    }
    ivec3 q = ivec3(clamp(round(d / uUnit), vec3(-1073741824.0), vec3(1073741824.0)));
    outd[3u * i] = q.x;
    outd[3u * i + 1u] = q.y;
    outd[3u * i + 2u] = q.z;
}
)";

// Micro levels: shared declarations (MicroTriangle::evalWeights, the grid's index and neighbours).
const char* kMicroCommon = R"(
layout(std430, binding = 4) readonly buffer Corners { vec4 corners[]; }; // per triangle p0 p1 p2 n0 n1 n2
layout(std430, binding = 5) readonly buffer Slots { uint slots[]; };     // slot0 | slot1 << 8 | slot2 << 16
layout(std430, binding = 6) readonly buffer Heights { uint heights[]; }; // int16 pairs, triangle k at k * S
layout(std430, binding = 7) readonly buffer Mask { uint maskv[]; };      // uint8 quads (when uHasMask)
layout(std430, binding = 8) readonly buffer Coords { uint coords[]; };   // fine grid: i | j << 16 per index
layout(std430, binding = 9) readonly buffer CCoords { uint ccoords[]; }; // the coarse grid's likewise
uniform uint uN, uS, uStride, uCM, uCS;
uniform int uHasMask;
uniform float uStep;
uint gindex(uint i, uint j, uint n) { return j * (n + 1u) - (j * (j - 1u)) / 2u + i; }
float heightAt(uint k, uint idx) {
    uint p = k * uS + idx;
    return float(bitfieldExtract(int(heights[p >> 1]), int((p & 1u) * 16u), 16));
}
float maskAt(uint k, uint idx) { // (bitfieldExtract: a variable shift of the loaded word misread on one driver)
    uint p = k * uS + idx;
    return float(bitfieldExtract(maskv[p >> 2], int((p & 3u) * 8u), 8));
}
void evalPoint(uint k, uint i, uint j, out vec3 surface, out vec3 normal) {
    float invN = 1.0 / float(uN);
    float wc[3] = float[3](float(uN - i - j) * invN, float(i) * invN, float(j) * invN);
    uint sl = slots[k];
    float w0 = wc[sl & 255u], w1 = wc[(sl >> 8) & 255u], w2 = wc[(sl >> 16) & 255u];
    vec3 p0 = corners[6u * k].xyz, p1 = corners[6u * k + 1u].xyz, p2 = corners[6u * k + 2u].xyz;
    vec3 n0 = corners[6u * k + 3u].xyz, n1 = corners[6u * k + 4u].xyz, n2 = corners[6u * k + 5u].xyz;
    precise vec3 f = p0 * w0;
    f += p1 * w1;
    f += p2 * w2;
    precise vec3 nn = n0 * w0;
    nn += n1 * w1;
    nn += n2 * w2;
    precise vec3 pull = n0 * (w0 * dot(f - p0, n0));
    pull += n1 * (w1 * dot(f - p1, n1));
    pull += n2 * (w2 * dot(f - p2, n2));
    surface = f - pull * 0.75;
    float len = length(nn);
    normal = len > 1e-20 ? nn / len : vec3(0.0, 0.0, 1.0);
}
)";

// Micro levels, the plane: per triangle (one workgroup each) the weighted normal, centre and weight over every
// second grid point under the brush (7 floats).
const char* kMicroPlane = R"(
layout(std430, binding = 2) writeonly buffer Partial { float partial[]; };
uniform vec3 uCenter;
uniform float uRadius, uFocal;
uniform uint uPlaneStride;
shared float sums[7][LOCAL];
void main() {
    uint k = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x, l = gl_LocalInvocationID.x;
    float v[7] = float[7](0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    if (k < uCount) {
        float r2 = uRadius * uRadius;
        for (uint idx = l; idx < uS; idx += uint(LOCAL)) {
            uint c = coords[idx], i = c & 65535u, j = c >> 16;
            if (i % uPlaneStride != 0u || j % uPlaneStride != 0u) continue;
            vec3 sp, nn;
            evalPoint(k, i, j, sp, nn);
            vec3 p = sp + nn * (heightAt(k, idx) * uStep);
            float d2 = dot(p - uCenter, p - uCenter);
            if (d2 >= r2) continue;
            float w = falloff(sqrt(d2), uRadius, uFocal);
            v[0] += nn.x * w; v[1] += nn.y * w; v[2] += nn.z * w;
            v[3] += p.x * w; v[4] += p.y * w; v[5] += p.z * w; v[6] += w;
        }
    }
    for (int q = 0; q < 7; ++q) sums[q][l] = v[q];
    barrier();
    for (uint s = uint(LOCAL) / 2u; s > 0u; s >>= 1) {
        if (l < s)
            for (int q = 0; q < 7; ++q) sums[q][l] += sums[q][l + s];
        barrier();
    }
    if (l == 0u && k < uCount)
        for (int q = 0; q < 7; ++q) partial[7u * k + uint(q)] = sums[q][0];
}
)";

// Micro levels, the coarse pass: the brush at every coarse point (grid points uStride apart) of every triangle,
// in world units (MicroStroke::dab's kernel).
const char* kMicroCoarse = R"(
layout(std430, binding = 10) writeonly buffer Coarse { float coarse[]; };
uniform vec3 uCenter, uAreaNormal, uAreaCenter;
uniform float uRadius, uFocal, uStrength, uSign, uInvert;
uniform int uBrush;
float before(uint k, uint ci, uint cj) { return heightAt(k, gindex(ci * uStride, cj * uStride, uN)); }
float neighbourAverage(uint k, uint ci, uint cj, float self, out float lo, out float hi) {
    float sum = 0.0;
    int c = 0;
    lo = 1e30; hi = -1e30;
    float v;
#define NB(a, b) { v = before(k, a, b); sum += v; ++c; lo = min(lo, v); hi = max(hi, v); }
    if (ci > 0u) NB(ci - 1u, cj)
    if (ci + cj < uCM) NB(ci + 1u, cj)
    if (cj > 0u) NB(ci, cj - 1u)
    if (ci + cj < uCM) NB(ci, cj + 1u)
    if (cj > 0u) NB(ci + 1u, cj - 1u)
    if (ci > 0u) NB(ci - 1u, cj + 1u)
#undef NB
    return c > 0 ? sum / float(c) : self;
}
float sharpen(uint k, uint ci, uint cj, float cur, float amount) {
    float lo, hi;
    float avg = neighbourAverage(k, ci, cj, cur, lo, hi);
    float target = clamp(cur + (cur - avg) * amount, lo, hi);
    return (target - cur) * uStep;
}
void main() {
    uint gid = invocation();
    if (gid >= uCount) return;
    uint k = gid / uCS, c = gid % uCS;
    uint cc = ccoords[c], ci = cc & 65535u, cj = cc >> 16;
    uint i = ci * uStride, j = cj * uStride, idx = gindex(i, j, uN);
    vec3 sp, nn;
    evalPoint(k, i, j, sp, nn);
    float cur = heightAt(k, idx);
    float d = cur * uStep;
    vec3 p = sp + nn * d;
    vec3 o = p - uCenter;
    float dist2 = dot(o, o), r = uRadius, s = uStrength, sign = uSign;
    float delta = 0.0;
    if (dist2 < r * r) {
        float f = falloff(sqrt(dist2), r, uFocal);
        if (uHasMask != 0 && uStride == 1u) f *= 1.0 - maskAt(k, idx) / 255.0;
        if (f > 0.0) {
            vec3 an = uAreaNormal;
            float facing = max(dot(nn, an), 0.3);
            if (uBrush == 0) delta = sign * s * r * 0.06 * f * dot(an, nn);
            else if (uBrush == 2) delta = sign * s * r * 0.05 * f;
            else if (uBrush == 3) {
                float lo, hi;
                float avg = neighbourAverage(k, ci, cj, cur, lo, hi);
                delta = (avg - cur) * uStep * min(1.0, s * f);
            } else if (uBrush == 4) {
                float h = dot(p - uAreaCenter, an);
                delta = -h * s * f * (uInvert > 0.5 ? -0.5 : 1.0) / facing;
            } else if (uBrush == 1) {
                vec3 pp = uAreaCenter + an * (sign * r * 0.05 * s);
                float h = dot(p - pp, an);
                if (sign * h < 0.0) delta = -h * f / facing;
            } else if (uBrush == 5) delta = sign * sharpen(k, ci, cj, cur, 0.6 * s * f);
            else if (uBrush == 6) delta = -sign * s * r * 0.05 * f * f + sharpen(k, ci, cj, cur, 0.5 * s * f);
        }
    }
    coarse[gid] = delta;
}
)";

// Micro levels, the fine pass: every grid point's change interpolated from the coarse points around it (exactly
// the coarse value on a coarse point), the mask applied, in height units rounded half away from zero; two
// points (int16) per invocation.
const char* kMicroFine = R"(
layout(std430, binding = 10) readonly buffer Coarse { float coarse[]; };
layout(std430, binding = 3) writeonly buffer Out { uint outd[]; };
uniform uint uPoints;
int fineDelta(uint q) {
    if (q >= uPoints) return 0;
    uint k = q / uS, idx = q % uS;
    uint c = coords[idx], i = c & 65535u, j = c >> 16;
    uint cj = j / uStride, rj = j % uStride, ci = i / uStride, ri = i % uStride;
    float inv = 1.0 / float(uStride);
    float fi = float(ri) * inv, fj = float(rj) * inv;
    uint base = k * uCS;
    float d;
    if (ri == 0u && rj == 0u) d = coarse[base + gindex(ci, cj, uCM)];
    else if (fi + fj <= 1.0)
        d = coarse[base + gindex(ci, cj, uCM)] * (1.0 - fi - fj) + coarse[base + gindex(ci + 1u, cj, uCM)] * fi +
            coarse[base + gindex(ci, cj + 1u, uCM)] * fj;
    else
        d = coarse[base + gindex(ci + 1u, cj, uCM)] * (1.0 - fj) + coarse[base + gindex(ci + 1u, cj + 1u, uCM)] * (fi + fj - 1.0) +
            coarse[base + gindex(ci, cj + 1u, uCM)] * (1.0 - fi);
    if (d == 0.0) return 0;
    if (uHasMask != 0 && (uStride > 1u || uGrab != 0)) d *= 1.0 - maskAt(k, idx) * (1.0 / 255.0);
    float u = clamp(d / uStep, -32767.0, 32767.0);
    return u >= 0.0 ? int(u + 0.5) : -int(0.5 - u);
}
void main() {
    uint gid = invocation();
    if (2u * gid >= uPoints) return;
    int a = fineDelta(2u * gid), b = fineDelta(2u * gid + 1u);
    outd[gid] = (uint(a) & 65535u) | (uint(b) << 16);
}
)";

GLuint compileCompute(const std::string& source, std::string& error) {
    GLuint sh = gl::CreateShader(GL_COMPUTE_SHADER);
    const char* src = source.c_str();
    gl::ShaderSource(sh, 1, &src, nullptr);
    gl::CompileShader(sh);
    GLint ok = 0;
    gl::GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl::GetShaderInfoLog(sh, sizeof log, nullptr, log);
        error = log;
        gl::DeleteShader(sh);
        return 0;
    }
    GLuint prog = gl::CreateProgram();
    gl::AttachShader(prog, sh);
    gl::LinkProgram(prog);
    gl::DeleteShader(sh);
    gl::GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl::GetProgramInfoLog(prog, sizeof log, nullptr, log);
        error = log;
        gl::DeleteProgram(prog);
        return 0;
    }
    return prog;
}

std::string source(int local, std::initializer_list<const char*> parts, const char* defines = "") {
    std::string s = kCommon;
    const size_t at = s.find('\n') + 1; // after #version
    s.insert(at, std::string("#define LOCAL ") + std::to_string(local) + "\n" + defines);
    for (const char* p : parts) s += p;
    return s;
}

void setU(GLuint prog, const char* name, uint32_t v) { Uniform1ui(gl::GetUniformLocation(prog, name), v); }
void setI(GLuint prog, const char* name, int v) { gl::Uniform1i(gl::GetUniformLocation(prog, name), v); }
void setF(GLuint prog, const char* name, float v) { gl::Uniform1f(gl::GetUniformLocation(prog, name), v); }
void setV(GLuint prog, const char* name, const glm::vec3& v) { gl::Uniform3fv(gl::GetUniformLocation(prog, name), 1, &v.x); }

constexpr int kLocal = 256, kMicroLocal = 64;

} // namespace

std::unique_ptr<GpuSculpt> GpuSculpt::create(gl::ProcLoader loader, std::string& reason) {
    GLint major = 0, minor = 0;
    gl::GetIntegerv(GL_MAJOR_VERSION, &major);
    gl::GetIntegerv(GL_MINOR_VERSION, &minor);
    for (int i = 0; i < 16 && gl::GetError() != 0; ++i) {}
    if (major * 10 + minor < 43) {
        reason = "OpenGL " + std::to_string(major) + "." + std::to_string(minor) + " has no compute shaders (4.3 needed)";
        return nullptr;
    }
    DispatchCompute = reinterpret_cast<PFN_DispatchCompute>(loader("glDispatchCompute"));
    MemoryBarrier = reinterpret_cast<PFN_MemoryBarrier>(loader("glMemoryBarrier"));
    BindBufferBase = reinterpret_cast<PFN_BindBufferBase>(loader("glBindBufferBase"));
    GetBufferSubData = reinterpret_cast<PFN_GetBufferSubData>(loader("glGetBufferSubData"));
    Uniform1ui = reinterpret_cast<PFN_Uniform1ui>(loader("glUniform1ui"));
    if (!DispatchCompute || !MemoryBarrier || !BindBufferBase || !GetBufferSubData || !Uniform1ui) {
        reason = "the driver lacks the compute shader functions";
        return nullptr;
    }
    GLint bindings = 0, blocks = 0;
    gl::GetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &bindings);
    gl::GetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &blocks);
    if (bindings < BufferCount || blocks < 8) {
        reason = "too few shader storage buffers (" + std::to_string(blocks) + ")";
        return nullptr;
    }
    std::unique_ptr<GpuSculpt> g(new GpuSculpt());
    if (!g->init(reason)) return nullptr;
    return g;
}

bool GpuSculpt::init(std::string& reason) {
    owner_ = std::this_thread::get_id();
    struct P {
        GLuint* prog;
        std::string src;
    } progs[] = {
        {&meshSums_, source(kLocal, {kMeshSums})},
        {&meshDeltas_, source(kLocal, {kMeshDeltas})},
        {&microPlane_, source(kMicroLocal, {kMicroCommon, kMicroPlane})},
        {&microCoarse_, source(kLocal, {kMicroCommon, kMicroCoarse})},
        {&microFine_, source(kLocal, {kMicroCommon, kMicroFine}, "uniform int uGrab;\n")},
    };
    for (P& p : progs) {
        std::string error;
        *p.prog = compileCompute(p.src, error);
        if (!*p.prog) {
            reason = "a brush kernel didn't compile: " + error.substr(0, 300);
            return false;
        }
    }
    gl::GenBuffers(BufferCount, buffers_);
    return true;
}

GpuSculpt::~GpuSculpt() {
    if (!onOwnerThread()) return; // (no context here: the driver frees them with the context)
    for (GLuint p : {meshSums_, meshDeltas_, microPlane_, microCoarse_, microFine_})
        if (p) gl::DeleteProgram(p);
    gl::DeleteBuffers(BufferCount, buffers_);
}

size_t GpuSculpt::gpuBytes() const {
    size_t b = 0;
    for (size_t s : sizes_) b += s;
    return b;
}

void GpuSculpt::bind(int index, size_t bytes, int binding) {
    bytes = std::max<size_t>(bytes, 16);
    gl::BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers_[index]);
    if (sizes_[index] < bytes) {
        const size_t grown = std::max(bytes, sizes_[index] + sizes_[index] / 2);
        gl::BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(grown), nullptr, index == Out || index == Partial ? GL_STREAM_READ : GL_DYNAMIC_DRAW);
        sizes_[index] = grown;
    }
    BindBufferBase(GL_SHADER_STORAGE_BUFFER, GLuint(binding), buffers_[index]);
}

void GpuSculpt::upload(int index, const void* data, size_t bytes, int binding) {
    bind(index, bytes, binding);
    if (bytes) gl::BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(bytes), data);
}

void GpuSculpt::dispatch(size_t invocations, int localSize) {
    const size_t groups = std::max<size_t>(1, (invocations + size_t(localSize) - 1) / size_t(localSize));
    const GLuint x = GLuint(std::min<size_t>(groups, 65535)), y = GLuint((groups + 65534) / 65535);
    DispatchCompute(x, y, 1);
}

void GpuSculpt::finish() {
    for (int b = 0; b < 11; ++b) BindBufferBase(GL_SHADER_STORAGE_BUFFER, GLuint(b), 0);
    gl::BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    gl::UseProgram(0);
}

bool GpuSculpt::meshDab(const MeshDabJob& job, MeshDabResult& out) {
    if (!onOwnerThread() || job.verts.empty()) return false;
    auto t = std::chrono::steady_clock::now();
    const size_t n = job.verts.size();
    stageA_.resize(n * 3);
    stageB_.resize(n * 3);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const glm::vec3& p = job.positions[job.verts[i]];
            const glm::vec3& q = job.normals[job.verts[i]];
            std::memcpy(&stageA_[i * 3], &p.x, 12);
            std::memcpy(&stageB_[i * 3], &q.x, 12);
        }
    }, 1 << 14);
    upload(Pos, stageA_.data(), n * 12, 0);
    upload(Nrm, stageB_.data(), n * 12, 1);
    const size_t groups = (n + kLocal - 1) / kLocal;
    bind(Partial, groups * 7 * 4, 2);
    bind(Out, n * 12, 3);
    stats.uploadMs += msSince(t);

    // Pass 1: the area plane.
    gl::UseProgram(meshSums_);
    setU(meshSums_, "uCount", uint32_t(n));
    setV(meshSums_, "uCenter", job.center);
    setF(meshSums_, "uRadius", job.radius);
    setF(meshSums_, "uFocal", job.focal);
    dispatch(n, kLocal);
    MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    partials_.resize(groups * 7);
    gl::BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers_[Partial]);
    GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(groups * 7 * 4), partials_.data());
    glm::vec3 nsum(0.0f), csum(0.0f);
    float w = 0.0f;
    for (size_t g = 0; g < groups; ++g) {
        const float* p = &partials_[g * 7];
        nsum += glm::vec3(p[0], p[1], p[2]);
        csum += glm::vec3(p[3], p[4], p[5]);
        w += p[6];
    }
    out.weight = w;
    if (w <= 0.0f) {
        stats.kernelMs += msSince(t);
        finish();
        return true;
    }
    out.areaCenter = csum / w;
    const float len = glm::length(nsum);
    out.areaNormal = len > 1e-12f ? nsum / len : -job.viewDir;

    // Pass 2: the deltas.
    gl::UseProgram(meshDeltas_);
    setU(meshDeltas_, "uCount", uint32_t(n));
    setV(meshDeltas_, "uCenter", job.center);
    setV(meshDeltas_, "uAreaNormal", out.areaNormal);
    setV(meshDeltas_, "uAreaCenter", out.areaCenter);
    setF(meshDeltas_, "uRadius", job.radius);
    setF(meshDeltas_, "uFocal", job.focal);
    setF(meshDeltas_, "uStrength", std::clamp(job.strength, 0.0f, 1.0f));
    setF(meshDeltas_, "uSign", job.invert ? -1.0f : 1.0f);
    setF(meshDeltas_, "uInvert", job.invert ? 1.0f : 0.0f);
    setF(meshDeltas_, "uUnit", job.unit);
    setI(meshDeltas_, "uBrush", int(job.brush));
    dispatch(n, kLocal);
    MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    stats.kernelMs += msSince(t);
    out.delta.resize(n);
    gl::BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers_[Out]);
    GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(n * 12), out.delta.data());
    finish();
    const bool ok = gl::GetError() == 0;
    stats.readbackMs += msSince(t);
    if (ok) {
        ++stats.dabs;
        stats.points += n;
    }
    return ok;
}

namespace {

// The coarse grid's coordinates (i | j << 16) for an m-grid.
void gridCoords(uint32_t m, std::vector<uint32_t>& out) {
    out.clear();
    for (uint32_t j = 0; j <= m; ++j)
        for (uint32_t i = 0; i + j <= m; ++i) out.push_back(i | j << 16);
}

} // namespace

bool GpuSculpt::microDab(const MicroDabJob& job, MicroDabResult& out) {
    if (!onOwnerThread() || job.tris.empty() || job.stride == 0 || job.n % job.stride) return false;
    auto t = std::chrono::steady_clock::now();
    const size_t K = job.tris.size(), S = job.samples;
    const uint32_t cm = job.n / job.stride, cs = (cm + 1) * (cm + 2) / 2;
    const size_t points = K * S;
    // Corners, slots, heights and mask of the triangles in reach.
    stageCorners_.resize(K * 6);
    stageSlots_.resize(K);
    stageHeights_.resize(points + 2);
    if (job.mask) stageMask_.resize(points + 4);
    poolFor(K, [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            for (int c = 0; c < 3; ++c) {
                stageCorners_[k * 6 + size_t(c)] = glm::vec4(job.cornerP[k * 3 + size_t(c)], 0.0f);
                stageCorners_[k * 6 + 3 + size_t(c)] = glm::vec4(job.cornerN[k * 3 + size_t(c)], 0.0f);
            }
            const uint8_t* sl = job.cornerSlot + k * 3;
            stageSlots_[k] = uint32_t(sl[0]) | uint32_t(sl[1]) << 8 | uint32_t(sl[2]) << 16;
            std::memcpy(&stageHeights_[k * S], job.heights + size_t(job.tris[k]) * S, S * 2);
            if (job.mask) std::memcpy(&stageMask_[k * S], job.mask + size_t(job.tris[k]) * S, S);
        }
    }, 64);
    upload(Corners, stageCorners_.data(), K * 6 * 16, 4);
    upload(Slots, stageSlots_.data(), K * 4, 5);
    upload(Heights, stageHeights_.data(), ((points + 1) / 2) * 4, 6);
    if (job.mask) upload(Mask, stageMask_.data(), ((points + 3) / 4) * 4, 7);
    else bind(Mask, 16, 7);
    if (coordsN_ != job.n) {
        const std::vector<uint32_t>& c = microGridCoords(job.n);
        upload(Coords, c.data(), c.size() * 4, 8);
        coordsN_ = job.n;
    } else bind(Coords, sizes_[Coords], 8);
    if (coarseN_ != cm) {
        std::vector<uint32_t> c;
        gridCoords(cm, c);
        upload(CoarseCoords, c.data(), c.size() * 4, 9);
        coarseN_ = cm;
    } else bind(CoarseCoords, sizes_[CoarseCoords], 9);
    bind(Coarse, K * cs * 4, 10);
    bind(Out, ((points + 1) / 2) * 4, 3);
    bind(Partial, K * 7 * 4, 2);
    stats.uploadMs += msSince(t);

    auto common = [&](GLuint prog) {
        gl::UseProgram(prog);
        setU(prog, "uN", job.n);
        setU(prog, "uS", uint32_t(S));
        setU(prog, "uStride", job.stride);
        setU(prog, "uCM", cm);
        setU(prog, "uCS", cs);
        setI(prog, "uHasMask", job.mask ? 1 : 0);
        setF(prog, "uStep", job.step);
    };
    // The plane.
    glm::vec3 areaNormal(0.0f), areaCenter(0.0f);
    if (job.needPlane) {
        common(microPlane_);
        setU(microPlane_, "uCount", uint32_t(K));
        setV(microPlane_, "uCenter", job.center);
        setF(microPlane_, "uRadius", job.radius);
        setF(microPlane_, "uFocal", job.focal);
        setU(microPlane_, "uPlaneStride", job.n >= 4 ? 2u : 1u);
        dispatch(K * kMicroLocal, kMicroLocal);
        MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        partials_.resize(K * 7);
        gl::BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers_[Partial]);
        GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(K * 7 * 4), partials_.data());
        glm::dvec3 nsum(0.0), csum(0.0);
        double w = 0.0;
        for (size_t k = 0; k < K; ++k) {
            const float* p = &partials_[k * 7];
            nsum += glm::dvec3(p[0], p[1], p[2]);
            csum += glm::dvec3(p[3], p[4], p[5]);
            w += p[6];
        }
        if (w <= 0.0) {
            areaNormal = -job.viewDir;
            areaCenter = job.center;
        } else {
            areaCenter = glm::vec3(csum / w);
            const double len = glm::length(nsum);
            areaNormal = len > 1e-12 ? glm::vec3(nsum / len) : -job.viewDir;
        }
    }
    out.areaNormal = areaNormal;
    out.areaCenter = areaCenter;
    // The coarse points.
    common(microCoarse_);
    setU(microCoarse_, "uCount", uint32_t(K * cs));
    setV(microCoarse_, "uCenter", job.center);
    setV(microCoarse_, "uAreaNormal", areaNormal);
    setV(microCoarse_, "uAreaCenter", areaCenter);
    setF(microCoarse_, "uRadius", job.radius);
    setF(microCoarse_, "uFocal", job.focal);
    setF(microCoarse_, "uStrength", std::clamp(job.strength, 0.0f, 1.0f));
    setF(microCoarse_, "uSign", job.invert ? -1.0f : 1.0f);
    setF(microCoarse_, "uInvert", job.invert ? 1.0f : 0.0f);
    setI(microCoarse_, "uBrush", int(job.brush));
    dispatch(K * cs, kLocal);
    MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    // Every grid point.
    common(microFine_);
    setU(microFine_, "uCount", uint32_t((points + 1) / 2));
    setU(microFine_, "uPoints", uint32_t(points));
    setI(microFine_, "uGrab", 0);
    dispatch((points + 1) / 2, kLocal);
    MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    stats.kernelMs += msSince(t);
    out.delta.resize(((points + 1) / 2) * 2);
    gl::BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers_[Out]);
    GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(((points + 1) / 2) * 4), out.delta.data());
    out.delta.resize(points);
    finish();
    const bool ok = gl::GetError() == 0;
    stats.readbackMs += msSince(t);
    if (ok) {
        ++stats.dabs;
        stats.points += points;
    }
    return ok;
}

bool GpuSculpt::microGrab(const MicroGrabJob& job, std::vector<int16_t>& delta) {
    if (!onOwnerThread() || job.tris.empty() || job.stride == 0 || job.n % job.stride) return false;
    auto t = std::chrono::steady_clock::now();
    const size_t K = job.tris.size(), S = job.samples;
    const uint32_t cm = job.n / job.stride, cs = (cm + 1) * (cm + 2) / 2;
    const size_t points = K * S;
    if (job.mask) {
        stageMask_.resize(points + 4);
        poolFor(K, [&](size_t b, size_t e) {
            for (size_t k = b; k < e; ++k) std::memcpy(&stageMask_[k * S], job.mask + size_t(job.tris[k]) * S, S);
        }, 64);
        upload(Mask, stageMask_.data(), ((points + 3) / 4) * 4, 7);
    } else bind(Mask, 16, 7);
    if (coordsN_ != job.n) {
        const std::vector<uint32_t>& c = microGridCoords(job.n);
        upload(Coords, c.data(), c.size() * 4, 8);
        coordsN_ = job.n;
    } else bind(Coords, sizes_[Coords], 8);
    upload(Coarse, job.coarse, K * cs * 4, 10);
    bind(Out, ((points + 1) / 2) * 4, 3);
    // (unused by this pass, but bound: the shader declares them)
    bind(Corners, 16, 4);
    bind(Slots, 16, 5);
    bind(Heights, 16, 6);
    bind(CoarseCoords, sizes_[CoarseCoords], 9);
    stats.uploadMs += msSince(t);
    gl::UseProgram(microFine_);
    setU(microFine_, "uN", job.n);
    setU(microFine_, "uS", uint32_t(S));
    setU(microFine_, "uStride", job.stride);
    setU(microFine_, "uCM", cm);
    setU(microFine_, "uCS", cs);
    setI(microFine_, "uHasMask", job.mask ? 1 : 0);
    setF(microFine_, "uStep", job.step);
    setU(microFine_, "uCount", uint32_t((points + 1) / 2));
    setU(microFine_, "uPoints", uint32_t(points));
    setI(microFine_, "uGrab", 1);
    dispatch((points + 1) / 2, kLocal);
    MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    stats.kernelMs += msSince(t);
    delta.resize(((points + 1) / 2) * 2);
    gl::BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers_[Out]);
    GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(((points + 1) / 2) * 4), delta.data());
    delta.resize(points);
    finish();
    const bool ok = gl::GetError() == 0;
    stats.readbackMs += msSince(t);
    if (ok) {
        ++stats.dabs;
        stats.points += points;
    }
    return ok;
}

} // namespace sf
