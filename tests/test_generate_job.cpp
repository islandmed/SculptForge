// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// image_to_3d / text_to_3d in two parts (ai/AiCommands.h GenerateJob): the service call (`work`, on a worker, no Document)
// and opening the result (`finish`, main thread), so the app's window never waits on a hosted model. Run in a row they
// are the command. A stub stands in for the service; no network. The Spaces' failures that move on to another Space
// (down, asleep, not answering) are told from the ones that don't (quota, the artist's Cancel).
#include "TestHarness.h"

#include "ai/AiCommands.h"
#include "ai/AiPolicy.h"
#include "ai/GradioClient.h"
#include "core/Primitives.h"
#include "ai/Consent.h"
#include "io/ObjIO.h"

#include <thread>

using namespace sf;
using namespace sf::ai;

namespace {

struct StubGuard {
    StubGuard() { setAiEnabled(true); }
    ~StubGuard() { setGeneratorStubForTests({}); }
};

AiSettings settings() { return AiSettings::defaults(AiSettings::Provider::Free); }

} // namespace

TEST(generate_job_work_runs_apart_from_finish) {
    StubGuard guard;
    std::thread::id workedOn;
    setGeneratorStubForTests([&](int, const std::string& stem) {
        workedOn = std::this_thread::get_id();
        GeneratorResult r;
        r.outputPath = stem + ".obj";
        r.ok = saveObj(r.outputPath, makeIcosphere(2, 1.0f), r.error);
        return r;
    });
    AiSettings s = settings();
    allowSend(s, generateTarget(s, false).id);
    GenerateJob job;
    CommandResult refused;
    CHECK(prepareGenerateJob("image_to_3d", {{"path", "any.png"}, {"remesh", std::string("false")}}, s, job, refused));

    GeneratorResult generated;
    std::thread::id workerId;
    std::thread worker([&] { // (what the app does)
        workerId = std::this_thread::get_id();
        generated = job.work(nullptr);
    });
    worker.join();
    CHECK(generated.ok && workedOn == workerId && workedOn != std::this_thread::get_id());

    Document doc;
    const CommandResult done = job.finish(doc, generated);
    CHECK(done.ok && done.message.find("ready to sculpt") != std::string::npos);
    CHECK(doc.mesh.triangleCount() > 0);
}

TEST(generate_job_failure_is_a_message_not_a_scene_change) {
    StubGuard guard;
    setGeneratorStubForTests([](int, const std::string&) {
        GeneratorResult r;
        r.error = "Hunyuan3D 2.1 (Tencent) - free, no token: the Space isn't answering";
        return r;
    });
    AiSettings s = settings();
    allowSend(s, generateTarget(s, false).id);
    GenerateJob job;
    CommandResult refused;
    CHECK(prepareGenerateJob("image_to_3d", {{"path", "any.png"}}, s, job, refused));
    GeneratorResult generated = job.work(nullptr);
    Document doc;
    doc.mesh = makeIcosphere(1, 1.0f);
    const size_t before = doc.mesh.triangleCount();
    const CommandResult done = job.finish(doc, generated);
    CHECK(!done.ok && done.message.find("isn't answering") != std::string::npos);
    CHECK(doc.mesh.triangleCount() == before);
}

TEST(generate_job_refuses_what_cannot_run_before_anything_starts) {
    GenerateJob job;
    CommandResult refused;
    CHECK(!prepareGenerateJob("text_to_3d", {{"prompt", "a fox"}, {"service", std::string("inflate")}}, settings(), job, refused));
    CHECK(!refused.ok && refused.message.find("works from an image") != std::string::npos);
    CHECK(!prepareGenerateJob("image_to_3d", {{"path", "a.png"}, {"remesh", std::string("sideways")}}, settings(), job, refused));
    CHECK(refused.message.find("remesh must be") != std::string::npos);
    CHECK(!prepareGenerateJob("text_to_3d", {{"prompt", " "}, {"service", std::string("comfyui")}}, settings(), job, refused));
    CHECK(refused.message == "Describe the model you want");
}

TEST(generate_job_registry_command_is_the_two_parts_in_a_row) {
    StubGuard guard;
    setGeneratorStubForTests([](int, const std::string& stem) {
        GeneratorResult r;
        r.outputPath = stem + ".obj";
        r.ok = saveObj(r.outputPath, makeIcosphere(2, 1.0f), r.error);
        return r;
    });
    AiSettings s = settings();
    allowSend(s, generateTarget(s, false).id);
    CommandRegistry registry;
    registerAiCommands(registry, [s] { return s; });
    Document doc;
    const CommandResult r = registry.execute(doc, "image_to_3d", {{"path", "any.png"}, {"remesh", std::string("false")}});
    CHECK(r.ok && r.message.find("ready to sculpt") != std::string::npos && doc.mesh.triangleCount() > 0);
}

TEST(gradio_space_unavailable_moves_on_but_a_quota_or_cancel_does_not) {
    CHECK(gradioSpaceUnavailable("Cannot reach https://x.hf.space: Timeout was reached"));
    CHECK(gradioSpaceUnavailable("tencent-hunyuan3d-2-1.hf.space isn't answering (still queued after 4 minutes)"));
    CHECK(gradioSpaceUnavailable(friendlyGradioError("HTTP 503 from https://x.hf.space (Space is unavailable)")));
    CHECK(gradioSpaceUnavailable("This Space has no 'shape_generation' endpoint (it may have changed)"));
    CHECK(gradioSpaceUnavailable("Connection lost: Operation timed out"));
    CHECK(!gradioSpaceUnavailable(friendlyGradioError("You have exceeded your ZeroGPU quota")));
    CHECK(!gradioSpaceUnavailable("Cancelled"));
    CHECK(!gradioSpaceUnavailable("The stub service said no"));
}
