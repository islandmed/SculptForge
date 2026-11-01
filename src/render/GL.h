// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal OpenGL 3.3 core loader covering exactly the functions we use.
// Avoids a generated loader dependency; works on Linux, macOS and Windows.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define SF_GLAPI __stdcall
#else
#define SF_GLAPI
#endif

using GLenum = unsigned int;
using GLbitfield = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

constexpr GLenum GL_FALSE = 0;
constexpr GLenum GL_TRUE = 1;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_LINES = 0x0001;
constexpr GLenum GL_LEQUAL = 0x0203;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_FRONT_AND_BACK = 0x0408;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_UNSIGNED_INT = 0x1405;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_LINE = 0x1B01;
constexpr GLenum GL_FILL = 0x1B02;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLenum GL_VERSION = 0x1F02;
constexpr GLenum GL_POLYGON_OFFSET_FILL = 0x8037;
constexpr GLenum GL_MULTISAMPLE = 0x809D;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum GL_MAX_SAMPLES = 0x8D57;
constexpr GLenum GL_NO_ERROR = 0;
constexpr GLenum GL_OUT_OF_MEMORY = 0x0505;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_ONE = 1;
constexpr GLenum GL_TRIANGLE_STRIP = 0x0005;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE1 = 0x84C1;
constexpr GLenum GL_RGBA32F = 0x8814;
constexpr GLenum GL_STREAM_DRAW = 0x88E0;
constexpr GLenum GL_TEXTURE_BUFFER = 0x8C2A;
constexpr GLenum GL_MAX_TEXTURE_BUFFER_SIZE = 0x8C2B;
constexpr GLenum GL_R32UI = 0x8236;
constexpr GLenum GL_RG32F = 0x8230;
constexpr GLenum GL_GEOMETRY_SHADER = 0x8DD9;
constexpr GLenum GL_SRGB8_ALPHA8 = 0x8C43;
constexpr GLenum GL_LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr GLenum GL_UNPACK_ROW_LENGTH = 0x0CF2;
constexpr GLenum GL_TEXTURE3 = 0x84C3;
constexpr GLenum GL_TEXTURE4 = 0x84C4;
constexpr GLenum GL_POINTS = 0x0000;
constexpr GLbitfield GL_MAP_WRITE_BIT = 0x0002;
constexpr GLbitfield GL_MAP_INVALIDATE_BUFFER_BIT = 0x0008;
constexpr GLenum GL_UNSIGNED_SHORT = 0x1403;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_R16UI = 0x8234;
constexpr GLenum GL_RED_INTEGER = 0x8D94;
constexpr GLenum GL_R16I = 0x8233;
constexpr GLenum GL_RGBA32UI = 0x8D70;
constexpr GLbitfield GL_MAP_INVALIDATE_RANGE_BIT = 0x0004;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT = 0x0100;
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x4000;

#define SF_GL_FUNCTIONS(X)                                                                  \
    X(void, Clear, GLbitfield)                                                               \
    X(void, ClearColor, GLfloat, GLfloat, GLfloat, GLfloat)                                  \
    X(void, Viewport, GLint, GLint, GLsizei, GLsizei)                                        \
    X(void, Scissor, GLint, GLint, GLsizei, GLsizei)                                         \
    X(void, Enable, GLenum)                                                                  \
    X(void, Disable, GLenum)                                                                 \
    X(void, DepthFunc, GLenum)                                                               \
    X(void, DepthMask, GLboolean)                                                            \
    X(void, ColorMask, GLboolean, GLboolean, GLboolean, GLboolean)                           \
    X(void, BlendFunc, GLenum, GLenum)                                                       \
    X(void, PolygonMode, GLenum, GLenum)                                                     \
    X(void, PolygonOffset, GLfloat, GLfloat)                                                 \
    X(const GLubyte*, GetString, GLenum)                                                     \
    X(void, Finish, void)                                                                    \
    X(GLenum, GetError, void)                                                                \
    X(void, GenBuffers, GLsizei, GLuint*)                                                    \
    X(void, DeleteBuffers, GLsizei, const GLuint*)                                           \
    X(void, BindBuffer, GLenum, GLuint)                                                      \
    X(void, BufferData, GLenum, GLsizeiptr, const void*, GLenum)                             \
    X(void, BufferSubData, GLenum, GLintptr, GLsizeiptr, const void*)                        \
    X(void, GenVertexArrays, GLsizei, GLuint*)                                               \
    X(void, DeleteVertexArrays, GLsizei, const GLuint*)                                      \
    X(void, BindVertexArray, GLuint)                                                         \
    X(void, EnableVertexAttribArray, GLuint)                                                 \
    X(void, VertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)     \
    X(GLuint, CreateShader, GLenum)                                                          \
    X(void, ShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*)               \
    X(void, CompileShader, GLuint)                                                           \
    X(void, GetShaderiv, GLuint, GLenum, GLint*)                                             \
    X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                            \
    X(GLuint, CreateProgram)                                                                 \
    X(void, AttachShader, GLuint, GLuint)                                                    \
    X(void, LinkProgram, GLuint)                                                             \
    X(void, GetProgramiv, GLuint, GLenum, GLint*)                                            \
    X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                           \
    X(void, DeleteShader, GLuint)                                                            \
    X(void, DeleteProgram, GLuint)                                                           \
    X(void, UseProgram, GLuint)                                                              \
    X(GLint, GetUniformLocation, GLuint, const GLchar*)                                      \
    X(void, UniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*)                     \
    X(void, UniformMatrix3fv, GLint, GLsizei, GLboolean, const GLfloat*)                     \
    X(void, Uniform3fv, GLint, GLsizei, const GLfloat*)                                      \
    X(void, Uniform4fv, GLint, GLsizei, const GLfloat*)                                      \
    X(void, DrawElements, GLenum, GLsizei, GLenum, const void*)                              \
    X(void, DrawArrays, GLenum, GLint, GLsizei)                                              \
    X(void, GenTextures, GLsizei, GLuint*)                                                   \
    X(void, DeleteTextures, GLsizei, const GLuint*)                                          \
    X(void, BindTexture, GLenum, GLuint)                                                     \
    X(void, TexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
    X(void, TexParameteri, GLenum, GLenum, GLint)                                            \
    X(void, TexSubImage2D, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) \
    X(void, GenerateMipmap, GLenum)                                                          \
    X(void, PixelStorei, GLenum, GLint)                                                      \
    X(void, ReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)               \
    X(void, GenFramebuffers, GLsizei, GLuint*)                                               \
    X(void, DeleteFramebuffers, GLsizei, const GLuint*)                                      \
    X(void, BindFramebuffer, GLenum, GLuint)                                                 \
    X(GLenum, CheckFramebufferStatus, GLenum)                                                \
    X(void, FramebufferRenderbuffer, GLenum, GLenum, GLenum, GLuint)                         \
    X(void, GenRenderbuffers, GLsizei, GLuint*)                                              \
    X(void, DeleteRenderbuffers, GLsizei, const GLuint*)                                     \
    X(void, BindRenderbuffer, GLenum, GLuint)                                                \
    X(void, RenderbufferStorage, GLenum, GLenum, GLsizei, GLsizei)                           \
    X(void, RenderbufferStorageMultisample, GLenum, GLsizei, GLenum, GLsizei, GLsizei)       \
    X(void, BlitFramebuffer, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) \
    X(void, FramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint)                    \
    X(void, TexBuffer, GLenum, GLenum, GLuint)                                               \
    X(void, VertexAttribIPointer, GLuint, GLint, GLenum, GLsizei, const void*)               \
    X(void, VertexAttribDivisor, GLuint, GLuint)                                             \
    X(void, DrawArraysInstanced, GLenum, GLint, GLsizei, GLsizei)                            \
    X(void, DrawElementsInstanced, GLenum, GLsizei, GLenum, const void*, GLsizei)            \
    X(void, ActiveTexture, GLenum)                                                           \
    X(void, GetIntegerv, GLenum, GLint*)                                                     \
    X(void, Uniform1i, GLint, GLint)                                                         \
    X(void, Uniform1iv, GLint, GLsizei, const GLint*)                                        \
    X(void, Uniform1f, GLint, GLfloat)                                                       \
    X(void, Uniform2f, GLint, GLfloat, GLfloat)                                              \
    X(void, Uniform2i, GLint, GLint, GLint)                                                  \
    X(void, Uniform2iv, GLint, GLsizei, const GLint*)                                        \
    X(void, DisableVertexAttribArray, GLuint)                                                \
    X(void, VertexAttrib1f, GLuint, GLfloat)                                                 \
    X(void, VertexAttrib3f, GLuint, GLfloat, GLfloat, GLfloat)                               \
    X(void, VertexAttrib4f, GLuint, GLfloat, GLfloat, GLfloat, GLfloat)                      \
    X(void, DrawElementsBaseVertex, GLenum, GLsizei, GLenum, const void*, GLint)             \
    X(void*, MapBufferRange, GLenum, GLintptr, GLsizeiptr, GLbitfield)                       \
    X(GLboolean, UnmapBuffer, GLenum)                                                        \
    X(void, MultiDrawElementsBaseVertex, GLenum, const GLsizei*, GLenum, const void* const*, GLsizei, const GLint*)

namespace gl {

#define SF_GL_DECLARE(ret, name, ...)                    \
    using PFN_##name = ret(SF_GLAPI*)(__VA_ARGS__);      \
    extern PFN_##name name;
SF_GL_FUNCTIONS(SF_GL_DECLARE)
#undef SF_GL_DECLARE

using GenericProc = void (*)();
using ProcLoader = GenericProc (*)(const char* name);

// Returns the name of the first missing function, or nullptr on success.
const char* load(ProcLoader loader);

} // namespace gl
