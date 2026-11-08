/* SculptForge sample plugin: Noise Displace (a deformer), the C version of tests/data/plugins/noise_displace.wat.
 * SPDX-License-Identifier: CC0-1.0
 *
 * Build (clang and wasm-ld from LLVM; no libc needed):
 *   clang --target=wasm32 -O2 -nostdlib -I.. -Wl,--no-entry -Wl,--export=sf_alloc -Wl,--export=sf_run \
 *         -o noise_displace.wasm noise_displace.c
 *   python3 ../../../tools/plugin/sfplugin.py noise_displace.wasm noise_displace.json -o "Noise Displace.sfplugin"
 * (noise_displace.json: tests/data/plugins/noise_displace.json). Put it in a pack's plugins/ folder.
 */
#include "sculptforge_plugin.h"

extern unsigned char __heap_base;
static uint32_t heap = 0;

/* A bump allocator: SculptForge calls it once per run, on a fresh instance. */
uint32_t sf_alloc(uint32_t bytes) {
    if (!heap) heap = SF_ADDR(&__heap_base);
    uint32_t p = (heap + 7u) & ~7u;
    uint64_t end = (uint64_t)p + bytes;
    uint64_t have = (uint64_t)__builtin_wasm_memory_size(0) << 16;
    if (end > have && __builtin_wasm_memory_grow(0, (uint32_t)((end - have + 65535u) >> 16)) == (__SIZE_TYPE__)-1)
        return 0;
    heap = (uint32_t)end;
    return p;
}

static float hash(int32_t x, int32_t y, int32_t z, int32_t s) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u ^ (uint32_t)s * 2654435761u;
    h = (h ^ (h >> 16)) * 0x45d9f3bu;
    h = (h ^ (h >> 16)) * 0x45d9f3bu;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) * 1.1920929e-7f - 1.0f;
}

static float fade(float t) { return t * t * (3.0f - 2.0f * t); }
static float lerp(float a, float b, float t) { return a + (b - a) * t; }

static float noise(float x, float y, float z, int32_t s) {
    const float fx = __builtin_floorf(x), fy = __builtin_floorf(y), fz = __builtin_floorf(z);
    const int32_t ix = (int32_t)fx, iy = (int32_t)fy, iz = (int32_t)fz;
    const float u = fade(x - fx), v = fade(y - fy), w = fade(z - fz);
    return lerp(lerp(lerp(hash(ix, iy, iz, s), hash(ix + 1, iy, iz, s), u),
                     lerp(hash(ix, iy + 1, iz, s), hash(ix + 1, iy + 1, iz, s), u), v),
                lerp(lerp(hash(ix, iy, iz + 1, s), hash(ix + 1, iy, iz + 1, s), u),
                     lerp(hash(ix, iy + 1, iz + 1, s), hash(ix + 1, iy + 1, iz + 1, s), u), v),
                w);
}

static sf_output out;

uint32_t sf_run(uint32_t address) {
    const sf_input* in = SF_PTR(const sf_input, address);
    float* p = SF_PTR(float, in->positions);
    const float* n = SF_PTR(const float, in->normals);
    const double* params = SF_PTR(const double, in->params);
    const float amount = (float)params[0], scale = (float)params[1];
    const int32_t seed = (int32_t)params[2];
    for (uint32_t i = 0; i < in->vertex_count; ++i, p += 3, n += 3) {
        const float d = amount * noise(p[0] * scale, p[1] * scale, p[2] * scale, seed);
        p[0] += n[0] * d;
        p[1] += n[1] * d;
        p[2] += n[2] * d;
    }
    out = (sf_output){SF_PLUGIN_ABI, 0, in->vertex_count, 0, in->positions, 0, 0, 0};
    return SF_ADDR(&out);
}
