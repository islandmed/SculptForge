// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/GL.h"

#ifdef SF_GLES
#include <cstring>
#include <string>
#endif

namespace gl {

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
GenericProc loadES(const char* name) {
    const std::string n = name;
    if (n == "glPolygonMode") return reinterpret_cast<GenericProc>(&polygonModeES);
    if (n == "glGetBufferSubData") return reinterpret_cast<GenericProc>(&getBufferSubDataES);
    if (n == "glMultiDrawElementsBaseVertex") {
        esDrawElementsBaseVertex = reinterpret_cast<PFN_DrawElementsBaseVertex>(esLoader("glDrawElementsBaseVertex"));
        esMultiDrawExt = reinterpret_cast<PFN_MultiDrawElementsBaseVertex>(esLoader("glMultiDrawElementsBaseVertexEXT"));
        return esMultiDrawExt ? reinterpret_cast<GenericProc>(esMultiDrawExt)
                              : reinterpret_cast<GenericProc>(&multiDrawElementsBaseVertexES);
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
    return nullptr;
}

} // namespace gl
