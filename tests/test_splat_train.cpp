// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Splat training from captures: frame selection, dataset and pose files,
// framing, Brush's log, and the training job driven by a fake Brush / COLMAP /
// ffmpeg (tests/tools/fake_splat_tools.cpp).
#include "TestHarness.h"

#include "ai/SplatTrainer.h"
#include "core/SplatCapture.h"
#include "io/PlyIO.h"

#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace sf;
using namespace sf::capture;
namespace fs = std::filesystem;

namespace {

void setEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

Image checker(int w, int h, int cell, bool flat = false) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(size_t(w) * size_t(h) * 4, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t v = flat ? 128 : ((x / cell + y / cell) % 2 ? 230 : 30);
            for (int c = 0; c < 3; ++c) img.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4 + size_t(c)] = v;
        }
    return img;
}

// A clean folder under the temp directory.
fs::path freshDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

fs::path photoFolder(const char* name, int count) {
    const fs::path d = freshDir(name);
    for (int i = 0; i < count; ++i) {
        std::string err;
        char file[32];
        std::snprintf(file, sizeof file, "IMG_%04d.png", i);
        checker(64, 48, 4 + i % 3).savePng((d / file).string(), err);
    }
    return d;
}

CameraView lookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up) {
    // OpenGL camera: looks down -z, y up.
    const glm::vec3 f = glm::normalize(target - eye), r = glm::normalize(glm::cross(f, up)), u = glm::cross(r, f);
    CameraView v;
    v.camToWorld = glm::mat4(glm::vec4(r, 0), glm::vec4(u, 0), glm::vec4(-f, 0), glm::vec4(eye, 1));
    return v;
}

const char* fakeTools() {
#ifdef SF_FAKE_SPLAT_TOOLS
    return SF_FAKE_SPLAT_TOOLS;
#else
    return "";
#endif
}

ai::TrainRequest fakeRequest(const fs::path& source, const fs::path& out) {
    ai::TrainRequest r;
    r.source = source.string();
    r.outDir = out.string();
    r.brush = r.colmap = r.ffmpeg = fakeTools();
    return r;
}

bool waitFor(ai::SplatTrainJob& job, const std::function<bool(const ai::TrainStatus&)>& cond, double seconds = 30.0) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(int(seconds * 1000));
    while (std::chrono::steady_clock::now() < until) {
        if (cond(job.status())) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return cond(job.status());
}

} // namespace

TEST(splat_capture_sharpness_prefers_sharp_frames) {
    const double sharp = sharpness(checker(320, 240, 8)), flat = sharpness(checker(320, 240, 8, true));
    CHECK(sharp > 1.0);
    CHECK(flat < 1e-6);
    CHECK(sharpness(checker(320, 240, 4)) > sharpness(checker(320, 240, 40))); // finer detail scores higher
    CHECK(sharpness(Image()) == 0.0);
}

TEST(splat_capture_selects_sharpest_frame_per_stretch) {
    // 12 frames, 4 picks: one per stretch of 3, the sharpest of each.
    const std::vector<double> s = {1, 5, 2, 9, 1, 1, 0, 0, 3, 2, 8, 2};
    const std::vector<size_t> pick = selectFrames(s, 4);
    CHECK(pick == std::vector<size_t>({1, 3, 8, 10}));
    CHECK(selectFrames(s, 20).size() == s.size()); // fewer frames than wanted: all of them
    CHECK(selectFrames(s, 0).empty());
    CHECK(selectFrames({}, 3).empty());
    // Ties keep the earliest frame, so the result never depends on anything but the scores.
    CHECK(selectFrames({2, 2, 2, 2}, 2) == std::vector<size_t>({0, 2}));
}

TEST(splat_capture_transforms_json_round_trip) {
    std::vector<CameraView> views;
    for (int i = 0; i < 3; ++i) {
        CameraView v = lookAt(glm::vec3(3.0f * std::cos(i * 2.0f), 0.5f, 3.0f * std::sin(i * 2.0f)), glm::vec3(0), glm::vec3(0, 1, 0));
        v.file = "images/" + std::to_string(i) + ".jpg";
        v.fx = v.fy = 500;
        v.width = 640;
        v.height = 480;
        views.push_back(v);
    }
    const std::string path = tempPath("sf_transforms.json");
    std::string err;
    CHECK(writeTransformsJson(path, views, err));
    std::vector<CameraView> back;
    CHECK(readTransformsJson(path, back, err));
    CHECK(back.size() == 3);
    if (back.size() == 3) {
        CHECK(back[2].file == "images/2.jpg");
        CHECK(back[1].fx == 500.0f && back[1].width == 640 && back[1].cx == 320.0f);
        float diff = 0;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) diff = std::max(diff, std::abs(back[1].camToWorld[c][r] - views[1].camToWorld[c][r]));
        CHECK(diff < 1e-5f);
    }
    CHECK(!readTransformsJson(tempPath("sf_no_such_transforms.json"), back, err));
}

TEST(splat_capture_reads_colmap_poses) {
    // A camera at (0,0,-5) looking at the origin, COLMAP style (world-to-camera, identity rotation).
    const fs::path dir = freshDir("sf_colmap_sparse");
    std::ofstream(dir / "images.txt") << "# comment\n1 1 0 0 0 0 0 5 1 a.jpg\n1.0 2.0 -1\n2 1 0 0 0 1 0 5 1 b.jpg\n\n";
    std::vector<CameraView> views;
    std::string err;
    CHECK(readColmapImages(dir.string(), views, err));
    CHECK(views.size() == 2);
    if (views.size() == 2) {
        CHECK(views[0].file == "images/a.jpg" && views[1].file == "images/b.jpg");
        const glm::vec3 c(views[0].camToWorld[3]);
        CHECK(glm::length(c - glm::vec3(0, 0, -5)) < 1e-5f);
        const glm::vec3 forward = -glm::vec3(views[0].camToWorld[2]); // OpenGL cameras look down -z
        CHECK(glm::dot(forward, glm::vec3(0, 0, 1)) > 0.999f);
        const glm::vec3 up(views[0].camToWorld[1]); // OpenCV y is down: up is -y
        CHECK(glm::dot(up, glm::vec3(0, -1, 0)) > 0.999f);
        CHECK(glm::length(glm::vec3(views[1].camToWorld[3]) - glm::vec3(-1, 0, -5)) < 1e-5f);
    }
    CHECK(!readColmapImages(freshDir("sf_colmap_empty").string(), views, err));
}

TEST(splat_capture_orbit_framing_centres_and_levels) {
    // Cameras circling (1,2,3) in a world whose up is +Z: the object comes to the origin, Z up becomes Y up.
    std::vector<CameraView> views;
    for (int i = 0; i < 12; ++i) {
        const float a = 6.2831853f * float(i) / 12.0f;
        views.push_back(lookAt(glm::vec3(1, 2, 3) + glm::vec3(4 * std::cos(a), 4 * std::sin(a), 1.5f), glm::vec3(1, 2, 3),
                               glm::vec3(0, 0, 1)));
    }
    const glm::mat4 m = orbitFraming(views);
    const glm::vec3 target(m * glm::vec4(1, 2, 3, 1));
    CHECK(glm::length(target) < 1e-3f);
    const glm::vec3 up = glm::normalize(glm::vec3(m * glm::vec4(0, 0, 1, 0)));
    CHECK(up.y > 0.95f);
    const glm::vec3 cam(m * views[0].camToWorld[3]);
    CHECK(std::abs(glm::length(cam) - 3.0f) < 0.01f); // cameras about 3 units away
    CHECK(orbitFraming({}) == glm::mat4(1.0f));
}

TEST(splat_capture_reads_phone_capture_json) {
    const fs::path dir = freshDir("sf_capture_json");
    std::ofstream(dir / "capture.json")
        << R"({"kind":"splat","device":"Test Phone","files":["a.jpg",{"name":"b.jpg","t":12}],)"
        << R"("fl_x":600,"w":640,"h":480,"frames":[{"file":"a.jpg","transform_matrix":[[1,0,0,0],[0,1,0,0],[0,0,1,2],[0,0,0,1]]},)"
        << R"({"file":"b.jpg","transform_matrix":[1,0,0,1, 0,1,0,0, 0,0,1,2, 0,0,0,1],"fl_x":700},{"file":"bad.jpg"}]})";
    CaptureInfo info;
    std::string err;
    CHECK(readCaptureJson((dir / "capture.json").string(), info, err));
    CHECK(info.kind == "splat" && info.device == "Test Phone");
    CHECK(info.files == std::vector<std::string>({"a.jpg", "b.jpg"}));
    CHECK(info.poses.size() == 2); // the frame without a matrix is skipped
    if (info.poses.size() == 2) {
        CHECK(info.poses[0].fx == 600.0f && info.poses[1].fx == 700.0f && info.poses[0].width == 640);
        CHECK(glm::vec3(info.poses[0].camToWorld[3]) == glm::vec3(0, 0, 2)); // row-major in JSON
        CHECK(glm::vec3(info.poses[1].camToWorld[3]) == glm::vec3(1, 0, 2));
    }
    std::ofstream(dir / "broken.json") << "{nope";
    CHECK(!readCaptureJson((dir / "broken.json").string(), info, err));
}

TEST(splat_capture_classifies_sources) {
    const fs::path root = freshDir("sf_capture_kinds");
    CHECK(classifySource((root / "missing").string()) == SourceKind::Missing);
    std::ofstream(root / "clip.MOV") << "x";
    std::ofstream(root / "notes.txt") << "x";
    CHECK(classifySource((root / "clip.MOV").string()) == SourceKind::Video);
    CHECK(classifySource((root / "notes.txt").string()) == SourceKind::Unsupported);
    fs::create_directories(root / "video_capture");
    std::ofstream(root / "video_capture" / "walkaround.mp4") << "x";
    CHECK(classifySource((root / "video_capture").string()) == SourceKind::VideoFolder);
    const fs::path photos = photoFolder("sf_capture_kinds_photos", 2);
    CHECK(classifySource(photos.string()) == SourceKind::Images);
    fs::create_directories(root / "colmap" / "images");
    fs::create_directories(root / "colmap" / "sparse" / "0");
    CHECK(classifySource((root / "colmap").string()) == SourceKind::ColmapDataset);
    fs::create_directories(root / "ns");
    std::ofstream(root / "ns" / "transforms.json") << "{}";
    CHECK(classifySource((root / "ns").string()) == SourceKind::NerfstudioDataset);
    fs::create_directories(root / "empty");
    CHECK(classifySource((root / "empty").string()) == SourceKind::Unsupported);
    CHECK(ai::defaultTrainOutDir((root / "clip.MOV").string()) == (root / "clip-splat").string());
    CHECK(ai::defaultTrainOutDir(photos.string()) == (photos / "splat").string());
}

TEST(splat_capture_brush_log_and_checkpoints) {
    TrainerLog t;
    CHECK(parseTrainerLog("[2026-10-26T10:00:00Z INFO  brush_cli] Refine iter 400, 51234 splats.", t));
    CHECK(t.refineIter == 400 && t.splats == 51234 && t.evalIter == -1);
    CHECK(parseTrainerLog("[x INFO  brush_cli] Eval iter 1000: PSNR 24.31, ssim 0.812", t));
    CHECK(t.evalIter == 1000 && std::abs(t.psnr - 24.31) < 1e-9 && std::abs(t.ssim - 0.812) < 1e-9);
    CHECK(parseTrainerLog("thread 'main' panicked at src/main.rs:3:5:", t) && t.error);
    CHECK(!parseTrainerLog("[x INFO  brush_cli] Loading dataset", t));
    CHECK(checkpointStep("export_1200.ply") == 1200);
    CHECK(checkpointStep("export_7.PLY") == 7);
    CHECK(checkpointStep("export_1200.ply.part") == -1);
    CHECK(checkpointStep("export.ply") == -1);
    const fs::path dir = freshDir("sf_checkpoints");
    for (const char* n : {"export_500.ply", "export_2000.ply", "export_1500.ply", "export_9000.ply.part"})
        std::ofstream(dir / n) << "x";
    int step = 0;
    CHECK(fs::path(latestCheckpoint(dir.string(), step)).filename() == "export_2000.ply" && step == 2000);
    CHECK(latestCheckpoint(freshDir("sf_checkpoints_none").string(), step).empty() && step == -1);
    CHECK(std::string(psnrQuality(24.0)) == "good" && std::string(psnrQuality(-1)).empty());
}

TEST(splat_capture_brush_arguments) {
    const TrainPreset& fast = trainPreset("fast");
    CHECK(std::string(trainPreset("nonsense").id) == "fast");
    CHECK(trainPreset("quality").steps > fast.steps);
    const auto a = brushArguments("/data/set", "/out/ckpt", fast, true);
    CHECK(a.front() == "/data/set");
    auto value = [&](const char* flag) {
        for (size_t i = 0; i + 1 < a.size(); ++i)
            if (a[i] == flag) return a[i + 1];
        return std::string();
    };
    CHECK(value("--total-steps") == std::to_string(fast.steps));
    CHECK(value("--export-every") == std::to_string(fast.exportEvery));
    CHECK(value("--export-path") == "/out/ckpt");
    CHECK(value("--export-name") == "export_{iter}.ply");
    CHECK(value("--eval-split-every") == "8");
    CHECK(value("--eval-every") == std::to_string(fast.exportEvery * 2));
    CHECK(brushArguments("d", "o", fast, false).size() + 4 == a.size());
}

#ifndef SF_IOS // (the iPad trains no splats: it starts no tools at all)
TEST(splat_train_job_reports_missing_tools) {
    const fs::path photos = photoFolder("sf_train_missing", 4);
    ai::SplatTrainJob job;
    ai::TrainRequest r = fakeRequest(photos, freshDir("sf_train_missing_out"));
    std::string err;
    r.brush.clear();
    CHECK(!job.start(r, err));
    CHECK(job.status().missingTrainer && job.status().stage == ai::TrainStage::Failed && !err.empty());
    r.brush = fakeTools();
    r.colmap.clear();
    CHECK(!job.start(r, err)); // photos without camera positions need COLMAP
    CHECK(job.status().missingPoses && !job.status().missingTrainer);
    const fs::path video = freshDir("sf_train_missing_video") / "walk.mp4";
    std::ofstream(video) << "x";
    r = fakeRequest(video, freshDir("sf_train_missing_out2"));
    r.ffmpeg.clear();
    CHECK(!job.start(r, err));
    CHECK(job.status().missingFfmpeg);
    r = fakeRequest(fs::temp_directory_path() / "sf_no_such_capture", freshDir("sf_train_missing_out3"));
    CHECK(!job.start(r, err) && err.find("not found") != std::string::npos);
    CHECK(!job.busy());
}
#endif

TEST(splat_train_job_photos_to_splat_with_live_previews) {
    if (!*fakeTools()) return;
    setEnv("FAKE_BRUSH_MS_PER_EXPORT", "15");
    setEnv("FAKE_BRUSH_FAIL_AT", nullptr);
    setEnv("FAKE_COLMAP_FAIL", nullptr);
    const fs::path photos = photoFolder("sf_train_photos", 10), out = freshDir("sf_train_photos_out");
    ai::SplatTrainJob job;
    job.pollSeconds = 0.01;
    std::string err;
    CHECK(job.start(fakeRequest(photos, out), err));
    CHECK(job.busy());
    CHECK(!job.start(fakeRequest(photos, out), err)); // one at a time
    // Previews arrive while it trains; each is taken once and placed by the cameras' framing.
    int previews = 0;
    std::shared_ptr<SplatCloud> last;
    while (job.busy() || previews == 0) {
        if (auto p = job.takePreview()) {
            ++previews;
            last = p;
        }
        if (!job.busy() && !last) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    job.wait();
    if (auto p = job.takePreview()) last = p, ++previews;
    const ai::TrainStatus st = job.status();
    CHECK(st.stage == ai::TrainStage::Done);
    CHECK(previews >= 2);
    CHECK(st.frames == 10);
    CHECK(st.step == st.totalSteps);
    CHECK(st.psnr > 25.0); // the fake's last evaluation
    CHECK(fs::exists(st.result) && fs::path(st.result).filename() == "sf_train_photos.ply");
    CHECK(fs::exists(out / "work" / "dataset" / "sparse" / "0" / "images.txt"));
    int checkpoints = 0;
    for (const auto& e : fs::directory_iterator(out / "work" / "checkpoints")) checkpoints += e.path().extension() == ".ply";
    CHECK(checkpoints == 1); // older checkpoints are deleted once a newer one is loaded
    CHECK(fs::exists(out / "work" / "dataset" / "images" / "IMG_0003.png")); // photos keep their names
    CHECK(last && last->size() > 100);
    if (last) {
        // The fake trains a sphere around (1,2,3) with cameras 4-4.5 units away: framed, it sits at the origin.
        const AABB b = last->bounds(0.98f);
        CHECK(glm::length(b.center()) < 0.3f);
        CHECK(b.extent().y < 1.5f);
    }
    CHECK(job.takePreview() == nullptr); // nothing new
}

TEST(splat_train_job_video_drops_blurred_frames) {
    if (!*fakeTools()) return;
    setEnv("FAKE_FFMPEG_FRAMES", "200");
    setEnv("FAKE_BRUSH_MS_PER_EXPORT", "1");
    const fs::path video = freshDir("sf_train_video") / "turntable.mov", out = freshDir("sf_train_video_out");
    std::ofstream(video) << "not really a video";
    ai::SplatTrainJob job;
    job.pollSeconds = 0.01;
    std::string err;
    CHECK(job.start(fakeRequest(video, out), err));
    job.wait();
    setEnv("FAKE_FFMPEG_FRAMES", nullptr);
    const ai::TrainStatus st = job.status();
    CHECK(st.stage == ai::TrainStage::Done);
    const std::vector<std::string> kept = listImages((out / "work" / "dataset" / "images").string());
    CHECK(int(kept.size()) == trainPreset("fast").maxFrames);
    CHECK(st.frames == int(kept.size()));
    int blurred = 0;
    for (const std::string& f : kept) {
        Image img;
        std::string e;
        if (!Image::load(f, img, e) || sharpness(img) < 1.0) ++blurred;
    }
    CHECK(blurred == 0);
    CHECK(!fs::exists(out / "work" / "frames")); // the extracted frames are cleaned up
    CHECK(fs::exists(out / "turntable.ply"));
}

TEST(splat_train_job_posed_capture_skips_colmap) {
    if (!*fakeTools()) return;
    setEnv("FAKE_BRUSH_MS_PER_EXPORT", "1");
    setEnv("FAKE_COLMAP_FAIL", "1"); // would fail if it were called
    const fs::path photos = photoFolder("sf_train_posed", 6);
    {
        std::ofstream j(photos / "capture.json");
        j << R"({"kind":"splat","device":"test","fl_x":60,"w":64,"h":48,"frames":[)";
        for (int i = 0; i < 6; ++i) {
            const float a = 6.2831853f * float(i) / 6.0f;
            const CameraView v = lookAt(glm::vec3(3 * std::cos(a), 0.5f, 3 * std::sin(a)), glm::vec3(0), glm::vec3(0, 1, 0));
            char name[32];
            std::snprintf(name, sizeof name, "IMG_%04d.png", i);
            j << (i ? "," : "") << R"({"file":")" << name << R"(","transform_matrix":[)";
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) j << (r || c ? "," : "") << v.camToWorld[c][r];
            j << "]}";
        }
        j << "]}";
    }
    ai::SplatTrainJob job;
    job.pollSeconds = 0.01;
    ai::TrainRequest r = fakeRequest(photos, freshDir("sf_train_posed_out"));
    r.colmap.clear(); // not installed, and not needed
    std::string err;
    CHECK(job.start(r, err));
    job.wait();
    setEnv("FAKE_COLMAP_FAIL", nullptr);
    CHECK(job.status().stage == ai::TrainStage::Done);
    std::vector<CameraView> views;
    CHECK(readTransformsJson((fs::path(r.outDir) / "work" / "dataset" / "transforms.json").string(), views, err));
    CHECK(views.size() == 6 && views[0].file == "images/IMG_0000.png" && views[0].fx == 60.0f);
}

TEST(splat_train_job_cancel_and_good_enough) {
    if (!*fakeTools()) return;
    setEnv("FAKE_BRUSH_MS_PER_EXPORT", "150");
    const fs::path photos = photoFolder("sf_train_cancel", 5);
    {
        ai::SplatTrainJob job;
        job.pollSeconds = 0.01;
        std::string err;
        const fs::path out = freshDir("sf_train_cancel_out");
        CHECK(job.start(fakeRequest(photos, out), err));
        CHECK(waitFor(job, [](const ai::TrainStatus& s) { return s.stage == ai::TrainStage::Training; }));
        job.cancel();
        job.wait();
        CHECK(job.status().stage == ai::TrainStage::Cancelled);
        CHECK(job.status().result.empty());
        CHECK(!fs::exists(out / "sf_train_cancel.ply"));
    }
    {
        ai::SplatTrainJob job;
        job.pollSeconds = 0.01;
        std::string err;
        const fs::path out = freshDir("sf_train_finish_out");
        CHECK(job.start(fakeRequest(photos, out), err));
        CHECK(waitFor(job, [](const ai::TrainStatus& s) { return s.previewVersion >= 1; }));
        job.finishNow(); // Good enough - stop: the newest checkpoint is the result
        job.wait();
        const ai::TrainStatus st = job.status();
        CHECK(st.stage == ai::TrainStage::Done);
        CHECK(st.step < st.totalSteps);
        CHECK(fs::exists(out / "sf_train_cancel.ply"));
    }
    {   // the destructor stops a running job
        auto job = std::make_unique<ai::SplatTrainJob>();
        std::string err;
        CHECK(job->start(fakeRequest(photos, freshDir("sf_train_drop_out")), err));
        job.reset();
    }
    setEnv("FAKE_BRUSH_MS_PER_EXPORT", nullptr);
}

TEST(splat_train_job_failures) {
    if (!*fakeTools()) return;
    setEnv("FAKE_BRUSH_MS_PER_EXPORT", "1");
    const fs::path photos = photoFolder("sf_train_fail", 5);
    std::string err;
    {
        setEnv("FAKE_BRUSH_FAIL_AT", "1000");
        ai::SplatTrainJob job;
        job.pollSeconds = 0.01;
        CHECK(job.start(fakeRequest(photos, freshDir("sf_train_fail_out")), err));
        job.wait();
        setEnv("FAKE_BRUSH_FAIL_AT", nullptr);
        const ai::TrainStatus st = job.status();
        CHECK(st.stage == ai::TrainStage::Failed);
        CHECK(st.error.find("Brush stopped") != std::string::npos);
        CHECK(st.error.find("panicked") != std::string::npos); // the log's last lines say why
    }
    {
        setEnv("FAKE_COLMAP_FAIL", "1");
        ai::SplatTrainJob job;
        job.pollSeconds = 0.01;
        CHECK(job.start(fakeRequest(photos, freshDir("sf_train_fail_out2")), err));
        job.wait();
        setEnv("FAKE_COLMAP_FAIL", nullptr);
        CHECK(job.status().stage == ai::TrainStage::Failed);
        CHECK(job.status().error.find("COLMAP could not place") != std::string::npos);
    }
    {   // too few photos
        ai::SplatTrainJob job;
        CHECK(job.start(fakeRequest(photoFolder("sf_train_two", 2), freshDir("sf_train_two_out")), err));
        job.wait();
        CHECK(job.status().stage == ai::TrainStage::Failed);
        CHECK(job.status().error.find("Only 2 usable photos") != std::string::npos);
    }
}

TEST(splat_train_settings_round_trip) {
    ai::SplatTrainSettings s;
    s.brushPath = "/opt/brush/brush_app";
    s.preset = "quality";
    s.copyToLibrary = false;
    const std::string path = tempPath("sf_splat_train.json");
    CHECK(s.save(path));
    const ai::SplatTrainSettings b = ai::SplatTrainSettings::load(path);
    CHECK(b.brushPath == s.brushPath && b.preset == "quality" && !b.copyToLibrary && b.colmapPath.empty());
    CHECK(ai::SplatTrainSettings::load(tempPath("sf_no_such_settings.json")).preset == "fast");
    // The environment wins over the settings (studios and tests point at their own tools).
    setEnv("SCULPTFORGE_BRUSH", fakeTools());
    if (*fakeTools()) CHECK(ai::findBrush(s) == fakeTools());
    setEnv("SCULPTFORGE_BRUSH", nullptr);
    if (const ai::ToolDownload* d = ai::brushDownload()) {
        CHECK(std::string(d->sha256).size() == 64);
        CHECK(std::string(d->url).rfind("https://github.com/ArthurBrussee/brush/releases/download/", 0) == 0);
    }
}

TEST(splat_capture_test_capture_poses_look_at_the_object) {
    // tests/data/splat_capture (tools/splat/make_test_capture.py): the phone-link capture.json layout with AR poses.
    CaptureInfo info;
    std::string err;
    CHECK(readCaptureJson(std::string(SF_TEST_DATA_DIR) + "/splat_capture/capture.json", info, err));
    CHECK(info.kind == "splat" && info.files.size() == 20 && info.poses.size() == 20);
    for (const CameraView& v : info.poses) {
        // The maquette's head (0, 0.55, 0) projects near the middle of every photo.
        const glm::vec4 p = glm::inverse(v.camToWorld) * glm::vec4(0, 0.55f, 0, 1);
        CHECK(p.z < 0); // in front of an OpenGL camera
        const float u = v.cx + v.fx * p.x / -p.z, w = v.cy - v.fy * p.y / -p.z;
        CHECK(std::abs(u - 64.0f) < 20.0f && std::abs(w - 48.0f) < 30.0f);
    }
    const glm::mat4 m = orbitFraming(info.poses);
    CHECK(glm::length(glm::vec3(m * glm::vec4(0, 0.4f, 0, 1))) < 0.3f);
    CHECK(glm::normalize(glm::vec3(m * glm::vec4(0, 1, 0, 0))).y > 0.99f);
}
