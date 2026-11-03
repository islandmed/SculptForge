// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/Renderer.h"
#include "render/MicroRenderer.h"

#include "core/Matcap.h"
#include "core/Tangents.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"
#include "core/ThreadPool.h"
#include "core/TierConfig.h"
#include "core/UploadOrder.h"

#include <SDL3/SDL_log.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <limits>
#include <vector>

namespace sf {

namespace {

// Mesh vertices are PackedVertex (render/Clusters.h): the position on the slot's grid (21 bits per axis in two
// words), an octahedral normal and the (companded) curvature in a third. Decoded here exactly as Clusters.cpp does.
// Compiled per feature set: PAINT (polypaint colour and mask attributes), PRIM (per-triangle data: the first
// drawn triangle of the vertex's cluster, from a per-block table indexed by gl_VertexID, or uPrimBase when the
// clusters are drawn one by one), TEXTURED and NORMALMAP (with a geometry shader, below), and FLAT (flat
// shading: the view-space position goes along, so the fragment shader takes each triangle's own normal from it).
const char* kMeshVS = R"(
layout(location = 0) in uvec2 aPos;
layout(location = 1) in uint aNormal;
#ifdef PAINT
layout(location = 2) in vec4 aColor; // polypaint, gamma 2.2 encoded (constant white when the mesh has none)
layout(location = 3) in float aMask; // constant 0 when the mesh has no mask
out vec4 vPaint;                     // linear colour, mask
#endif
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uGridOrigin;
uniform float uGridStep;
#ifdef PRIM
uniform usamplerBuffer uClusterPrims;
uniform int uPrimBase; // >= 0: this cluster's first drawn triangle (clusters drawn one by one)
flat out int vPrimBase;
#endif
out vec4 vNormalCurv; // view-space normal, curvature
#ifdef FLAT
out vec3 vViewPos;
#endif
vec3 decodeNormal(uint a) {
    vec2 e = vec2(float(int(a & 2047u) - 1024), float(int((a >> 11) & 2047u) - 1024)) / 1023.0;
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
#ifdef GHOST
// The predictive stroke preview (render/Ghost.h): the dabs the solver hasn't placed yet move the vertices as the
// brush would. Keep in step with sf::ghostMove (Ghost.cpp).
uniform vec4 uGhostA[64];    // centre, radius
uniform vec4 uGhostB[64];    // area normal, strength x pressure
uniform vec4 uGhostC[64];    // area centre, weight
uniform int uGhostGroups;    // runs of dabs (one per mirror), each with a sphere holding its dabs
uniform vec4 uGhostBound[8];
uniform ivec2 uGhostRange[8]; // [first, end) of each run
uniform ivec2 uGhostKind;    // kernel (sf::GhostKind), inverted
uniform float uGhostFocal;
uniform float uGhostEdge;    // the mesh's edge length under the brush (the cavity term's scale)
// The falloff and its first two derivatives in the distance (brushFalloff with Focal Shift).
vec3 ghostFalloff(float d, float r) {
    float u = clamp(1.0 - d / r, 0.0, 1.0);
    if (u <= 0.0) return vec3(0.0);
    if (uGhostFocal == 0.0) // (the plain smoothstep: t = u)
        return vec3(u * u * (3.0 - 2.0 * u), -6.0 * u * (1.0 - u) / r, (6.0 - 12.0 * u) / (r * r));
    float k = exp2(2.0 * clamp(uGhostFocal, -1.0, 1.0));
    float t = pow(u, k), dt = -k * t / (u * r), ddt = k * (k - 1.0) * t / (u * u * r * r);
    return vec3(t * t * (3.0 - 2.0 * t), 6.0 * t * (1.0 - t) * dt, (6.0 - 12.0 * t) * dt * dt + 6.0 * t * (1.0 - t) * ddt);
}
vec3 onPlane(vec3 v, vec3 n) { return v - n * dot(v, n); }
// Moves x as the dabs [first, end) would, and follows what the moves do to the surface seen as a height field
// over the vertex's tangent plane (normal n): the height's gradient g (in that plane) and its Laplacian lap
// (forward differentiation of each kernel's height change; lateral moves don't count). The shading normal is
// then n - g, and the cavity term changes by the edge length over four times lap (curvatureAt).
vec3 ghostMove(vec3 x, vec3 n, int first, int end, inout vec3 g, inout float lap) {
    float sgn = uGhostKind.y != 0 ? -1.0 : 1.0;
    int kind = uGhostKind.x;
    for (int i = first; i < end; ++i) {
        vec3 c = uGhostA[i].xyz;
        float r = uGhostA[i].w;
        float dist = length(x - c);
        if (dist >= r) continue;
        float w = uGhostC[i].w;
        vec3 F = ghostFalloff(dist, r) * w; // f, f', f'' in the distance
        float f = F.x;
        float s = uGhostB[i].w;
        vec3 an = uGhostB[i].xyz;
        vec3 ac = uGhostC[i].xyz;
        vec3 radial = onPlane(x - c, n);
        float rl = length(radial);
        radial = rl > 1e-12 ? radial / rl : vec3(0.0);
        float inv = 1.0 / max(dist, r * 1e-3);
        float cn = dot(an, n);              // the kernel moves along an: the height changes by cn per unit
        vec3 dh = onPlane(an, n) + cn * g;  // gradient of dot(x, an) over the surface
        float radialLap = F.z + F.y * inv;  // Laplacian of f
        if (kind == 1 || kind == 2) {       // Standard (along the area normal), Inflate (along the vertex's)
            float a = sgn * s * r * (kind == 1 ? 0.06 : 0.05);
            float k = kind == 1 ? cn : 1.0;
            x += (kind == 1 ? an : n) * (a * f);
            g += radial * (k * a * F.y);
            lap += k * a * radialLap;
        } else if (kind == 3 || kind == 4) { // Clay, Clay Buildup: everything below a plane pulled up to it
            float h = dot(x - (ac + an * (sgn * r * (kind == 3 ? 0.05 : 0.08) * s)), an);
            if (sgn * h < 0.0) {
                vec3 W = F; // the pull's profile and its derivatives
                if (kind == 4) {
                    float e = clamp((1.0 - dist / r) / 0.35, 0.0, 1.0), de = e > 0.0 && e < 1.0 ? -1.0 / (0.35 * r) : 0.0;
                    W = vec3(e * e * (3.0 - 2.0 * e), 6.0 * e * (1.0 - e) * de, (6.0 - 12.0 * e) * de * de) * w;
                }
                x -= an * (h * W.x);
                lap -= cn * (2.0 * W.y * dot(radial, dh) + W.x * cn * lap + h * (W.z + W.y * inv));
                g -= cn * (W.x * dh + h * W.y * radial);
            }
        } else if (kind == 5) { // Flatten
            float m = s * (uGhostKind.y != 0 ? -0.5 : 1.0);
            float h = dot(x - ac, an);
            x -= an * (h * m * f);
            lap -= cn * m * (2.0 * F.y * dot(radial, dh) + f * cn * lap + h * radialLap);
            g -= cn * m * (f * dh + h * F.y * radial);
        } else if (kind == 6) { // Crease: a valley of f^2, the sides pinched in
            float a = -sgn * s * r * 0.05;
            x += an * (a * f * f) + onPlane(c - x, an) * (0.25 * s * f);
            g += radial * (cn * a * 2.0 * f * F.y);
            lap += cn * a * (2.0 * (F.y * F.y + f * F.z) + 2.0 * f * F.y * inv);
        } else if (kind == 7) { // Pinch: sideways only
            x += onPlane(c - x, an) * (sgn * 0.2 * s * f);
        } else if (kind == 8) { // Dam Standard: f^4, the sides pinched in
            float a = -sgn * s * r * 0.08;
            float f3 = f * f * f;
            x += an * (a * f3 * f) + onPlane(c - x, an) * (0.3 * s * f);
            g += radial * (cn * a * 4.0 * f3 * F.y);
            lap += cn * a * (12.0 * f * f * F.y * F.y + 4.0 * f3 * F.z + 4.0 * f3 * F.y * inv);
        } else if (kind == 9) { // Trim: what sticks out above a plane cut off
            float h = dot(x - (ac - an * (sgn * r * 0.02 * s)), an);
            if (sgn * h > 0.0) {
                float q = sqrt(max(f, 0.0)) * (0.5 + s);
                float m = min(1.0, q), dm = q < 1.0 && f > 1e-6 ? (0.5 + s) * F.y / (2.0 * sqrt(f)) : 0.0;
                x -= an * (h * m);
                lap -= cn * (2.0 * dm * dot(radial, dh) + m * cn * lap + h * dm * inv);
                g -= cn * (m * dh + h * dm * radial);
            }
        }
    }
    return x;
}
#endif
void main() {
    uvec3 q = uvec3(aPos.x & 0x1FFFFFu, (aPos.x >> 21) | ((aPos.y & 0x3FFu) << 11), (aPos.y >> 10) & 0x1FFFFFu);
    vec3 world = uGridOrigin + vec3(q) * uGridStep;
    vec3 normal = decodeNormal(aNormal);
    float curvature = float(int(aNormal >> 22) - 512) / 511.0; // sign(c) sqrt(|c|), render/Clusters.h
    curvature *= abs(curvature);
#ifdef GHOST
    // The moved position, normal and cavity term (one evaluation per vertex, no extra varyings: a tiled GPU
    // writes every vertex's outputs to memory and reads them back).
    for (int k = 0; k < uGhostGroups; ++k) {
        if (distance(world, uGhostBound[k].xyz) >= uGhostBound[k].w) continue;
        vec3 g = vec3(0.0);
        float lap = 0.0;
        world = ghostMove(world, normal, uGhostRange[k].x, uGhostRange[k].y, g, lap);
        normal = normalize(normal - g);
        curvature = clamp(curvature + 0.25 * uGhostEdge * lap, -1.0, 1.0);
    }
#endif
    vec4 p = uView * vec4(world, 1.0);
    vNormalCurv = vec4(mat3(uView) * normal, curvature);
#ifdef FLAT
    vViewPos = p.xyz;
#endif
#ifdef PAINT
    vPaint = vec4(pow(aColor.rgb, vec3(2.2)), aMask);
#endif
#ifdef PRIM
    vPrimBase = uPrimBase >= 0 ? uPrimBase : int(texelFetch(uClusterPrims, gl_VertexID >> 7).r);
#endif
    gl_Position = uProj * p;
}
)";

// Texture painting (core/Texture.h): the same shading with the per-corner UVs of each drawn triangle
// fetched from a texture buffer by primitive id (the vertices are shared, the UVs are not). Only meshes
// with a texture use it, so sculpting plain meshes keeps the geometry-shader-free path.
// With NORMALMAP, a baked normal map preview (core/Bake.h): each corner's MikkTSpace tangent (xyz, bitangent
// sign in w) too, fetched like the UVs and turned into view space.
const char* kMeshGS = R"(
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec4 vNormalCurv[];
#ifdef PAINT
in vec4 vPaint[];
#endif
#ifdef FLAT
in vec3 vViewPos[];
#endif
flat in int vPrimBase[];
uniform samplerBuffer uUvs; // RG32F, 3 corners per drawn triangle
#ifdef NORMALMAP
uniform samplerBuffer uTangents; // RGBA32F, 3 corners per drawn triangle
uniform mat4 uView;
#endif
out FragData {
    vec4 vNormalCurv;
#ifdef PAINT
    vec4 vPaint;
#endif
    vec2 vUv;
#ifdef NORMALMAP
    vec4 vTangent;
#endif
#ifdef FLAT
    vec3 vViewPos;
#endif
    flat int vPrimBase;
} o;
void main() {
    int prim = vPrimBase[0] + gl_PrimitiveIDIn;
    for (int i = 0; i < 3; ++i) {
        o.vNormalCurv = vNormalCurv[i];
#ifdef FLAT
        o.vViewPos = vViewPos[i];
#endif
#ifdef PAINT
        o.vPaint = vPaint[i];
#endif
        o.vUv = texelFetch(uUvs, prim * 3 + i).xy;
#ifdef NORMALMAP
        vec4 t = texelFetch(uTangents, prim * 3 + i);
        o.vTangent = vec4(mat3(uView) * t.xyz, t.w);
#endif
        o.vPrimBase = vPrimBase[0];
        gl_PrimitiveID = gl_PrimitiveIDIn;
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
)";

// Studio "clay" shading: wrapped key light, cool fill, rim, soft spec, and curvature-based
// cavity/ridge shading so sculpted detail reads at a glance. The view direction comes from the pixel's
// position (uViewport, uProjScale) rather than an interpolated position, which saves the vertices a varying.
const char* kMeshFS = R"(
#ifdef TEXTURED
in FragData {
    vec4 vNormalCurv;
#ifdef PAINT
    vec4 vPaint;
#endif
    vec2 vUv;
#ifdef NORMALMAP
    vec4 vTangent;
#endif
#ifdef FLAT
    vec3 vViewPos;
#endif
    flat int vPrimBase;
};
uniform sampler2D uPaintTex; // sRGB texture: samples come back linear
#ifdef PBR
uniform sampler2D uOrmTex;      // PBR view: AO, roughness, metallic, opacity (linear)
uniform sampler2D uEmissiveTex; // PBR view: emissive (sRGB)
uniform int uHasOrm;
uniform int uHasEmissive;
#endif
#ifdef NORMALMAP
uniform sampler2D uNormalTex; // baked tangent-space normal map (linear RGB)
uniform int uHasPaint;        // 1: a painted texture is bound to uPaintTex too
uniform int uNormalFlipY;     // 1: DirectX convention (green down)
#endif
#else
#ifndef MICRO
in vec4 vNormalCurv;
#endif
#ifdef FLAT
in vec3 vViewPos;
#endif
#ifdef PAINT
in vec4 vPaint;
#endif
#ifdef PRIM
flat in int vPrimBase;
#endif
#endif
#ifdef PAINT
#define V_COLOR vPaint.rgb
#define V_MASK vPaint.a
#else
#define V_COLOR vec3(1.0)
#define V_MASK 0.0
#endif
#ifdef MICRO
// Micro-displacement levels (render/MicroRenderer.cpp): the full-resolution shading grid of the base triangle,
// interpolated on the micro triangle under the pixel (normal, curvature, mask), whatever level the geometry drew.
flat in int vSlot;
in vec2 vIJ;
uniform samplerBuffer uShade;
uniform int uN;
uniform int uSamples;
uniform mat4 uView;
vec4 gNC;
float gMask;
vec4 shadeAt(int i, int j) { return texelFetch(uShade, vSlot * uSamples + j * (uN + 1) - (j * (j - 1)) / 2 + i); }
vec3 octNormal(vec2 v) {
    vec2 e = v * 2.0 - 1.0;
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
void microFetch() {
    float fi = clamp(vIJ.x, 0.0, float(uN)), fj = clamp(vIJ.y, 0.0, float(uN));
    int i0 = min(int(fi), uN - 1), j0 = min(int(fj), uN - 1);
    if (i0 + j0 > uN - 1) {
        if (fi >= fj) i0 = uN - 1 - j0;
        else j0 = uN - 1 - i0;
    }
    float a = fi - float(i0), b = fj - float(j0);
    vec4 s0, s1, s2;
    vec3 w;
    if (a + b <= 1.0 || i0 + j0 + 2 > uN) {
        s0 = shadeAt(i0, j0); s1 = shadeAt(i0 + 1, j0); s2 = shadeAt(i0, j0 + 1);
        w = vec3(1.0 - a - b, a, b);
    } else {
        s0 = shadeAt(i0 + 1, j0); s1 = shadeAt(i0 + 1, j0 + 1); s2 = shadeAt(i0, j0 + 1);
        w = vec3(1.0 - b, a + b - 1.0, 1.0 - a);
    }
    w = clamp(w, 0.0, 1.0);
    vec3 n = octNormal(s0.xy) * w.x + octNormal(s1.xy) * w.y + octNormal(s2.xy) * w.z;
    vec3 c3 = (vec3(s0.z, s1.z, s2.z) * 255.0 - 128.0) / 127.0;
    float c = dot(c3, w);
    gNC = vec4(mat3(uView) * normalize(n), c * abs(c));
    gMask = dot(vec3(s0.w, s1.w, s2.w), w);
}
#define vNormalCurv gNC
#undef V_MASK
#define V_MASK gMask
#endif
uniform vec4 uViewport;  // the view's rectangle in window pixels
uniform vec2 uProjScale; // projection x and y scale
uniform vec3 uBaseColor;
uniform vec3 uTintShift; // the material tint less 1 (0, the default, is no tint): the active colour, no vertex paint
uniform int uPaint;      // 1: vertex colours replace the base colour
uniform int uShowMask;   // 1: darken masked areas
uniform int uGroups;     // 0: off, 1: per-triangle ids from uGroupIds, 2: everything is group 0, 3: per-triangle
                         // colours (0xBBGGRR) from uGroupIds (the UV Editor's overlay)
uniform usamplerBuffer uGroupIds;
uniform int uChecker;        // 1: a checker on the UVs (TEXTURED), tinted per UDIM tile
uniform float uCheckerScale; // checker squares per UV unit
uniform int uMatcap;          // 1: shade with the matcap texture instead of the studio lights
uniform sampler2D uMatcapTex; // sRGB-encoded lit sphere (core/Matcap.h)
uniform vec2 uMatcapCavity;   // crease darkening, ridge lift (sf::matcapCavity)
uniform int uDim;             // 1: an inactive SubTool, shown a little darker and greyer
uniform int uTwoSided;        // 1: back faces shade with the normal turned towards the viewer; 0: the motion proxy,
                              // whose folds face backwards on the outside of the surface
out vec4 fragColor;
vec4 finish(vec3 c) {
    if (uDim == 1) c = mix(c, vec3(dot(c, vec3(0.2126, 0.7152, 0.0722))), 0.25) * 0.72;
    return vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
// Keep in sync with sf::polygroupColor.
vec3 groupColor(uint id) {
    uint h = id * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    float hue = float(h & 0xFFFFu) / 65536.0;
    float sat = 0.45 + 0.25 * float((h >> 16) & 0xFFu) / 255.0;
    float val = 0.70 + 0.25 * float((h >> 24) & 0xFFu) / 255.0;
    vec3 rgb = clamp(abs(fract(hue + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return val * mix(vec3(1.0), rgb, sat);
}
vec3 faceColor(uint c) {
    return pow(vec3(float(c & 255u), float((c >> 8) & 255u), float((c >> 16) & 255u)) / 255.0, vec3(2.2));
}
// The UV checker: light and dark squares, the whole tile tinted by its UDIM number so tiles tell apart.
vec3 checkerColor(vec2 uv) {
    vec2 cell = floor(uv * uCheckerScale);
    float odd = mod(cell.x + cell.y, 2.0);
    vec2 tile = floor(uv);
    float hue = fract(0.13 + 0.37 * (tile.x + 10.0 * tile.y));
    vec3 tint = clamp(abs(fract(hue + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    vec3 light = mix(vec3(0.86), tint, 0.18), dark = mix(vec3(0.16), tint * 0.45, 0.35);
    return mix(light, dark, odd);
}
// The unit vector from the pixel's point towards the eye, in view space.
vec3 viewRay() {
    vec2 ndc = (gl_FragCoord.xy - uViewport.xy) / uViewport.zw * 2.0 - 1.0;
    return normalize(vec3(-ndc / uProjScale, 1.0));
}
#ifdef PBR
// PBR viewport (core/EnvironmentMath.h): Cook-Torrance GGX through the split sum (the environment prefiltered per
// roughness level in the mips of uEnvTex, the BRDF's scale and bias in uBrdfLut), diffuse from the environment's
// SH9 (already cosine-convolved and divided by pi), exposure, then a tone map. Directions are turned from view
// space into the environment's with uViewToEnv (the inverse view rotation and the environment's yaw).
uniform sampler2D uEnvTex;
uniform sampler2D uBrdfLut;
uniform vec3 uSH[9];
uniform mat3 uViewToEnv;
uniform float uEnvLevels; // the roughest level's mip
uniform float uExposure;  // stops
uniform int uToneMap;     // sf::ToneMap: 0 ACES, 1 AgX, 2 Reinhard, 3 none
uniform vec2 uMaterial;   // roughness, metallic of a mesh without a PBR texture
uniform float uPbrCavity; // how much creases darken (the sculpt's curvature), 0 = none
vec3 envSample(vec3 dirView, float lod) {
    vec3 d = uViewToEnv * dirView;
    vec2 uv = vec2(atan(d.x, -d.z) / 6.2831853 + 0.5, acos(clamp(d.y, -1.0, 1.0)) / 3.14159265);
    return textureLod(uEnvTex, uv, lod).rgb;
}
vec3 shIrradiance(vec3 dirView) {
    vec3 d = uViewToEnv * dirView;
    vec3 r = uSH[0] * 0.282095 + uSH[1] * (0.488603 * d.y) + uSH[2] * (0.488603 * d.z) + uSH[3] * (0.488603 * d.x) +
             uSH[4] * (1.092548 * d.x * d.y) + uSH[5] * (1.092548 * d.y * d.z) + uSH[6] * (0.315392 * (3.0 * d.z * d.z - 1.0)) +
             uSH[7] * (1.092548 * d.x * d.z) + uSH[8] * (0.546274 * (d.x * d.x - d.y * d.y));
    return max(r, vec3(0.0));
}
// Keep in sync with sf::toneMap (core/EnvironmentMath.cpp).
vec3 toneMapped(vec3 c) {
    c = max(c, vec3(0.0));
    if (uToneMap == 0) return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    if (uToneMap == 1) {
        mat3 inset = mat3(0.842479, 0.0423282, 0.0423756, 0.0784336, 0.878468, 0.0784336, 0.0792237, 0.0791661, 0.879142);
        mat3 outset = mat3(1.19688, -0.0528968, -0.0529716, -0.0980209, 1.15190, -0.0980435, -0.0990297, -0.0989612, 1.15107);
        vec3 x = inset * c;
        x = clamp((log2(max(x, vec3(1e-10))) + 12.47393) / 16.5, 0.0, 1.0);
        const float p = 0.6060606;
        vec3 lo = 0.5 * pow(x / p, vec3(2.2)), hi = 1.0 - 0.5 * pow((1.0 - x) / (1.0 - p), vec3(1.6));
        x = mix(lo, hi, step(vec3(p), x));
        return clamp(pow(max(outset * x, vec3(0.0)), vec3(2.2)), 0.0, 1.0);
    }
    if (uToneMap == 2) {
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        return l > 0.0 ? clamp(c * (l * (1.0 + l / 36.0) / (1.0 + l) / l), 0.0, 1.0) : vec3(0.0);
    }
    return clamp(c, 0.0, 1.0);
}
vec3 pbrShade(vec3 albedo, vec3 n, vec3 v, float rough, float metal, float ao, float curvature, vec3 emissive) {
    rough = clamp(rough, 0.03, 1.0);
    metal = clamp(metal, 0.0, 1.0);
    float NdotV = max(dot(n, v), 1e-4);
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 F = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - NdotV, 5.0);
    vec2 ab = texture(uBrdfLut, vec2(NdotV, rough)).rg;
    vec3 irr = shIrradiance(n);
    vec3 spec = envSample(reflect(-v, n), rough * uEnvLevels) * (F0 * ab.x + ab.y);
    // Multiple scattering (Fdez-Aguera 2019): rough metals keep the energy single scattering loses.
    float ess = ab.x + ab.y;
    spec += (F0 * ab.x + ab.y) * (1.0 - ess) / max(ess, 1e-3) * F0 * irr;
    vec3 diffuse = (1.0 - F) * (1.0 - metal) * albedo * irr;
    float specOcc = clamp(pow(NdotV + ao, exp2(-16.0 * rough - 1.0)) - 1.0 + ao, 0.0, 1.0); // Lagarde & de Rousiers
    float cavity = 1.0 - uPbrCavity * 0.6 * smoothstep(0.0, 0.16, curvature);
    return ((diffuse * ao + spec * specOcc) * cavity + emissive) * exp2(uExposure);
}
#endif
void main() {
#ifdef MICRO
    microFetch();
#endif
    vec3 tint = vec3(1.0) + uTintShift; // multiplies a matcap: polypaint or polygroup colours, or the active colour
    vec3 albedo = uBaseColor * tint;
#ifdef PRIM
    if (uGroups == 3) albedo = tint = faceColor(texelFetch(uGroupIds, vPrimBase + gl_PrimitiveID).r);
    else if (uGroups == 1) albedo = tint = groupColor(texelFetch(uGroupIds, vPrimBase + gl_PrimitiveID).r);
    else
#endif
    if (uGroups == 2 || uGroups == 1) albedo = tint = groupColor(0u);
    else if (uPaint == 1) albedo = tint = V_COLOR;
#if defined(NORMALMAP)
    if (uChecker == 1) albedo = tint = (uGroups == 3 ? albedo : vec3(1.0)) * checkerColor(vUv);
    else if (uGroups == 0 && uHasPaint == 1) albedo = tint = (uPaint == 1 ? V_COLOR : vec3(1.0)) * texture(uPaintTex, vUv).rgb;
#elif defined(TEXTURED)
    if (uChecker == 1) albedo = tint = (uGroups == 3 ? albedo : vec3(1.0)) * checkerColor(vUv);
    else if (uGroups == 0) albedo = tint = (uPaint == 1 ? V_COLOR : vec3(1.0)) * texture(uPaintTex, vUv).rgb;
#endif
#ifdef FLAT
    // The triangle's own normal, from its plane's screen-space slopes, turned towards the viewer: a back face (seen
    // through a hole) keeps it that way unless it is the motion proxy's, which shades back faces turned away.
    vec3 n = cross(dFdx(vViewPos), dFdy(vViewPos));
    n = dot(n, n) > 1e-30 ? normalize(n) : normalize(vNormalCurv.xyz);
    // Towards the viewer: against the pixel's view ray (in an orthographic view the eye can sit inside the
    // model, where the direction from it to the point would turn the facet the wrong way).
    if (dot(n, viewRay()) < 0.0) n = -n;
    if (!gl_FrontFacing && uTwoSided == 0) n = -n;
#else
    vec3 n = normalize(vNormalCurv.xyz);
#endif
    float curvature = vNormalCurv.w;
#ifdef NORMALMAP
    // MikkTSpace's pixel shader: bitangent rebuilt per pixel, n = normalize(tn.x T + tn.y B + tn.z N).
    vec3 tn = texture(uNormalTex, vUv).xyz * 2.0 - 1.0;
    if (uNormalFlipY == 1) tn.y = -tn.y;
    vec3 T = vTangent.xyz - n * dot(n, vTangent.xyz);
    T = dot(T, T) > 1e-12 ? normalize(T) : vec3(1.0, 0.0, 0.0);
    vec3 B = (vTangent.w < 0.0 ? -1.0 : 1.0) * cross(n, T);
    n = normalize(tn.x * T + tn.y * B + tn.z * n);
#endif
#ifndef FLAT
    if (!gl_FrontFacing && uTwoSided == 1) n = -n;
#endif
    vec3 v = viewRay();
#ifdef PBR
    {
        float rough = uMaterial.x, metal = uMaterial.y, ao = 1.0;
        vec3 emissive = vec3(0.0);
#ifdef TEXTURED
        if (uHasOrm == 1) {
            vec4 orm = texture(uOrmTex, vUv);
            ao = orm.r;
            rough = orm.g;
            metal = orm.b;
        }
        if (uHasEmissive == 1) emissive = texture(uEmissiveTex, vUv).rgb;
#endif
        vec3 c = toneMapped(pbrShade(albedo, n, v, rough, metal, ao, curvature, emissive));
        if (uShowMask == 1) c *= 1.0 - 0.75 * clamp(V_MASK, 0.0, 1.0);
        fragColor = finish(c);
        return;
    }
#endif
    if (uMatcap == 1) {
        // View-aligned basis so the material stays put towards the edges of a perspective view.
        vec3 bx = normalize(vec3(v.z, 0.0, -v.x));
        vec3 by = cross(v, bx);
        vec2 uv = vec2(dot(bx, n), dot(by, n)) * 0.495 + 0.5;
        vec3 mc = pow(texture(uMatcapTex, uv).rgb, vec3(2.2));
        // Starts above the curvature of a smooth sphere's own facets, so only real creases darken and a broad
        // stroke doesn't pool into a smear.
        float mcrease = smoothstep(0.03, 0.18, curvature);
        float mridge = smoothstep(0.03, 0.2, -curvature);
        vec3 c = mc * tint * (1.0 - uMatcapCavity.x * mcrease) * (1.0 + uMatcapCavity.y * mridge);
        if (uShowMask == 1) c *= 1.0 - 0.75 * clamp(V_MASK, 0.0, 1.0);
        fragColor = finish(c);
        return;
    }
    vec3 key = normalize(vec3(0.45, 0.65, 0.6));
    vec3 fill = normalize(vec3(-0.7, -0.1, 0.4));
    float diff = max((dot(n, key) + 0.25) / 1.25, 0.0);
    float fillL = max(dot(n, fill), 0.0) * 0.25;
    float hemi = 0.5 + 0.5 * n.y;
    vec3 h = normalize(key + v);
    float spec = pow(max(dot(n, h), 0.0), 40.0) * 0.18;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.3;
    float facing = smoothstep(-0.2, 0.6, dot(n, v));
    // curvature > 0 in creases, < 0 on ridges (see curvatureAt in Renderer.cpp).
    float crease = smoothstep(0.0, 0.16, curvature);
    float ridge = smoothstep(0.0, 0.2, -curvature);
    float cavity = 1.0 - 0.62 * crease;
    vec3 c = albedo * (0.12 * hemi + 0.95 * diff * diff + fillL * vec3(0.7, 0.8, 1.0)) * mix(0.7, 1.0, facing) * cavity;
    c *= mix(vec3(1.0), vec3(0.72, 0.55, 0.5), crease * 0.7); // warm, deeper colour in the cracks
    c += albedo * ridge * 0.3 * diff;                           // ridges catch the key light
    c += vec3(spec) * (1.0 - crease) + rim * vec3(0.8, 0.85, 1.0);
    if (uShowMask == 1) c *= 1.0 - 0.75 * clamp(V_MASK, 0.0, 1.0);
    fragColor = finish(c);
}
)";

// The wireframe: positions only. Every edge is drawn once; the old overlay drew each edge twice (once per
// triangle) at 0.35 opacity, which this alpha matches.
const char* kWireVS = R"(#version 330 core
layout(location = 0) in uvec2 aPos;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uGridOrigin;
uniform float uGridStep;
void main() {
    uvec3 q = uvec3(aPos.x & 0x1FFFFFu, (aPos.x >> 21) | ((aPos.y & 0x3FFu) << 11), (aPos.y >> 10) & 0x1FFFFFu);
    gl_Position = uProj * (uView * vec4(uGridOrigin + vec3(q) * uGridStep, 1.0));
}
)";

const char* kWireFS = R"(#version 330 core
uniform float uAlpha;
out vec4 fragColor;
void main() { fragColor = vec4(0.05, 0.05, 0.08, uAlpha); }
)";

// checkVertexIdBase: does gl_VertexID include the base vertex of glDrawElementsBaseVertex (it should)?
const char* kVertexIdVS = R"(#version 330 core
flat out uint vId;
void main() {
    vId = uint(gl_VertexID);
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)";
const char* kVertexIdFS = R"(#version 330 core
flat in uint vId;
out uint fragId;
void main() { fragId = vId; }
)";

const char* kBgVS = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}
)";

const char* kBgFS = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
// Neutral charcoal, lightest a little above the middle (behind the model) and falling off in a soft
// radial vignette down to the shade of the shelves around it, so the model reads against it without a
// coloured cast and the canvas meets the shell without a darker band. A little noise hides banding.
uniform vec3 uCentre; // RenderSettings::gradientCentre: vec3(0.215, 0.215, 0.222) in the Classic look
uniform vec3 uEdge;   // gradientEdge: the shell's shade (Theme shellBg #1b1b1d), so there's no step at the seams
void main() {
    vec3 centre = uCentre;
    vec3 edge = uEdge;
    float d = length((vUv - vec2(0.5, 0.56)) * vec2(1.0, 1.15));
    float t = smoothstep(0.05, 0.85, d);
    vec3 c = mix(centre, edge, t);
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    fragColor = vec4(c + (noise - 0.5) / 255.0, 1.0);
}
)";

// The PBR view's environment behind the model: the view ray of each pixel into the prefiltered environment at
// the background's blur, exposed and tone mapped like the shading.
const char* kEnvBgFS = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform mat4 uInvProj;
uniform float uLod;
uniform sampler2D uEnvTex;
uniform mat3 uViewToEnv;
uniform float uExposure;
uniform int uToneMap;
vec3 envSample(vec3 dirView, float lod) {
    vec3 d = uViewToEnv * dirView;
    vec2 uv = vec2(atan(d.x, -d.z) / 6.2831853 + 0.5, acos(clamp(d.y, -1.0, 1.0)) / 3.14159265);
    return textureLod(uEnvTex, uv, lod).rgb;
}
// Keep in sync with sf::toneMap (core/EnvironmentMath.cpp).
vec3 toneMapped(vec3 c) {
    c = max(c, vec3(0.0));
    if (uToneMap == 0) return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    if (uToneMap == 1) {
        mat3 inset = mat3(0.842479, 0.0423282, 0.0423756, 0.0784336, 0.878468, 0.0784336, 0.0792237, 0.0791661, 0.879142);
        mat3 outset = mat3(1.19688, -0.0528968, -0.0529716, -0.0980209, 1.15190, -0.0980435, -0.0990297, -0.0989612, 1.15107);
        vec3 x = inset * c;
        x = clamp((log2(max(x, vec3(1e-10))) + 12.47393) / 16.5, 0.0, 1.0);
        const float p = 0.6060606;
        vec3 lo = 0.5 * pow(x / p, vec3(2.2)), hi = 1.0 - 0.5 * pow((1.0 - x) / (1.0 - p), vec3(1.6));
        x = mix(lo, hi, step(vec3(p), x));
        return clamp(pow(max(outset * x, vec3(0.0)), vec3(2.2)), 0.0, 1.0);
    }
    if (uToneMap == 2) {
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        return l > 0.0 ? clamp(c * (l * (1.0 + l / 36.0) / (1.0 + l) / l), 0.0, 1.0) : vec3(0.0);
    }
    return clamp(c, 0.0, 1.0);
}
void main() {
    vec4 p = uInvProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = abs(p.w) > 1e-6 ? normalize(p.xyz / p.w) : vec3(0.0, 0.0, -1.0);
    vec3 c = toneMapped(envSample(dir, uLod) * exp2(uExposure));
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    fragColor = vec4(pow(c, vec3(1.0 / 2.2)) + (noise - 0.5) / 255.0, 1.0);
}
)";

// 3D Gaussian splats (EWA projection of each 3D covariance to a screen-space ellipse).
const char* kSplatVS = R"(#version 330 core
layout(location = 0) in uint aIndex;
uniform samplerBuffer uSplats;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec2 uViewport;
uniform float uOpacity;
out vec4 vColor;
out vec2 vOffset;
const vec2 kCorners[4] = vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
void main() {
    int base = int(aIndex) * 4;
    vec4 t0 = texelFetch(uSplats, base);     // centre, opacity
    vec4 t1 = texelFetch(uSplats, base + 1); // covariance xx xy xz yy
    vec4 t2 = texelFetch(uSplats, base + 2); // covariance yz zz
    vec4 t3 = texelFetch(uSplats, base + 3); // colour
    vec4 cam = uView * vec4(t0.xyz, 1.0);
    vec4 clip = uProj * cam;
    float depth = -cam.z;
    if (depth <= 0.0 || abs(clip.x) > 1.3 * clip.w || abs(clip.y) > 1.3 * clip.w) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }
    mat3 sigma = mat3(t1.x, t1.y, t1.z, t1.y, t1.w, t2.x, t1.z, t2.x, t2.y);
    vec2 focal = vec2(uProj[0][0], uProj[1][1]) * uViewport * 0.5;
    mat3 J = mat3(focal.x / depth, 0.0, 0.0,
                  0.0, focal.y / depth, 0.0,
                  focal.x * cam.x / (depth * depth), focal.y * cam.y / (depth * depth), 0.0);
    mat3 T = J * mat3(uView);
    mat3 cov = T * sigma * transpose(T);
    float a = cov[0][0] + 0.3, b = cov[0][1], c = cov[1][1] + 0.3;
    float mid = 0.5 * (a + c);
    float rad = length(vec2(0.5 * (a - c), b));
    float l1 = mid + rad, l2 = max(mid - rad, 0.1);
    vec2 axis1 = abs(b) > 1e-6 ? normalize(vec2(b, l1 - a)) : (a >= c ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    vec2 axis2 = vec2(-axis1.y, axis1.x);
    vec2 corner = kCorners[gl_VertexID];
    vec2 px = corner.x * 3.0 * min(sqrt(l1), 1024.0) * axis1 + corner.y * 3.0 * min(sqrt(l2), 1024.0) * axis2;
    gl_Position = clip + vec4(px / uViewport * 2.0 * clip.w, 0.0, 0.0);
    vOffset = corner * 3.0;
    vColor = vec4(t3.rgb, t0.w * uOpacity);
}
)";

const char* kSplatFS = R"(#version 330 core
in vec4 vColor;
in vec2 vOffset;
out vec4 fragColor;
void main() {
    float alpha = min(vColor.a * exp(-0.5 * dot(vOffset, vOffset)), 0.99);
    if (alpha < 1.0 / 255.0) discard;
    fragColor = vec4(vColor.rgb, alpha);
}
)";

constexpr GLenum kTexture7 = 0x84C7; // GL_TEXTURE7 (the cluster table)
constexpr GLenum kTexture2 = 0x84C2; // GL_TEXTURE2 (matcap)
constexpr GLenum kTexture5 = 0x84C5; // kTexture5 (baked normal map)
constexpr GLenum kTexture6 = 0x84C6; // kTexture6 (its tangents)
constexpr GLenum kRgb = 0x1907, kRgb8 = 0x8051; // GL_RGB, GL_RGB8
// PBR view: ORM 8, emissive 9, environment 10, BRDF table 11.
constexpr GLenum kTexture8 = 0x84C8, kTexture9 = 0x84C9, kTexture10 = 0x84CA, kTexture11 = 0x84CB;
constexpr GLenum kRgb16f = 0x881B, kRg16f = 0x822F, kRg = 0x8227, kRepeat = 0x2901, kMaxLevel = 0x813D;

GLuint compile(GLenum type, const char* src, std::string& error, const char* prefix = nullptr) {
    GLuint s = gl::CreateShader(type);
    const char* sources[2] = {prefix, src};
    if (prefix) gl::ShaderSource(s, 2, sources, nullptr);
    else gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        error = std::string("Shader compile error: ") + log;
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}

// `fsPrefix` goes before the fragment source (the #version line and defines); `gs` is an optional geometry shader.
GLuint link(const char* vs, const char* fs, std::string& error, const char* fsPrefix = nullptr, const char* gs = nullptr) {
    GLuint v = compile(GL_VERTEX_SHADER, vs, error);
    if (!v) return 0;
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, error, fsPrefix);
    if (!f) {
        gl::DeleteShader(v);
        return 0;
    }
    GLuint g = gs ? compile(GL_GEOMETRY_SHADER, gs, error) : 0;
    if (gs && !g) {
        gl::DeleteShader(v);
        gl::DeleteShader(f);
        return 0;
    }
    GLuint p = gl::CreateProgram();
    gl::AttachShader(p, v);
    gl::AttachShader(p, f);
    if (g) gl::AttachShader(p, g);
    gl::LinkProgram(p);
    gl::DeleteShader(v);
    gl::DeleteShader(f);
    if (g) gl::DeleteShader(g);
    GLint ok = 0;
    gl::GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetProgramInfoLog(p, sizeof log, nullptr, log);
        error = std::string("Shader link error: ") + log;
        gl::DeleteProgram(p);
        return 0;
    }
    return p;
}

} // namespace

glm::vec3 polygroupColor(uint32_t id) {
    uint32_t h = id * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    const float hue = float(h & 0xFFFFu) / 65536.0f;
    const float sat = 0.45f + 0.25f * float((h >> 16) & 0xFFu) / 255.0f;
    const float val = 0.70f + 0.25f * float((h >> 24) & 0xFFu) / 255.0f;
    const glm::vec3 rgb = glm::clamp(glm::abs(glm::fract(hue + glm::vec3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f,
                                     0.0f, 1.0f);
    return val * glm::mix(glm::vec3(1.0f), rgb, sat);
}

bool Renderer::init(std::string& error) {
    for (int f : {0, kPaint}) // the common ones up front, so a broken driver fails here rather than mid-session
        if (!meshProgram(f)) {
            error = programError_;
            return false;
        }
    wireProgram_ = link(kWireVS, kWireFS, error);
    if (!wireProgram_) return false;
    bgProgram_ = link(kBgVS, kBgFS, error);
    if (!bgProgram_) return false;
    splatProgram_ = link(kSplatVS, kSplatFS, error);
    if (!splatProgram_) return false;
    gl::GenVertexArrays(1, &splatVao_);
    gl::GenBuffers(1, &splatData_);
    gl::GenBuffers(1, &splatOrder_);
    gl::GenTextures(1, &splatTexture_);
    gl::BindVertexArray(splatVao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, splatOrder_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(uint32_t), nullptr);
    gl::VertexAttribDivisor(0, 1);
    gl::BindVertexArray(0);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);

    gl::GenVertexArrays(1, &emptyVao_);

    // Built-in matcaps, generated once (256 KB each); custom ones upload in syncMatcaps.
    matcapTextures_.assign(size_t(builtinMatcapCount()), 0);
    matcapHashes_.assign(size_t(builtinMatcapCount()), 0);
    gl::GenTextures(GLsizei(matcapTextures_.size()), matcapTextures_.data());
    for (int i = 0; i < builtinMatcapCount(); ++i) {
        constexpr int kSize = 256;
        uploadMatcap(matcapTextures_[size_t(i)], kSize, generateMatcap(i, kSize));
    }
    syncMatcaps();
    // SF_NO_VERTEXID_BASE=1 takes the other path, to check it on drivers that don't need it.
    vertexIdHasBase_ = checkVertexIdBase() && !std::getenv("SF_NO_VERTEXID_BASE");
    return true;
}

const Renderer::MeshProgram* Renderer::meshProgram(int features) {
    if (features & (kTextured | kNormalMap)) features |= kTextured | kPrim;
    // (micro levels: no per-vertex paint, per-triangle data or predictive preview)
    if (features & kMicro) features &= kMicro | kFlat | kPbr;
    MeshProgram& p = programs_[features & 255];
    if (p.id) return &p;
    std::string prefix = "#version 330 core\n";
    if (features & kMicro) prefix += "#define MICRO\n";
    if (features & kPaint) prefix += "#define PAINT\n";
    if (features & kPrim) prefix += "#define PRIM\n";
    if (features & kTextured) prefix += "#define TEXTURED\n";
    if (features & kNormalMap) prefix += "#define NORMALMAP\n";
    if (features & kFlat) prefix += "#define FLAT\n";
    if (features & kGhost) prefix += "#define GHOST\n";
    if (features & kPbr) prefix += "#define PBR\n";
    const std::string vs = prefix + ((features & kMicro) ? microVertexShaderSource() : kMeshVS), fs = prefix + kMeshFS,
                      gs = prefix + kMeshGS;
    p.id = link(vs.c_str(), fs.c_str(), programError_, nullptr, (features & kTextured) ? gs.c_str() : nullptr);
    if (!p.id) return nullptr;
    auto at = [&](const char* name) { return gl::GetUniformLocation(p.id, name); };
    p.view = at("uView");
    p.proj = at("uProj");
    p.gridOrigin = at("uGridOrigin");
    p.gridStep = at("uGridStep");
    p.viewport = at("uViewport");
    p.projScale = at("uProjScale");
    p.baseColor = at("uBaseColor");
    p.tintShift = at("uTintShift");
    p.paint = at("uPaint");
    p.showMask = at("uShowMask");
    p.groups = at("uGroups");
    p.matcap = at("uMatcap");
    p.matcapCavity = at("uMatcapCavity");
    p.dim = at("uDim");
    p.primBase = at("uPrimBase");
    p.hasPaint = at("uHasPaint");
    p.normalFlipY = at("uNormalFlipY");
    p.twoSided = at("uTwoSided");
    p.checker = at("uChecker");
    p.checkerScale = at("uCheckerScale");
    p.ghostGroups = at("uGhostGroups");
    p.ghostRange = at("uGhostRange");
    p.ghostEdge = at("uGhostEdge");
    p.ghostA = at("uGhostA");
    p.ghostB = at("uGhostB");
    p.ghostC = at("uGhostC");
    p.ghostBound = at("uGhostBound");
    p.ghostKind = at("uGhostKind");
    p.ghostFocal = at("uGhostFocal");
    p.hasOrm = at("uHasOrm");
    p.hasEmissive = at("uHasEmissive");
    p.envLevels = at("uEnvLevels");
    p.exposure = at("uExposure");
    p.toneMap = at("uToneMap");
    p.material = at("uMaterial");
    p.pbrCavity = at("uPbrCavity");
    p.viewToEnv = at("uViewToEnv");
    p.sh = at("uSH");
    // Texture units: groups 1, matcap 2, UVs 3, painted texture 4, normal map 5, tangents 6, cluster table 7.
    gl::UseProgram(p.id);
    gl::Uniform1i(at("uGroupIds"), 1);
    gl::Uniform1i(at("uMatcapTex"), 2);
    gl::Uniform1i(at("uUvs"), 3);
    gl::Uniform1i(at("uPaintTex"), 4);
    gl::Uniform1i(at("uNormalTex"), 5);
    gl::Uniform1i(at("uTangents"), 6);
    gl::Uniform1i(at("uClusterPrims"), 7);
    gl::Uniform1i(at("uOrmTex"), 8);
    gl::Uniform1i(at("uEmissiveTex"), 9);
    gl::Uniform1i(at("uEnvTex"), 10);
    gl::Uniform1i(at("uBrdfLut"), 11);
    gl::UseProgram(0);
    return &p;
}

// Draws one point with index 2 and base vertex 1000 into a 1x1 integer target and reads back the gl_VertexID
// the shader saw: 1002 as the spec says, or 2 on drivers that leave the base out (then the per-triangle data of
// polygroups and textures is drawn cluster by cluster with the base as a uniform).
bool Renderer::checkVertexIdBase() {
    std::string error;
    const GLuint prog = link(kVertexIdVS, kVertexIdFS, error);
    if (!prog) return false;
    GLuint fbo = 0, rb = 0, vao = 0, ibo = 0;
    gl::GenFramebuffers(1, &fbo);
    gl::GenRenderbuffers(1, &rb);
    gl::BindRenderbuffer(GL_RENDERBUFFER, rb);
    gl::RenderbufferStorage(GL_RENDERBUFFER, GL_R32UI, 1, 1);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    uint32_t id = 0;
    if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        gl::GenVertexArrays(1, &vao);
        gl::GenBuffers(1, &ibo);
        gl::BindVertexArray(vao);
        const uint16_t indices[4] = {0, 1, 2, 3};
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW);
        gl::Viewport(0, 0, 1, 1);
        gl::Disable(GL_DEPTH_TEST);
        gl::UseProgram(prog);
        gl::DrawElementsBaseVertex(GL_POINTS, 1, GL_UNSIGNED_SHORT, reinterpret_cast<const void*>(2 * sizeof(uint16_t)), 1000);
        gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl::ReadPixels(0, 0, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &id);
        gl::UseProgram(0);
        gl::BindVertexArray(0);
        gl::DeleteBuffers(1, &ibo);
        gl::DeleteVertexArrays(1, &vao);
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(1, &fbo);
    gl::DeleteRenderbuffers(1, &rb);
    gl::DeleteProgram(prog);
    for (int i = 0; i < 16 && gl::GetError() != GL_NO_ERROR; ++i) {
    }
    return id == 1002;
}

void Renderer::uploadMatcap(GLuint texture, int size, const std::vector<uint8_t>& pixels) {
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::BindTexture(GL_TEXTURE_2D, texture);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    gl::BindTexture(GL_TEXTURE_2D, 0);
}

// Custom MatCaps (core/Matcap.h) added or changed since the last call: a texture each, at their own size.
void Renderer::syncMatcaps() {
    if (matcapTextures_.empty() || matcapRevision_ == matcapRevision()) return;
    matcapRevision_ = matcapRevision();
    for (int i = builtinMatcapCount(); i < matcapCount(); ++i) {
        const CustomMatcap* c = customMatcap(i);
        if (size_t(i) >= matcapTextures_.size()) {
            GLuint t = 0;
            gl::GenTextures(1, &t);
            matcapTextures_.push_back(t);
            matcapHashes_.push_back(0);
        }
        if (matcapHashes_[size_t(i)] == c->hash) continue;
        uploadMatcap(matcapTextures_[size_t(i)], c->size, c->rgba);
        matcapHashes_[size_t(i)] = c->hash;
    }
}

GLuint Renderer::matcapTexture(int index) const {
    return index >= 0 && index < int(matcapTextures_.size()) ? matcapTextures_[size_t(index)] : 0;
}

Renderer::MeshSlot& Renderer::slot(uint32_t id) {
    for (auto& s : slots_)
        if (s.id == id) return s;
    MeshSlot& s = slots_.emplace_back();
    s.id = id;
    gl::GenVertexArrays(1, &s.vao);
    gl::GenVertexArrays(1, &s.lineVao);
    gl::GenBuffers(1, &s.vertexBuffer);
    gl::GenBuffers(1, &s.indexBuffer);
    gl::GenBuffers(1, &s.lineBuffer);
    gl::GenBuffers(1, &s.colorBuffer);
    gl::GenBuffers(1, &s.maskBuffer);
    gl::GenBuffers(1, &s.groupData);
    gl::GenTextures(1, &s.groupTexture);
    gl::GenBuffers(1, &s.primData);
    gl::GenTextures(1, &s.primTexture);
    gl::GenBuffers(1, &s.uvData);
    gl::GenTextures(1, &s.uvTexture);
    gl::GenVertexArrays(1, &s.proxyVao);
    gl::GenBuffers(1, &s.proxyBuffer);
    for (GLuint vao : {s.vao, s.lineVao, s.proxyVao}) {
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, s.vertexBuffer);
        gl::EnableVertexAttribArray(0);
        gl::VertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(PackedVertex), nullptr);
        if (vao == s.lineVao) {
            gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.lineBuffer);
            continue;
        }
        gl::EnableVertexAttribArray(1);
        gl::VertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(PackedVertex), reinterpret_cast<const void*>(2 * sizeof(uint32_t)));
        // Colour (2) and mask (3) arrays are only enabled while the mesh has them.
        gl::BindBuffer(GL_ARRAY_BUFFER, s.colorBuffer);
        gl::VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 4, nullptr);
        gl::BindBuffer(GL_ARRAY_BUFFER, s.maskBuffer);
        gl::VertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_TRUE, 1, nullptr);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vao == s.proxyVao ? s.proxyBuffer : s.indexBuffer);
    }
    gl::BindVertexArray(0);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    return s;
}

void Renderer::freeSlot(MeshSlot& s) {
    for (GLuint* b : {&s.vertexBuffer, &s.indexBuffer, &s.lineBuffer, &s.proxyBuffer, &s.colorBuffer, &s.maskBuffer, &s.groupData,
                      &s.primData, &s.uvData})
        gl::DeleteBuffers(1, b);
    gl::DeleteTextures(1, &s.groupTexture);
    gl::DeleteTextures(1, &s.primTexture);
    gl::DeleteTextures(1, &s.uvTexture);
    if (s.paintTexture) gl::DeleteTextures(1, &s.paintTexture);
    if (s.normalTexture) gl::DeleteTextures(1, &s.normalTexture);
    for (GLuint* t : {&s.ormTexture, &s.matNormalTexture, &s.emissiveTexture})
        if (*t) gl::DeleteTextures(1, t);
    if (s.tangentData) gl::DeleteBuffers(1, &s.tangentData);
    if (s.tangentTexture) gl::DeleteTextures(1, &s.tangentTexture);
    gl::DeleteVertexArrays(1, &s.vao);
    gl::DeleteVertexArrays(1, &s.lineVao);
    gl::DeleteVertexArrays(1, &s.proxyVao);
    s.micro.reset();
    if (s.patchVao) {
        for (GLuint* b : {&s.patchVertices, &s.patchIndices, &s.patchLines, &s.patchColors, &s.patchMask, &s.patchGroupData})
            gl::DeleteBuffers(1, b);
        gl::DeleteTextures(1, &s.patchGroupTexture);
        gl::DeleteVertexArrays(1, &s.patchVao);
        gl::DeleteVertexArrays(1, &s.patchLineVao);
    }
    s = MeshSlot();
}

size_t Renderer::gpuBytes() const {
    size_t total = 0;
    for (const MeshSlot& s : slots_) {
        for (size_t b : s.bytes) total += b;
        total += s.patchBytes;
        if (s.proxyAllocated) total += (s.order.triangleCount() / 4) * 3 * sizeof(uint16_t);
        if (s.paintTexture) total += size_t(s.textureW) * size_t(s.textureH) * 4 * 4 / 3; // with mipmaps
        total += size_t(std::popcount(s.pbrPlanes)) * size_t(s.textureW) * size_t(s.textureH) * 4 * 4 / 3;
        if (s.micro) total += s.micro->gpuBytes();
    }
    return total;
}

void Renderer::shutdown() {
    for (auto& s : slots_) freeSlot(s);
    slots_.clear();
    drawList_.clear();
    if (!matcapTextures_.empty()) gl::DeleteTextures(GLsizei(matcapTextures_.size()), matcapTextures_.data());
    matcapTextures_.clear();
    matcapHashes_.clear();
    matcapRevision_ = 0;
    for (GLuint* t : {&envTexture_, &brdfTexture_})
        if (*t) gl::DeleteTextures(1, t);
    envIndex_ = envFailedIndex_ = -1;
    if (envBgProgram_) gl::DeleteProgram(envBgProgram_);
    envBgProgram_ = 0;
    gl::DeleteVertexArrays(1, &emptyVao_);
    for (MeshProgram& p : programs_) {
        if (p.id) gl::DeleteProgram(p.id);
        p = MeshProgram();
    }
    gl::DeleteProgram(wireProgram_);
    gl::DeleteProgram(bgProgram_);
    gl::DeleteProgram(splatProgram_);
    gl::DeleteBuffers(1, &splatData_);
    gl::DeleteBuffers(1, &splatOrder_);
    gl::DeleteTextures(1, &splatTexture_);
    gl::DeleteVertexArrays(1, &splatVao_);
    if (viewFbo_) {
        gl::DeleteFramebuffers(1, &viewFbo_);
        gl::DeleteFramebuffers(1, &viewResolveFbo_);
        gl::DeleteRenderbuffers(3, viewRb_);
        viewFbo_ = viewResolveFbo_ = 0;
    }
}

void Renderer::syncSplats(const SplatCloud& c, uint64_t version) {
    if (version == splatVersion_) return;
    splatVersion_ = version;
    GLint maxTexels = 0;
    gl::GetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexels);
    const size_t n = std::min(c.size(), size_t(std::max(maxTexels, 0)) / 4);
    std::vector<glm::vec4> texels(n * 4);
    splatCenters_.resize(n);
    const glm::mat3 M(c.transform);
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 p = c.worldPosition(i);
        const glm::mat3 A = M * glm::mat3_cast(c.rotations[i]) *
                            glm::mat3(glm::vec3(c.scales[i].x, 0, 0), glm::vec3(0, c.scales[i].y, 0), glm::vec3(0, 0, c.scales[i].z));
        const glm::mat3 S = A * glm::transpose(A);
        splatCenters_[i] = p;
        texels[i * 4 + 0] = glm::vec4(p, c.opacities[i]);
        texels[i * 4 + 1] = glm::vec4(S[0][0], S[0][1], S[0][2], S[1][1]);
        texels[i * 4 + 2] = glm::vec4(S[1][2], S[2][2], 0.0f, 0.0f);
        texels[i * 4 + 3] = glm::vec4(glm::clamp(c.color(i), glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f);
    }
    gl::BindBuffer(GL_TEXTURE_BUFFER, splatData_);
    gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(texels.size() * sizeof(glm::vec4)), texels.data(), GL_STATIC_DRAW);
    gl::BindTexture(GL_TEXTURE_BUFFER, splatTexture_);
    gl::TexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, splatData_);
    gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
    splatCount_ = GLsizei(n);
    lastSortView_ = glm::mat4(0.0f); // force a re-sort
}

void Renderer::drawSplats(const glm::mat4& view, const glm::mat4& proj, int fbWidth, int fbHeight, float opacity) {
    if (splatCount_ == 0) return;
    // Back-to-front order, re-sorted only when the camera moves (16-bit counting sort).
    if (view != lastSortView_) {
        lastSortView_ = view;
        const size_t n = size_t(splatCount_);
        std::vector<float> depth(n);
        float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
        for (size_t i = 0; i < n; ++i) {
            const glm::vec3& p = splatCenters_[i];
            depth[i] = -(view[0][2] * p.x + view[1][2] * p.y + view[2][2] * p.z + view[3][2]);
            lo = std::min(lo, depth[i]);
            hi = std::max(hi, depth[i]);
        }
        const float scale = hi > lo ? 65535.0f / (hi - lo) : 0.0f;
        std::vector<uint32_t> count(65537, 0);
        std::vector<uint16_t> key(n);
        for (size_t i = 0; i < n; ++i) {
            key[i] = uint16_t(65535 - int((depth[i] - lo) * scale)); // far first
            ++count[size_t(key[i]) + 1];
        }
        for (size_t k = 1; k < count.size(); ++k) count[k] += count[k - 1];
        splatSorted_.resize(n);
        for (size_t i = 0; i < n; ++i) splatSorted_[count[key[i]]++] = uint32_t(i);
        gl::BindBuffer(GL_ARRAY_BUFFER, splatOrder_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(n * sizeof(uint32_t)), splatSorted_.data(), GL_STREAM_DRAW);
        gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    }
    gl::UseProgram(splatProgram_);
    gl::UniformMatrix4fv(gl::GetUniformLocation(splatProgram_, "uView"), 1, GL_FALSE, glm::value_ptr(view));
    gl::UniformMatrix4fv(gl::GetUniformLocation(splatProgram_, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
    gl::Uniform2f(gl::GetUniformLocation(splatProgram_, "uViewport"), float(fbWidth), float(fbHeight));
    gl::Uniform1f(gl::GetUniformLocation(splatProgram_, "uOpacity"), opacity);
    gl::Uniform1i(gl::GetUniformLocation(splatProgram_, "uSplats"), 0);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_BUFFER, splatTexture_);
    gl::Enable(GL_DEPTH_TEST);
    gl::DepthMask(GL_FALSE);
    gl::Enable(GL_BLEND);
    gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl::BindVertexArray(splatVao_);
    gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, splatCount_);
    gl::BindVertexArray(0);
    gl::Disable(GL_BLEND);
    gl::DepthMask(GL_TRUE);
    gl::BindTexture(GL_TEXTURE_BUFFER, 0);
}

namespace {

// Signed curvature at vertex `v`: how far the neighbour average sits above (crease, > 0) or below (ridge, < 0)
// the surface, relative to the local edge length (Mesh::curvatureAt).
float curvatureAt(const Mesh& mesh, uint32_t v) { return mesh.curvatureAt(v); }

using Runs = std::vector<std::pair<uint32_t, uint32_t>>;

// Uploads the GPU index ranges `runs` of one vertex array (`allocate` sizes it to `count` elements first):
// fill(begin, end, dst) writes those elements, in GPU order, to dst. The runs are cut into pieces of a few
// thousand elements filled in parallel (a dab's runs are short, so per run would be one thread), batched
// through `staging` so a full upload never copies the whole array at once. Pieces start on block boundaries.
// With `changed` (a flag per UploadOrder block, set by fill), only the blocks flagged are sent.
template <class Fill>
void uploadGathered(std::vector<uint8_t>& staging, GLuint buffer, size_t elementSize, size_t count, bool allocate,
                    const Runs& runs, const Fill& fill, const std::vector<uint8_t>* changed = nullptr) {
    constexpr uint32_t kPiece = 2048, kBatch = 1u << 18;
    gl::BindBuffer(GL_ARRAY_BUFFER, buffer);
    if (allocate) gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(count * elementSize), nullptr, GL_DYNAMIC_DRAW);
    struct Piece {
        uint32_t begin, end;
        size_t offset; // in staging, bytes
    };
    std::vector<Piece> pieces;
    auto flush = [&]() {
        if (pieces.empty()) return;
        poolFor(pieces.size(), [&](size_t i0, size_t i1) {
            for (size_t i = i0; i < i1; ++i) fill(pieces[i].begin, pieces[i].end, staging.data() + pieces[i].offset);
        }, 1, 2);
        // One upload per stretch of adjacent (changed) elements; adjacent pieces are adjacent in staging too.
        uint32_t from = 0, to = 0;
        size_t fromOffset = 0;
        auto send = [&]() {
            if (to > from)
                gl::BufferSubData(GL_ARRAY_BUFFER, GLintptr(size_t(from) * elementSize), GLsizeiptr(size_t(to - from) * elementSize),
                                  staging.data() + fromOffset);
            from = to = 0;
        };
        for (const Piece& p : pieces)
            for (uint32_t b = p.begin; b < p.end; b = std::min(p.end, (b & ~(UploadOrder::kBlock - 1)) + UploadOrder::kBlock)) {
                const uint32_t e = std::min(p.end, (b & ~(UploadOrder::kBlock - 1)) + UploadOrder::kBlock);
                if (changed && !(*changed)[b >> UploadOrder::kBlockShift]) continue;
                if (b != to || to == from) {
                    send();
                    from = b;
                    fromOffset = p.offset + size_t(b - p.begin) * elementSize;
                }
                to = e;
            }
        send();
        pieces.clear();
    };
    // A whole array: written straight into the mapped buffer, in parallel (no staging copy).
    if (allocate && runs.size() == 1 && runs[0].first == 0 && runs[0].second == count && count > 0) {
        if (auto* dst = static_cast<uint8_t*>(gl::MapBufferRange(GL_ARRAY_BUFFER, 0, GLsizeiptr(count * elementSize),
                                                                 GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT))) {
            poolFor(count, [&](size_t b, size_t e) { fill(uint32_t(b), uint32_t(e), dst + b * elementSize); }, kPiece);
            if (gl::UnmapBuffer(GL_ARRAY_BUFFER)) return;
            // (The data was lost, which drivers may do on a mode switch: sent the usual way below.)
        }
    }
    size_t used = 0;
    staging.resize(size_t(std::min<size_t>(kBatch, count)) * elementSize);
    for (const auto& [b, e] : runs)
        for (uint32_t c = b; c < e; c += kPiece) {
            const uint32_t ce = std::min(e, c + kPiece);
            if (used + (ce - c) > kBatch) {
                flush();
                used = 0;
            }
            if ((used + (ce - c)) * elementSize > staging.size()) staging.resize((used + (ce - c)) * elementSize);
            pieces.push_back({c, ce, used * elementSize});
            used += ce - c;
        }
    flush();
}

// fill() for uploadGathered: fn(mesh vertex) for every slot.
template <class T, class F>
auto gatherWith(std::span<const uint32_t> order, F fn) {
    return [order, fn](uint32_t b, uint32_t e, void* dst) {
        T* out = static_cast<T*>(dst);
        for (uint32_t i = b; i < e; ++i) out[i - b] = fn(order[i]);
    };
}

// The mesh's bounding box (every vertex), in parallel.
AABB meshBounds(const Mesh& mesh) {
    constexpr size_t kBlock = size_t(1) << 16;
    const size_t n = mesh.vertexCount();
    std::vector<AABB> parts((n + kBlock - 1) / kBlock);
    poolFor(n, [&](size_t b, size_t e) {
        AABB box;
        for (size_t i = b; i < e; ++i) box.expand(mesh.positions[i]);
        parts[b / kBlock] = box;
    }, kBlock);
    AABB box;
    for (const AABB& p : parts)
        if (p.valid()) box.expand(p);
    return box;
}

// Index of the edge a-b (either direction) in triangle `tri` (edge k joins corners k and k + 1), or -1.
int edgeIndex(const glm::uvec3& tri, uint32_t a, uint32_t b) {
    for (int k = 0; k < 3; ++k)
        if ((tri[k] == a && tri[(k + 1) % 3] == b) || (tri[k] == b && tri[(k + 1) % 3] == a)) return k;
    return -1;
}

// The edge of triangle t the wireframe leaves out: the diagonal inside its quad, unless the other half is
// hidden (then it is the visible outline). -1: none.
int hiddenDiagonal(const Mesh& mesh, uint32_t t) {
    const uint32_t u = mesh.quadPartnerAt(t);
    return u != Mesh::kNoQuad && !mesh.triangleHidden(u) ? mesh.quadDiagonal(t, u) : -1;
}

} // namespace

// The lighting of an environment is built on a worker thread (decode, prefilter, SH9: a few hundred ms); the view
// keeps the environment it has until the new one is ready (the very first one is waited for).
struct Renderer::EnvJob {
    int index = -1;
    std::atomic<bool> done{false};
    bool ok = false;
    EnvironmentLighting light;
};

bool Renderer::outOfMemory(uint32_t id) const {
    for (const MeshSlot& s : slots_)
        if (s.id == id) return s.outOfMemory;
    return false;
}

void Renderer::syncScene(const std::vector<SceneMesh>& scene) {
    syncMatcaps();
    if (envJob_ && envJob_->done) ensureEnvironment(envJob_->index); // (a new environment is ready: upload it)
    // Free the slots of SubTools that are gone (deleted, merged, a new scene).
    for (auto it = slots_.begin(); it != slots_.end();) {
        const bool live = std::any_of(scene.begin(), scene.end(), [&](const SceneMesh& m) { return m.id == it->id; });
        if (live) {
            ++it;
            continue;
        }
        freeSlot(*it);
        it = slots_.erase(it);
    }
    drawList_.clear();
    proxyPending_ = false;
    for (const SceneMesh& m : scene) {
        if (!m.visible || !m.mesh) continue; // hidden: keeps its dirty state until shown
        MeshSlot& ms = slot(m.id);
        ms.overlay = m.active ? m.overlay : nullptr;
        sync(ms, *m.mesh, m.normalMap, m.wireframe && m.active, m.pbr);
        drawList_.push_back({m.id, m.active, m.flat});
    }
}

// The packed vertices of dirtyRuns_ (all of them when `full`, which also sizes the buffer). A vertex that left
// the position grid (sculpting outwards) gets the mesh a new grid and every vertex re-sent.
void Renderer::uploadVertices(MeshSlot& s, const Mesh& mesh, bool full) {
    const size_t ns = s.order.size();
    const size_t nb = (ns + UploadOrder::kBlock - 1) / UploadOrder::kBlock;
    const std::span<const uint32_t> order = s.order.fromGpu();
    std::atomic<bool> outside{false};
    const bool normals = mesh.normals.size() == mesh.vertexCount();
    const uint32_t nv = uint32_t(mesh.vertexCount());
    // Per block, hashes of what it last sent (positions; normals and curvature): a block a dab marked but didn't
    // change (paint, mask) isn't sent again, and a cluster whose positions didn't move keeps its bounds and proxy.
    s.blockHash.resize(nb * 2, 0);
    s.blockBox.resize(nb);
    blockChanged_.assign(nb, 0);
    blockMoved_.assign(nb, 0);
    bool everything = full;
    auto fill = [&](uint32_t b, uint32_t e, void* dst) {
        PackedVertex* out = static_cast<PackedVertex*>(dst);
        bool inside = true;
        uint64_t hp = 0, hn = 0;
        AABB box;
        for (uint32_t i = b; i < e; ++i) {
            const uint32_t v = order[i];
            PackedVertex pv{}; // packed here and stored once: `out` may be mapped GPU memory, slow to read back
            if (v < nv) { // (else a vertex dynamic topology removed, which only patched clusters named)
                inside &= packVertex(s.grid, mesh.positions[v], normals ? mesh.normals[v] : glm::vec3(0.0f, 0.0f, 1.0f),
                                     curvatureAt(mesh, v), pv);
                box.expand(mesh.positions[v]);
            }
            out[i - b] = pv;
            hp = (hp ^ (uint64_t(pv.pos[0]) | uint64_t(pv.pos[1]) << 32)) * 0x100000001B3ull + i;
            hn = (hn ^ pv.normal) * 0x100000001B3ull + i;
            if ((i + 1) % UploadOrder::kBlock == 0 || i + 1 == e) {
                const size_t g = i >> UploadOrder::kBlockShift;
                blockMoved_[g] = everything || s.blockHash[g * 2] != hp;
                blockChanged_[g] = blockMoved_[g] || s.blockHash[g * 2 + 1] != hn;
                s.blockHash[g * 2] = hp;
                s.blockHash[g * 2 + 1] = hn;
                s.blockBox[g] = box;
                hp = hn = 0;
                box = AABB();
            }
        }
        if (!inside) outside.store(true, std::memory_order_relaxed);
    };
    uploadGathered(staging_, s.vertexBuffer, sizeof(PackedVertex), ns, full, dirtyRuns_, fill, &blockChanged_);
    if (outside.load()) {
        // A vertex left the grid: a new grid, and everything sent again.
        s.grid = PositionGrid::around(meshBounds(mesh));
        dirtyRuns_.assign(1, {0u, uint32_t(ns)});
        everything = true;
        uploadGathered(staging_, s.vertexBuffer, sizeof(PackedVertex), ns, false, dirtyRuns_, fill, &blockChanged_);
    }
    // The runs whose positions moved, for the culling bounds and motion proxies.
    movedRuns_.clear();
    for (const auto& [b, e] : dirtyRuns_)
        for (uint32_t g = b >> UploadOrder::kBlockShift; g < (e + UploadOrder::kBlock - 1) >> UploadOrder::kBlockShift; ++g) {
            if (!blockMoved_[g]) continue;
            const uint32_t lo = g << UploadOrder::kBlockShift, hi = std::min(uint32_t(ns), lo + UploadOrder::kBlock);
            if (!movedRuns_.empty() && movedRuns_.back().second == lo) movedRuns_.back().second = hi;
            else movedRuns_.emplace_back(lo, hi);
        }
    s.bytes[0] = ns * sizeof(PackedVertex);
}

// Culling bounds of the clusters movedRuns_ touches (every copy of a moved vertex is in a run, so every cluster
// with a moved triangle is), or of all.
void Renderer::updateBounds(MeshSlot& s, const Mesh& mesh, bool all, bool computed) {
    const auto clusters = s.order.clusters();
    s.bounds.resize(clusters.size());
    cullScratch_.clear();
    if (all) {
        for (uint32_t c = 0; c < clusters.size(); ++c) cullScratch_.push_back(c);
    } else {
        for (const auto& [b, e] : movedRuns_) {
            if (b >= e) continue;
            const uint32_t c0 = s.order.clusterOfSlot(b), c1 = s.order.clusterOfSlot(e - 1);
            for (uint32_t c = c0; c <= c1; ++c)
                if (cullScratch_.empty() || c > cullScratch_.back()) cullScratch_.push_back(c);
        }
    }
    s.proxyStale.resize(clusters.size(), 1);
    for (uint32_t c : cullScratch_) s.proxyStale[c] = 1;
    if (computed) cullScratch_.clear();
    poolFor(cullScratch_.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
            const uint32_t c = cullScratch_[i];
            const bool patched = c < s.clusterPatch.size() && s.clusterPatch[c] != ~0u; // (drawn by its patch)
            s.bounds[c] = patched ? ClusterBounds() : clusterBounds(mesh, s.order, c);
        }
    }, 1, 2);
    s.box = AABB();
    s.volume = 0.0;
    for (const ClusterBounds& b : s.bounds) s.volume += b.volume;
    for (const ClusterBounds& b : s.bounds)
        if (b.radius >= 0.0f) {
            s.box.expand(b.center - glm::vec3(b.radius));
            s.box.expand(b.center + glm::vec3(b.radius));
        }
}

// The index buffer: each cluster's drawn triangles (hidden ones left out) first in its part of the buffer.
void Renderer::uploadIndices(MeshSlot& s, const Mesh& mesh) {
    const size_t nt = mesh.triangleCount();
    const bool hidden = mesh.hidden.size() == nt && nt > 0;
    const size_t nc = s.order.clusters().size();
    s.drawCount.assign(nc, 0);
    gl::BindVertexArray(s.vao);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(nt * 3 * sizeof(uint16_t)), nullptr, GL_STATIC_DRAW);
    constexpr size_t kChunk = 64; // clusters per upload
    std::vector<uint16_t> staging;
    for (size_t c0 = 0; c0 < nc; c0 += kChunk) {
        const size_t c1 = std::min(nc, c0 + kChunk);
        const size_t t0 = s.order.firstTriangle(uint32_t(c0));
        const size_t t1 = s.order.firstTriangle(uint32_t(c1 - 1)) + s.order.clusterTriangles(uint32_t(c1 - 1));
        staging.resize((t1 - t0) * 3);
        poolFor(c1 - c0, [&](size_t i0, size_t i1) {
            thread_local std::vector<uint16_t> local;
            for (size_t i = i0; i < i1; ++i) {
                const uint32_t c = uint32_t(c0 + i);
                const size_t first = s.order.firstTriangle(c);
                const uint32_t count = s.order.clusterTriangles(c);
                uint16_t* out = staging.data() + (first - t0) * 3;
                if (c < s.clusterPatch.size() && s.clusterPatch[c] != ~0u) { // drawn by its patch
                    s.drawCount[c] = 0;
                    continue;
                }
                if (!hidden) {
                    s.order.localTriangles(mesh, c, out);
                    s.drawCount[c] = GLsizei(count * 3);
                    continue;
                }
                local.resize(count * 3);
                s.order.localTriangles(mesh, c, local.data());
                size_t drawn = 0;
                for (uint32_t t = 0; t < count; ++t) {
                    if (mesh.hidden[s.order.triangle(c, t)]) continue;
                    std::copy_n(local.data() + t * 3, 3, out + drawn * 3);
                    ++drawn;
                }
                s.drawCount[c] = GLsizei(drawn * 3);
            }
        }, 1, 2);
        gl::BufferSubData(GL_ELEMENT_ARRAY_BUFFER, GLintptr(t0 * 3 * sizeof(uint16_t)), GLsizeiptr(staging.size() * sizeof(uint16_t)),
                          staging.data());
    }
    gl::BindVertexArray(0);
}

// The index buffer (unless `indicesDone`), the first drawn triangle of each cluster, and the polygroup id and
// UVs of every drawn triangle in draw order (looked up by that plus gl_PrimitiveID). The wireframe's edges and
// motion proxies are rebuilt on next need.
void Renderer::uploadFaces(MeshSlot& s, const Mesh& mesh, bool wantUvs, bool indicesDone) {
    const size_t nt = mesh.triangleCount();
    const bool hidden = mesh.hidden.size() == nt && nt > 0;
    const bool overlay = s.overlay && s.overlay->colors.size() == nt && nt > 0;
    const bool groups = (mesh.polygroups.size() == nt || overlay) && nt > 0;
    const auto clusters = s.order.clusters();
    const size_t nc = clusters.size();
    s.primBase.assign(nc, 0);
    s.bytes[1] = nt * 3 * sizeof(uint16_t);
    if (!indicesDone) uploadIndices(s, mesh);
    size_t drawn = 0;
    for (size_t c = 0; c < nc; ++c) {
        s.primBase[c] = uint32_t(drawn);
        drawn += size_t(s.drawCount[c]) / 3;
    }
    s.drawnTriangles = drawn;
    s.anyHidden = hidden && drawn < nt;
    // Per GPU block (clusters are block aligned): its cluster's first drawn triangle.
    {
        std::vector<uint32_t> table((s.order.size() + UploadOrder::kBlock - 1) / UploadOrder::kBlock, 0);
        for (size_t c = 0; c < nc; ++c) {
            const size_t g0 = clusters[c].firstVertex / UploadOrder::kBlock;
            const size_t g1 = c + 1 < nc ? clusters[c + 1].firstVertex / UploadOrder::kBlock : table.size();
            std::fill(table.begin() + std::ptrdiff_t(g0), table.begin() + std::ptrdiff_t(g1), s.primBase[c]);
        }
        gl::BindBuffer(GL_TEXTURE_BUFFER, s.primData);
        gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(table.size() * sizeof(uint32_t)), table.data(), GL_STATIC_DRAW);
        gl::BindTexture(GL_TEXTURE_BUFFER, s.primTexture);
        gl::TexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, s.primData);
        gl::BindTexture(GL_TEXTURE_BUFFER, 0);
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
        s.bytes[6] = table.size() * sizeof(uint32_t);
    }

    // The drawn triangles in draw order (the clusters' order), for the per-triangle data.
    GLint maxTexels = 0;
    gl::GetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexels);
    const bool textured = wantUvs && mesh.hasUvs() && drawn * 3 <= size_t(std::max(maxTexels, 0));
    std::vector<uint32_t> drawnTris(groups || textured ? drawn : 0);
    if (!drawnTris.empty()) poolFor(nc, [&](size_t c0, size_t c1) {
        for (size_t c = c0; c < c1; ++c) {
            uint32_t* out = drawnTris.data() + s.primBase[c];
            if (s.drawCount[c] == 0) continue; // (nothing drawn: hidden, or drawn by a patch)
            for (uint32_t i = 0; i < s.order.clusterTriangles(uint32_t(c)); ++i) {
                const uint32_t t = s.order.triangle(uint32_t(c), i);
                if (!hidden || !mesh.hidden[t]) *out++ = t;
            }
        }
    }, 16);
    visibleGroups_.resize(groups ? drawn : 0);
    if (groups)
        poolFor(drawn, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) visibleGroups_[i] = overlay ? s.overlay->colors[drawnTris[i]] : mesh.polygroups[drawnTris[i]];
        }, 1 << 16);
    s.hasGroups = groups && !visibleGroups_.empty() && visibleGroups_.size() <= size_t(std::max(maxTexels, 0));
    s.overlayColors = overlay && s.hasGroups;
    s.overlaySerial = s.overlay ? s.overlay->serial : 0;
    s.bytes[5] = 0;
    if (s.hasGroups) {
        // 16 bits per triangle when every id fits (nearly always).
        const bool small = !overlay && *std::max_element(visibleGroups_.begin(), visibleGroups_.end()) <= 0xFFFFu;
        gl::BindBuffer(GL_TEXTURE_BUFFER, s.groupData);
        if (small) {
            std::vector<uint16_t> ids(visibleGroups_.size());
            poolFor(ids.size(), [&](size_t b, size_t e) {
                for (size_t i = b; i < e; ++i) ids[i] = uint16_t(visibleGroups_[i]);
            }, 1 << 16);
            gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(ids.size() * sizeof(uint16_t)), ids.data(), GL_STATIC_DRAW);
        } else {
            gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(visibleGroups_.size() * sizeof(uint32_t)), visibleGroups_.data(), GL_STATIC_DRAW);
        }
        gl::BindTexture(GL_TEXTURE_BUFFER, s.groupTexture);
        gl::TexBuffer(GL_TEXTURE_BUFFER, small ? GL_R16UI : GL_R32UI, s.groupData);
        gl::BindTexture(GL_TEXTURE_BUFFER, 0);
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
        s.bytes[5] = visibleGroups_.size() * (small ? 2 : 4);
    }
    // Texture painting (and a baked normal map preview): the UVs of every drawn triangle's corners, looked up
    // by the drawn triangle's index.
    s.tangentsStale = true;
    s.bytes[7] = 0;
    if (textured) {
        visibleUvs_.resize(drawn * 3);
        poolFor(drawn, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) std::copy_n(mesh.uvs.data() + size_t(drawnTris[i]) * 3, 3, visibleUvs_.data() + i * 3);
        }, 1 << 16);
        const std::vector<glm::vec2>& uvs = visibleUvs_;
        gl::BindBuffer(GL_TEXTURE_BUFFER, s.uvData);
        gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(uvs.size() * sizeof(glm::vec2)), uvs.data(), GL_STATIC_DRAW);
        gl::BindTexture(GL_TEXTURE_BUFFER, s.uvTexture);
        gl::TexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, s.uvData);
        gl::BindTexture(GL_TEXTURE_BUFFER, 0);
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
        s.bytes[7] = uvs.size() * sizeof(glm::vec2);
    } else if (s.hasUvs) {
        gl::BindBuffer(GL_TEXTURE_BUFFER, s.uvData);
        gl::BufferData(GL_TEXTURE_BUFFER, 0, nullptr, GL_STATIC_DRAW);
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
    }
    s.hasUvs = textured;
    s.linesValid = false;
    s.proxyStale.assign(nc, 1);
    if (textured) s.drawnOrder = std::move(drawnTris); // the normal map's tangents follow it
    else std::vector<uint32_t>().swap(s.drawnOrder);
    if (visibleUvs_.capacity() > (size_t(1) << 16)) std::vector<glm::vec2>().swap(visibleUvs_);
    // Scratch only: don't keep a big mesh's copy around.
    if (visibleGroups_.capacity() > (size_t(1) << 16)) std::vector<uint32_t>().swap(visibleGroups_);
}

// The wireframe's edges, each once (from the lowest-numbered drawn triangle using it), without quad diagonals;
// per cluster, in the cluster's local indices like its triangles.
void Renderer::buildLines(MeshSlot& s, const Mesh& mesh) {
    const size_t nt = mesh.triangleCount();
    const size_t nc = s.order.clusters().size();
    std::vector<std::vector<uint16_t>> lines(nc);
    poolFor(nc, [&](size_t c0, size_t c1) {
        thread_local std::vector<uint16_t> local;
        for (size_t c = c0; c < c1; ++c) {
            if (c < s.clusterPatch.size() && s.clusterPatch[c] != ~0u) continue; // drawn by its patch
            const uint32_t count = s.order.clusterTriangles(uint32_t(c));
            local.resize(size_t(count) * 3);
            s.order.localTriangles(mesh, uint32_t(c), local.data());
            std::vector<uint16_t>& out = lines[c];
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t t = s.order.triangle(uint32_t(c), i);
                if (mesh.triangleHidden(t)) continue;
                const glm::uvec3& tri = mesh.triangles[t];
                const int diagonal = hiddenDiagonal(mesh, t);
                for (int e = 0; e < 3; ++e) {
                    if (e == diagonal) continue;
                    const uint32_t a = tri[e], b = tri[(e + 1) % 3];
                    if (a >= mesh.vertexCount() || b >= mesh.vertexCount()) continue;
                    bool owner = true;
                    for (uint32_t u : mesh.vertexTriangles(a)) {
                        if (u >= t || u >= nt || mesh.triangleHidden(u)) continue;
                        const int k = edgeIndex(mesh.triangles[u], a, b);
                        if (k >= 0 && k != hiddenDiagonal(mesh, u)) {
                            owner = false;
                            break;
                        }
                    }
                    if (owner) out.insert(out.end(), {local[i * 3 + size_t(e)], local[i * 3 + size_t((e + 1) % 3)]});
                }
            }
        }
    }, 1);
    s.lineFirst.assign(nc, 0);
    s.lineCount.assign(nc, 0);
    size_t total = 0;
    for (size_t c = 0; c < nc; ++c) {
        s.lineFirst[c] = uint32_t(total);
        s.lineCount[c] = uint32_t(lines[c].size());
        total += lines[c].size();
    }
    gl::BindVertexArray(s.lineVao);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(total * sizeof(uint16_t)), nullptr, GL_STATIC_DRAW);
    for (size_t c = 0; c < nc; ++c)
        if (!lines[c].empty())
            gl::BufferSubData(GL_ELEMENT_ARRAY_BUFFER, GLintptr(size_t(s.lineFirst[c]) * sizeof(uint16_t)),
                              GLsizeiptr(lines[c].size() * sizeof(uint16_t)), lines[c].data());
    gl::BindVertexArray(0);
    s.bytes[2] = total * sizeof(uint16_t);
    s.linesValid = true;
}

// The motion proxies of the clusters that changed since they were built (all of them at first), each in its
// own part of the proxy buffer.
void Renderer::buildProxies(MeshSlot& s, const Mesh& mesh) {
    const size_t nc = s.order.clusters().size();
    s.proxyStale.resize(nc, 1);
    s.proxyCount.resize(nc, 0);
    s.proxyBounds.resize(nc);
    cullScratch_.clear();
    for (uint32_t c = 0; c < nc; ++c) {
        if (c < s.clusterPatch.size() && s.clusterPatch[c] != ~0u) { // drawn by its patch
            s.proxyStale[c] = 0;
            s.proxyCount[c] = 0;
        }
        if (s.proxyStale[c]) cullScratch_.push_back(c);
    }
    if (cullScratch_.empty()) return;
    if (!s.proxyAllocated) {
        gl::BindVertexArray(s.proxyVao);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr((s.order.triangleCount() / 4) * 3 * sizeof(uint16_t)), nullptr, GL_STATIC_DRAW);
        gl::BindVertexArray(0);
        s.proxyAllocated = true;
    }
    // A few milliseconds a frame: after a long stroke most of the front is stale, and building all of it in the first
    // moving frame stalled the view (180-300 ms at 33M). Stale clusters draw in full detail until theirs is built.
    constexpr double kProxyBudgetMs = 8.0;
    constexpr size_t kProxyBatch = 1024; // clusters per parallel batch
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::vector<uint16_t>> built;
    gl::BindVertexArray(s.proxyVao);
    for (size_t at = 0; at < cullScratch_.size();) {
        const size_t n = std::min(kProxyBatch, cullScratch_.size() - at);
        built.assign(n, {});
        poolFor(n, [&](size_t i0, size_t i1) {
            for (size_t i = i0; i < i1; ++i) {
                const uint32_t c = cullScratch_[at + i];
                ClusterBounds b = s.bounds[c];
                if (!clusterProxy(mesh, s.order, c, s.order.clusterTriangles(c) / 4, built[i], b)) built[i].clear();
                s.proxyBounds[c] = b;
            }
        }, 1, 2);
        for (size_t i = 0; i < n; ++i) {
            const uint32_t c = cullScratch_[at + i];
            s.proxyCount[c] = uint32_t(built[i].size());
            s.proxyStale[c] = 0;
            if (!built[i].empty())
                gl::BufferSubData(GL_ELEMENT_ARRAY_BUFFER, GLintptr(size_t(s.order.firstTriangle(c) / 4) * 3 * sizeof(uint16_t)),
                                  GLsizeiptr(built[i].size() * sizeof(uint16_t)), built[i].data());
        }
        at += n;
        if (at < cullScratch_.size() &&
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() > kProxyBudgetMs) {
            proxyPending_ = true;
            break;
        }
    }
    gl::BindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Dynamic topology patches (MeshSlot::patches)
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t kPatchTriangles = UploadOrder::kClusterTriangles; // per part: a cluster's worth
constexpr uint32_t kPatchVertices = 3 * kPatchTriangles;            // (local indices stay 16-bit)
constexpr uint32_t kPatchLineIndices = 6 * kPatchTriangles;         // 2 per edge, 3 edges per triangle at most
constexpr uint32_t kTailPatch = 0x80000000u;
constexpr uint32_t kNoPatch = ~0u;

} // namespace

void Renderer::resetPatches(MeshSlot& s) {
    s.patches.clear();
    s.clusterPatch.clear();
    s.tailPatch.clear();
}

bool Renderer::notePatches(MeshSlot& s, const Mesh& mesh) {
    const size_t nc = s.order.clusters().size();
    if (s.clusterPatch.size() != nc) s.clusterPatch.assign(nc, kNoPatch);
    if (s.chunkCluster.empty() && s.layoutTriangles > 0) { // which cluster each chunk of the layout went to
        s.chunkCluster.assign((size_t(s.layoutTriangles) + UploadOrder::kChunkTriangles - 1) >> UploadOrder::kChunkShift, 0);
        for (uint32_t c = 0; c < nc; ++c)
            for (uint32_t i = 0; i < s.order.clusterTriangles(c); i += UploadOrder::kChunkTriangles)
                s.chunkCluster[s.order.triangle(c, i) >> UploadOrder::kChunkShift] = c;
    }
    // More than this many and a full upload (a fresh layout) is cheaper to keep drawing.
    const size_t limit = std::max<size_t>(64, nc / 4);
    for (uint32_t t : mesh.changedTriangles) {
        uint32_t* slotOfPatch;
        uint32_t key;
        if (t < s.layoutTriangles) {
            const uint32_t chunk = t >> UploadOrder::kChunkShift;
            if (chunk >= s.chunkCluster.size()) return false;
            key = s.chunkCluster[chunk];
            slotOfPatch = &s.clusterPatch[key];
        } else {
            const uint32_t block = (t - s.layoutTriangles) / kPatchTriangles;
            if (block >= s.tailPatch.size()) s.tailPatch.resize(size_t(block) + 1, kNoPatch);
            key = kTailPatch + block;
            slotOfPatch = &s.tailPatch[block];
        }
        if (*slotOfPatch == kNoPatch) {
            if (s.patches.size() >= limit) return false;
            *slotOfPatch = uint32_t(s.patches.size());
            MeshSlot::Patch& p = s.patches.emplace_back();
            p.key = key;
            p.part = uint32_t(s.patches.size() - 1);
            if (key < kTailPatch) { // the layout no longer draws this cluster
                s.drawnTriangles -= std::min<size_t>(s.drawnTriangles, size_t(s.drawCount[key]) / 3);
                s.drawCount[key] = 0;
                if (key < s.lineCount.size()) s.lineCount[key] = 0;
                if (key < s.proxyCount.size()) s.proxyCount[key] = 0;
                if (key < s.bounds.size()) s.bounds[key] = ClusterBounds();
            }
        }
        s.patches[*slotOfPatch].stale = true;
    }
    return true;
}

bool Renderer::preparePatches(MeshSlot& s, const Mesh& mesh, bool wireframe) {
    const uint32_t nt = uint32_t(mesh.triangleCount()), nv = uint32_t(mesh.vertexCount());
    if (patchLocal_.size() < nv) {
        patchLocal_.resize(nv + nv / 4);
        patchStamp_.resize(nv + nv / 4, 0);
    }
    if (wireframe && !s.patchLinesOn)
        for (MeshSlot::Patch& p : s.patches) p.stale = true; // (their edges weren't built)
    const bool hidden = mesh.hidden.size() == nt && nt > 0;
    const bool groups = mesh.polygroups.size() == nt && nt > 0;
    for (MeshSlot::Patch& p : s.patches) {
        if (!p.stale) continue;
        if (++patchStampValue_ == 0) {
            std::fill(patchStamp_.begin(), patchStamp_.end(), 0);
            patchStampValue_ = 1;
        }
        p.verts.clear();
        p.local.clear();
        p.groups.clear();
        auto add = [&](uint32_t t) {
            if (t >= nt || (hidden && mesh.hidden[t])) return;
            const glm::uvec3& tri = mesh.triangles[t];
            for (int c = 0; c < 3; ++c) {
                const uint32_t v = tri[c];
                if (patchStamp_[v] != patchStampValue_) {
                    patchStamp_[v] = patchStampValue_;
                    patchLocal_[v] = uint32_t(p.verts.size());
                    p.verts.push_back(v);
                }
                p.local.push_back(uint16_t(std::min<uint32_t>(patchLocal_[v], 0xFFFF)));
            }
            if (groups) p.groups.push_back(mesh.polygroups[t]);
        };
        if (p.key < kTailPatch) {
            for (uint32_t i = 0; i < s.order.clusterTriangles(p.key); ++i) add(s.order.triangle(p.key, i));
        } else {
            const uint32_t first = s.layoutTriangles + (p.key - kTailPatch) * kPatchTriangles;
            for (uint32_t t = first; t < first + kPatchTriangles && t < nt; ++t) add(t);
        }
        if (p.verts.size() > kPatchVertices) return false; // scattered triangles: time for a fresh layout
    }
    return true;
}

void Renderer::uploadPatches(MeshSlot& s, const Mesh& mesh, bool wireframe) {
    if (s.patches.empty()) return;
    const uint32_t nv = uint32_t(mesh.vertexCount());
    const bool colors = nv > 0 && mesh.colors.size() == nv, mask = nv > 0 && mesh.mask.size() == nv;
    const bool normals = mesh.normals.size() == nv;
    bool all = s.patchGrid.step != s.grid.step || s.patchGrid.origin != s.grid.origin || colors != s.patchColorsOn ||
               mask != s.patchMaskOn;
    if (!s.patchVao) {
        gl::GenVertexArrays(1, &s.patchVao);
        gl::GenVertexArrays(1, &s.patchLineVao);
        for (GLuint* b : {&s.patchVertices, &s.patchIndices, &s.patchLines, &s.patchColors, &s.patchMask, &s.patchGroupData})
            gl::GenBuffers(1, b);
        gl::GenTextures(1, &s.patchGroupTexture);
        for (GLuint vao : {s.patchVao, s.patchLineVao}) {
            gl::BindVertexArray(vao);
            gl::BindBuffer(GL_ARRAY_BUFFER, s.patchVertices);
            gl::EnableVertexAttribArray(0);
            gl::VertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(PackedVertex), nullptr);
            if (vao == s.patchLineVao) {
                gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.patchLines);
                continue;
            }
            gl::EnableVertexAttribArray(1);
            gl::VertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(PackedVertex), reinterpret_cast<const void*>(2 * sizeof(uint32_t)));
            gl::BindBuffer(GL_ARRAY_BUFFER, s.patchColors);
            gl::VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 4, nullptr);
            gl::BindBuffer(GL_ARRAY_BUFFER, s.patchMask);
            gl::VertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_TRUE, 1, nullptr);
            gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.patchIndices);
        }
        gl::BindVertexArray(0);
    }
    if (s.patches.size() > s.patchParts) { // room for more parts: new buffers, everything sent again
        s.patchParts = std::max<size_t>({s.patches.size(), s.patchParts * 2, 8});
        const size_t parts = s.patchParts;
        auto alloc = [](GLenum target, GLuint buffer, size_t bytes) {
            gl::BindBuffer(target, buffer);
            gl::BufferData(target, GLsizeiptr(bytes), nullptr, GL_DYNAMIC_DRAW);
        };
        alloc(GL_ARRAY_BUFFER, s.patchVertices, parts * kPatchVertices * sizeof(PackedVertex));
        alloc(GL_ARRAY_BUFFER, s.patchColors, parts * kPatchVertices * sizeof(uint32_t));
        alloc(GL_ARRAY_BUFFER, s.patchMask, parts * kPatchVertices);
        alloc(GL_TEXTURE_BUFFER, s.patchGroupData, parts * kPatchTriangles * sizeof(uint32_t));
        gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
        gl::BindTexture(GL_TEXTURE_BUFFER, s.patchGroupTexture);
        gl::TexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, s.patchGroupData);
        gl::BindTexture(GL_TEXTURE_BUFFER, 0);
        gl::BindVertexArray(s.patchVao);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(parts * kPatchTriangles * 3 * sizeof(uint16_t)), nullptr, GL_DYNAMIC_DRAW);
        gl::BindVertexArray(s.patchLineVao);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(parts * kPatchLineIndices * sizeof(uint16_t)), nullptr, GL_DYNAMIC_DRAW);
        gl::BindVertexArray(0);
        s.patchBytes = parts * (kPatchVertices * (sizeof(PackedVertex) + 5) + kPatchTriangles * (3 * 2 + 4) + kPatchLineIndices * 2);
        for (MeshSlot::Patch& p : s.patches) p.uploaded = false;
        all = true;
    }
    if (colors != s.patchColorsOn || mask != s.patchMaskOn) {
        gl::BindVertexArray(s.patchVao);
        if (colors) gl::EnableVertexAttribArray(2);
        else gl::DisableVertexAttribArray(2);
        if (mask) gl::EnableVertexAttribArray(3);
        else gl::DisableVertexAttribArray(3);
        gl::BindVertexArray(0);
        s.patchColorsOn = colors;
        s.patchMaskOn = mask;
    }
    std::vector<PackedVertex> packed;
    std::vector<uint32_t> packedColors;
    std::vector<uint8_t> packedMask;
    std::vector<uint16_t> lines;
    std::vector<uint64_t> edges;
    bool outside = false;
    const bool anyDirty = mesh.dirtyBegin < mesh.dirtyEnd;
    for (MeshSlot::Patch& p : s.patches) {
        const bool topology = p.stale || !p.uploaded;
        bool verts = topology || all;
        if (!verts && anyDirty)
            for (uint32_t v : p.verts)
                if (mesh.vertexDirty(v)) {
                    verts = true;
                    break;
                }
        if (topology) {
            if (p.stale) {
                p.indices = uint32_t(p.local.size());
                // The wireframe: each edge of the patch once, quad diagonals left out as the layout does.
                p.lines = 0;
                if (wireframe) {
                    edges.clear();
                    const uint32_t nt = uint32_t(mesh.triangleCount());
                    size_t k = 0;
                    auto addEdges = [&](uint32_t t) {
                        if (t >= nt || mesh.triangleHidden(t)) return;
                        const int diagonal = hiddenDiagonal(mesh, t);
                        for (int e = 0; e < 3; ++e) {
                            const uint64_t a = p.local[k + size_t(e)], b = p.local[k + size_t((e + 1) % 3)];
                            if (e != diagonal) edges.push_back(std::min(a, b) << 16 | std::max(a, b));
                        }
                        k += 3;
                    };
                    if (p.key < kTailPatch) {
                        for (uint32_t i = 0; i < s.order.clusterTriangles(p.key); ++i) addEdges(s.order.triangle(p.key, i));
                    } else {
                        const uint32_t first = s.layoutTriangles + (p.key - kTailPatch) * kPatchTriangles;
                        for (uint32_t t = first; t < first + kPatchTriangles && t < nt; ++t) addEdges(t);
                    }
                    std::sort(edges.begin(), edges.end());
                    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
                    lines.clear();
                    for (uint64_t e : edges) {
                        lines.push_back(uint16_t(e >> 16));
                        lines.push_back(uint16_t(e & 0xFFFF));
                    }
                    p.lines = uint32_t(std::min<size_t>(lines.size(), kPatchLineIndices));
                }
            }
            gl::BindVertexArray(s.patchVao);
            if (!p.local.empty())
                gl::BufferSubData(GL_ELEMENT_ARRAY_BUFFER, GLintptr(size_t(p.part) * kPatchTriangles * 3 * sizeof(uint16_t)),
                                  GLsizeiptr(p.local.size() * sizeof(uint16_t)), p.local.data());
            if (wireframe && p.lines > 0) {
                gl::BindVertexArray(s.patchLineVao);
                gl::BufferSubData(GL_ELEMENT_ARRAY_BUFFER, GLintptr(size_t(p.part) * kPatchLineIndices * sizeof(uint16_t)),
                                  GLsizeiptr(size_t(p.lines) * sizeof(uint16_t)), lines.data());
            }
            gl::BindVertexArray(0);
            if (!p.groups.empty()) {
                gl::BindBuffer(GL_TEXTURE_BUFFER, s.patchGroupData);
                gl::BufferSubData(GL_TEXTURE_BUFFER, GLintptr(size_t(p.part) * kPatchTriangles * sizeof(uint32_t)),
                                  GLsizeiptr(p.groups.size() * sizeof(uint32_t)), p.groups.data());
                gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
            }
            if (p.stale) {
                std::vector<uint16_t>().swap(p.local);
                std::vector<uint32_t>().swap(p.groups);
            }
            p.stale = false;
            p.uploaded = true;
        }
        if (!verts || p.verts.empty()) continue;
        const size_t n = p.verts.size();
        packed.resize(n);
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        for (size_t i = 0; i < n; ++i) {
            const uint32_t v = p.verts[i];
            const glm::vec3& pos = mesh.positions[v];
            outside |= !packVertex(s.grid, pos, normals ? mesh.normals[v] : glm::vec3(0.0f, 0.0f, 1.0f), curvatureAt(mesh, v), packed[i]);
            lo = glm::min(lo, pos);
            hi = glm::max(hi, pos);
        }
        p.bounds = ClusterBounds();
        p.bounds.center = (lo + hi) * 0.5f;
        p.bounds.radius = glm::length(hi - lo) * 0.5f;
        gl::BindBuffer(GL_ARRAY_BUFFER, s.patchVertices);
        gl::BufferSubData(GL_ARRAY_BUFFER, GLintptr(size_t(p.part) * kPatchVertices * sizeof(PackedVertex)), GLsizeiptr(n * sizeof(PackedVertex)),
                          packed.data());
        if (colors) {
            packedColors.resize(n);
            for (size_t i = 0; i < n; ++i) packedColors[i] = packColor(mesh.colors[p.verts[i]]);
            gl::BindBuffer(GL_ARRAY_BUFFER, s.patchColors);
            gl::BufferSubData(GL_ARRAY_BUFFER, GLintptr(size_t(p.part) * kPatchVertices * sizeof(uint32_t)), GLsizeiptr(n * sizeof(uint32_t)),
                              packedColors.data());
        }
        if (mask) {
            packedMask.resize(n);
            for (size_t i = 0; i < n; ++i) packedMask[i] = packMask(mesh.mask[p.verts[i]]);
            gl::BindBuffer(GL_ARRAY_BUFFER, s.patchMask);
            gl::BufferSubData(GL_ARRAY_BUFFER, GLintptr(size_t(p.part) * kPatchVertices), GLsizeiptr(n), packedMask.data());
        }
    }
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    s.patchGrid = s.grid;
    s.patchLinesOn = wireframe;
    if (outside) s.fresh = true; // a vertex left the position grid: a full upload (with a new grid) next time
    // The totals the culling and drawing use: the drawn triangles, the closed mesh's volume, its box.
    size_t drawn = 0;
    for (size_t c = 0; c < s.drawCount.size(); ++c) drawn += size_t(s.drawCount[c]) / 3;
    for (const MeshSlot::Patch& p : s.patches) {
        drawn += p.indices / 3;
        if (p.bounds.radius >= 0.0f) {
            s.box.expand(p.bounds.center - glm::vec3(p.bounds.radius));
            s.box.expand(p.bounds.center + glm::vec3(p.bounds.radius));
        }
    }
    s.drawnTriangles = drawn;
}

void Renderer::drawPatches(const MeshSlot& s, GLenum mode, Part part, GLint primBaseLocation) {
    for (const MeshSlot::Patch& p : s.patches) {
        const uint32_t count = part == Part::Lines ? p.lines : p.indices;
        if (count == 0 || !insideFrustum(frameCull_, p.bounds)) continue;
        if (primBaseLocation >= 0) gl::Uniform1i(primBaseLocation, GLint(p.part * kPatchTriangles));
        const size_t first = part == Part::Lines ? size_t(p.part) * kPatchLineIndices : size_t(p.part) * kPatchTriangles * 3;
        gl::DrawElementsBaseVertex(mode, GLsizei(count), GL_UNSIGNED_SHORT, reinterpret_cast<const void*>(first * sizeof(uint16_t)),
                                   GLint(p.part * kPatchVertices));
        if (part != Part::Lines) lastDrawn_ += count / 3;
    }
    if (primBaseLocation >= 0) gl::Uniform1i(primBaseLocation, -1);
}

void Renderer::sync(MeshSlot& s, Mesh& mesh, const NormalMapView* normalMap, bool wireframe, bool pbr) {
    if (mesh.micro && microMatches(mesh)) { // a micro level draws its own way (render/MicroRenderer.h)
        if (!s.micro) s.micro = std::make_shared<MicroRenderer>();
        s.outOfMemory = !s.micro->sync(mesh);
        s.drawnTriangles = s.outOfMemory ? 0 : 1; // (draw() skips empty slots)
        s.fresh = true;                           // (back to a mesh: a full upload)
        return;
    }
    if (s.micro) {
        s.micro.reset();
        s.fresh = true;
    }
    const size_t nv = mesh.vertexCount();
    if (normalMap && (!normalMap->rgb || !normalMap->tangents || normalMap->tangents->size() != mesh.uvs.size() ||
                      !mesh.hasUvs() || normalMap->rgb->size() != size_t(normalMap->width) * size_t(normalMap->height) * 3))
        normalMap = nullptr; // made for another mesh
    const bool wantUvs = hasTexture(mesh) || normalMap || (s.overlay && s.overlay->checker);
    const bool overlayChanged = (s.overlay ? s.overlay->serial : 0) != s.overlaySerial;
    // Dynamic topology changes topology a little at a time: patches (notePatches) instead of a full upload,
    // until there are too many of them.
    const bool patched = !s.patches.empty() || !mesh.changedTriangles.empty();
    bool full = s.fresh || mesh.topologyDirty || (nv != s.vertexCapacity && !patched) || (patched && wantUvs);
    if (!full && !mesh.changedTriangles.empty() && !notePatches(s, mesh)) full = true;
    if (!full && !s.patches.empty() && mesh.facesDirty)
        for (MeshSlot::Patch& p : s.patches) p.stale = true; // (groups or hidden triangles changed)
    if (!full && !s.patches.empty() && !preparePatches(s, mesh, wireframe)) full = true;
    // The GPU keeps the vertices in the slot's cluster layout (core/UploadOrder.h), so a dab re-sends a few
    // short ranges holding what it changed, gathered from the mesh arrays.
    bool indicesDone = false;
    if (full) {
        // The index buffer is written while the layout is made (mapped: the build's threads write each
        // cluster's drawn triangles straight into it).
        const size_t nt = mesh.triangleCount();
        const bool hidden = mesh.hidden.size() == nt && nt > 0;
        s.drawCount.assign(UploadOrder::clusterCount(nt), 0);
        gl::BindVertexArray(s.vao);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(nt * 3 * sizeof(uint16_t)), nullptr, GL_STATIC_DRAW);
        auto* indices = nt ? static_cast<uint16_t*>(gl::MapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, GLsizeiptr(nt * 3 * sizeof(uint16_t)),
                                                                        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT))
                           : nullptr;
        // The culling bounds too, while each cluster's triangles are at hand.
        s.bounds.assign(UploadOrder::clusterCount(nt), ClusterBounds());
        UploadOrder::ClusterCallback write = [&](uint32_t c, const uint16_t* local) {
            s.bounds[c] = clusterBounds(mesh, s.order, c);
            if (!indices) return;
            const uint32_t count = s.order.clusterTriangles(c);
            uint16_t* out = indices + size_t(s.order.firstTriangle(c)) * 3;
            size_t drawn = count;
            if (!hidden) {
                std::copy_n(local, size_t(count) * 3, out);
            } else {
                drawn = 0;
                for (uint32_t t = 0; t < count; ++t)
                    if (!mesh.hidden[s.order.triangle(c, t)]) std::copy_n(local + size_t(t) * 3, 3, out + 3 * drawn++);
            }
            s.drawCount[c] = GLsizei(drawn * 3);
        };
        resetPatches(s);
        s.order.build(mesh, write);
        s.layoutTriangles = uint32_t(nt);
        std::vector<uint32_t>().swap(s.chunkCluster); // (made when patches first need it)
        indicesDone = indices && gl::UnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
        gl::BindVertexArray(0);
        s.grid = PositionGrid::around(meshBounds(mesh));
        dirtyRuns_.assign(1, {0u, uint32_t(s.order.size())});
    } else if (s.regrid) { // a stroke's vertex left the grid (prepareStrokeUpload): a new one, everything sent
        s.grid = PositionGrid::around(meshBounds(mesh));
        dirtyRuns_.assign(1, {0u, uint32_t(s.order.size())});
        std::fill(s.blockHash.begin(), s.blockHash.end(), ~uint64_t(0) >> 1); // (no block keeps its hash)
    } else {
        s.order.dirtyRuns(mesh, dirtyRuns_);
    }
    s.regrid = false;
    const size_t ns = s.order.size();
    const bool ranged = !full && !dirtyRuns_.empty();
    const std::span<const uint32_t> order = s.order.fromGpu();
    static const bool syncLog = std::getenv("SF_SYNC_LOG") != nullptr; // where a vertex upload's time went
    auto lap = std::chrono::steady_clock::now();
    const auto lapStart = lap;
    double vertsMs = 0, boundsMs = 0, proxyMs = 0;
    const auto lapMs = [](std::chrono::steady_clock::time_point& t) {
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - t).count();
        t = now;
        return ms;
    };
    if (full || ranged) {
        uploadVertices(s, mesh, full);
        if (syncLog) vertsMs = lapMs(lap);
        updateBounds(s, mesh, full, full); // (a full upload computed them while building)
        if (syncLog) boundsMs = lapMs(lap);
    }
    if (full) {
        s.proxyAllocated = false; // sized for the old mesh
        s.proxyCount.clear();
        // (closedOriented also accepts unwelded seams, the same point twice, which the layout's check doesn't.)
        s.closed = s.order.closed() || closedOriented(mesh);
        uploadFaces(s, mesh, wantUvs, indicesDone);
        s.vertexCapacity = nv;
        // A mesh too big for the GPU's memory isn't drawn (rather than taking the driver down); the next full
        // upload (e.g. after Lower Res or a decimation) tries again.
        s.outOfMemory = false;
        for (int i = 0; i < 16; ++i) {
            const GLenum e = gl::GetError();
            if (e == GL_NO_ERROR) break;
            s.outOfMemory = s.outOfMemory || e == GL_OUT_OF_MEMORY;
        }
        if (s.outOfMemory) {
            gl::BindBuffer(GL_ARRAY_BUFFER, s.vertexBuffer);
            gl::BufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
            gl::BindVertexArray(s.vao);
            gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
            gl::BindVertexArray(0);
            s.drawCount.assign(s.drawCount.size(), 0);
            s.drawnTriangles = 0;
            s.bytes[0] = s.bytes[1] = 0;
        }
    }
    if (!full && (mesh.facesDirty || wantUvs != s.hasUvs || overlayChanged)) uploadFaces(s, mesh, wantUvs);
    if (wireframe && !s.linesValid && !s.outOfMemory) buildLines(s, mesh);
    // Small meshes draw in full while the view moves too (their proxy would save little).
    constexpr size_t kProxyMinTriangles = size_t(1) << 18;
    if (syncLog) lapMs(lap);
    if (proxyUsed_ && !s.outOfMemory && s.drawnTriangles >= kProxyMinTriangles) buildProxies(s, mesh);
    if (syncLog) proxyMs = lapMs(lap);
    syncTexture(s, mesh, pbr);
    syncNormalMap(s, mesh, normalMap);

    // Optional per-vertex attributes: a full upload when they appear (or the
    // mesh changed), the dirty runs otherwise, and a disabled array when absent.
    const Runs everything{{0u, uint32_t(ns)}};
    auto syncAttribute = [&](GLuint buffer, GLuint location, bool& uploaded, auto gather, size_t elementSize, bool present,
                             size_t& bytes) {
        gl::BindBuffer(GL_ARRAY_BUFFER, buffer);
        if (present && (full || !uploaded)) {
            uploadGathered(staging_, buffer, elementSize, ns, true, everything, gather);
        } else if (present && ranged) {
            uploadGathered(staging_, buffer, elementSize, ns, false, dirtyRuns_, gather);
        } else if (!present && uploaded) {
            gl::BufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        }
        if (present != uploaded) {
            for (GLuint vao : {s.vao, s.proxyVao}) {
                gl::BindVertexArray(vao);
                if (present) gl::EnableVertexAttribArray(location);
                else gl::DisableVertexAttribArray(location);
            }
            gl::BindVertexArray(0);
        }
        uploaded = present;
        bytes = present ? ns * elementSize : 0;
    };
    // (A slot may name a vertex dynamic topology removed since the layout was made: only patched clusters use it.)
    syncAttribute(s.colorBuffer, 2, s.hasColors,
                  gatherWith<uint32_t>(order, [&](uint32_t v) { return v < nv ? packColor(mesh.colors[v]) : 0u; }),
                  sizeof(uint32_t), nv > 0 && mesh.colors.size() == nv, s.bytes[3]);
    syncAttribute(s.maskBuffer, 3, s.hasMask, gatherWith<uint8_t>(order, [&](uint32_t v) { return v < nv ? packMask(mesh.mask[v]) : uint8_t(0); }),
                  sizeof(uint8_t), nv > 0 && mesh.mask.size() == nv, s.bytes[4]);
    // Scratch only: don't keep a big upload's staging copy around.
    if (staging_.capacity() > (size_t(1) << 22)) std::vector<uint8_t>().swap(staging_);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    s.fresh = false;
    uploadPatches(s, mesh, wireframe); // (may ask for a full upload next time)
    mesh.clearDirty();
    if (syncLog && (full || ranged || proxyMs > 1.0)) {
        size_t sent = 0;
        for (const auto& [b0, e0] : dirtyRuns_) sent += e0 - b0;
        SDL_Log("sync: %s %zu slots in %zu runs, %.1f ms (vertices %.1f, bounds %.1f, proxy %.1f)", full ? "full" : "ranged",
                sent, dirtyRuns_.size(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lapStart).count(),
                vertsMs, boundsMs, proxyMs);
    }
}

// The painted texture (core/Texture.h): a full upload when it is new or replaced, then only the rectangle
// painted since this slot's last upload (PaintTexture::revision / dirtyFrom), with fresh mipmaps. In the PBR view
// the material channels go up the same way as packed planes (pbrViewTexels): ORM, normal and emissive.
void Renderer::syncTexture(MeshSlot& s, Mesh& mesh, bool pbr) {
    PaintTexture* tex = s.hasUvs ? mesh.texture.get() : nullptr;
    GLuint* planeTex[3] = {&s.ormTexture, &s.matNormalTexture, &s.emissiveTexture};
    const uint32_t want = tex && pbr ? pbrViewPlanes(*tex) : 0u;
    for (int b = 0; b < 3; ++b)
        if (!(want & (1u << b)) && *planeTex[b]) {
            gl::DeleteTextures(1, planeTex[b]);
            *planeTex[b] = 0;
        }
    if (!tex) {
        if (s.paintTexture) gl::DeleteTextures(1, &s.paintTexture);
        s.paintTexture = 0;
        s.textureSerial = 0;
        s.pbrPlanes = 0;
        return;
    }
    const bool fresh = !s.paintTexture || s.textureSerial != tex->serial || s.textureW != tex->width || s.textureH != tex->height;
    // Planes to send whole: new ones, or all of them with a new texture.
    const uint32_t wholePlanes = fresh || s.pbrSerial != tex->serial ? want : want & ~s.pbrPlanes;
    s.pbrPlanes &= want;
    if (!fresh && s.textureRevision == tex->revision && !wholePlanes) return;
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const bool rect = !fresh && s.textureRevision >= tex->dirtyFrom && tex->dirtyMin.x < tex->dirtyMax.x && tex->dirtyMin.y < tex->dirtyMax.y;
    const bool changed = fresh || s.textureRevision != tex->revision;
    // Calls fn(x0, y0, x1, y1) for the runs of dirty tiles along each tile row of the dirty rectangle.
    auto dirtyRuns = [&](auto&& fn) {
        const int tx0 = tex->dirtyMin.x / kTextureTile, tx1 = (tex->dirtyMax.x - 1) / kTextureTile;
        for (int ty = tex->dirtyMin.y / kTextureTile; ty <= (tex->dirtyMax.y - 1) / kTextureTile; ++ty)
            for (int tx = tx0; tx <= tx1;) {
                if (!tex->tileDirty(tx, ty)) {
                    ++tx;
                    continue;
                }
                const int start = tx;
                while (tx <= tx1 && tex->tileDirty(tx, ty)) ++tx;
                fn(start * kTextureTile, ty * kTextureTile, std::min(tx * kTextureTile, tex->width),
                   std::min((ty + 1) * kTextureTile, tex->height));
            }
    };
    auto setParams = [] {
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR_MIPMAP_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    };
    if (fresh) {
        if (!s.paintTexture) gl::GenTextures(1, &s.paintTexture);
        gl::BindTexture(GL_TEXTURE_2D, s.paintTexture);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_SRGB8_ALPHA8), tex->width, tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                       tex->shown().data());
        setParams();
        s.textureSerial = tex->serial;
        s.textureW = tex->width;
        s.textureH = tex->height;
        gl::GenerateMipmap(GL_TEXTURE_2D);
    } else if (changed) {
        gl::BindTexture(GL_TEXTURE_2D, s.paintTexture);
        gl::PixelStorei(GL_UNPACK_ROW_LENGTH, tex->width);
        auto upload = [&](int x0, int y0, int x1, int y1) {
            x1 = std::min(x1, tex->width), y1 = std::min(y1, tex->height);
            gl::TexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x1 - x0, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE,
                              tex->shown().data() + (size_t(y0) * size_t(tex->width) + size_t(x0)) * 4);
        };
        if (!rect) upload(0, 0, tex->width, tex->height);
        else dirtyRuns(upload); // only the dirty tiles of the rectangle, as runs along each tile row
        gl::PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl::GenerateMipmap(GL_TEXTURE_2D);
    }
    // PBR planes: packed rectangle by rectangle (a staging buffer the size of what is sent).
    std::vector<uint8_t> staging;
    for (int b = 0; b < 3; ++b) {
        const uint32_t bit = 1u << b;
        if (!(want & bit)) continue;
        const bool whole = (wholePlanes & bit) || !rect;
        if (!whole && !changed) continue;
        GLuint& t = *planeTex[b];
        const GLint format = b == 2 ? GLint(GL_SRGB8_ALPHA8) : GLint(GL_RGBA8);
        auto pack = [&](int x0, int y0, int x1, int y1) {
            staging.assign(size_t(x1 - x0) * size_t(y1 - y0) * 4, 0);
            pbrViewTexels(*tex, x0, y0, x1, y1, b == 0 ? staging.data() : nullptr, b == 1 ? staging.data() : nullptr,
                          b == 2 ? staging.data() : nullptr);
        };
        if (whole) {
            if (!t) gl::GenTextures(1, &t);
            gl::BindTexture(GL_TEXTURE_2D, t);
            pack(0, 0, tex->width, tex->height);
            gl::TexImage2D(GL_TEXTURE_2D, 0, format, tex->width, tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, staging.data());
            setParams();
        } else {
            gl::BindTexture(GL_TEXTURE_2D, t);
            // (one texel wider for the normal: the height's normal reads its neighbours)
            const int pad = b == 1 ? 1 : 0;
            dirtyRuns([&](int x0, int y0, int x1, int y1) {
                x0 = std::max(0, x0 - pad), y0 = std::max(0, y0 - pad);
                x1 = std::min(tex->width, x1 + pad), y1 = std::min(tex->height, y1 + pad);
                pack(x0, y0, x1, y1);
                gl::TexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x1 - x0, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE, staging.data());
            });
        }
        gl::GenerateMipmap(GL_TEXTURE_2D);
        s.pbrPlanes |= bit;
    }
    s.pbrSerial = tex->serial;
    gl::BindTexture(GL_TEXTURE_2D, 0);
    s.textureRevision = tex->revision;
    tex->clearDirty();
}

// Tangents (MikkTSpace, per mesh corner) into the slot's texture buffer, in draw order (the clusters' order,
// without hidden triangles), like the UVs.
void Renderer::uploadTangents(MeshSlot& s, const std::vector<glm::vec4>& all) {
    if (!s.tangentData) gl::GenBuffers(1, &s.tangentData);
    if (!s.tangentTexture) gl::GenTextures(1, &s.tangentTexture);
    std::vector<glm::vec4> tans(s.drawnOrder.size() * 3);
    for (size_t i = 0; i < s.drawnOrder.size(); ++i)
        if (size_t(s.drawnOrder[i]) * 3 + 3 <= all.size()) std::copy_n(all.data() + size_t(s.drawnOrder[i]) * 3, 3, tans.data() + i * 3);
    gl::BindBuffer(GL_TEXTURE_BUFFER, s.tangentData);
    gl::BufferData(GL_TEXTURE_BUFFER, GLsizeiptr(tans.size() * sizeof(glm::vec4)), tans.data(), GL_STATIC_DRAW);
    gl::BindTexture(GL_TEXTURE_BUFFER, s.tangentTexture);
    gl::TexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, s.tangentData);
    gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    gl::BindBuffer(GL_TEXTURE_BUFFER, 0);
    s.hasTangents = true;
    s.tangentsStale = false;
    s.bytes[7] = s.drawnOrder.size() * 3 * (sizeof(glm::vec2) + sizeof(glm::vec4));
}

// The baked normal map preview: the map uploads when its serial changes, the tangents (in draw order, without
// hidden triangles, like the UVs) whenever the faces were re-uploaded too. Without a preview, the PBR view's
// material normal (syncTexture) gets tangents computed here, once per UV layout.
void Renderer::syncNormalMap(MeshSlot& s, const Mesh& mesh, const NormalMapView* nm) {
    if (!nm || !s.hasUvs) {
        if (s.normalTexture) gl::DeleteTextures(1, &s.normalTexture);
        s.normalTexture = 0;
        s.normalSerial = 0;
        if (!s.hasUvs || !(s.pbrPlanes & 2u)) {
            s.hasTangents = false;
            s.tangentsFromMaterial = false;
            std::vector<glm::vec4>().swap(s.matTangents);
            s.matTangentKey = 0;
            return;
        }
        const uint64_t key = meshUvFingerprint(mesh) ^ (uint64_t(mesh.vertexCount()) << 32) ^ uint64_t(mesh.triangleCount());
        if (s.matTangentKey != key || s.matTangents.size() != mesh.uvs.size()) {
            s.matTangents = computeTangents(mesh);
            s.matTangentKey = key;
            s.tangentsStale = true;
        }
        if (!s.tangentsFromMaterial) s.tangentsStale = true;
        s.tangentsFromMaterial = true;
        s.normalFlipGreen = false;
        if (s.tangentsStale || !s.hasTangents) uploadTangents(s, s.matTangents);
        return;
    }
    if (!s.normalTexture || s.normalSerial != nm->serial) {
        if (!s.normalTexture) gl::GenTextures(1, &s.normalTexture);
        gl::BindTexture(GL_TEXTURE_2D, s.normalTexture);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(kRgb8), nm->width, nm->height, 0, kRgb, GL_UNSIGNED_BYTE, nm->rgb->data());
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR_MIPMAP_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
        gl::GenerateMipmap(GL_TEXTURE_2D);
        gl::BindTexture(GL_TEXTURE_2D, 0);
        s.normalSerial = nm->serial;
        s.tangentsStale = true;
    }
    if (s.tangentsFromMaterial) s.tangentsStale = true;
    s.tangentsFromMaterial = false;
    s.normalFlipGreen = nm->flipGreen;
    if (!s.tangentsStale && s.hasTangents) return;
    uploadTangents(s, *nm->tangents);
}

// ---------------------------------------------------------------------------
// PBR view: image-based lighting
// ---------------------------------------------------------------------------

bool Renderer::ensureEnvironment(int index) {
    if (index < 0 || index >= environmentCount()) index = 0;
    if (envFailedIndex_ == index) index = 0; // (a file that failed once: the built-in studio instead)
    if (!brdfTexture_) {
        const std::vector<glm::vec2> lut = brdfLut(kBrdfLutSize);
        gl::GenTextures(1, &brdfTexture_);
        gl::BindTexture(GL_TEXTURE_2D, brdfTexture_);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(kRg16f), kBrdfLutSize, kBrdfLutSize, 0, kRg, GL_FLOAT, lut.data());
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    }
    if (envIndex_ == index && envTexture_ && !envJob_) return true;
    if (!envJob_ || envJob_->index != index) {
        auto job = std::make_shared<EnvJob>();
        job->index = index;
        envJob_ = job;
        std::thread([job, wake = envWake_] {
            std::string error;
            job->ok = buildEnvironmentLighting(job->index, job->light, error);
            job->done = true;
            if (wake) wake();
        }).detach();
    }
    if (!envJob_->done) {
        if (envTexture_) return true; // the old one meanwhile
        while (!envJob_->done) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::shared_ptr<EnvJob> job = std::move(envJob_);
    if (!job->ok) {
        envFailedIndex_ = job->index;
        if (job->index == 0) return envTexture_ != 0;
        return ensureEnvironment(0);
    }
    const EnvironmentLighting& light = job->light;
    if (!envTexture_) gl::GenTextures(1, &envTexture_);
    gl::BindTexture(GL_TEXTURE_2D, envTexture_);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (size_t k = 0; k < light.levels.size(); ++k) {
        const EnvImage& l = light.levels[k];
        gl::TexImage2D(GL_TEXTURE_2D, GLint(k), GLint(kRgb16f), l.width, l.height, 0, kRgb, GL_FLOAT, l.texels.data());
    }
    gl::TexParameteri(GL_TEXTURE_2D, kMaxLevel, GLint(light.levels.size()) - 1);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR_MIPMAP_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(kRepeat));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    gl::BindTexture(GL_TEXTURE_2D, 0);
    envSh_ = light.sh;
    envIndex_ = job->index;
    return true;
}

bool Renderer::environmentPending() const { return envJob_ != nullptr; }

uint32_t Renderer::pbrPlanes(uint32_t id) const {
    for (const auto& s : slots_)
        if (s.id == id) return s.pbrPlanes;
    return 0;
}

void Renderer::setPbrUniforms(const MeshProgram& p, const RenderSettings& settings, bool textured, const MeshSlot& s) {
    if (p.envLevels < 0 && p.material < 0) return; // (not a PBR program)
    gl::ActiveTexture(kTexture10);
    gl::BindTexture(GL_TEXTURE_2D, envTexture_);
    gl::ActiveTexture(kTexture11);
    gl::BindTexture(GL_TEXTURE_2D, brdfTexture_);
    gl::ActiveTexture(kTexture8);
    gl::BindTexture(GL_TEXTURE_2D, textured ? s.ormTexture : 0);
    gl::ActiveTexture(kTexture9);
    gl::BindTexture(GL_TEXTURE_2D, textured ? s.emissiveTexture : 0);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::Uniform1i(p.hasOrm, textured && s.ormTexture ? 1 : 0);
    gl::Uniform1i(p.hasEmissive, textured && s.emissiveTexture ? 1 : 0);
    gl::Uniform1f(p.envLevels, float(kEnvLevels - 1));
    gl::Uniform1f(p.exposure, settings.exposure);
    gl::Uniform1i(p.toneMap, int(settings.toneMap));
    gl::Uniform2f(p.material, std::clamp(settings.pbrRoughness, 0.0f, 1.0f), std::clamp(settings.pbrMetallic, 0.0f, 1.0f));
    gl::Uniform1f(p.pbrCavity, std::clamp(settings.pbrCavity, 0.0f, 1.0f));
    gl::UniformMatrix3fv(p.viewToEnv, 1, GL_FALSE, glm::value_ptr(frameViewToEnv_));
    gl::Uniform3fv(p.sh, 9, glm::value_ptr(envSh_.c[0]));
}

GLuint Renderer::paintTexture(uint32_t id) const {
    for (const auto& s : slots_)
        if (s.id == id) return s.paintTexture;
    return 0;
}

bool Renderer::renderToImage(const Camera& camera, int width, int height, const RenderSettings& settings,
                             std::vector<uint8_t>& rgba, std::string& error) {
    GLuint fbo[2] = {}, rb[3] = {};
    gl::GenFramebuffers(2, fbo);
    gl::GenRenderbuffers(3, rb);
    // Multisampled colour + depth for drawing, plain colour for resolving.
    gl::BindRenderbuffer(GL_RENDERBUFFER, rb[0]);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, width, height);
    gl::BindRenderbuffer(GL_RENDERBUFFER, rb[1]);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, width, height);
    gl::BindRenderbuffer(GL_RENDERBUFFER, rb[2]);
    gl::RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);
    bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[2]);
    ok = ok && gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
        draw(camera, width, height, settings);
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, fbo[0]);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[1]);
        gl::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, fbo[1]);
        std::vector<uint8_t> pixels(size_t(width) * size_t(height) * 4);
        gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl::ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        rgba.resize(pixels.size());
        const size_t row = size_t(width) * 4;
        for (int y = 0; y < height; ++y) // GL rows are bottom-up
            std::copy_n(pixels.data() + size_t(height - 1 - y) * row, row, rgba.data() + size_t(y) * row);
    } else {
        error = "Offscreen framebuffer is not supported by this GPU/driver";
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(2, fbo);
    gl::DeleteRenderbuffers(3, rb);
    return ok;
}

bool Renderer::viewTarget(int width, int height, int samples) {
    if (viewFbo_ && width == viewW_ && height == viewH_ && samples == viewSamples_) return true;
    if (width == viewW_ && height == viewH_ && samples == viewFailed_) return false; // don't retry every frame
    if (viewFbo_) {
        gl::DeleteFramebuffers(1, &viewFbo_);
        gl::DeleteFramebuffers(1, &viewResolveFbo_);
        gl::DeleteRenderbuffers(3, viewRb_);
        viewFbo_ = viewResolveFbo_ = 0;
    }
    for (int i = 0; i < 16 && gl::GetError() != GL_NO_ERROR; ++i) {
    }
    gl::GenFramebuffers(1, &viewFbo_);
    gl::GenFramebuffers(1, &viewResolveFbo_);
    gl::GenRenderbuffers(3, viewRb_);
    gl::BindRenderbuffer(GL_RENDERBUFFER, viewRb_[0]);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    gl::BindRenderbuffer(GL_RENDERBUFFER, viewRb_[1]);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    gl::BindRenderbuffer(GL_RENDERBUFFER, viewRb_[2]);
    gl::RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    gl::BindFramebuffer(GL_FRAMEBUFFER, viewFbo_);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, viewRb_[0]);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, viewRb_[1]);
    bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::BindFramebuffer(GL_FRAMEBUFFER, viewResolveFbo_);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, viewRb_[2]);
    ok = ok && gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && gl::GetError() == GL_NO_ERROR;
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    viewW_ = width;
    viewH_ = height;
    if (!ok) {
        gl::DeleteFramebuffers(1, &viewFbo_);
        gl::DeleteFramebuffers(1, &viewResolveFbo_);
        gl::DeleteRenderbuffers(3, viewRb_);
        viewFbo_ = viewResolveFbo_ = 0;
        viewSamples_ = 0;
        viewFailed_ = samples;
        return false;
    }
    viewSamples_ = samples;
    viewFailed_ = 0;
    return true;
}

int Renderer::drawView(const Camera& camera, int width, int height, const RenderSettings& settings, const Viewport& viewport,
                       int samples) {
    GLint maxSamples = 1;
    gl::GetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples = std::clamp(samples, 1, std::max(1, int(maxSamples)));
    if (directView_) samples = 1;
    while (samples > 1 && !viewTarget(width, height, samples)) samples /= 2;
    if (samples <= 1) {
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        draw(camera, width, height, settings, viewport);
        return 1;
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, viewFbo_);
    draw(camera, width, height, settings, viewport);
    for (int i = 0; i < 16 && gl::GetError() != GL_NO_ERROR; ++i) {
    }
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, viewFbo_); // resolve: same format and size on both sides
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, viewResolveFbo_);
    gl::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, viewResolveFbo_); // single-sample blit: may convert to the window's format
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    const GLenum blitError = gl::GetError();
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (blitError != GL_NO_ERROR) { // the view never reached the window: draw this frame and later ones directly
        directView_ = true;
        SDL_Log("View copy to the window failed (GL error 0x%04x); drawing the view directly without antialiasing",
                unsigned(blitError));
        draw(camera, width, height, settings, viewport);
        return 1;
    }
    return samples;
}

bool Renderer::beginCapture(int width, int height, std::string& error) {
    if (width != captureW_ || height != captureH_) {
        if (captureFbo_[0]) {
            gl::DeleteFramebuffers(2, captureFbo_);
            gl::DeleteRenderbuffers(3, captureRb_);
        }
        gl::GenFramebuffers(2, captureFbo_);
        gl::GenRenderbuffers(3, captureRb_);
        gl::BindRenderbuffer(GL_RENDERBUFFER, captureRb_[0]);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, width, height);
        gl::BindRenderbuffer(GL_RENDERBUFFER, captureRb_[1]);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, width, height);
        gl::BindRenderbuffer(GL_RENDERBUFFER, captureRb_[2]);
        gl::RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
        gl::BindFramebuffer(GL_FRAMEBUFFER, captureFbo_[0]);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, captureRb_[0]);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRb_[1]);
        bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        gl::BindFramebuffer(GL_FRAMEBUFFER, captureFbo_[1]);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, captureRb_[2]);
        ok = ok && gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!ok) {
            error = "Offscreen framebuffer is not supported by this GPU/driver";
            return false;
        }
        captureW_ = width;
        captureH_ = height;
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, captureFbo_[0]);
    return true;
}

void Renderer::endCapture(std::vector<uint8_t>& rgba) {
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, captureFbo_[0]);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, captureFbo_[1]);
    gl::BlitFramebuffer(0, 0, captureW_, captureH_, 0, 0, captureW_, captureH_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, captureFbo_[1]);
    rgba.resize(size_t(captureW_) * size_t(captureH_) * 4);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, captureW_, captureH_, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    // Flip to top-down rows for video encoders.
    const size_t row = size_t(captureW_) * 4;
    std::vector<uint8_t> tmp(row);
    for (int y = 0; y < captureH_ / 2; ++y) {
        uint8_t* a = rgba.data() + size_t(y) * row;
        uint8_t* b = rgba.data() + size_t(captureH_ - 1 - y) * row;
        std::copy_n(a, row, tmp.data());
        std::copy_n(b, row, a);
        std::copy_n(tmp.data(), row, b);
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::draw(const Camera& camera, int targetWidth, int targetHeight, const RenderSettings& settings,
                    const Viewport& viewport) {
    syncMatcaps();
    forceBackfaces_ = settings.backfaceCulling && !thumbnailPass_; // (#68; thumbnails show the whole Part)
    // Known state before the clears: a scissor box, colour mask or depth mask left over from another pass (UI,
    // overlays, calibration) makes glClear partial. Some drivers (NVIDIA on Windows, #29) then show old frames
    // under the cursor and stale depth hides the mesh.
    gl::Disable(GL_SCISSOR_TEST);
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::DepthMask(GL_TRUE);
    gl::Viewport(0, 0, targetWidth, targetHeight);
    const bool inset = viewport.w > 0 && viewport.h > 0;
    if (inset) {
        gl::ClearColor(viewport.surround.x, viewport.surround.y, viewport.surround.z, 1.0f);
        gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl::Viewport(viewport.x, viewport.y, viewport.w, viewport.h);
        gl::Scissor(viewport.x, viewport.y, viewport.w, viewport.h);
        gl::Enable(GL_SCISSOR_TEST);
    }
    const int fbWidth = inset ? viewport.w : targetWidth, fbHeight = inset ? viewport.h : targetHeight;
    if (thumbnailPass_) gl::ClearColor(0.0f, 0.0f, 0.0f, 0.0f); // renderMeshThumbnail: transparent background
    else if (!settings.gradientBackground)
        gl::ClearColor(settings.backgroundColor.x, settings.backgroundColor.y, settings.backgroundColor.z, 1.0f);
    else gl::ClearColor(0.075f, 0.075f, 0.08f, 1.0f);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl::Enable(GL_MULTISAMPLE);
    struct ScissorOff {
        bool on;
        ~ScissorOff() {
            if (on) gl::Disable(GL_SCISSOR_TEST);
        }
    } scissorOff{inset};

    const glm::mat4 view = camera.view(), proj = camera.projection();
    // PBR view: the environment's lighting (built when it first shows), and the rotation from view space into it.
    // (Thumbnails use whatever environment the view has: their settings must not start another build.)
    const bool pbr = settings.pbr() && (thumbnailPass_ ? envTexture_ != 0 : ensureEnvironment(settings.environment));
    if (pbr) {
        const float yaw = glm::radians(settings.environmentRotation);
        const glm::mat3 spin(glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw)), glm::vec3(0.0f, 1.0f, 0.0f),
                             glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)));
        frameViewToEnv_ = spin * glm::transpose(glm::mat3(view));
    }

    // Background: charcoal with a radial vignette (or the flat colour cleared above); in the PBR view optionally
    // the environment itself, blurred.
    gl::Disable(GL_DEPTH_TEST);
    gl::BindVertexArray(emptyVao_);
    if (!thumbnailPass_ && pbr && settings.environmentBackground) {
        if (!envBgProgram_) {
            std::string error;
            envBgProgram_ = link(kBgVS, kEnvBgFS, error);
        }
        if (envBgProgram_) {
            gl::UseProgram(envBgProgram_);
            auto at = [&](const char* n) { return gl::GetUniformLocation(envBgProgram_, n); };
            const glm::mat4 invProj = glm::inverse(proj);
            gl::UniformMatrix4fv(at("uInvProj"), 1, GL_FALSE, glm::value_ptr(invProj));
            gl::UniformMatrix3fv(at("uViewToEnv"), 1, GL_FALSE, glm::value_ptr(frameViewToEnv_));
            gl::Uniform1f(at("uLod"), std::clamp(settings.backgroundBlur, 0.0f, 1.0f) * float(kEnvLevels - 1));
            gl::Uniform1f(at("uExposure"), settings.exposure);
            gl::Uniform1i(at("uToneMap"), int(settings.toneMap));
            gl::Uniform1i(at("uEnvTex"), 10);
            gl::ActiveTexture(kTexture10);
            gl::BindTexture(GL_TEXTURE_2D, envTexture_);
            gl::ActiveTexture(GL_TEXTURE0);
            gl::DrawArrays(GL_TRIANGLES, 0, 3);
        }
    } else if (!thumbnailPass_ && settings.gradientBackground) {
        gl::UseProgram(bgProgram_);
        gl::Uniform3fv(gl::GetUniformLocation(bgProgram_, "uCentre"), 1, glm::value_ptr(settings.gradientCentre));
        gl::Uniform3fv(gl::GetUniformLocation(bgProgram_, "uEdge"), 1, glm::value_ptr(settings.gradientEdge));
        gl::DrawArrays(GL_TRIANGLES, 0, 3);
    }

    frameView_ = view;
    frameProj_ = proj;
    frameViewport_ = inset ? glm::vec4(float(viewport.x), float(viewport.y), float(viewport.w), float(viewport.h))
                           : glm::vec4(0.0f, 0.0f, float(targetWidth), float(targetHeight));
    frameCull_ = CullView::from(view, proj);
    lastDrawn_ = 0;
    gl::Enable(GL_DEPTH_TEST);
    gl::DepthFunc(GL_LEQUAL);
    if (drawList_.empty() || !settings.showMesh) {
        if (settings.showSplats) drawSplats(view, proj, fbWidth, fbHeight, settings.splatOpacity);
        gl::UseProgram(0);
        return;
    }

    auto findSlot = [this](uint32_t id) -> const MeshSlot* {
        for (const auto& s : slots_)
            if (s.id == id) return &s;
        return nullptr;
    };

    gl::ActiveTexture(kTexture2);
    gl::BindTexture(GL_TEXTURE_2D, matcapTexture(settings.matcap));
    gl::ActiveTexture(GL_TEXTURE0);
    gl::Enable(GL_POLYGON_OFFSET_FILL);
    gl::PolygonOffset(1.0f, 1.0f);
    for (const DrawItem& item : drawList_)
        if (const MeshSlot* s = findSlot(item.id); s && s->drawnTriangles > 0) drawSlot(*s, item.active, item.flat, settings);
    proxyUsed_ = settings.motionProxy;
    gl::Disable(GL_POLYGON_OFFSET_FILL);
    for (GLenum unit : {kTexture7, kTexture6, GL_TEXTURE3, GL_TEXTURE1}) {
        gl::ActiveTexture(unit);
        gl::BindTexture(GL_TEXTURE_BUFFER, 0);
    }
    for (GLenum unit : {kTexture11, kTexture10, kTexture9, kTexture8, kTexture5, GL_TEXTURE4, kTexture2}) {
        gl::ActiveTexture(unit);
        gl::BindTexture(GL_TEXTURE_2D, 0);
    }
    gl::ActiveTexture(GL_TEXTURE0);

    if (settings.wireframe) { // the active SubTool only
        gl::UseProgram(wireProgram_);
        gl::UniformMatrix4fv(gl::GetUniformLocation(wireProgram_, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        gl::UniformMatrix4fv(gl::GetUniformLocation(wireProgram_, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        gl::Enable(GL_BLEND);
        gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl::DepthMask(GL_FALSE);
        for (const DrawItem& item : drawList_) {
            const MeshSlot* s = item.active ? findSlot(item.id) : nullptr;
            if (!s || s->drawnTriangles == 0 || s->micro) continue; // (micro levels: no wireframe)
            gl::Uniform3fv(gl::GetUniformLocation(wireProgram_, "uGridOrigin"), 1, glm::value_ptr(s->grid.origin));
            gl::Uniform1f(gl::GetUniformLocation(wireProgram_, "uGridStep"), s->grid.step);
            cull(*s, frameCull_);
            if (!s->patches.empty() && s->patchVao) { // dynamic topology's patches
                const bool lines = s->patchLinesOn;
                gl::Uniform1f(gl::GetUniformLocation(wireProgram_, "uAlpha"), lines ? 0.5775f : 0.35f);
                if (!lines) gl::PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                gl::BindVertexArray(lines ? s->patchLineVao : s->patchVao);
                drawPatches(*s, lines ? GL_LINES : GL_TRIANGLES, lines ? Part::Lines : Part::Triangles, -1);
                if (!lines) gl::PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
            if (s->linesValid) {
                gl::Uniform1f(gl::GetUniformLocation(wireProgram_, "uAlpha"), 0.5775f); // 1 - (1 - 0.35)^2
                gl::BindVertexArray(s->lineVao);
                drawClusters(*s, drawClusters_, GL_LINES, Part::Lines, -1);
            } else { // edges not built (a mesh not synced for the wireframe): outline the triangles
                gl::Uniform1f(gl::GetUniformLocation(wireProgram_, "uAlpha"), 0.35f);
                gl::PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                gl::BindVertexArray(s->vao);
                drawClusters(*s, drawClusters_, GL_TRIANGLES, Part::Triangles, -1);
                gl::PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
        }
        gl::DepthMask(GL_TRUE);
        gl::Disable(GL_BLEND);
    }
    gl::BindVertexArray(0);
    if (settings.showSplats) drawSplats(view, proj, fbWidth, fbHeight, settings.splatOpacity);
    gl::UseProgram(0);
}

// The clusters of `s` inside the view, less those facing away when that can't open a hole: the mesh is closed
// and wound consistently (closedOriented) with nothing hidden, the eye is outside its bounds, and no cluster
// reaches into the near plane's slab (a cut-open front would show the back faces behind it).
void Renderer::cull(const MeshSlot& s, const CullView& frame, bool proxy) {
    drawClusters_.clear();
    proxyClusters_.clear();
    proxy = proxy && s.proxyCount.size() == s.drawCount.size() && s.proxyBounds.size() == s.drawCount.size();
    CullView v = frame;
    const glm::vec3 e = v.eye;
    // Backface culling on (#68): back faces go on any mesh from any view, the GPU skipping each one and whole
    // clusters facing away going first (in perspective: an orthographic view has no eye to judge them from).
    if (forceBackfaces_) {
        v.backfaces = !v.ortho;
        cullBackfaces_ = true;
        const size_t n = std::min(s.bounds.size(), s.drawCount.size());
        for (uint32_t c = 0; c < n; ++c) {
            if (s.drawCount[c] == 0 || !insideFrustum(v, s.bounds[c])) continue;
            if (proxy && s.proxyCount[c] > 0 && !s.proxyStale[c]) {
                if (!facingAway(v, s.proxyBounds[c])) proxyClusters_.push_back(c);
            } else if (!facingAway(v, s.bounds[c])) {
                drawClusters_.push_back(c);
            }
        }
        return;
    }
    v.backfaces = !v.ortho && s.closed && s.volume > 0.0 && !s.anyHidden && s.box.valid() &&
                  (e.x < s.box.min.x || e.y < s.box.min.y || e.z < s.box.min.z || e.x > s.box.max.x || e.y > s.box.max.y ||
                   e.z > s.box.max.z);
    auto reachesNear = [&](const ClusterBounds& b) {
        if (b.radius < 0.0f) return false;
        const float z = glm::dot(b.center - e, v.forward);
        return z - b.radius < v.nearDistance * 1.01f && z + b.radius > 0.0f;
    };
    if (v.backfaces)
        for (const ClusterBounds& b : s.bounds)
            if (reachesNear(b)) {
                v.backfaces = false;
                break;
            }
    if (v.backfaces)
        for (const MeshSlot::Patch& p : s.patches)
            if (reachesNear(p.bounds)) {
                v.backfaces = false;
                break;
            }
    cullBackfaces_ = v.backfaces;
    const size_t nc = std::min(s.bounds.size(), s.drawCount.size());
    for (uint32_t c = 0; c < nc; ++c) {
        if (s.drawCount[c] == 0) continue;
        const ClusterBounds& b = s.bounds[c];
        if (!insideFrustum(v, b)) continue;
        if (proxy && s.proxyCount[c] > 0 && !s.proxyStale[c]) {
            if (!facingAway(v, s.proxyBounds[c])) proxyClusters_.push_back(c);
        } else if (!facingAway(v, b)) {
            drawClusters_.push_back(c);
        }
    }
}

// Draws `clusters` of `s` (the VAO for `part` bound): their triangles, the wireframe's edges, or their motion
// proxies. One multi-draw, or with per-triangle data on a driver whose gl_VertexID lacks the base vertex, one
// draw per cluster with its first drawn triangle in `primBaseLocation`.
void Renderer::drawClusters(const MeshSlot& s, const std::vector<uint32_t>& clusters, GLenum mode, Part part, GLint primBaseLocation) {
    drawCounts_.clear();
    drawOffsets_.clear();
    drawBases_.clear();
    const auto layout = s.order.clusters();
    size_t indices = 0;
    for (uint32_t c : clusters) {
        GLsizei count = 0;
        size_t first = 0;
        switch (part) {
        case Part::Triangles:
            count = s.drawCount[c];
            first = size_t(s.order.firstTriangle(c)) * 3;
            break;
        case Part::Lines:
            count = GLsizei(s.lineCount[c]);
            first = s.lineFirst[c];
            break;
        case Part::Proxy:
            count = GLsizei(s.proxyCount[c]);
            first = size_t(s.order.firstTriangle(c) / 4) * 3;
            break;
        }
        drawCounts_.push_back(count);
        drawOffsets_.push_back(reinterpret_cast<const void*>(first * sizeof(uint16_t)));
        drawBases_.push_back(GLint(layout[c].firstVertex));
        indices += size_t(count);
    }
    if (drawCounts_.empty()) return;
    if (part != Part::Lines) lastDrawn_ += indices / 3;
    if (primBaseLocation >= 0 && !vertexIdHasBase_) {
        for (size_t i = 0; i < clusters.size(); ++i) {
            if (drawCounts_[i] == 0) continue;
            gl::Uniform1i(primBaseLocation, GLint(s.primBase[clusters[i]]));
            gl::DrawElementsBaseVertex(mode, drawCounts_[i], GL_UNSIGNED_SHORT, drawOffsets_[i], drawBases_[i]);
        }
        return;
    }
    if (primBaseLocation >= 0) gl::Uniform1i(primBaseLocation, -1);
    gl::MultiDrawElementsBaseVertex(mode, drawCounts_.data(), GL_UNSIGNED_SHORT, drawOffsets_.data(), GLsizei(drawCounts_.size()),
                                    drawBases_.data());
}

// One mesh. Mask and polygroup colours are for the SubTool being edited; polypaint shows on all of them.
// The material tint a mesh gets (RenderSettings::colorTintsMaterial), less 1: Lit Sphere shading, no vertex paint.
static glm::vec3 materialTintShift(const RenderSettings& settings, bool hasColors) {
    if (!settings.colorTintsMaterial || settings.pbr() || hasColors) return glm::vec3(0.0f);
    return settings.materialTint - glm::vec3(1.0f);
}

void Renderer::drawMicro(const MeshSlot& s, bool active, bool flat, const RenderSettings& settings) {
    const MeshProgram* p = meshProgram(kMicro | (flat ? kFlat : 0) | (settings.pbr() && envTexture_ ? kPbr : 0));
    if (!p) return;
    gl::UseProgram(p->id);
    setPbrUniforms(*p, settings, false, s);
    gl::UniformMatrix4fv(p->view, 1, GL_FALSE, glm::value_ptr(frameView_));
    gl::UniformMatrix4fv(p->proj, 1, GL_FALSE, glm::value_ptr(frameProj_));
    gl::Uniform4fv(p->viewport, 1, glm::value_ptr(frameViewport_));
    const bool orthographic = frameProj_[3][3] == 1.0f;
    gl::Uniform2f(p->projScale, orthographic ? 1e20f : frameProj_[0][0], orthographic ? 1e20f : frameProj_[1][1]);
    gl::Uniform3fv(p->baseColor, 1, glm::value_ptr(settings.baseColor));
    gl::Uniform3fv(p->tintShift, 1, glm::value_ptr(materialTintShift(settings, s.hasColors)));
    gl::Uniform1i(p->matcap, matcapTexture(settings.matcap) ? 1 : 0);
    const MatcapCavity cavity = matcapCavity(settings.matcap);
    gl::Uniform2f(p->matcapCavity, cavity.crease, cavity.ridge);
    gl::Uniform1i(p->paint, 0);
    gl::Uniform1i(p->showMask, active && settings.showMask ? 1 : 0);
    gl::Uniform1i(p->groups, active && settings.showPolygroups ? 2 : 0);
    gl::Uniform1i(p->dim, !active && settings.dimInactive ? 1 : 0);
    gl::Uniform1i(p->twoSided, 1);
    // Moving (Fast Navigation): a coarser target and half the budget, so orbiting stays smooth.
    const bool moving = settings.motionProxy;
    const size_t full = settings.renderTriangleBudget ? settings.renderTriangleBudget : size_t(currentTierConfig().activeTriangleBudget);
    const size_t budget = moving ? full / 2 : full;
    const float target = settings.microTargetPx * (moving ? 2.0f : 1.0f);
    // (the budget is shared by the micro SubTools on screen: each gets it whole; one is the usual case)
    const_cast<MeshSlot&>(s).microDrawn = s.micro->draw(p->id, frameCull_, frameProj_, frameViewport_, budget, target);
    lastDrawn_ += s.microDrawn;
}

void Renderer::drawSlot(const MeshSlot& s, bool active, bool flat, const RenderSettings& settings) {
    if (s.micro) {
        drawMicro(s, active, flat, settings);
        return;
    }
    // The UV Editor's checker on the model.
    const bool checker = active && s.hasUvs && s.overlay && s.overlay->checker;
    // A painted texture shows through the textured program (polypaint still multiplies it).
    const bool textured = s.hasUvs && ((s.paintTexture && settings.showPolypaint && settings.showTexture) || checker);
    const bool pbr = settings.pbr() && envTexture_;
    // A baked normal map preview bends the shading normal (core/Bake.h), and in the PBR view the texture's
    // Normal / Height channels do (syncTexture's material normal, with tangents from syncNormalMap).
    const GLuint normalTex = s.normalTexture ? s.normalTexture : (pbr && textured ? s.matNormalTexture : 0);
    const bool normalMapped = s.hasUvs && normalTex && s.hasTangents;
    const int groups = active && s.overlayColors                 ? 3
                       : active && settings.showPolygroups && !s.overlayColors ? (s.hasGroups ? 1 : 2)
                                                                               : 0;
    // (Not through the textured program's geometry shader: those meshes are sculpted without the preview.)
    const bool ghost = active && s.id == ghostId_ && !ghost_.empty() && !settings.motionProxy && !textured && !normalMapped;
    const int features = (s.hasColors || s.hasMask ? kPaint : 0) | (groups == 1 || groups == 3 ? kPrim : 0) |
                         (textured ? kTextured : 0) | (normalMapped ? kNormalMap : 0) | (flat ? kFlat : 0) | (pbr ? kPbr : 0);
    const MeshProgram* p = meshProgram(features);
    if (!p) return;
    const auto bind = [&](const MeshProgram* p) {
    gl::UseProgram(p->id);
    gl::UniformMatrix4fv(p->view, 1, GL_FALSE, glm::value_ptr(frameView_));
    gl::UniformMatrix4fv(p->proj, 1, GL_FALSE, glm::value_ptr(frameProj_));
    gl::Uniform3fv(p->gridOrigin, 1, glm::value_ptr(s.grid.origin));
    gl::Uniform1f(p->gridStep, s.grid.step);
    gl::Uniform4fv(p->viewport, 1, glm::value_ptr(frameViewport_));
    // The shader turns a pixel into its view ray as (-ndc / scale, 1): right for perspective. An orthographic
    // view (Numpad 5) looks straight along the axis at every pixel, which an unbounded scale gives (its own
    // scale tilted the ray at the edges until the matcap and the lights came from the other side, #24).
    const bool orthographic = frameProj_[3][3] == 1.0f;
    gl::Uniform2f(p->projScale, orthographic ? 1e20f : frameProj_[0][0], orthographic ? 1e20f : frameProj_[1][1]);
    gl::Uniform3fv(p->baseColor, 1, glm::value_ptr(settings.baseColor));
    gl::Uniform3fv(p->tintShift, 1, glm::value_ptr(materialTintShift(settings, s.hasColors)));
    gl::Uniform1i(p->matcap, matcapTexture(settings.matcap) ? 1 : 0);
    const MatcapCavity cavity = matcapCavity(settings.matcap);
    gl::Uniform2f(p->matcapCavity, cavity.crease, cavity.ridge);
    if (textured || normalMapped) {
        gl::ActiveTexture(GL_TEXTURE3);
        gl::BindTexture(GL_TEXTURE_BUFFER, s.uvTexture);
        gl::ActiveTexture(GL_TEXTURE4);
        gl::BindTexture(GL_TEXTURE_2D, textured ? s.paintTexture : 0);
    }
    if (normalMapped) {
        gl::ActiveTexture(kTexture5);
        gl::BindTexture(GL_TEXTURE_2D, normalTex);
        gl::ActiveTexture(kTexture6);
        gl::BindTexture(GL_TEXTURE_BUFFER, s.tangentTexture);
        gl::Uniform1i(p->hasPaint, textured ? 1 : 0);
        gl::Uniform1i(p->normalFlipY, s.normalTexture && s.normalFlipGreen ? 1 : 0);
    }
    if (pbr) setPbrUniforms(*p, settings, textured, s);
    gl::ActiveTexture(kTexture7);
    gl::BindTexture(GL_TEXTURE_BUFFER, s.primTexture);
    gl::ActiveTexture(GL_TEXTURE1);
    gl::BindTexture(GL_TEXTURE_BUFFER, s.hasGroups ? s.groupTexture : 0);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::Uniform1i(p->paint, settings.showPolypaint && s.hasColors ? 1 : 0);
    gl::Uniform1i(p->showMask, active && settings.showMask && s.hasMask ? 1 : 0);
    gl::Uniform1i(p->groups, groups);
    gl::Uniform1i(p->dim, !active && settings.dimInactive ? 1 : 0);
    gl::Uniform1i(p->twoSided, 1);
    gl::Uniform1i(p->checker, checker ? 1 : 0);
    gl::Uniform1f(p->checkerScale, checker ? s.overlay->checkerScale : 8.0f);
    };
    bind(p);
    gl::BindVertexArray(s.vao);
    // Constant values for the attribute arrays that are disabled.
    if (!s.hasColors) gl::VertexAttrib4f(2, 1.0f, 1.0f, 1.0f, 1.0f);
    if (!s.hasMask) gl::VertexAttrib1f(3, 0.0f);
    // While the view moves, the motion proxy where it is built: not with per-triangle data, which follows the
    // full triangles, nor under the wireframe, whose edges would sink into it.
    const bool perTriangle = features & (kPrim | kTextured | kNormalMap); // (not kFlat: no per-triangle data)
    cull(s, frameCull_, settings.motionProxy && !perTriangle && !(active && settings.wireframe));
    // The back faces left in clusters along the silhouette: the GPU skips them too when they can't show.
    if (cullBackfaces_) gl::Enable(GL_CULL_FACE);
    // The predictive preview (render/Ghost.h): the clusters a ghost dab reaches draw with the GHOST program, the
    // rest as usual (most of the mesh, which then doesn't pay for the preview).
    // A draft stroke (Sculptor::draftStrokes) left the normals of the clusters it moved as they were: those draw
    // with normals from the moved triangles themselves (the FLAT program's per-pixel face normals) until it ends.
    const bool draft = active && s.id == draftId_ && !perTriangle && s.looseMark.size() == s.bounds.size();
    for (auto& list : extraClusters_) list.clear();
    if (ghost || draft) {
        size_t kept = 0;
        for (uint32_t c : drawClusters_) {
            const ClusterBounds& b = s.bounds[c];
            bool reached = false;
            if (ghost) {
                reached = b.radius < 0.0f;
                for (size_t i = 0; i < ghost_.dabs.size() && !reached; ++i)
                    reached = glm::distance(b.center, ghost_.dabs[i].center) < b.radius + ghost_.dabs[i].radius * 1.05f;
            }
            const bool rough = draft && s.looseMark[c];
            if (reached || rough) extraClusters_[(reached ? 1 : 0) | (rough ? 2 : 0)].push_back(c);
            else drawClusters_[kept++] = c;
        }
        drawClusters_.resize(kept);
    }
    drawClusters(s, drawClusters_, GL_TRIANGLES, Part::Triangles, perTriangle ? p->primBase : -1);
    for (int k = 1; k < 4; ++k) {
        if (extraClusters_[k].empty()) continue;
        if (const MeshProgram* pg = meshProgram(features | ((k & 1) ? kGhost : 0) | ((k & 2) ? kFlat : 0))) {
            bind(pg);
            if (k & 1) setGhostUniforms(*pg);
            drawClusters(s, extraClusters_[k], GL_TRIANGLES, Part::Triangles, -1);
            bind(p);
        }
    }
    if (!s.patches.empty() && s.patchVao) { // dynamic topology: what changed since the layout (in full while moving)
        gl::BindVertexArray(s.patchVao);
        if (!s.patchColorsOn) gl::VertexAttrib4f(2, 1.0f, 1.0f, 1.0f, 1.0f);
        if (!s.patchMaskOn) gl::VertexAttrib1f(3, 0.0f);
        if (perTriangle) {
            gl::ActiveTexture(GL_TEXTURE1);
            gl::BindTexture(GL_TEXTURE_BUFFER, s.hasGroups ? s.patchGroupTexture : 0);
            gl::ActiveTexture(GL_TEXTURE0);
        }
        drawPatches(s, GL_TRIANGLES, Part::Triangles, perTriangle ? p->primBase : -1);
        gl::BindVertexArray(s.vao);
    }
    if (!proxyClusters_.empty()) {
        gl::BindVertexArray(s.proxyVao);
        if (!s.hasColors) gl::VertexAttrib4f(2, 1.0f, 1.0f, 1.0f, 1.0f);
        if (!s.hasMask) gl::VertexAttrib1f(3, 0.0f);
        gl::Uniform1i(p->twoSided, 0);
        drawClusters(s, proxyClusters_, GL_TRIANGLES, Part::Proxy, -1);
    }
    gl::Disable(GL_CULL_FACE);
}

// ---------------------------------------------------------------------------
// Strokes on heavy meshes: the ghost and the two-part upload (app/AsyncStroke.cpp)
// ---------------------------------------------------------------------------

void Renderer::setGhost(uint32_t id, const GhostStroke& ghost) {
    ghostId_ = id;
    ghost_.kind = ghost.kind;
    ghost_.invert = ghost.invert;
    ghost_.focal = ghost.focal;
    ghost_.edge = ghost.edge;
    ghost_.dabs.assign(ghost.dabs.begin(), ghost.dabs.begin() + std::ptrdiff_t(std::min(ghost.dabs.size(), GhostStroke::kMaxDabs)));
}

void Renderer::setGhostUniforms(const MeshProgram& p) {
    glm::vec4 a[GhostStroke::kMaxDabs], b[GhostStroke::kMaxDabs], c[GhostStroke::kMaxDabs];
    glm::vec4 bound[GhostStroke::kMaxMirrors];
    GLint range[GhostStroke::kMaxMirrors * 2];
    int groups = 0;
    const size_t n = ghost_.dabs.size();
    for (size_t i = 0; i < n; ++i) {
        const GhostDab& d = ghost_.dabs[i];
        a[i] = glm::vec4(d.center, d.radius);
        b[i] = glm::vec4(d.normal, d.strength);
        c[i] = glm::vec4(d.areaCenter, d.weight);
        const bool next = i == 0 || d.mirror != ghost_.dabs[i - 1].mirror;
        if (next) {
            if (groups == int(GhostStroke::kMaxMirrors)) break;
            range[groups * 2] = range[groups * 2 + 1] = GLint(i);
            ++groups;
        }
        range[groups * 2 - 1] = GLint(i + 1);
    }
    // Each run's bounding sphere (around its dabs' spheres, a little wider: the moves reach out a little).
    for (int g = 0; g < groups; ++g) {
        AABB box;
        for (GLint i = range[g * 2]; i < range[g * 2 + 1]; ++i) {
            box.expand(glm::vec3(a[i]) - glm::vec3(a[i].w));
            box.expand(glm::vec3(a[i]) + glm::vec3(a[i].w));
        }
        bound[g] = glm::vec4(box.center(), glm::length(box.max - box.min) * 0.5f * 1.1f);
    }
    gl::Uniform1i(p.ghostGroups, groups);
    gl::Uniform4fv(p.ghostBound, groups, glm::value_ptr(bound[0]));
    gl::Uniform2iv(p.ghostRange, groups, range);
    gl::Uniform4fv(p.ghostA, GLsizei(n), glm::value_ptr(a[0]));
    gl::Uniform4fv(p.ghostB, GLsizei(n), glm::value_ptr(b[0]));
    gl::Uniform4fv(p.ghostC, GLsizei(n), glm::value_ptr(c[0]));
    gl::Uniform2i(p.ghostKind, GLint(ghost_.kind), ghost_.invert ? 1 : 0);
    gl::Uniform1f(p.ghostFocal, ghost_.focal);
    gl::Uniform1f(p.ghostEdge, ghost_.edge);
}

bool Renderer::prepareStrokeUpload(uint32_t id, Mesh& mesh, StrokeUpload& out, const float* curvature) {
    out.id = id;
    out.ready = false;
    out.data.clear();
    out.ranges.clear();
    out.movedClusters.clear();
    out.bounds.clear();
    out.exactClusters.clear();
    out.exactBounds.clear();
    out.regrid = false;
    out.packMs = out.boundsMs = 0.0;
    const auto clock = std::chrono::steady_clock::now();
    MeshSlot* found = static_cast<MeshSlot*>(out.slot); // (bindStrokeUpload: never the list from this thread)
    if (!found || found->id != id) return false;
    MeshSlot& s = *found;
    const size_t nv = mesh.vertexCount();
    // What only the regular sync handles: a new layout, faces, attributes appearing, dynamic topology, textures.
    if (s.micro || s.fresh || s.outOfMemory || s.regrid || mesh.topologyDirty || mesh.facesDirty || nv != s.vertexCapacity ||
        !mesh.changedTriangles.empty() || !s.patches.empty() || mesh.normals.size() != nv || s.hasUvs ||
        s.hasColors != (nv > 0 && mesh.colors.size() == nv) || s.hasMask != (nv > 0 && mesh.mask.size() == nv))
        return false;
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    s.order.dirtyRuns(mesh, runs);
    if (runs.empty()) {
        out.ready = true;
        return true;
    }
    // The colours and mask of the dirty runs would go up too (a sculpt stroke doesn't change them; a stroke that
    // does takes the regular sync).
    const size_t ns = s.order.size();
    const size_t nb = (ns + UploadOrder::kBlock - 1) / UploadOrder::kBlock;
    const std::span<const uint32_t> order = s.order.fromGpu();
    s.blockHash.resize(nb * 2, 0);
    out.blockChanged.assign(nb, 0);
    out.blockMoved.assign(nb, 0);
    // Pieces of a few thousand slots packed in parallel, each at its offset in `data`.
    constexpr uint32_t kPiece = 2048;
    struct Piece {
        uint32_t begin, end;
        size_t offset;
    };
    std::vector<Piece> pieces;
    const auto cut = [&] {
        pieces.clear();
        size_t total = 0;
        for (const auto& [b, e] : runs)
            for (uint32_t c = b; c < e; c += kPiece) {
                const uint32_t ce = std::min(e, c + kPiece);
                pieces.push_back({c, ce, total});
                total += ce - c;
            }
        out.data.resize(total);
    };
    cut();
    std::atomic<bool> outside{false};
    std::vector<uint64_t> hashes(nb * 2, 0); // (written per block, then compared: a failed prepare keeps the old)
    std::vector<AABB> boxes(nb);
    s.blockBox.resize(nb);
    PositionGrid grid = s.grid;
    bool everything = false;
    const auto pack = [&] {
    poolFor(pieces.size(), [&](size_t i0, size_t i1) {
        for (size_t pi = i0; pi < i1; ++pi) {
            const Piece& pc = pieces[pi];
            PackedVertex* dst = out.data.data() + pc.offset;
            bool inside = true;
            uint64_t hp = 0, hn = 0;
            AABB box;
            for (uint32_t i = pc.begin; i < pc.end; ++i) {
                const uint32_t v = order[i];
                PackedVertex pv{};
                if (v < nv) {
                    inside &= packVertex(grid, mesh.positions[v], mesh.normals[v], curvature ? curvature[v] : curvatureAt(mesh, v), pv);
                    box.expand(mesh.positions[v]);
                }
                dst[i - pc.begin] = pv;
                hp = (hp ^ (uint64_t(pv.pos[0]) | uint64_t(pv.pos[1]) << 32)) * 0x100000001B3ull + i;
                hn = (hn ^ pv.normal) * 0x100000001B3ull + i;
                if ((i + 1) % UploadOrder::kBlock == 0 || i + 1 == pc.end) {
                    const size_t g = i >> UploadOrder::kBlockShift;
                    hashes[g * 2] = hp;
                    hashes[g * 2 + 1] = hn;
                    out.blockMoved[g] = everything || s.blockHash[g * 2] != hp;
                    out.blockChanged[g] = out.blockMoved[g] || s.blockHash[g * 2 + 1] != hn;
                    boxes[g] = box;
                    hp = hn = 0;
                    box = AABB();
                }
            }
            if (!inside) outside.store(true, std::memory_order_relaxed);
        }
    }, 1, 2);
    };
    pack();
    if (outside.load()) {
        // A vertex left the position grid: a new one round the mesh, and every vertex packed again, here on this
        // thread (the GL thread keeps drawing with the old grid and data until the commit swaps both; the
        // regular sync did this in the frame, a stall of seconds at 33M triangles).
        grid = PositionGrid::around(meshBounds(mesh));
        runs.assign(1, {0u, uint32_t(ns)});
        cut();
        everything = true;
        outside = false;
        pack();
        if (outside.load()) return false; // (can't happen: the grid holds the mesh)
        out.regrid = true;
        out.grid = grid;
    }
    out.packMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count();
    // The changed blocks' GPU ranges and where they are in `data`.
    for (const Piece& pc : pieces)
        for (uint32_t b = pc.begin; b < pc.end;) {
            const uint32_t e = std::min(pc.end, (b & ~(UploadOrder::kBlock - 1)) + UploadOrder::kBlock);
            const size_t g = b >> UploadOrder::kBlockShift;
            s.blockBox[g] = boxes[g];
            if (out.blockChanged[g]) {
                s.blockHash[g * 2] = hashes[g * 2];
                s.blockHash[g * 2 + 1] = hashes[g * 2 + 1];
                const size_t offset = pc.offset + (b - pc.begin);
                if (!out.ranges.empty() && out.ranges.back().end == b && out.ranges.back().offset +
                                                                          (out.ranges.back().end - out.ranges.back().begin) == offset)
                    out.ranges.back().end = e;
                else out.ranges.push_back({b, e, offset});
            }
            b = e;
        }
    // The clusters with a block that moved, and their culling bounds.
    for (const auto& [b, e] : runs)
        for (uint32_t g = b >> UploadOrder::kBlockShift; g < (e + UploadOrder::kBlock - 1) >> UploadOrder::kBlockShift; ++g) {
            if (!out.blockMoved[g]) continue;
            const uint32_t c = s.order.clusterOfSlot(g << UploadOrder::kBlockShift);
            if (out.movedClusters.empty() || c > out.movedClusters.back()) out.movedClusters.push_back(c);
        }
    // Their quick bounds: a sphere round their blocks' boxes, no normal cone (never culled as facing away), the
    // volume as it was (a stroke doesn't turn the mesh inside out); refineStrokeBounds makes them exact.
    out.bounds.resize(out.movedClusters.size());
    const auto layout = s.order.clusters();
    for (size_t i = 0; i < out.movedClusters.size(); ++i) {
        const uint32_t c = out.movedClusters[i];
        AABB box;
        for (uint32_t g = layout[c].firstVertex >> UploadOrder::kBlockShift,
                      ge = (layout[c].firstVertex + layout[c].vertexCount + UploadOrder::kBlock - 1) >> UploadOrder::kBlockShift;
             g < ge && g < nb; ++g)
            if (s.blockBox[g].valid()) box.expand(s.blockBox[g]);
        ClusterBounds& cb = out.bounds[i];
        cb = ClusterBounds();
        if (box.valid()) {
            cb.center = box.center();
            cb.radius = glm::length(box.max - box.min) * 0.5f;
        }
        cb.coneAngle = -1.0f;
        cb.volume = c < s.bounds.size() ? s.bounds[c].volume : 0.0;
    }
    mesh.clearDirty();
    out.boundsMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count() - out.packMs;
    out.ready = true;
    return true;
}

void Renderer::commitStrokeUpload(StrokeUpload& up) {
    if (!up.ready) return;
    up.ready = false;
    MeshSlot* found = nullptr;
    for (MeshSlot& s : slots_)
        if (s.id == up.id) found = &s;
    if (!found) return;
    MeshSlot& s = *found;
    if (up.regrid) s.grid = up.grid; // (with every vertex, packed on it)
    if (!up.ranges.empty()) {
        gl::BindBuffer(GL_ARRAY_BUFFER, s.vertexBuffer);
        for (const StrokeUpload::Range& r : up.ranges)
            gl::BufferSubData(GL_ARRAY_BUFFER, GLintptr(size_t(r.begin) * sizeof(PackedVertex)),
                              GLsizeiptr(size_t(r.end - r.begin) * sizeof(PackedVertex)), up.data.data() + r.offset);
        gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    }
    if (!up.movedClusters.empty()) {
        s.bounds.resize(s.order.clusters().size());
        s.proxyStale.resize(s.bounds.size(), 1);
        for (size_t i = 0; i < up.movedClusters.size(); ++i) {
            s.bounds[up.movedClusters[i]] = up.bounds[i];
            s.proxyStale[up.movedClusters[i]] = 1;
        }
        s.looseClusters.insert(s.looseClusters.end(), up.movedClusters.begin(), up.movedClusters.end());
        s.looseMark.resize(s.bounds.size(), 0);
        for (uint32_t c : up.movedClusters) s.looseMark[c] = 1;
    }
    if (up.exact) { // the stroke's end (refineStrokeBoundsInto): exact bounds for every cluster it moved
        s.bounds.resize(s.order.clusters().size());
        for (size_t i = 0; i < up.exactClusters.size(); ++i)
            if (up.exactClusters[i] < s.bounds.size()) s.bounds[up.exactClusters[i]] = up.exactBounds[i];
        s.looseClusters.clear();
        std::fill(s.looseMark.begin(), s.looseMark.end(), 0);
    }
    if (!up.movedClusters.empty() || up.exact) {
        s.box = AABB();
        s.volume = 0.0;
        for (const ClusterBounds& b : s.bounds) {
            s.volume += b.volume;
            if (b.radius >= 0.0f) {
                s.box.expand(b.center - glm::vec3(b.radius));
                s.box.expand(b.center + glm::vec3(b.radius));
            }
        }
    }
}

void Renderer::setDraftNormals(uint32_t id) { draftId_ = id; }

void Renderer::bindStrokeUpload(uint32_t id, StrokeUpload& up) {
    up.slot = nullptr;
    for (MeshSlot& s : slots_)
        if (s.id == id) up.slot = &s;
}

void Renderer::refineStrokeBoundsInto(uint32_t id, const Mesh& mesh, StrokeUpload& up) {
    up.exact = false;
    up.exactClusters.clear();
    up.exactBounds.clear();
    MeshSlot* found = static_cast<MeshSlot*>(up.slot);
    if (!found || found->id != id) return;
    MeshSlot& s = *found;
    const size_t nc = s.order.clusters().size();
    if (s.fresh || mesh.topologyDirty || s.bounds.size() != nc) return;
    // Those moved before and those this upload moves (the main thread doesn't touch the list while we run).
    up.exactClusters = s.looseClusters;
    up.exactClusters.insert(up.exactClusters.end(), up.movedClusters.begin(), up.movedClusters.end());
    std::sort(up.exactClusters.begin(), up.exactClusters.end());
    up.exactClusters.erase(std::unique(up.exactClusters.begin(), up.exactClusters.end()), up.exactClusters.end());
    while (!up.exactClusters.empty() && up.exactClusters.back() >= nc) up.exactClusters.pop_back();
    up.exactBounds.resize(up.exactClusters.size());
    poolFor(up.exactClusters.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) up.exactBounds[i] = clusterBounds(mesh, s.order, up.exactClusters[i]);
    }, 1, 2);
    up.exact = true;
    up.ready = true;
}

void Renderer::refineStrokeBounds(uint32_t id, const Mesh& mesh) {
    MeshSlot* found = nullptr;
    for (MeshSlot& s : slots_)
        if (s.id == id) found = &s;
    if (!found) return;
    std::fill(found->looseMark.begin(), found->looseMark.end(), 0);
    if (found->looseClusters.empty()) return;
    MeshSlot& s = *found;
    std::sort(s.looseClusters.begin(), s.looseClusters.end());
    s.looseClusters.erase(std::unique(s.looseClusters.begin(), s.looseClusters.end()), s.looseClusters.end());
    const size_t nc = s.order.clusters().size();
    if (s.fresh || mesh.topologyDirty || s.bounds.size() != nc) { // (a full upload since makes them anyway)
        s.looseClusters.clear();
        return;
    }
    poolFor(s.looseClusters.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i)
            if (s.looseClusters[i] < nc) s.bounds[s.looseClusters[i]] = clusterBounds(mesh, s.order, s.looseClusters[i]);
    }, 1, 2);
    s.looseClusters.clear();
    s.volume = 0.0;
    s.box = AABB();
    for (const ClusterBounds& b : s.bounds) {
        s.volume += b.volume;
        if (b.radius >= 0.0f) {
            s.box.expand(b.center - glm::vec3(b.radius));
            s.box.expand(b.center + glm::vec3(b.radius));
        }
    }
}

// ---------------------------------------------------------------------------
// Thumbnails (LightBox / pickers)
// ---------------------------------------------------------------------------

bool Renderer::renderMeshThumbnail(Mesh& mesh, const Camera& camera, int size, const RenderSettings& settings,
                                   std::vector<uint8_t>& rgba, std::string& error) {
    constexpr GLenum kFramebufferBinding = 0x8CA6, kViewport = 0x0BA2;
    GLint fbo = 0, viewport[4] = {};
    gl::GetIntegerv(kFramebufferBinding, &fbo);
    gl::GetIntegerv(kViewport, viewport);

    // A scratch slot outside the SubTool id range; the scene's draw list is put back afterwards.
    constexpr uint32_t kThumbnailSlot = 0xFFFFFFF0u;
    mesh.topologyDirty = true;
    sync(slot(kThumbnailSlot), mesh);
    std::vector<DrawItem> scene;
    scene.swap(drawList_);
    drawList_.push_back({kThumbnailSlot, true});
    thumbnailPass_ = true;
    const bool ok = renderToImage(camera, size, size, settings, rgba, error);
    thumbnailPass_ = false;
    drawList_.swap(scene);
    for (auto it = slots_.begin(); it != slots_.end(); ++it)
        if (it->id == kThumbnailSlot) {
            freeSlot(*it);
            slots_.erase(it);
            break;
        }

    gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(fbo));
    gl::Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return ok;
}

bool Renderer::renderSubtoolThumbnail(uint32_t id, float yaw, float pitch, int size, const RenderSettings& settings,
                                      std::vector<uint8_t>& rgba, std::string& error) {
    const MeshSlot* s = nullptr;
    for (const MeshSlot& slot : slots_)
        if (slot.id == id) s = &slot;
    if (!s || s->fresh || s->outOfMemory || (!s->micro && s->drawnTriangles == 0) || !s->box.valid()) return false;
    constexpr GLenum kFramebufferBinding = 0x8CA6, kViewport = 0x0BA2;
    GLint fbo = 0, viewport[4] = {};
    gl::GetIntegerv(kFramebufferBinding, &fbo);
    gl::GetIntegerv(kViewport, viewport);

    Camera camera;
    camera.aspect = 1.0f;
    camera.yaw = glm::radians(yaw);
    camera.pitch = glm::radians(pitch);
    camera.frame(s->box);
    camera.distance *= 0.6f; // (frame leaves room around the model for the view; a thumbnail fills its tile)
    RenderSettings thumb = settings;
    thumb.showMask = thumb.showPolygroups = thumb.wireframe = false;
    thumb.dimInactive = false;
    constexpr size_t kProxyTriangles = size_t(1) << 20; // a big mesh: its lighter proxy, where current, is plenty at this size
    thumb.motionProxy = s->drawnTriangles >= kProxyTriangles;
    bool flat = false;
    for (const DrawItem& item : drawList_)
        if (item.id == id) flat = item.flat;
    std::vector<DrawItem> scene;
    scene.swap(drawList_);
    drawList_.push_back({id, false, flat}); // (not active: no ghost, draft or mask on it)
    const bool proxyUsed = proxyUsed_;
    thumbnailPass_ = true;
    const bool ok = renderToImage(camera, size, size, thumb, rgba, error);
    thumbnailPass_ = false;
    proxyUsed_ = proxyUsed; // (what the view drew decides which proxies sync keeps current)
    drawList_.swap(scene);

    gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(fbo));
    gl::Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return ok;
}

} // namespace sf

