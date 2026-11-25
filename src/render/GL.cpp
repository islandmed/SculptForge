// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/GL.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace gl {

namespace {
uint32_t g_testTexelLimit = 0;
int g_testCopyUploads = -1; // -1: the driver decides
int g_layerCopyUploads = -1; // -1: not looked at yet
}

void setTestCopyUploads(int copies) { g_testCopyUploads = copies; }

bool copyUploads() {
    if (g_testCopyUploads >= 0) return g_testCopyUploads != 0;
    if (g_layerCopyUploads < 0) {
        const auto* r = reinterpret_cast<const char*>(GetString(GL_RENDERER));
        g_layerCopyUploads = r && std::strstr(r, "D3D12") ? 1 : 0; // (Mesa's OpenGL over D3D12: "D3D12 (NVIDIA GeForce ...)")
    }
    return g_layerCopyUploads != 0;
}

void setTestTexelLimit(uint32_t texels) { g_testTexelLimit = texels; }
namespace { bool g_testRejectTextured = false; }
void setTestRejectTextured(bool reject) { g_testRejectTextured = reject; }
bool testRejectTextured() { return g_testRejectTextured; }

namespace {
void SF_GLAPI debugMessage(GLenum, GLenum type, GLuint id, GLenum, GLsizei length, const GLchar* message, const void*) {
    if (type != GL_DEBUG_TYPE_ERROR && type != GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR) return;
    static std::vector<GLuint> seen;
    if (seen.size() >= 40 || std::find(seen.begin(), seen.end(), id) != seen.end()) return;
    seen.push_back(id);
    SDL_Log("GL driver message %u: %.*s", id, std::min(length, 400), message);
}
} // namespace

void enableDebugLog() {
#ifndef SF_GLES
    if (!std::getenv("SF_GL_DEBUG")) return;
#endif
    if (!DebugMessageCallback) return;
    DebugMessageCallback(&debugMessage, nullptr);
    Enable(GL_DEBUG_OUTPUT);
    while (GetError() != GL_NO_ERROR) { // (a stub entry point, or a context without it, may say so: nothing to log)
    }
}
bool testTinyGpu() { return g_testTexelLimit != 0 && g_testTexelLimit <= 256; }

GLint maxTextureBufferTexels() {
    GLint texels = 0;
    GetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &texels);
    return g_testTexelLimit ? std::min(texels, GLint(g_testTexelLimit)) : texels;
}

bool contextHealthy() {
    while (gl::GetError() != GL_NO_ERROR) {
    }
    GLuint fbo = 0, rb = 0;
    gl::GenFramebuffers(1, &fbo);
    gl::GenRenderbuffers(1, &rb);
    gl::BindRenderbuffer(GL_RENDERBUFFER, rb);
    gl::RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 4, 4);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    const bool complete = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(1, &fbo);
    gl::DeleteRenderbuffers(1, &rb);
    return complete && gl::GetError() == GL_NO_ERROR;
}

uint32_t bufferChunkTexels(uint32_t natural) { // (the driver's limit always: a Mali's 65536 texels, not only a test's)
    const GLint driver = maxTextureBufferTexels();
    return driver > 0 ? std::min(natural, uint32_t(driver)) : natural;
}

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
    const char* es = "#version 320 es\n"
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

GenericProc loadES(const char* name) {
    const std::string n = name;
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

namespace {
PFN_TexBuffer g_realTexBuffer = nullptr;
uint32_t g_texBufferOver = 0;
// Bytes of one texel in a buffer texture's format (0: not one this renderer uses).
size_t texelBytes(GLenum format) {
    switch (format) {
    case GL_R16UI: case 0x8233 /* R16I */: return 2;
    case GL_R32UI: case GL_RG16F: case GL_RGBA8: return 4;
    case GL_RG32F: case 0x881A /* RGBA16F */: return 8;
    case GL_RGBA32F: case GL_RGBA32UI: return 16;
    default: return 0;
    }
}
// TexBuffer, then a look at the buffer's size: a buffer texture holds at most GL_MAX_TEXTURE_BUFFER_SIZE texels (a Mali's 65536).
void SF_GLAPI checkedTexBuffer(GLenum target, GLenum format, GLuint buffer) {
    g_realTexBuffer(target, format, buffer);
    const size_t bytes = texelBytes(format);
    if (!bytes || !buffer) return;
    GLint previous = 0, size = 0;
    GetIntegerv(GL_COPY_READ_BUFFER, &previous); // (GL_COPY_READ_BUFFER_BINDING has the same value)
    BindBuffer(GL_COPY_READ_BUFFER, buffer);
    GetBufferParameteriv(GL_COPY_READ_BUFFER, 0x8764 /* GL_BUFFER_SIZE */, &size);
    BindBuffer(GL_COPY_READ_BUFFER, GLuint(previous));
    const size_t texels = size_t(size) / bytes;
    if (texels <= size_t(std::max(maxTextureBufferTexels(), 0))) return;
    ++g_texBufferOver;
    static int said = 0;
    if (said++ < 8) SDL_Log("Buffer texture past the GPU's limit: %zu texels (format 0x%04x), limit %d", texels, unsigned(format), maxTextureBufferTexels());
}
} // namespace
uint32_t texBufferOverLimit() { return g_texBufferOver; }
bool testTexelLimitSet() { return g_testTexelLimit != 0; }

const char* load(ProcLoader loader) {
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
    g_realTexBuffer = TexBuffer;
    TexBuffer = &checkedTexBuffer;
    return nullptr;
}

} // namespace gl
