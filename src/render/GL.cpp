// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/GL.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gl {

namespace {
uint32_t g_testTexelLimit = 0;
bool g_es30 = false;
}

bool es30Tier() { return g_es30; }

namespace {
GLuint g_windowFramebuffer = 0;
PFN_BindFramebuffer g_realBindFramebuffer = nullptr;
void SF_GLAPI bindFramebufferToWindow(GLenum target, GLuint framebuffer) {
    g_realBindFramebuffer(target, framebuffer ? framebuffer : g_windowFramebuffer);
}
} // namespace

void setWindowFramebuffer(GLuint framebuffer) { g_windowFramebuffer = framebuffer; }

void setTestTexelLimit(uint32_t texels) { g_testTexelLimit = texels; }

GLint maxTextureBufferTexels() {
    GLint texels = 0;
    GetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &texels);
    return g_testTexelLimit ? std::min(texels, GLint(g_testTexelLimit)) : texels;
}

uint32_t bufferChunkTexels(uint32_t natural) { return g_testTexelLimit ? std::min(natural, g_testTexelLimit) : natural; }

#define SF_GL_DEFINE(ret, name, ...) PFN_##name name = nullptr;
SF_GL_FUNCTIONS(SF_GL_DEFINE)
SF_GL_OPTIONAL(SF_GL_DEFINE)
#undef SF_GL_DEFINE

#ifdef SF_GLES
// OpenGL ES 3.2 (Android, SCULPTFORGE_GLES): the same calls as desktop GL 3.3 core, except four the renderer makes,
// which these stand in for. Shaders are written once as "#version 330 core" and compiled here as GLSL ES 3.20.
namespace {
ProcLoader esLoader = nullptr;
PFN_ShaderSource esShaderSource = nullptr;
PFN_Enable esEnable = nullptr;
PFN_Disable esDisable = nullptr;
PFN_DrawElementsBaseVertex esDrawElementsBaseVertex = nullptr;
PFN_MultiDrawElementsBaseVertex esMultiDrawExt = nullptr;
using PFN_MapBufferRangeES = void*(SF_GLAPI*)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
using PFN_UnmapBufferES = GLboolean(SF_GLAPI*)(GLenum);
constexpr GLenum kMultisample = 0x809D; // GL_MULTISAMPLE: always on in ES, and not a valid enable there
constexpr GLbitfield kMapRead = 0x0001;  // GL_MAP_READ_BIT

void SF_GLAPI shaderSourceES(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    std::string src;
    for (GLsizei i = 0; i < count; ++i) {
        if (!strings[i]) continue;
        if (lengths && lengths[i] >= 0) src.append(strings[i], size_t(lengths[i]));
        else src += strings[i];
    }
    const char* es = g_es30 ? "#version 300 es\n" // (no buffer samplers: the tier's 2D stand-ins, below)
                              "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
                              "precision highp usampler2D;\nprecision highp isampler2D;\n"
                            : "#version 320 es\n"
                              "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
                              "precision highp usampler2D;\nprecision highp isampler2D;\nprecision highp samplerBuffer;\n"
                              "precision highp usamplerBuffer;\nprecision highp isamplerBuffer;\n";
    const std::string desktop = "#version 330 core";
    if (const size_t at = src.find(desktop); at != std::string::npos) src.replace(at, desktop.size() + 1, es);
    else src.insert(0, es);
    const GLchar* one = src.c_str();
    esShaderSource(shader, 1, &one, nullptr);
}
void SF_GLAPI enableES(GLenum cap) { if (cap != kMultisample) esEnable(cap); }
void SF_GLAPI disableES(GLenum cap) { if (cap != kMultisample) esDisable(cap); }
// Wireframe has no polygon mode in ES: the triangles draw filled (M2: an edge pass of its own).
void SF_GLAPI polygonModeES(GLenum, GLenum) {}
void SF_GLAPI multiDrawElementsBaseVertexES(GLenum mode, const GLsizei* counts, GLenum type, const void* const* offsets,
                                            GLsizei drawCount, const GLint* baseVertex) {
    if (!esDrawElementsBaseVertex) { // (ES before 3.2 without the extension: never call a null entry point; say so once)
        static bool said = false;
        if (!said) SDL_Log("GL ES: no DrawElementsBaseVertex in this driver, the mesh is not drawn");
        said = true;
        return;
    }
    for (GLsizei i = 0; i < drawCount; ++i)
        if (counts[i] > 0) esDrawElementsBaseVertex(mode, counts[i], type, offsets[i], baseVertex[i]);
}
void SF_GLAPI getBufferSubDataES(GLenum target, GLintptr offset, GLsizeiptr size, void* data) {
    static auto map = reinterpret_cast<PFN_MapBufferRangeES>(esLoader("glMapBufferRange"));
    static auto unmap = reinterpret_cast<PFN_UnmapBufferES>(esLoader("glUnmapBuffer"));
    if (const void* p = map(target, offset, size, kMapRead)) {
        std::memcpy(data, p, size_t(size));
        unmap(target);
    }
}
// Whether the driver lists an extension (the loader is the context's, so this runs once the context is current).
bool hasExtensionES(const char* name) {
    using PFN_GetIntegervES = void(SF_GLAPI*)(GLenum, GLint*);
    using PFN_GetStringiES = const GLubyte*(SF_GLAPI*)(GLenum, GLuint);
    const auto getIntegerv = reinterpret_cast<PFN_GetIntegervES>(esLoader("glGetIntegerv"));
    const auto getStringi = reinterpret_cast<PFN_GetStringiES>(esLoader("glGetStringi"));
    if (!getIntegerv || !getStringi) return false;
    GLint n = 0;
    getIntegerv(0x821D /* GL_NUM_EXTENSIONS */, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = reinterpret_cast<const char*>(getStringi(0x1F03 /* GL_EXTENSIONS */, GLuint(i)));
        if (e && std::strcmp(e, name) == 0) return true;
    }
    return false;
}

void SF_GLAPI texBufferMissing(GLenum, GLenum, GLuint) {}
void SF_GLAPI drawElementsBaseVertexMissing(GLenum, GLsizei, GLenum, const void*, GLint) {}

GenericProc loadES(const char* name) {
    const std::string n = name;
    // ES 3.0 (the tier) has neither: the tier stands in for both (installEs30Tier), so a null here is not a missing function.
    if (g_es30 && (n == "glTexBuffer" || n == "glDrawElementsBaseVertex")) {
        if (GenericProc real = esLoader(name)) return real;
        return n == "glTexBuffer" ? reinterpret_cast<GenericProc>(&texBufferMissing)
                                  : reinterpret_cast<GenericProc>(&drawElementsBaseVertexMissing);
    }
    if (n == "glPolygonMode") return reinterpret_cast<GenericProc>(&polygonModeES);
    if (n == "glGetBufferSubData") return reinterpret_cast<GenericProc>(&getBufferSubDataES);
    if (n == "glMultiDrawElementsBaseVertex") {
        esDrawElementsBaseVertex = reinterpret_cast<PFN_DrawElementsBaseVertex>(esLoader("glDrawElementsBaseVertex"));
        if (!esDrawElementsBaseVertex) // (the core call is ES 3.2; before that the extension's)
            esDrawElementsBaseVertex = reinterpret_cast<PFN_DrawElementsBaseVertex>(esLoader("glDrawElementsBaseVertexEXT"));
        if (!esDrawElementsBaseVertex)
            esDrawElementsBaseVertex = reinterpret_cast<PFN_DrawElementsBaseVertex>(esLoader("glDrawElementsBaseVertexOES"));
        esMultiDrawExt = reinterpret_cast<PFN_MultiDrawElementsBaseVertex>(esLoader("glMultiDrawElementsBaseVertexEXT"));
        // Android's EGL hands out an entry point for an extension function the driver doesn't have (it does nothing, and no
        // error is raised: the mesh is "drawn" and never appears). So the extension version is used only when the driver
        // lists the extensions that define it; otherwise one DrawElementsBaseVertex (core in ES 3.2) per range.
        const bool listed = esMultiDrawExt && hasExtensionES("GL_EXT_multi_draw_arrays") &&
                            (hasExtensionES("GL_EXT_draw_elements_base_vertex") || hasExtensionES("GL_OES_draw_elements_base_vertex"));
        SDL_Log("GL ES: multi-draw %s (entry point %s)", listed ? "from the extension" : "as one draw call per range",
                esMultiDrawExt ? "present" : "absent");
        return listed ? reinterpret_cast<GenericProc>(esMultiDrawExt) : reinterpret_cast<GenericProc>(&multiDrawElementsBaseVertexES);
    }
    if (n == "glShaderSource") {
        esShaderSource = reinterpret_cast<PFN_ShaderSource>(esLoader(name));
        return esShaderSource ? reinterpret_cast<GenericProc>(&shaderSourceES) : nullptr;
    }
    if (n == "glEnable" || n == "glDisable") {
        GenericProc real = esLoader(name);
        if (!real) return nullptr;
        if (n == "glEnable") esEnable = reinterpret_cast<PFN_Enable>(real);
        else esDisable = reinterpret_cast<PFN_Disable>(real);
        return n == "glEnable" ? reinterpret_cast<GenericProc>(&enableES) : reinterpret_cast<GenericProc>(&disableES);
    }
    return esLoader(name);
}
} // namespace
#endif

// ---- The OpenGL ES 3.0 tier (GL.h es30Tier) ----
namespace es30 {

// Texture buffers as 2D textures kRow texels wide: texel i at (i % kRow, i / kRow). Each "buffer texture" keeps a copy of its
// buffer, refreshed (the rows a write touched) before the next draw that samples it or when it is bound.
constexpr GLint kRow = 4096, kRowShift = 12;
constexpr GLenum kPixelUnpackBuffer = 0x88EC, kElementBinding = 0x8895, kMaxTextureSize = 0x0D33;
constexpr int kUnits = 32;

PFN_BindTexture rBindTexture;
PFN_ActiveTexture rActiveTexture;
PFN_BindBuffer rBindBuffer;
PFN_BufferData rBufferData;
PFN_BufferSubData rBufferSubData;
PFN_GetBufferSubData rGetBufferSubData;
PFN_MapBufferRange rMapBufferRange;
PFN_UnmapBuffer rUnmapBuffer;
PFN_CopyBufferSubData rCopyBufferSubData;
PFN_DeleteBuffers rDeleteBuffers;
PFN_DeleteTextures rDeleteTextures;
PFN_TexImage2D rTexImage2D;
PFN_TexSubImage2D rTexSubImage2D;
PFN_TexParameteri rTexParameteri;
PFN_GetIntegerv rGetIntegerv;
PFN_ShaderSource rShaderSource;
PFN_BindVertexArray rBindVertexArray;
PFN_DeleteVertexArrays rDeleteVertexArrays;
PFN_VertexAttribPointer rVertexAttribPointer;
PFN_VertexAttribIPointer rVertexAttribIPointer;
PFN_VertexAttribDivisor rVertexAttribDivisor;
PFN_DrawElements rDrawElements;
PFN_DrawArrays rDrawArrays;
PFN_DrawArraysInstanced rDrawArraysInstanced;
PFN_DrawElementsInstanced rDrawElementsInstanced;

struct Format {
    GLenum format, type;
    size_t bytes;
};
bool formatOf(GLenum internal, Format& f) {
    switch (internal) {
    case 0x8229: f = {0x1903, 0x1401, 1}; return true;  // R8: RED, UNSIGNED_BYTE
    case 0x8234: f = {0x8D94, 0x1403, 2}; return true;  // R16UI: RED_INTEGER, UNSIGNED_SHORT
    case 0x8233: f = {0x8D94, 0x1402, 2}; return true;  // R16I: RED_INTEGER, SHORT
    case 0x8236: f = {0x8D94, 0x1405, 4}; return true;  // R32UI
    case 0x8235: f = {0x8D94, 0x1404, 4}; return true;  // R32I
    case 0x822E: f = {0x1903, 0x1406, 4}; return true;  // R32F: RED, FLOAT
    case 0x8230: f = {0x8227, 0x1406, 8}; return true;  // RG32F: RG, FLOAT
    case 0x823C: f = {0x8228, 0x1405, 8}; return true;  // RG32UI: RG_INTEGER
    case 0x8815: f = {0x1907, 0x1406, 12}; return true; // RGB32F: RGB, FLOAT
    case 0x8058: f = {0x1908, 0x1401, 4}; return true;  // RGBA8: RGBA, UNSIGNED_BYTE
    case 0x881A: f = {0x1908, 0x140B, 8}; return true;  // RGBA16F: RGBA, HALF_FLOAT
    case 0x8814: f = {0x1908, 0x1406, 16}; return true; // RGBA32F
    case 0x8D70: f = {0x8D99, 0x1405, 16}; return true; // RGBA32UI: RGBA_INTEGER
    default: return false;
    }
}

struct BufferTexture {
    GLuint buffer = 0;
    GLenum internal = 0;
    GLint rows = 0;                 // allocated (0: not yet)
    size_t dirtyLo = 0, dirtyHi = 0; // bytes of the buffer not copied yet (lo >= hi: none)
};
std::unordered_map<GLuint, BufferTexture> g_textures;          // texture -> its buffer
std::unordered_map<GLuint, std::vector<GLuint>> g_views;       // buffer -> the textures that show it
std::unordered_map<GLuint, size_t> g_sizes;                    // buffer -> BufferData's size
struct Mapped {
    size_t offset, length;
    bool write;
};
std::unordered_map<GLuint, Mapped> g_mapped;
GLuint g_unitBuffer[kUnits] = {}; // the buffer texture bound on each unit (0: none)
GLuint g_unit2D[kUnits] = {};     // the real 2D texture bound there
GLuint g_active = 0;
GLuint g_bound[4] = {};           // ARRAY, COPY_READ, COPY_WRITE, the virtual TEXTURE_BUFFER
bool g_anyDirty = false;
GLint g_maxRows = 0;

int slot(GLenum target) {
    switch (target) {
    case GL_ARRAY_BUFFER: return 0;
    case GL_COPY_READ_BUFFER: return 1;
    case GL_COPY_WRITE_BUFFER: return 2;
    case GL_TEXTURE_BUFFER: return 3;
    default: return -1;
    }
}
GLuint boundTo(GLenum target) {
    if (const int i = slot(target); i >= 0) return g_bound[i];
    GLint b = 0;
    if (target == GL_ELEMENT_ARRAY_BUFFER) rGetIntegerv(kElementBinding, &b); // (vertex array state)
    return GLuint(b);
}
// GL_TEXTURE_BUFFER is no buffer target in ES 3.0: calls on it run on GL_COPY_WRITE_BUFFER with the buffer bound there.
struct Target {
    GLenum target;
    bool swapped = false;
    explicit Target(GLenum t) : target(t) {
        if (t != GL_TEXTURE_BUFFER) return;
        target = GL_COPY_WRITE_BUFFER;
        swapped = g_bound[3] != g_bound[2];
        if (swapped) rBindBuffer(GL_COPY_WRITE_BUFFER, g_bound[3]);
    }
    ~Target() {
        if (swapped) rBindBuffer(GL_COPY_WRITE_BUFFER, g_bound[2]);
    }
};

void written(GLuint buffer, size_t lo, size_t hi) {
    auto v = g_views.find(buffer);
    if (v == g_views.end() || lo >= hi) return;
    for (GLuint t : v->second) {
        BufferTexture& b = g_textures[t];
        if (b.dirtyLo >= b.dirtyHi) b.dirtyLo = lo, b.dirtyHi = hi;
        else b.dirtyLo = std::min(b.dirtyLo, lo), b.dirtyHi = std::max(b.dirtyHi, hi);
    }
    g_anyDirty = true;
}

size_t sizeOf(GLuint buffer) {
    if (auto it = g_sizes.find(buffer); it != g_sizes.end()) return it->second;
    return 0;
}

// Copies the rows of a buffer texture (bound to GL_TEXTURE_2D on the active unit) that its buffer changed in.
void refresh(BufferTexture& t) {
    Format f{};
    if (!t.buffer || !formatOf(t.internal, f)) return;
    const size_t texels = sizeOf(t.buffer) / f.bytes;
    const GLint rows = GLint(std::max<size_t>(1, (texels + kRow - 1) / kRow));
    if (rows > g_maxRows) {
        static bool said = false;
        if (!said) SDL_Log("GL ES 3.0: a buffer of %zu texels is past this GPU's %d x %d stand-in texture", texels, kRow, g_maxRows);
        said = true;
    }
    const GLint drawnRows = std::min(rows, g_maxRows);
    if (rows != t.rows) {
        rTexImage2D(GL_TEXTURE_2D, 0, GLint(t.internal), kRow, drawnRows, 0, f.format, f.type, nullptr);
        rTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_NEAREST));
        rTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_NEAREST));
        rTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
        rTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
        t.rows = rows;
        t.dirtyLo = 0, t.dirtyHi = texels * f.bytes;
    }
    if (t.dirtyLo >= t.dirtyHi || texels == 0) {
        t.dirtyLo = t.dirtyHi = 0;
        return;
    }
    const size_t first = std::min(t.dirtyLo / f.bytes, texels), end = std::min((t.dirtyHi + f.bytes - 1) / f.bytes, texels);
    t.dirtyLo = t.dirtyHi = 0;
    if (first >= end) return;
    const GLint r0 = GLint(first / kRow), full = std::min(GLint(texels / kRow), drawnRows);
    const GLint r1 = std::min(GLint((end + kRow - 1) / kRow), drawnRows);
    rBindBuffer(kPixelUnpackBuffer, t.buffer);
    if (r0 < std::min(r1, full))
        rTexSubImage2D(GL_TEXTURE_2D, 0, 0, r0, kRow, std::min(r1, full) - r0, f.format, f.type,
                       reinterpret_cast<const void*>(size_t(r0) * kRow * f.bytes));
    if (r1 > full && full < drawnRows) // (the last, partly filled row)
        rTexSubImage2D(GL_TEXTURE_2D, 0, 0, full, GLsizei(texels - size_t(full) * kRow), 1, f.format, f.type,
                       reinterpret_cast<const void*>(size_t(full) * kRow * f.bytes));
    rBindBuffer(kPixelUnpackBuffer, 0);
}

// Before a draw: the buffer textures bound on any unit, up to date.
void refreshBound() {
    if (!g_anyDirty) return;
    g_anyDirty = false;
    for (int u = 0; u < kUnits; ++u) {
        const GLuint tex = g_unitBuffer[u];
        if (!tex) continue;
        auto it = g_textures.find(tex);
        if (it == g_textures.end() || it->second.dirtyLo >= it->second.dirtyHi) continue;
        if (GLuint(u) != g_active) rActiveTexture(GL_TEXTURE0 + GLenum(u));
        refresh(it->second);
        if (GLuint(u) != g_active) rActiveTexture(GL_TEXTURE0 + g_active);
    }
}

void unview(GLuint texture, GLuint buffer) {
    auto v = g_views.find(buffer);
    if (v == g_views.end()) return;
    std::erase(v->second, texture);
    if (v->second.empty()) g_views.erase(v);
}

void SF_GLAPI activeTexture(GLenum unit) {
    g_active = std::min<GLuint>(unit - GL_TEXTURE0, kUnits - 1);
    rActiveTexture(unit);
}
void SF_GLAPI bindTexture(GLenum target, GLuint texture) {
    if (target == GL_TEXTURE_BUFFER) {
        g_unitBuffer[g_active] = texture;
        rBindTexture(GL_TEXTURE_2D, texture ? texture : g_unit2D[g_active]);
        if (auto it = g_textures.find(texture); it != g_textures.end()) refresh(it->second);
        return;
    }
    if (target == GL_TEXTURE_2D) {
        g_unit2D[g_active] = texture;
        g_unitBuffer[g_active] = 0;
    }
    rBindTexture(target, texture);
}
void SF_GLAPI texBuffer(GLenum, GLenum internal, GLuint buffer) {
    const GLuint texture = g_unitBuffer[g_active];
    if (!texture) return;
    BufferTexture& t = g_textures[texture];
    if (t.buffer != buffer) {
        unview(texture, t.buffer);
        if (buffer) g_views[buffer].push_back(texture);
    }
    t.buffer = buffer;
    if (t.internal != internal) t.rows = 0;
    t.internal = internal;
    t.dirtyLo = 0, t.dirtyHi = sizeOf(buffer);
    refresh(t);
}
void SF_GLAPI deleteTextures(GLsizei n, const GLuint* textures) {
    for (GLsizei i = 0; i < n; ++i) {
        if (auto it = g_textures.find(textures[i]); it != g_textures.end()) {
            unview(textures[i], it->second.buffer);
            g_textures.erase(it);
        }
        for (int u = 0; u < kUnits; ++u) {
            if (g_unitBuffer[u] == textures[i]) g_unitBuffer[u] = 0;
            if (g_unit2D[u] == textures[i]) g_unit2D[u] = 0;
        }
    }
    rDeleteTextures(n, textures);
}
void SF_GLAPI bindBuffer(GLenum target, GLuint buffer) {
    if (const int i = slot(target); i >= 0) g_bound[i] = buffer;
    if (target != GL_TEXTURE_BUFFER) rBindBuffer(target, buffer);
}
void SF_GLAPI bufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    Target t(target);
    const GLuint b = boundTo(target);
    rBufferData(t.target, size, data, usage);
    g_sizes[b] = size_t(size);
    written(b, 0, size_t(size));
}
void SF_GLAPI bufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    Target t(target);
    rBufferSubData(t.target, offset, size, data);
    written(boundTo(target), size_t(offset), size_t(offset + size));
}
void SF_GLAPI getBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void* data) {
    Target t(target);
    rGetBufferSubData(t.target, offset, size, data);
}
void* SF_GLAPI mapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    Target t(target);
    void* p = rMapBufferRange(t.target, offset, length, access);
    if (p) g_mapped[boundTo(target)] = {size_t(offset), size_t(length), (access & GL_MAP_WRITE_BIT) != 0};
    return p;
}
GLboolean SF_GLAPI unmapBuffer(GLenum target) {
    Target t(target);
    const GLuint b = boundTo(target);
    const GLboolean ok = rUnmapBuffer(t.target);
    if (auto it = g_mapped.find(b); it != g_mapped.end()) {
        if (it->second.write) written(b, it->second.offset, it->second.offset + it->second.length);
        g_mapped.erase(it);
    }
    return ok;
}
void SF_GLAPI copyBufferSubData(GLenum read, GLenum write, GLintptr from, GLintptr to, GLsizeiptr size) {
    // (one of them may be the virtual texture buffer target: both run on the copy targets)
    const GLuint src = boundTo(read), dst = boundTo(write);
    rBindBuffer(GL_COPY_READ_BUFFER, src);
    rBindBuffer(GL_COPY_WRITE_BUFFER, dst);
    rCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, from, to, size);
    rBindBuffer(GL_COPY_READ_BUFFER, g_bound[1]);
    rBindBuffer(GL_COPY_WRITE_BUFFER, g_bound[2]);
    written(dst, size_t(to), size_t(to + size));
}
void SF_GLAPI deleteBuffers(GLsizei n, const GLuint* buffers) {
    for (GLsizei i = 0; i < n; ++i) {
        const GLuint b = buffers[i];
        if (auto v = g_views.find(b); v != g_views.end()) {
            for (GLuint t : v->second) g_textures[t].buffer = 0;
            g_views.erase(v);
        }
        g_sizes.erase(b);
        g_mapped.erase(b);
        for (GLuint& bound : g_bound)
            if (bound == b) bound = 0;
    }
    rDeleteBuffers(n, buffers);
}
void SF_GLAPI getIntegerv(GLenum name, GLint* value) {
    if (name == GL_MAX_TEXTURE_BUFFER_SIZE) {
        *value = GLint(std::min<int64_t>(int64_t(kRow) * g_maxRows, 0x7FFFFFFF));
        return;
    }
    rGetIntegerv(name, value);
}

// The shaders: each buffer sampler becomes a 2D one of the same kind, and texelFetch on it sfFetch (texel i of the row layout).
std::string rewrite(std::string src) {
    static const std::regex decl(R"(uniform\s+(highp\s+|mediump\s+|lowp\s+)?([iu]?)samplerBuffer\s+(\w+))");
    std::vector<std::string> names;
    for (auto it = std::sregex_iterator(src.begin(), src.end(), decl); it != std::sregex_iterator(); ++it) names.push_back((*it)[3]);
    if (names.empty()) return src;
    src = std::regex_replace(src, decl, "uniform highp $2sampler2D $3");
    for (const std::string& n : names) src = std::regex_replace(src, std::regex("texelFetch\\s*\\(\\s*" + n + "\\s*,"), "sfFetch(" + n + ",");
    const std::string helpers =
        "ivec2 sfAt(int i) { return ivec2(i & " + std::to_string(kRow - 1) + ", i >> " + std::to_string(kRowShift) + "); }\n"
        "vec4 sfFetch(highp sampler2D s, int i) { return texelFetch(s, sfAt(i), 0); }\n"
        "uvec4 sfFetch(highp usampler2D s, int i) { return texelFetch(s, sfAt(i), 0); }\n"
        "ivec4 sfFetch(highp isampler2D s, int i) { return texelFetch(s, sfAt(i), 0); }\n";
    // after the #version line and the preprocessor lines right below it (the shaders' own #defines)
    size_t at = src.find("#version");
    at = at == std::string::npos ? 0 : src.find('\n', at) + 1;
    while (at < src.size() && src[at] == '#' && src.compare(at, 3, "#if") != 0) {
        const size_t nl = src.find('\n', at);
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    src.insert(at, helpers);
    return src;
}
void SF_GLAPI shaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    std::string src;
    for (GLsizei i = 0; i < count; ++i) {
        if (!strings[i]) continue;
        if (lengths && lengths[i] >= 0) src.append(strings[i], size_t(lengths[i]));
        else src += strings[i];
    }
    src = rewrite(std::move(src));
    const GLchar* one = src.c_str();
    rShaderSource(shader, 1, &one, nullptr);
}

// The base vertex: the vertex array's attributes (those with a buffer, not per instance) start `base` vertices later.
struct Attrib {
    bool set = false, integer = false, normalized = false;
    GLint size = 0;
    GLenum type = 0;
    GLsizei stride = 0;
    const void* pointer = nullptr;
    GLuint buffer = 0, divisor = 0;
    GLint applied = 0;
};
constexpr int kAttribs = 16;
struct VertexArray {
    Attrib a[kAttribs];
    bool offset = false; // some attribute is re-pointed now
};
std::unordered_map<GLuint, VertexArray> g_vaos;
GLuint g_vao = 0;
VertexArray* g_current = nullptr;

VertexArray& current() {
    if (!g_current) g_current = &g_vaos[g_vao];
    return *g_current;
}
size_t typeBytes(GLenum type, GLint size) {
    switch (type) {
    case 0x1400: case 0x1401: return size_t(size);     // BYTE, UNSIGNED_BYTE
    case 0x1402: case 0x1403: case 0x140B: return 2u * size_t(size); // SHORT, UNSIGNED_SHORT, HALF_FLOAT
    case 0x8D9F: case 0x8368: return 4;                // the packed 2_10_10_10 formats
    default: return 4u * size_t(size);                 // INT, UNSIGNED_INT, FLOAT
    }
}
void point(const Attrib& a, GLuint index, GLint base) {
    const size_t stride = a.stride ? size_t(a.stride) : typeBytes(a.type, a.size);
    const void* p = static_cast<const char*>(a.pointer) + ptrdiff_t(base) * ptrdiff_t(stride);
    if (a.integer) rVertexAttribIPointer(index, a.size, a.type, a.stride, p);
    else rVertexAttribPointer(index, a.size, a.type, a.normalized, a.stride, p);
}
void applyBase(GLint base) {
    VertexArray& v = current();
    if (base == 0 && !v.offset) return;
    GLuint bound = g_bound[0];
    bool offset = false;
    for (int i = 0; i < kAttribs; ++i) {
        Attrib& a = v.a[i];
        if (!a.set || a.divisor || !a.buffer) continue;
        if (a.applied != base) {
            if (a.buffer != bound) rBindBuffer(GL_ARRAY_BUFFER, bound = a.buffer);
            point(a, GLuint(i), base);
            a.applied = base;
        }
        offset |= base != 0;
    }
    v.offset = offset;
    if (bound != g_bound[0]) rBindBuffer(GL_ARRAY_BUFFER, g_bound[0]);
}
void SF_GLAPI bindVertexArray(GLuint vao) {
    g_vao = vao;
    g_current = nullptr;
    rBindVertexArray(vao);
}
void SF_GLAPI deleteVertexArrays(GLsizei n, const GLuint* vaos) {
    for (GLsizei i = 0; i < n; ++i) {
        g_vaos.erase(vaos[i]);
        if (vaos[i] == g_vao) g_vao = 0;
    }
    g_current = nullptr;
    rDeleteVertexArrays(n, vaos);
}
void record(GLuint index, bool integer, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) {
    if (index >= GLuint(kAttribs)) return;
    Attrib& a = current().a[index];
    const GLuint divisor = a.divisor;
    a = {true, integer, normalized != 0, size, type, stride, pointer, g_bound[0], divisor, 0};
}
void SF_GLAPI vertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) {
    record(index, false, size, type, normalized, stride, pointer);
    rVertexAttribPointer(index, size, type, normalized, stride, pointer);
}
void SF_GLAPI vertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer) {
    record(index, true, size, type, GL_FALSE, stride, pointer);
    rVertexAttribIPointer(index, size, type, stride, pointer);
}
void SF_GLAPI vertexAttribDivisor(GLuint index, GLuint divisor) {
    if (index < GLuint(kAttribs)) current().a[index].divisor = divisor;
    rVertexAttribDivisor(index, divisor);
}
void SF_GLAPI drawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    applyBase(0);
    refreshBound();
    rDrawElements(mode, count, type, indices);
}
void SF_GLAPI drawArrays(GLenum mode, GLint first, GLsizei count) {
    applyBase(0);
    refreshBound();
    rDrawArrays(mode, first, count);
}
void SF_GLAPI drawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    applyBase(0);
    refreshBound();
    rDrawArraysInstanced(mode, first, count, instances);
}
void SF_GLAPI drawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances) {
    applyBase(0);
    refreshBound();
    rDrawElementsInstanced(mode, count, type, indices, instances);
}
void SF_GLAPI drawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint base) {
    applyBase(base);
    refreshBound();
    rDrawElements(mode, count, type, indices);
}
void SF_GLAPI multiDrawElementsBaseVertex(GLenum mode, const GLsizei* counts, GLenum type, const void* const* indices,
                                          GLsizei draws, const GLint* bases) {
    refreshBound();
    for (GLsizei i = 0; i < draws; ++i) {
        if (counts[i] <= 0) continue;
        applyBase(bases[i]);
        rDrawElements(mode, counts[i], type, indices[i]);
    }
}

void install() {
#define SF_ES30_SWAP(name, fn) r##name = name, name = &fn
    SF_ES30_SWAP(BindTexture, bindTexture);
    SF_ES30_SWAP(ActiveTexture, activeTexture);
    SF_ES30_SWAP(BindBuffer, bindBuffer);
    SF_ES30_SWAP(BufferData, bufferData);
    SF_ES30_SWAP(BufferSubData, bufferSubData);
    SF_ES30_SWAP(GetBufferSubData, getBufferSubData);
    SF_ES30_SWAP(MapBufferRange, mapBufferRange);
    SF_ES30_SWAP(UnmapBuffer, unmapBuffer);
    SF_ES30_SWAP(CopyBufferSubData, copyBufferSubData);
    SF_ES30_SWAP(DeleteBuffers, deleteBuffers);
    SF_ES30_SWAP(DeleteTextures, deleteTextures);
    SF_ES30_SWAP(GetIntegerv, getIntegerv);
    SF_ES30_SWAP(ShaderSource, shaderSource);
    SF_ES30_SWAP(BindVertexArray, bindVertexArray);
    SF_ES30_SWAP(DeleteVertexArrays, deleteVertexArrays);
    SF_ES30_SWAP(VertexAttribPointer, vertexAttribPointer);
    SF_ES30_SWAP(VertexAttribIPointer, vertexAttribIPointer);
    SF_ES30_SWAP(VertexAttribDivisor, vertexAttribDivisor);
    SF_ES30_SWAP(DrawElements, drawElements);
    SF_ES30_SWAP(DrawArrays, drawArrays);
    SF_ES30_SWAP(DrawArraysInstanced, drawArraysInstanced);
    SF_ES30_SWAP(DrawElementsInstanced, drawElementsInstanced);
#undef SF_ES30_SWAP
    rTexImage2D = TexImage2D;
    rTexSubImage2D = TexSubImage2D;
    rTexParameteri = TexParameteri;
    TexBuffer = &texBuffer;
    DrawElementsBaseVertex = &drawElementsBaseVertex;
    MultiDrawElementsBaseVertex = &multiDrawElementsBaseVertex;
    BufferStorage = nullptr; // (a persistent mapping is written without a call: its texture copies could not follow)
    rGetIntegerv(kMaxTextureSize, &g_maxRows);
    g_maxRows = std::max(g_maxRows, 1);
    SDL_Log("GL ES 3.0 tier: texture buffers as %d x %d textures, base vertex by attribute offsets", kRow, g_maxRows);
}

} // namespace es30

namespace {
// Whether this context gets the ES 3.0 tier: SF_GLES_LEVEL=300 (any driver), or an ES context before 3.2.
bool wantsEs30(ProcLoader loader) {
    if (const char* level = std::getenv("SF_GLES_LEVEL")) return std::atoi(level) == 300;
#ifdef SF_GLES
    using PFN_GetStringES = const GLubyte*(SF_GLAPI*)(GLenum);
    const auto getString = reinterpret_cast<PFN_GetStringES>(loader("glGetString"));
    const char* v = getString ? reinterpret_cast<const char*>(getString(GL_VERSION)) : nullptr;
    int major = 0, minor = 0;
    if (v && std::sscanf(v, "OpenGL ES %d.%d", &major, &minor) == 2) return major * 10 + minor < 32;
#else
    (void)loader;
#endif
    return false;
}
} // namespace

const char* load(ProcLoader loader) {
    g_es30 = wantsEs30(loader);
#ifdef SF_GLES
    esLoader = loader;
    loader = &loadES;
#endif
#define SF_GL_LOAD(ret, name, ...)                                    \
    name = reinterpret_cast<PFN_##name>(loader("gl" #name));          \
    if (!name) return "gl" #name;
    SF_GL_FUNCTIONS(SF_GL_LOAD)
#undef SF_GL_LOAD
#define SF_GL_LOAD_OPTIONAL(ret, name, ...) name = reinterpret_cast<PFN_##name>(loader("gl" #name));
    SF_GL_OPTIONAL(SF_GL_LOAD_OPTIONAL)
#undef SF_GL_LOAD_OPTIONAL
    if (g_es30) es30::install();
    if (g_windowFramebuffer && BindFramebuffer != &bindFramebufferToWindow) {
        g_realBindFramebuffer = BindFramebuffer;
        BindFramebuffer = &bindFramebufferToWindow;
    }
    return nullptr;
}

} // namespace gl
