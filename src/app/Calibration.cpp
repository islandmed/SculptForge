// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Calibration.h"

#include "core/ThreadPool.h"
#include "core/TierConfig.h"
#include "render/GL.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace sf {

namespace {

using Clock = std::chrono::steady_clock;
double nowMs() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

#if defined(_WIN32)
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
#endif

// --- The few GL entry points beyond render/GL.h, loaded here so this module stays self-contained ---
constexpr GLenum kGL_VENDOR = 0x1F00, kGL_EXTENSIONS = 0x1F03, kGL_NUM_EXTENSIONS = 0x821D, kGL_MAX_TEXTURE_SIZE = 0x0D33;
constexpr GLenum kGL_TIME_ELAPSED = 0x88BF, kGL_QUERY_RESULT = 0x8866;
constexpr GLenum kGL_FRAMEBUFFER_BINDING = 0x8CA6, kGL_VIEWPORT = 0x0BA2, kGL_CURRENT_PROGRAM = 0x8B8D,
                 kGL_VERTEX_ARRAY_BINDING = 0x85B5, kGL_ARRAY_BUFFER_BINDING = 0x8894, kGL_DEPTH_FUNC = 0x0B74,
                 kGL_DEPTH_WRITEMASK = 0x0B72, kGL_RENDERBUFFER_BINDING = 0x8CA7;
// GL_NVX_gpu_memory_info (KB), GL_ATI_meminfo (KB, 4 values: the first is the pool's free total).
constexpr GLenum kGL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX = 0x9047, kGL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX = 0x9049;
constexpr GLenum kGL_TEXTURE_FREE_MEMORY_ATI = 0x87FC;
#if defined(__linux__) || defined(__FreeBSD__)
// GLX_MESA_query_renderer.
constexpr int kGLX_RENDERER_ACCELERATED_MESA = 0x8186, kGLX_RENDERER_VIDEO_MEMORY_MESA = 0x8187,
              kGLX_RENDERER_UNIFIED_MEMORY_ARCHITECTURE_MESA = 0x8188;
#endif

struct GlExtra {
    const GLubyte*(SF_GLAPI* GetStringi)(GLenum, GLuint) = nullptr;
    void(SF_GLAPI* GenQueries)(GLsizei, GLuint*) = nullptr;
    void(SF_GLAPI* DeleteQueries)(GLsizei, const GLuint*) = nullptr;
    void(SF_GLAPI* BeginQuery)(GLenum, GLuint) = nullptr;
    void(SF_GLAPI* EndQuery)(GLenum) = nullptr;
    void(SF_GLAPI* GetQueryObjectui64v)(GLuint, GLenum, uint64_t*) = nullptr;
    void(SF_GLAPI* DrawElementsInstanced)(GLenum, GLsizei, GLenum, const void*, GLsizei) = nullptr;
    bool timer() const { return GenQueries && DeleteQueries && BeginQuery && EndQuery && GetQueryObjectui64v; }
};

GlExtra loadExtra() {
    GlExtra e;
    auto get = [](const char* name) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); };
    e.GetStringi = reinterpret_cast<decltype(e.GetStringi)>(get("glGetStringi"));
    e.GenQueries = reinterpret_cast<decltype(e.GenQueries)>(get("glGenQueries"));
    e.DeleteQueries = reinterpret_cast<decltype(e.DeleteQueries)>(get("glDeleteQueries"));
    e.BeginQuery = reinterpret_cast<decltype(e.BeginQuery)>(get("glBeginQuery"));
    e.EndQuery = reinterpret_cast<decltype(e.EndQuery)>(get("glEndQuery"));
    e.GetQueryObjectui64v = reinterpret_cast<decltype(e.GetQueryObjectui64v)>(get("glGetQueryObjectui64v"));
    e.DrawElementsInstanced = reinterpret_cast<decltype(e.DrawElementsInstanced)>(get("glDrawElementsInstanced"));
    return e;
}

std::string glString(GLenum name) {
    const GLubyte* s = gl::GetString(name);
    return s ? reinterpret_cast<const char*>(s) : "";
}

bool hasExtension(const GlExtra& e, const char* name) {
    if (!e.GetStringi) return false;
    GLint n = 0;
    gl::GetIntegerv(kGL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const GLubyte* s = e.GetStringi(kGL_EXTENSIONS, GLuint(i));
        if (s && std::strcmp(reinterpret_cast<const char*>(s), name) == 0) return true;
    }
    return false;
}

#if defined(_WIN32)
// The adapter DXGI lists for the GL vendor, with the most dedicated memory (MB); 0 if none.
uint64_t dxgiVramMB(const std::string& glVendor, uint64_t* sharedMB) {
    const std::string v = lower(glVendor);
    UINT want = 0;
    if (v.find("nvidia") != std::string::npos) want = 0x10DE;
    else if (v.find("amd") != std::string::npos || v.find("ati") != std::string::npos) want = 0x1002;
    else if (v.find("intel") != std::string::npos) want = 0x8086;
    else if (v.find("qualcomm") != std::string::npos) want = 0x5143;
    HMODULE dll = LoadLibraryW(L"dxgi.dll");
    if (!dll) return 0;
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    auto create = reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dll, "CreateDXGIFactory1")));
    uint64_t best = 0;
    IDXGIFactory1* factory = nullptr;
    if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(adapter->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (want == 0 || d.VendorId == want)) {
                const uint64_t mb = uint64_t(d.DedicatedVideoMemory) >> 20;
                if (mb >= best) {
                    best = mb;
                    if (sharedMB) *sharedMB = uint64_t(d.SharedSystemMemory) >> 20;
                }
            }
            adapter->Release();
        }
        factory->Release();
    }
    FreeLibrary(dll);
    return best;
}
#endif

#if defined(__APPLE__)
// CGL's video memory for the context's current renderer (MB); 0 if CGL doesn't say. Looked up at run time
// (OpenGL.framework is loaded with the context).
uint64_t cglVramMB() {
    using GetCurrentContext = void* (*)();
    using GetParameter = int (*)(void*, int, int*);
    using QueryRendererInfo = int (*)(unsigned, void**, int*);
    using DescribeRenderer = int (*)(void*, int, int, int*);
    using DestroyRendererInfo = int (*)(void*);
    auto current = reinterpret_cast<GetCurrentContext>(dlsym(RTLD_DEFAULT, "CGLGetCurrentContext"));
    auto getParameter = reinterpret_cast<GetParameter>(dlsym(RTLD_DEFAULT, "CGLGetParameter"));
    auto query = reinterpret_cast<QueryRendererInfo>(dlsym(RTLD_DEFAULT, "CGLQueryRendererInfo"));
    auto describe = reinterpret_cast<DescribeRenderer>(dlsym(RTLD_DEFAULT, "CGLDescribeRenderer"));
    auto destroy = reinterpret_cast<DestroyRendererInfo>(dlsym(RTLD_DEFAULT, "CGLDestroyRendererInfo"));
    if (!current || !getParameter || !query || !describe || !destroy) return 0;
    constexpr int kCGLCPCurrentRendererID = 309, kCGLRPRendererID = 70, kCGLRPVideoMemoryMegabytes = 131;
    void* ctx = current();
    int rendererId = 0;
    if (!ctx || getParameter(ctx, kCGLCPCurrentRendererID, &rendererId) != 0) return 0;
    void* info = nullptr;
    int count = 0;
    if (query(0xFFFFFFFFu, &info, &count) != 0 || !info) return 0;
    uint64_t mb = 0;
    for (int i = 0; i < count; ++i) {
        int id = 0, mem = 0;
        if (describe(info, i, kCGLRPRendererID, &id) != 0) continue;
        if ((id & 0xFFFFFF00) != (rendererId & 0xFFFFFF00)) continue;
        if (describe(info, i, kCGLRPVideoMemoryMegabytes, &mem) == 0 && mem > 0) mb = std::max<uint64_t>(mb, uint64_t(mem));
    }
    destroy(info);
    return mb;
}
#endif

// Saves the GL state the benchmarks change and puts it back.
struct GlStateGuard {
    GLint fbo = 0, viewport[4] = {}, program = 0, vao = 0, arrayBuffer = 0, depthFunc = 0, depthMask = 1, rb = 0;
    GLint depthTest = 0, blend = 0, cull = 0, scissor = 0;
    GlStateGuard() {
        gl::GetIntegerv(kGL_FRAMEBUFFER_BINDING, &fbo);
        gl::GetIntegerv(kGL_VIEWPORT, viewport);
        gl::GetIntegerv(kGL_CURRENT_PROGRAM, &program);
        gl::GetIntegerv(kGL_VERTEX_ARRAY_BINDING, &vao);
        gl::GetIntegerv(kGL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        gl::GetIntegerv(kGL_DEPTH_FUNC, &depthFunc);
        gl::GetIntegerv(kGL_DEPTH_WRITEMASK, &depthMask);
        gl::GetIntegerv(kGL_RENDERBUFFER_BINDING, &rb);
        gl::GetIntegerv(GL_DEPTH_TEST, &depthTest);
        gl::GetIntegerv(GL_BLEND, &blend);
        gl::GetIntegerv(GL_CULL_FACE, &cull);
        gl::GetIntegerv(GL_SCISSOR_TEST, &scissor);
    }
    ~GlStateGuard() {
        gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(fbo));
        gl::Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        gl::UseProgram(GLuint(program));
        gl::BindVertexArray(GLuint(vao));
        gl::BindBuffer(GL_ARRAY_BUFFER, GLuint(arrayBuffer));
        gl::BindRenderbuffer(GL_RENDERBUFFER, GLuint(rb));
        gl::DepthFunc(GLenum(depthFunc));
        gl::DepthMask(GLboolean(depthMask ? GL_TRUE : GL_FALSE));
        auto set = [](GLenum cap, GLint on) { on ? gl::Enable(cap) : gl::Disable(cap); };
        set(GL_DEPTH_TEST, depthTest);
        set(GL_BLEND, blend);
        set(GL_CULL_FACE, cull);
        set(GL_SCISSOR_TEST, scissor);
        while (gl::GetError() != GL_NO_ERROR) {} // nothing the benchmarks caused should reach the app's checks
    }
};

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform float uDepthStep;
out vec3 vNormal;
void main() {
    float k = float(gl_InstanceID);
    vec2 p = aPos * 2.0 - 1.0 + vec2(k * 0.0007, k * 0.0003);
    float a = aPos.x * 40.0 + k, b = aPos.y * 40.0;
    vNormal = normalize(vec3(cos(a), sin(b), 1.0));
    gl_Position = vec4(p, 0.9 - k * uDepthStep + sin(a) * cos(b) * 0.0005, 1.0);
}
)";
const char* kFragmentShader = R"(#version 330 core
in vec3 vNormal;
out vec4 color;
void main() {
    float d = max(dot(normalize(vNormal), normalize(vec3(0.3, 0.5, 0.8))), 0.0);
    color = vec4(vec3(0.2 + 0.8 * d), 1.0);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        SDL_Log("Calibration shader: %s", log);
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}

// Triangle throughput: a dense grid (about 4 pixels per triangle, like a sculpt seen whole) drawn instanced into
// a 1024 x 1024 offscreen target, every instance in front of the last (all of it shaded). Million triangles/s.
double measureTriangles(const GlExtra& e, bool quick, bool& usedTimer) {
    usedTimer = false;
    if (!e.DrawElementsInstanced) return 0.0;
    GlStateGuard guard;
    constexpr int kSize = 1024, kGx = 512, kGy = 256;
    constexpr GLsizei kTriangles = kGx * kGy * 2;
    GLuint fbo = 0, rbs[2] = {}, vao = 0, buffers[2] = {}, program = 0;
    gl::GenFramebuffers(1, &fbo);
    gl::GenRenderbuffers(2, rbs);
    gl::BindRenderbuffer(GL_RENDERBUFFER, rbs[0]);
    gl::RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, kSize, kSize);
    gl::BindRenderbuffer(GL_RENDERBUFFER, rbs[1]);
    gl::RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kSize, kSize);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbs[0]);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbs[1]);
    double result = 0.0;
    const GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader), fs = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vs && fs && gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        program = gl::CreateProgram();
        gl::AttachShader(program, vs);
        gl::AttachShader(program, fs);
        gl::LinkProgram(program);
        GLint linked = 0;
        gl::GetProgramiv(program, GL_LINK_STATUS, &linked);
        std::vector<float> verts;
        verts.reserve(size_t(kGx + 1) * (kGy + 1) * 2);
        for (int y = 0; y <= kGy; ++y)
            for (int x = 0; x <= kGx; ++x) {
                verts.push_back(float(x) / kGx);
                verts.push_back(float(y) / kGy);
            }
        std::vector<uint32_t> indices;
        indices.reserve(size_t(kTriangles) * 3);
        for (int y = 0; y < kGy; ++y)
            for (int x = 0; x < kGx; ++x) {
                const uint32_t a = uint32_t(y * (kGx + 1) + x), b = a + 1, c = a + uint32_t(kGx + 1), d = c + 1;
                indices.insert(indices.end(), {a, b, d, a, d, c});
            }
        gl::GenVertexArrays(1, &vao);
        gl::BindVertexArray(vao);
        gl::GenBuffers(2, buffers);
        gl::BindBuffer(GL_ARRAY_BUFFER, buffers[0]);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
        gl::EnableVertexAttribArray(0);
        gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
        if (linked) {
            gl::UseProgram(program);
            gl::Viewport(0, 0, kSize, kSize);
            gl::Disable(GL_BLEND);
            gl::Disable(GL_CULL_FACE);
            gl::Disable(GL_SCISSOR_TEST);
            gl::Enable(GL_DEPTH_TEST);
            gl::DepthFunc(GL_LEQUAL);
            gl::DepthMask(GL_TRUE);
            const GLint depthStep = gl::GetUniformLocation(program, "uDepthStep");
            GLuint query = 0;
            const bool timer = e.timer();
            if (timer) e.GenQueries(1, &query);
            // One timed draw of `instances` grids: milliseconds on the GPU (timer query) or the CPU clock.
            auto run = [&](int instances, bool& gpuTimed) {
                gl::ClearColor(0.1f, 0.1f, 0.1f, 1.0f);
                gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                gl::Uniform1f(depthStep, 0.8f / float(std::max(instances, 1)));
                gl::Finish();
                const double t0 = nowMs();
                if (timer) e.BeginQuery(kGL_TIME_ELAPSED, query);
                e.DrawElementsInstanced(GL_TRIANGLES, kTriangles * 3, GL_UNSIGNED_INT, nullptr, instances);
                if (timer) e.EndQuery(kGL_TIME_ELAPSED);
                gl::Finish();
                const double cpu = nowMs() - t0;
                gpuTimed = false;
                if (timer) {
                    uint64_t ns = 0;
                    e.GetQueryObjectui64v(query, kGL_QUERY_RESULT, &ns);
                    if (ns > 0 && double(ns) * 1e-6 <= cpu * 1.5 + 1.0) { // a plausible GPU time
                        gpuTimed = true;
                        return double(ns) * 1e-6;
                    }
                }
                return cpu;
            };
            bool gpuTimed = false;
            run(1, gpuTimed); // warm up: shader compilation, buffer residency
            const double one = std::max(0.05, run(1, gpuTimed));
            // Enough instances for about 25 ms a run (15 quick), so timer resolution and fixed costs don't matter.
            const int instances = std::clamp(int(std::lround((quick ? 15.0 : 25.0) / one)), 1, 256);
            std::vector<double> rates;
            int timed = 0;
            for (int i = 0; i < (quick ? 3 : 5); ++i) {
                const double ms = run(instances, gpuTimed);
                timed += gpuTimed ? 1 : 0;
                if (ms > 0.0) rates.push_back(double(kTriangles) * instances / (ms * 1e-3) / 1e6);
            }
            usedTimer = timed > 0;
            result = median(rates);
            if (query) e.DeleteQueries(1, &query);
        }
    }
    if (vs) gl::DeleteShader(vs);
    if (fs) gl::DeleteShader(fs);
    if (program) gl::DeleteProgram(program);
    gl::DeleteBuffers(2, buffers);
    gl::DeleteVertexArrays(1, &vao);
    gl::DeleteFramebuffers(1, &fbo);
    gl::DeleteRenderbuffers(2, rbs);
    return result;
}

// Upload bandwidth: glBufferSubData of a whole buffer (as the renderer sends changed vertices), waited for. GB/s.
double measureUpload(bool quick) {
    GlStateGuard guard;
    const size_t bytes = (quick ? size_t(16) : size_t(32)) << 20;
    std::vector<uint8_t> data(bytes);
    for (size_t i = 0; i < bytes; ++i) data[i] = uint8_t(i * 2654435761u >> 24);
    GLuint buffer = 0;
    gl::GenBuffers(1, &buffer);
    gl::BindBuffer(GL_ARRAY_BUFFER, buffer);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(bytes), nullptr, GL_DYNAMIC_DRAW);
    gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), data.data()); // first touch allocates
    gl::Finish();
    std::vector<double> rates;
    for (int i = 0; i < (quick ? 3 : 5); ++i) {
        data[size_t(i) * 7919] ^= 0xFF;
        const double t0 = nowMs();
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), data.data());
        gl::Finish();
        const double ms = nowMs() - t0;
        if (ms > 0.0) rates.push_back(double(bytes) / (ms * 1e-3) / 1e9);
    }
    gl::DeleteBuffers(1, &buffer);
    return median(rates);
}

} // namespace

void probeHardware(HardwareInfo& info) {
    probeSystem(info);
    const GlExtra e = loadExtra();
    info.glVendor = glString(kGL_VENDOR);
    info.glRenderer = glString(GL_RENDERER);
    info.glVersion = glString(GL_VERSION);
    info.gpuKind = classifyGpu(info.glVendor, info.glRenderer);
    GLint maxTexture = 0;
    gl::GetIntegerv(kGL_MAX_TEXTURE_SIZE, &maxTexture);
    info.maxTextureSize = maxTexture;
    info.timerQuery = e.timer();
    info.vramMB = info.vramAvailableMB = 0;
    info.vramSource = "none";
    while (gl::GetError() != GL_NO_ERROR) {}
    // NVIDIA (and Mesa's radeonsi): dedicated and free video memory.
    if (hasExtension(e, "GL_NVX_gpu_memory_info")) {
        GLint dedicated = 0, available = 0;
        gl::GetIntegerv(kGL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &dedicated);
        gl::GetIntegerv(kGL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &available);
        if (gl::GetError() == GL_NO_ERROR && dedicated > 0) {
            info.vramMB = uint64_t(dedicated) >> 10;
            info.vramAvailableMB = uint64_t(std::max(available, 0)) >> 10;
            info.vramSource = "GL_NVX_gpu_memory_info";
        }
    }
    // AMD: free memory only (no total).
    if (hasExtension(e, "GL_ATI_meminfo")) {
        GLint free[4] = {};
        gl::GetIntegerv(kGL_TEXTURE_FREE_MEMORY_ATI, free);
        if (gl::GetError() == GL_NO_ERROR && free[0] > 0 && info.vramAvailableMB == 0) {
            info.vramAvailableMB = uint64_t(free[0]) >> 10;
            if (info.vramSource == "none") info.vramSource = "GL_ATI_meminfo (free only)";
        }
    }
#if defined(__linux__) || defined(__FreeBSD__)
    // Mesa on X11 (GLX): video memory, unified memory and whether it's accelerated. Not on Wayland (EGL).
    const char* driver = SDL_GetCurrentVideoDriver();
    if (info.vramMB == 0 && driver && std::strcmp(driver, "x11") == 0 && !SDL_GetHintBoolean(SDL_HINT_VIDEO_FORCE_EGL, false)) {
        using QueryRenderer = int (*)(int, unsigned*);
        auto query = reinterpret_cast<QueryRenderer>(reinterpret_cast<void*>(SDL_GL_GetProcAddress("glXQueryCurrentRendererIntegerMESA")));
        unsigned value = 0;
        if (query && query(kGLX_RENDERER_VIDEO_MEMORY_MESA, &value) && value > 0) {
            info.vramMB = value;
            info.vramSource = "GLX_MESA_query_renderer";
            unsigned unified = 0, accelerated = 1;
            if (query(kGLX_RENDERER_UNIFIED_MEMORY_ARCHITECTURE_MESA, &unified) && unified &&
                (info.gpuKind == GpuKind::Discrete || info.gpuKind == GpuKind::Unknown))
                info.gpuKind = GpuKind::Integrated;
            if (query(kGLX_RENDERER_ACCELERATED_MESA, &accelerated) && !accelerated) info.gpuKind = GpuKind::Software;
        }
    }
    // amdgpu exposes its VRAM size in sysfs (works on Wayland too).
    if (info.vramMB == 0 && info.gpuKind != GpuKind::Software && info.gpuKind != GpuKind::Unified) {
        if (const uint64_t mb = sysfsVramMB(info.glVendor)) {
            info.vramMB = mb;
            info.vramSource = "sysfs amdgpu mem_info_vram_total";
            if (info.gpuKind == GpuKind::Discrete && mb <= 2048 && info.vramAvailableMB == 0) info.gpuKind = GpuKind::Integrated; // an APU's carve-out
        }
    }
#endif
#if defined(_WIN32)
    if (info.vramMB == 0 && info.gpuKind != GpuKind::Software) {
        uint64_t shared = 0;
        if (const uint64_t mb = dxgiVramMB(info.glVendor, &shared)) {
            info.vramMB = mb;
            info.vramSource = "DXGI";
            if (mb < 512 && info.gpuKind != GpuKind::Discrete) info.gpuKind = GpuKind::Integrated; // a carve-out only
        }
    }
#endif
#if defined(__APPLE__)
    if (info.vramMB == 0 && info.gpuKind != GpuKind::Unified) {
        if (const uint64_t mb = cglVramMB()) {
            info.vramMB = mb;
            info.vramSource = "CGL";
        }
    }
#endif
    if (info.vramMB == 0 && info.gpuKind == GpuKind::Unified) info.vramSource = "unified (system RAM)";
    while (gl::GetError() != GL_NO_ERROR) {}
}

bool GpuFrameTimer::available() {
    if (!loaded_) {
        loaded_ = true;
        const GlExtra e = loadExtra();
        ok_ = e.timer();
        if (ok_) {
            e.GenQueries(kSlots, queries_);
            while (gl::GetError() != GL_NO_ERROR) {}
        }
    }
    return ok_;
}

void GpuFrameTimer::begin() {
    if (!available() || active_ >= 0) return;
    if (pending_[next_]) pending_[next_] = false; // never read in time: dropped
    static const GlExtra e = loadExtra();
    e.BeginQuery(kGL_TIME_ELAPSED, queries_[next_]);
    active_ = next_;
}

void GpuFrameTimer::end(uint64_t frame) {
    if (active_ < 0) return;
    static const GlExtra e = loadExtra();
    e.EndQuery(kGL_TIME_ELAPSED);
    frames_[active_] = frame;
    pending_[active_] = true;
    next_ = (active_ + 1) % kSlots;
    active_ = -1;
}

bool GpuFrameTimer::poll(uint64_t& frame, double& ms) {
    if (!ok_) return false;
    static const GlExtra e = loadExtra();
    constexpr GLenum kGL_QUERY_RESULT_AVAILABLE = 0x8867;
    // Oldest first: the slot after the next one to be written.
    for (int k = 0; k < kSlots; ++k) {
        const int i = (next_ + k) % kSlots;
        if (!pending_[i] || i == active_) continue;
        uint64_t ready = 0;
        e.GetQueryObjectui64v(queries_[i], kGL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) return false; // later ones aren't either
        uint64_t ns = 0;
        e.GetQueryObjectui64v(queries_[i], kGL_QUERY_RESULT, &ns);
        pending_[i] = false;
        frame = frames_[i];
        ms = double(ns) * 1e-6;
        return true;
    }
    return false;
}

void GpuFrameTimer::release() {
    if (ok_) {
        static const GlExtra e = loadExtra();
        if (active_ >= 0) e.EndQuery(kGL_TIME_ELAPSED);
        e.DeleteQueries(kSlots, queries_);
    }
    ok_ = false;
    loaded_ = false;
    active_ = -1;
    for (bool& p : pending_) p = false;
}

Calibrator::~Calibrator() = default;

const char* Calibrator::stageName() const {
    switch (stage_) {
    case 0: return "Detecting the GPU";
    case 1: return "Measuring triangle throughput";
    case 2: return "Measuring upload bandwidth";
    case 3: return "Measuring memory bandwidth";
    case 4: return "Measuring brush speed";
    default: return "Done";
    }
}

bool Calibrator::step() {
    if (done()) return false;
    if (stage_ == 0) startedMs_ = nowMs();
    switch (stage_) {
    case 0: probeHardware(hw_); break;
    case 1: cal_.trianglesPerSecondM = measureTriangles(loadExtra(), quick_, cal_.gpuTimer); break;
    case 2: cal_.uploadGBps = measureUpload(quick_); break;
    case 3: cal_.memoryGBps = measureMemoryBandwidthGBps(quick_ ? 3 : 5); break;
    case 4:
        cal_.dabMs = measureDabMs(quick_ ? 200'000 : 1'000'000, quick_ ? 3 : 5, quick_ ? 12 : 20, &cal_.dabVertices);
        break;
    }
    ++stage_;
    cal_.runs = quick_ ? 3 : 5;
    cal_.durationMs = nowMs() - startedMs_;
    return !done();
}

void Calibrator::skip() {
    if (stage_ == 0) probeHardware(hw_); // what the GPU is takes no time; only the measuring is skipped
    cal_ = Calibration{};
    cal_.skipped = true;
    stage_ = kStages;
}

HardwareProfile Calibrator::profile(const ProfileOverrides& overrides, const ProfileTier* previous) const {
    return mapProfile(hw_, cal_, overrides, previous);
}

void applyCachedThreadCount() {
    HardwareProfile cached;
    if (loadHardwareProfile(hardwareProfilePath(), cached) && cached.settings.threads > 0)
        ThreadPool::setSharedThreadCount(cached.settings.threads);
}

HardwareProfile calibrateNow(bool quick, const ProfileOverrides& overrides, const ProfileTier* previous) {
    Calibrator c(quick);
    while (c.step()) {}
    return c.profile(overrides, previous); // the same GPU as last time: its tier holds near a boundary
}

int runCalibrateCommandLine(bool quick, bool save) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* window = SDL_CreateWindow("SculptForge calibration", 320, 200, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) {
        std::fprintf(stderr, "OpenGL 3.3 context creation failed: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    int code = 1;
    if (const char* missing = gl::load([](const char* n) { return reinterpret_cast<gl::GenericProc>(SDL_GL_GetProcAddress(n)); })) {
        std::fprintf(stderr, "Missing OpenGL function: %s\n", missing);
    } else {
        HardwareProfile cached;
        const std::string path = hardwareProfilePath();
        const bool hadCache = loadHardwareProfile(path, cached);
        HardwareInfo now;
        probeHardware(now);
        const bool sameGpu = hadCache && cached.hardware.glRenderer == now.glRenderer && cached.hardware.glVendor == now.glVendor;
        const HardwareProfile p = calibrateNow(quick, hadCache ? cached.overrides : ProfileOverrides{}, sameGpu ? &cached.measuredTier : nullptr);
        // The tier configuration for this power source and the primary display.
        PowerSource power = PowerSource::Unknown;
        switch (SDL_GetPowerInfo(nullptr, nullptr)) {
        case SDL_POWERSTATE_ON_BATTERY: power = PowerSource::Battery; break;
        case SDL_POWERSTATE_NO_BATTERY:
        case SDL_POWERSTATE_CHARGING:
        case SDL_POWERSTATE_CHARGED: power = PowerSource::AC; break;
        default: break;
        }
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
        const TierConfig tier = tierConfigFor(p, power, mode ? int(mode->refresh_rate + 0.5f) : 0);
        const Json j = calibrationJson(p, &tier);
        std::string error;
        if (!validateCalibrationJson(j, error)) {
            std::fprintf(stderr, "calibration JSON is invalid: %s\n", error.c_str());
        } else {
            std::puts(j.dump(true).c_str());
            code = 0;
            if (save && !saveHardwareProfile(path, p, error)) std::fprintf(stderr, "not saved: %s\n", error.c_str());
        }
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}

} // namespace sf
