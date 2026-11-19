// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The render bridge (ai/RenderBridge.h): the render stage (camera, lights, settings), the helper's command line and
// help, what may be started and with which environment, the helper's fingerprint, and whole renders through
// tests/data/render_bridge/fake_usdrecord (a Python stand-in for usdrecord). With OpenUSD's own usdrecord on PATH the
// USD written for the bridge is also rendered for real (Storm); without it that test says so and is skipped.
#include "TestHarness.h"

#include "ai/RenderBridge.h"
#include "core/Commands.h"
#include "core/ManagedPolicy.h"
#include "core/Primitives.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace sf;
namespace rb = sf::renderbridge;
namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

double numberAfter(const std::string& text, const std::string& key) {
    const size_t at = text.find(key);
    return at == std::string::npos ? -1.0 : std::atof(text.c_str() + at + key.size());
}

fs::path scratch(const char* name) {
    const fs::path d = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

#ifdef SF_TEST_DATA_DIR
std::string fakeHelper() { return (fs::path(SF_TEST_DATA_DIR) / "render_bridge" / "fake_usdrecord").string(); }
// The fake is a Python script: it runs where a Python 3 does (POSIX: its #! line; Windows: python on PATH).
bool canRunFake() {
#if defined(_WIN32)
    return !ai::findOnPath("python").empty();
#else
    return !ai::findOnPath("python3").empty();
#endif
}
#endif

} // namespace

TEST(render_bridge_stage_has_the_view_camera) {
    rb::Request r;
    r.width = 1600;
    r.height = 900;
    const glm::mat4 view = glm::lookAt(glm::vec3(1, 2, 5), glm::vec3(0, 0.5f, 0), glm::vec3(0, 1, 0));
    r.view.cameraToWorld = glm::inverse(view);
    r.view.fovY = glm::radians(35.0f);
    const std::string s = rb::stageText(r, "scene.usda");
    CHECK(s.rfind("#usda 1.0", 0) == 0);
    CHECK(s.find("subLayers = [\n        @./scene.usda@") != std::string::npos);
    CHECK(s.find("defaultPrim = \"SculptForgeRender\"") != std::string::npos);
    CHECK(s.find("upAxis = \"Y\"") != std::string::npos && s.find("metersPerUnit = 1") != std::string::npos);
    CHECK(s.find("def Camera \"Camera\"") != std::string::npos);
    CHECK(s.find("token projection = \"perspective\"") != std::string::npos);
    // The apertures have the frame's shape and the view's field of view.
    const double h = numberAfter(s, "horizontalAperture = "), v = numberAfter(s, "verticalAperture = ");
    const double focal = numberAfter(s, "focalLength = ");
    CHECK(std::abs(h / v - 1600.0 / 900.0) < 1e-4);
    CHECK(std::abs(2.0 * std::atan(v * 0.5 / focal) - glm::radians(35.0)) < 1e-4);
    // The camera sits at the eye: the transform's last row is the eye position (USD's row vectors).
    {
        const size_t line = s.find("xformOp:transform = ");
        const size_t last = s.rfind("), (", s.find(" )", line)) + 4; // the fourth row
        double x = 0, y = 0, z = 0, w = 0;
        CHECK(std::sscanf(s.c_str() + last, "%lf, %lf, %lf, %lf", &x, &y, &z, &w) == 4);
        CHECK(std::abs(x - 1) < 1e-5 && std::abs(y - 2) < 1e-5 && std::abs(z - 5) < 1e-5 && w == 1.0);
    }
    CHECK(s.find("rel camera = </SculptForgeRender/Camera>") != std::string::npos);
    CHECK(s.find("uniform int2 resolution = (1600, 900)") != std::string::npos);
    // The studio light: three distant lights and a dome; off, none.
    CHECK(s.find("def DistantLight \"Key\"") != std::string::npos && s.find("def DistantLight \"Rim\"") != std::string::npos);
    CHECK(s.find("def DomeLight \"Sky\"") != std::string::npos);
    r.studioLight = false;
    const std::string dark = rb::stageText(r, "scene.usda");
    CHECK(dark.find("DistantLight") == std::string::npos && dark.find("DomeLight") == std::string::npos);
    CHECK(s.find("nan") == std::string::npos && s.find("inf") == std::string::npos);
}

TEST(render_bridge_stage_orthographic_and_shifted) {
    rb::Request r;
    r.width = 1000;
    r.height = 1000;
    r.view.ortho = true;
    r.view.orthoHalfHeight = 2.5f;
    r.view.shift = glm::vec2(0.2f, -0.1f);
    const std::string s = rb::stageText(r, "scene.usda");
    CHECK(s.find("token projection = \"orthographic\"") != std::string::npos);
    // Orthographic apertures are the frame in tenths of a scene unit: 5 units tall.
    CHECK(std::abs(numberAfter(s, "verticalAperture = ") - 50.0) < 1e-3);
    // The lens shift moves the window the other way (the target lands right of centre).
    CHECK(std::abs(numberAfter(s, "horizontalApertureOffset = ") - (-0.2 * 50.0 * 0.5)) < 1e-3);
    CHECK(std::abs(numberAfter(s, "verticalApertureOffset = ") - (0.1 * 50.0 * 0.5)) < 1e-3);
}

TEST(render_bridge_framed_view_sees_the_box) {
    const rb::View v = rb::framedView(glm::vec3(-1), glm::vec3(1), 16.0f / 9.0f);
    const glm::mat4 view = glm::inverse(v.cameraToWorld);
    const glm::vec4 centre = view * glm::vec4(0, 0, 0, 1);
    CHECK(centre.z < -1.5f && std::abs(centre.x) < 1e-4f && std::abs(centre.y) < 1e-4f); // looking at it, in front
    const glm::mat4 proj = glm::perspective(v.fovY, 16.0f / 9.0f, v.nearClip, v.farClip);
    for (int k = 0; k < 8; ++k) {
        const glm::vec4 c = proj * view * glm::vec4(k & 1 ? 1 : -1, k & 2 ? 1 : -1, k & 4 ? 1 : -1, 1);
        CHECK(std::abs(c.x / c.w) <= 1.0f && std::abs(c.y / c.w) <= 1.0f);
    }
}

TEST(render_bridge_reads_the_helpers_help) {
    const std::string help =
        "usage: usdrecord [-h] [--camera CAMERA] [--renderer {Storm,Embree,Other Renderer}]\n"
        "                 [--imageWidth IMAGEWIDTH] [--renderSettingsPrimPath RENDERSETTINGSPRIMPATH]\n"
        "                 [--disableCameraLight] usdFilePath outputImagePath\n"
        "  --renderer {Storm,Embree,Other Renderer}, -r {Storm,Embree,Other Renderer}\n";
    const rb::HelperInfo info = rb::parseHelp(help);
    CHECK(info.ok);
    CHECK((info.renderers == std::vector<std::string>{"Storm", "Embree", "Other Renderer"}));
    CHECK(info.renderSettings && info.disableCameraLight);
    // An older usdrecord: no settings prim, no camera light switch, renderer names as they are.
    const rb::HelperInfo old = rb::parseHelp("usage: usdrecord [--camera CAMERA] [--renderer {GL,Embree}] [--imageWidth W]");
    CHECK(old.ok && old.renderers.size() == 2 && !old.renderSettings && !old.disableCameraLight);
    // Not usdrecord at all.
    CHECK(!rb::parseHelp("usage: gimp [options] [files]").ok);
    // Names that could be options or shell text never make the list.
    const rb::HelperInfo odd = rb::parseHelp("--camera --imageWidth --renderer {Storm,--evil,a;b,$(x)}");
    CHECK((odd.renderers == std::vector<std::string>{"Storm"}));
}

TEST(render_bridge_command_line) {
    rb::Request r;
    r.width = 1280;
    r.renderer = "Storm";
    rb::HelperInfo info;
    info.renderSettings = true;
    info.disableCameraLight = true;
    const auto a = rb::helperArgs(r, info, "/j/render.usda", "/j/render.png");
    CHECK((a == std::vector<std::string>{"--camera", "/SculptForgeRender/Camera", "--imageWidth", "1280", "--renderer",
                                         "Storm", "--renderSettingsPrimPath", "/SculptForgeRender/Settings",
                                         "--disableCameraLight", "/j/render.usda", "/j/render.png"}));
    r.renderer.clear();
    r.studioLight = false; // (the helper's own camera light then)
    const auto b = rb::helperArgs(r, rb::HelperInfo{}, "s.usda", "o.exr");
    CHECK((b == std::vector<std::string>{"--camera", "/SculptForgeRender/Camera", "--imageWidth", "1280", "s.usda", "o.exr"}));
    CHECK(rb::validRendererName("Storm") && rb::validRendererName("Karma XPU") && !rb::validRendererName("-x") &&
          !rb::validRendererName("a\"b") && !rb::validRendererName("") && !rb::validRendererName(std::string(65, 'a')));
}

TEST(render_bridge_only_starts_real_programs) {
    const fs::path dir = scratch("sf_render_bridge_launch");
    std::string program, error;
    std::vector<std::string> prefix;
    CHECK(!rb::launchFor("", program, prefix, error) && !error.empty());
    CHECK(!rb::launchFor("usdrecord", program, prefix, error));                        // a name, not a path
    CHECK(!rb::launchFor((dir / "missing").string(), program, prefix, error));         // not there
    std::ofstream(dir / "usdrecord.cmd") << "@echo off\n";
    std::ofstream(dir / "run.BAT") << "@echo off\n";
    CHECK(!rb::launchFor((dir / "usdrecord.cmd").string(), program, prefix, error) && error.find("Batch") != std::string::npos);
    CHECK(!rb::launchFor((dir / "run.BAT").string(), program, prefix, error));
    std::ofstream(dir / "usdrecord") << "#!/usr/bin/env python3\nprint('hi')\n";
#if !defined(_WIN32)
    CHECK(rb::launchFor((dir / "usdrecord").string(), program, prefix, error) && program == (dir / "usdrecord").string() &&
          prefix.empty());
#endif
}

TEST(render_bridge_environment_drops_ours_and_secrets) {
    const std::vector<std::string> env = {"PATH=/usr/bin", "HOME=/home/a", "PXR_PLUGINPATH_NAME=/opt/delegates",
                                          "PYTHONPATH=/opt/usd/lib/python", "RLM_LICENSE=5053@lic", "SF_POLICY=/x.json",
                                          "SF_USER_DATA_DIR=/t", "SCULPTFORGE_BLENDER_EXCHANGE=/b", "HF_TOKEN=hf_x",
                                          "OPENAI_API_KEY=sk", "FAL_KEY=k", "AWS_SECRET_ACCESS_KEY=s", "DB_PASSWORD=p",
                                          "GOOGLE_APPLICATION_CREDENTIALS=/c.json", "OCIO=/aces/config.ocio", "broken"};
    const auto out = rb::cleanEnvironment(env);
    CHECK((out == std::vector<std::string>{"PATH=/usr/bin", "HOME=/home/a", "PXR_PLUGINPATH_NAME=/opt/delegates",
                                           "PYTHONPATH=/opt/usd/lib/python", "RLM_LICENSE=5053@lic", "OCIO=/aces/config.ocio"}));
}

TEST(render_bridge_finds_a_prebuilt_installs_own_folders) {
    const fs::path root = scratch("sf_render_bridge_install");
    for (const char* d : {"bin", "lib/python/pxr", "plugin/usd", "python", "libraries"}) fs::create_directories(root / d);
    std::ofstream(root / "bin" / "usdrecord") << "#!/usr/bin/env python\n";
#if defined(_WIN32)
    const std::string sep = ";";
#else
    const std::string sep = ":";
#endif
    const auto out = rb::installEnvironment({"PATH=/usr/bin", "HOME=/h"}, (root / "bin" / "usdrecord").string());
    const std::string path = (root / "python").string() + sep + (root / "lib").string() + sep +
                             (root / "plugin" / "usd").string() + sep + (root / "bin").string() + sep + "/usr/bin";
    CHECK(std::find(out.begin(), out.end(), "PATH=" + path) != out.end());
    CHECK(std::find(out.begin(), out.end(), "PYTHONPATH=" + (root / "lib" / "python").string()) != out.end());
    CHECK(std::find(out.begin(), out.end(), "PXR_MTLX_STDLIB_SEARCH_PATHS=" + (root / "libraries").string()) != out.end());
    CHECK(std::find(out.begin(), out.end(), "HOME=/h") != out.end());
    // A usdrecord that isn't in such an install (or a pxr-less one) changes nothing.
    std::ofstream(root / "usdrecord") << "#!/usr/bin/env python\n";
    const std::vector<std::string> env = {"PATH=/usr/bin"};
    CHECK(rb::installEnvironment(env, (root / "usdrecord").string()) == env);
    CHECK(rb::installEnvironment(env, "/nowhere/bin/usdrecord") == env);
}

TEST(render_bridge_settings_and_fingerprint) {
    const fs::path dir = scratch("sf_render_bridge_settings");
    const fs::path helper = dir / "usdrecord";
    std::ofstream(helper) << "#!/usr/bin/env python3\n";
    rb::Settings s;
    s.helper = helper.string();
    CHECK(!s.helperTrusted()); // never confirmed
    s.helperSha256 = rb::fileFingerprint(s.helper);
    CHECK(s.helperSha256.size() == 64 && s.helperTrusted());
    s.renderer = "Storm";
    s.width = 800;
    s.format = "exr";
    s.openInEditor = true;
    s.editor = "/usr/bin/krita";
    CHECK(s.save((dir / "render_bridge.json").string()));
    const rb::Settings back = rb::Settings::load((dir / "render_bridge.json").string());
    CHECK(back.helper == s.helper && back.helperSha256 == s.helperSha256 && back.renderer == "Storm" && back.width == 800 &&
          back.format == "exr" && back.openInEditor && back.editor == "/usr/bin/krita" && back.helperTrusted());
    // The file changes (an update, or something replaced it): no longer trusted until confirmed again.
    std::ofstream(helper, std::ios::app) << "import os\n";
    CHECK(!back.helperTrusted());
    // A hand-edited file can't smuggle an option in as a renderer, or an odd format.
    std::ofstream(dir / "odd.json") << R"({"renderer": "--x", "format": "bmp", "width": 99999999})";
    const rb::Settings odd = rb::Settings::load((dir / "odd.json").string());
    CHECK(odd.renderer.empty() && odd.format == "png" && odd.width == 16384);
}

TEST(render_bridge_keeps_numbered_renders) {
    const fs::path dir = scratch("sf_render_bridge_keep");
    std::ofstream(dir / "render.png") << "png";
    std::string err;
    const std::string a = rb::keepRender((dir / "render.png").string(), (dir / "out").string(), "My Head", err);
    const std::string b = rb::keepRender((dir / "render.png").string(), (dir / "out").string(), "My Head", err);
    CHECK(fs::path(a).filename() == "My_Head_0001.png" && fs::path(b).filename() == "My_Head_0002.png");
    const std::string c = rb::keepRender((dir / "render.png").string(), (dir / "out").string(), "../../etc", err);
    CHECK(fs::path(c).parent_path() == dir / "out"); // (the name can't climb out of the folder)
}

TEST(render_bridge_policy_turns_it_off) {
    policy::Policy p;
    std::string err;
    CHECK(policy::parsePolicy(R"({"format":"sculptforge-policy","version":1,"render":{"bridge":"never"}})", "t", p, err));
    CHECK(!p.renderBridgeAllowed && policy::describe(p).find("render bridge never") != std::string::npos);
    CHECK(!policy::parsePolicy(R"({"format":"sculptforge-policy","version":1,"render":{"bridge":"sometimes"}})", "t", p, err));
    CHECK(!policy::parsePolicy(R"({"format":"sculptforge-policy","version":1,"render":{"bridges":"never"}})", "t", p, err));
    policy::Policy never;
    never.renderBridgeAllowed = false;
    policy::setPolicyForTests(never);
    CommandRegistry reg;
    rb::registerCommands(reg, [] { return rb::Settings{}; });
    Document doc = makeDoc(makeIcosphere(2));
    const CommandResult r = reg.execute(doc, "render_bridge", {{"path", std::string("x.png")}});
    CHECK(!r.ok && r.message.find("policy") != std::string::npos);
    CHECK(!reg.execute(doc, "render_bridge_renderers").ok);
    policy::resetPolicyForTests();
    // Without a helper set up it says how to set one up; a changed helper asks for confirmation (never runs it).
    const CommandResult none = reg.execute(doc, "render_bridge", {{"path", std::string("x.png")}});
    CHECK(!none.ok && none.message.find("Preferences > Render bridge") != std::string::npos);
    const fs::path dir = scratch("sf_render_bridge_changed");
    std::ofstream(dir / "usdrecord") << "#!/bin/sh\ntouch " << (dir / "ran").string() << "\n";
    CommandRegistry reg2;
    rb::registerCommands(reg2, [dir] {
        rb::Settings s;
        s.helper = (dir / "usdrecord").string();
        s.helperSha256 = std::string(64, '0');
        return s;
    });
    const CommandResult changed = reg2.execute(doc, "render_bridge", {{"path", std::string("x.png")}});
    CHECK(!changed.ok && changed.message.find("changed") != std::string::npos && !fs::exists(dir / "ran"));
    // The commands name no program: an MCP client can't pick what runs.
    for (const char* name : {"render_bridge", "render_bridge_renderers"})
        for (const ParamSpec& p2 : reg.find(name)->params) CHECK(p2.name != "helper" && p2.name != "program");
}

#ifdef SF_TEST_DATA_DIR
TEST(render_bridge_renders_through_the_helper) {
    if (!canRunFake()) {
        std::printf("  render_bridge_renders_through_the_helper: skipped (no Python 3 to run the stand-in helper)\n");
        return;
    }
    const fs::path jobs = scratch("sf_render_bridge_jobs");
#if defined(_WIN32)
    _putenv_s("SF_RENDER_BRIDGE_DIR", jobs.string().c_str());
#else
    setenv("SF_RENDER_BRIDGE_DIR", jobs.string().c_str(), 1);
#endif
    std::vector<std::string> env = ai::currentEnvironment();
    env.push_back("SF_SECRET_TEST=1"); // (must not reach the helper: the stand-in fails if any SF_ variable does)
    const rb::HelperInfo info = rb::probe(fakeHelper(), env);
    CHECK(info.ok);
    CHECK((info.renderers == std::vector<std::string>{"Storm", "Broken", "Slow"}));
    const Mesh sphere = makeIcosphere(3);
    const std::vector<MeshObjectRef> meshes = {{"Head", &sphere}};
    rb::Request r;
    r.width = 200;
    r.height = 100;
    r.renderer = "Storm";
    r.view = rb::framedView(glm::vec3(-1), glm::vec3(1), 2.0f);
    rb::Job job;
    std::string err;
    CHECK(job.start(fakeHelper(), info, r, meshes, "Head", env, err));
    while (job.poll() == rb::State::Running) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(job.state() == rb::State::Done);
    if (job.state() != rb::State::Done) std::printf("  %s\n", job.error().c_str());
    Image image;
    CHECK(rb::loadImage(job.image(), image, err) && image.width == 200 && image.height == 100);
    CHECK(fs::exists(fs::path(job.folder()) / "scene.usda") && fs::exists(fs::path(job.folder()) / "render.usda"));
    CHECK(readAll(fs::path(job.folder()) / "scene.usda").find("def Mesh") != std::string::npos);
    // A renderer that fails: the reason with the log's last lines, nothing shown.
    r.renderer = "Broken";
    CHECK(job.start(fakeHelper(), info, r, meshes, "Head", env, err));
    while (job.poll() == rb::State::Running) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(job.state() == rb::State::Failed && job.error().find("exit code 1") != std::string::npos &&
          job.error().find("Failed to load the Broken render delegate") != std::string::npos);
    // A renderer the helper doesn't list is refused before anything runs.
    r.renderer = "Nope";
    CHECK(!job.start(fakeHelper(), info, r, meshes, "Head", env, err) && err.find("doesn't list") != std::string::npos);
    // Stop ends a slow render at once.
    r.renderer = "Slow";
    CHECK(job.start(fakeHelper(), info, r, meshes, "Head", env, err));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto t0 = std::chrono::steady_clock::now();
    job.cancel();
    CHECK(job.state() == rb::State::Cancelled);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5));
    // The time limit stops a hung helper.
    job.timeLimitSeconds = 0.3;
    CHECK(job.start(fakeHelper(), info, r, meshes, "Head", env, err));
    while (job.poll() == rb::State::Running) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(job.state() == rb::State::Failed && job.error().find("longer than") != std::string::npos);
    // Only the newest few job folders stay.
    int folders = 0;
    for (const auto& e : fs::directory_iterator(jobs)) folders += e.is_directory();
    CHECK(folders <= 6);
    // The headless command, with the helper given on the command line (sculptforge-cli --render-helper).
    CommandRegistry reg;
    rb::registerCommands(reg, [] { return rb::Settings{}; }, fakeHelper());
    Document doc = makeDoc(makeIcosphere(2));
    const std::string out = (jobs / "cli.png").string();
    const CommandResult cr = reg.execute(doc, "render_bridge", {{"path", out}, {"width", 120.0}, {"height", 120.0}});
    CHECK(cr.ok && fs::exists(out));
    if (!cr.ok) std::printf("  %s\n", cr.message.c_str());
    const CommandResult list = reg.execute(doc, "render_bridge_renderers");
    CHECK(list.ok && list.message == "Storm, Broken, Slow");
#if defined(_WIN32)
    _putenv_s("SF_RENDER_BRIDGE_DIR", "");
#else
    unsetenv("SF_RENDER_BRIDGE_DIR");
#endif
}
#endif

// The USD the bridge writes, rendered by OpenUSD's own usdrecord (Storm) when one is installed.
TEST(render_bridge_usd_renders_in_openusd) {
    const std::string helper = rb::foundHelper();
    if (helper.empty()) {
        std::printf("  render_bridge_usd_renders_in_openusd: skipped (OpenUSD's usdrecord is not on PATH)\n");
        return;
    }
    const std::vector<std::string> env = ai::currentEnvironment();
    const rb::HelperInfo info = rb::probe(helper, env);
    CHECK(info.ok);
    const Mesh sphere = makeIcosphere(3);
    rb::Request r;
    r.width = 320;
    r.height = 240;
    r.view = rb::framedView(glm::vec3(-1), glm::vec3(1), 320.0f / 240.0f);
    rb::Job job;
    std::string err;
    CHECK(job.start(helper, info, r, {{"Sphere", &sphere}}, "Sphere", env, err));
    while (job.poll() == rb::State::Running) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (job.state() != rb::State::Done) std::printf("  usdrecord: %s\n", job.error().c_str());
    CHECK(job.state() == rb::State::Done);
    Image image;
    CHECK(rb::loadImage(job.image(), image, err) && image.width == 320 && image.height == 240);
}
