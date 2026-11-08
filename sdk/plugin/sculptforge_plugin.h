/* SculptForge plugin ABI 1 (src/core/Plugin.h; docs/design/plugins.md).
 * SPDX-License-Identifier: CC0-1.0 (copy it into your plugin freely)
 *
 * A plugin is a WebAssembly module (wasm32, no imports at all: no WASI, no libc that needs the system) that exports
 *   memory              its linear memory
 *   sf_alloc(bytes)     returns an 8-byte aligned block of BYTES in its memory (0 when it can't)
 *   sf_run(input)       reads the sf_input at INPUT and returns the address of an sf_output
 * and carries its manifest (JSON) in a custom section named "sculptforge.plugin" (tools/plugin/sfplugin.py adds it).
 * SculptForge allocates the input with sf_alloc, fills it, calls sf_run once, copies the result out and checks it.
 * Every run starts from a fresh instance, so nothing survives between runs. All values are little-endian; every
 * "address" is an offset into the plugin's own memory.
 */
#ifndef SCULPTFORGE_PLUGIN_H
#define SCULPTFORGE_PLUGIN_H

#include <stdint.h>

#define SF_PLUGIN_ABI 1u

enum sf_kind { SF_DEFORMER = 0, SF_GENERATOR = 1 };

typedef struct sf_input {      /* 48 bytes */
    uint32_t abi;              /* SF_PLUGIN_ABI */
    uint32_t kind;             /* sf_kind */
    uint32_t vertex_count;
    uint32_t triangle_count;
    uint32_t param_count;      /* as many as the manifest declares, in its order */
    uint32_t positions;        /* float[3 * vertex_count] */
    uint32_t normals;          /* float[3 * vertex_count], unit length */
    uint32_t mask;             /* float[vertex_count], 1 = fully masked; 0 when the mesh has no mask */
    uint32_t triangles;        /* uint32_t[3 * triangle_count], counter-clockwise */
    uint32_t params;           /* double[param_count]: numbers, ints (whole numbers) and bools (0 or 1), in range */
    uint32_t reserved[2];
} sf_input;

typedef struct sf_output {     /* 32 bytes, 4-byte aligned */
    uint32_t abi;              /* SF_PLUGIN_ABI */
    uint32_t status;           /* 0 = ok; anything else = failed, with the message shown */
    uint32_t vertex_count;     /* deformer: the input's; generator: at least 3 */
    uint32_t triangle_count;   /* deformer: 0 (the triangles stay); generator: at least 1 */
    uint32_t positions;        /* float[3 * vertex_count]; may be the input's positions, changed in place */
    uint32_t triangles;        /* generator: uint32_t[3 * triangle_count], each index < vertex_count, no repeats */
    uint32_t message;          /* UTF-8, shown when status != 0 (at most 512 bytes) */
    uint32_t message_len;
} sf_output;

/* For C compiled with clang --target=wasm32 (a pointer is the address): */
#define SF_PTR(type, address) ((type*)(uintptr_t)(address))
#define SF_ADDR(pointer) ((uint32_t)(uintptr_t)(pointer))

#endif
