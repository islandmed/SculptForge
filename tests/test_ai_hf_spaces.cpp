// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generate 3D on the free Hugging Face Spaces: what each preset sends and how it reads the reply, against the Spaces'
// recorded /config and replies (tests/data/hf_spaces, 11-11), in both the old and the current input layouts. No network.
// Hunyuan3D's shape_generation grew from 2 inputs to 13 and TRELLIS.2's image_to_3d takes 15: presets name the slots they
// fill (image, prompt) and everything else takes the Space's own defaults, so a reordered or longer signature still works.
#include "TestHarness.h"

#include "ai/GradioClient.h"
#include "ai/ImageTo3DService.h"

#include <fstream>
#include <sstream>

using namespace sf;
using namespace sf::ai;

namespace {

Json parsed(const std::string& text) {
    Json j;
    std::string err;
    CHECK(Json::parse(text, j, err));
    return j;
}

Json fixture(const std::string& name) {
    std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/hf_spaces/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return parsed(ss.str());
}

Json uploaded() {
    return parsed(R"({"path":"/tmp/gradio/up/input.png","meta":{"_type":"gradio.FileData"},"orig_name":"input.png"})");
}

const HfPreset* preset(const std::string& id, bool text = false) {
    for (const auto& p : text ? hfTextPresets() : hfPresets())
        if (id == p.id) return &p;
    return nullptr;
}

// The `data` the app would join the queue with for step `n` of a preset (the app's argument rules: runHuggingFace).
bool inputsFor(const Json& config, const HfPreset& p, size_t n, const std::string& prompt, const Json& previous, Json& data,
               std::string& error) {
    const HfStep& step = p.steps[n];
    auto resolve = [&](const std::string& a) {
        if (a == "$IMG") return uploaded();
        if (a == "$PROMPT") return Json(prompt);
        if (a == "$OUT1") return previous;
        return parsed(a);
    };
    std::vector<Json> args;
    for (const auto& a : step.args) args.push_back(resolve(a));
    std::vector<GradioSlot> slots;
    for (const auto& s : step.slots) slots.push_back({s.label, s.type, resolve(s.arg), s.optional});
    int fn = -1;
    return gradioBuildInputs(config, step.api, args, slots, data, fn, error);
}

// The same Space with the inputs of every endpoint in reverse order.
Json reversed(const Json& config) {
    Json out = Json::object();
    out.set("components", config["components"]);
    Json deps = Json::array();
    for (const auto& d : config["dependencies"].items()) {
        Json r = Json::object();
        r.set("id", d["id"]);
        r.set("api_name", d["api_name"]);
        r.set("outputs", d["outputs"]);
        Json ins = Json::array();
        for (size_t i = d["inputs"].size(); i-- > 0;) ins.push(d["inputs"][i]);
        r.set("inputs", ins);
        deps.push(r);
    }
    out.set("dependencies", deps);
    return out;
}

// Inputs are labelled and typed in the config; this renames every label (a Space that reworded its UI).
Json relabelled(const Json& config) {
    Json out = Json::object();
    Json comps = Json::array();
    for (const auto& c : config["components"].items()) {
        Json r = Json::object();
        r.set("id", c["id"]);
        r.set("type", c["type"]);
        Json props = Json::object();
        props.set("label", "Renamed");
        if (c["props"].has("value")) props.set("value", c["props"]["value"]);
        r.set("props", props);
        comps.push(r);
    }
    out.set("components", comps);
    out.set("dependencies", config["dependencies"]);
    return out;
}

} // namespace

TEST(hf_spaces_hunyuan_current_signature) {
    // 13 inputs: Text Prompt (or the 2.1 state), five images, steps, guidance, seed, octree, remove background, chunks, randomize.
    struct Space { const char* preset; const char* config; int steps; bool textbox; };
    for (const Space& sp : {Space{"hunyuan3d-2-turbo", "tencent-hunyuan3d-2mini-turbo", 5, true},
                            Space{"hunyuan3d-2", "tencent-hunyuan3d-2", 30, true},
                            Space{"hunyuan3d-2.1", "tencent-hunyuan3d-2-1", 30, false}}) {
        const Json config = fixture(std::string("config-") + sp.config + ".json");
        const HfPreset* p = preset(sp.preset);
        CHECK(p != nullptr && p->steps.size() == 1);
        Json data;
        std::string err;
        CHECK(inputsFor(config, *p, 0, {}, {}, data, err));
        CHECK(data.size() == 13);
        if (data.size() != 13) continue;
        if (sp.textbox) CHECK(data[0].isString() && data[0].asString().empty()); // empty prompt: image to 3D
        else CHECK(data[0].isNull());                                            // 2.1: state, held by the session
        CHECK(data[1]["path"].asString() == "/tmp/gradio/up/input.png");         // the image
        for (int i = 2; i <= 5; ++i) CHECK(data[size_t(i)].isNull());            // front / back / left / right views: unused
        CHECK(int(data[6].asNumber()) == sp.steps);                              // the Space's own defaults from here
        CHECK(data[7].asNumber() == 5.0 && int(data[8].asNumber()) == 1234 && int(data[9].asNumber()) == 256);
        CHECK(data[10].asBool(false) && int(data[11].asNumber()) == 8000 && data[12].asBool(false));
    }
}

TEST(hf_spaces_hunyuan_text_to_3d) {
    for (const char* id : {"hunyuan3d-2-turbo", "hunyuan3d-2"}) {
        const Json config = fixture(std::string("config-") + (std::string(id) == "hunyuan3d-2" ? "tencent-hunyuan3d-2" : "tencent-hunyuan3d-2mini-turbo") + ".json");
        Json data;
        std::string err;
        CHECK(inputsFor(config, *preset(id, true), 0, "a dragon", {}, data, err));
        CHECK(data.size() == 13 && data[0].asString() == "a dragon" && data[1].isNull());
    }
}

TEST(hf_spaces_trellis_current_signature) {
    const Json config = fixture("config-microsoft-trellis-2.json");
    const HfPreset* p = preset("trellis-2");
    CHECK(p != nullptr && p->steps.size() == 4);
    Json data;
    std::string err;
    CHECK(inputsFor(config, *p, 0, {}, {}, data, err) && data.size() == 0); // start_session
    CHECK(inputsFor(config, *p, 1, {}, {}, data, err));                     // preprocess_image: image -> image
    CHECK(data.size() == 1 && data[0]["path"].asString() == "/tmp/gradio/up/input.png");
    const Json preprocessed = parsed(R"({"path":"/tmp/gradio/pre/image.png","orig_name":"image.png"})");
    CHECK(inputsFor(config, *p, 2, {}, preprocessed, data, err)); // image_to_3d: image + 14 parameters
    CHECK(data.size() == 15 && data[0]["path"].asString() == "/tmp/gradio/pre/image.png");
    CHECK(int(data[1].asNumber()) == 0 && data[2].asString() == "1024");       // seed, resolution
    CHECK(data[3].asNumber() == 7.5 && data[4].asNumber() == 0.7 && int(data[5].asNumber()) == 12 && data[6].asNumber() == 5.0);
    CHECK(data[8].asNumber() == 0.5 && data[9].asNumber() == 12 && data[10].asNumber() == 3.0 && data[11].asNumber() == 1.0 && data[14].asNumber() == 3.0);
    CHECK(inputsFor(config, *p, 3, {}, {}, data, err)); // extract_glb: state + decimation + texture size
    CHECK(data.size() == 3 && data[0].isNull() && int(data[1].asNumber()) == 300000 && int(data[2].asNumber()) == 2048);
}

TEST(hf_spaces_old_signature_still_works) {
    // Hunyuan3D before the extra inputs (11-09): a prompt, one image, then three parameters.
    const Json legacy = parsed(R"({"components":[
        {"id":1,"type":"textbox","props":{"label":"Text Prompt"}},{"id":2,"type":"image","props":{"label":"Image"}},
        {"id":3,"type":"slider","props":{"label":"Inference Steps","value":30}},{"id":4,"type":"slider","props":{"label":"Seed","value":1234}},
        {"id":5,"type":"checkbox","props":{"label":"Remove Background","value":true}},{"id":6,"type":"file"}],
        "dependencies":[{"id":7,"api_name":"shape_generation","inputs":[1,2,3,4,5],"outputs":[6]}]})");
    Json data;
    std::string err;
    CHECK(inputsFor(legacy, *preset("hunyuan3d-2"), 0, {}, {}, data, err));
    CHECK(data.size() == 5 && data[0].asString().empty() && data[1]["path"].isString() && int(data[2].asNumber()) == 30);
    CHECK(inputsFor(legacy, *preset("hunyuan3d-2.1"), 0, {}, {}, data, err) && data.size() == 5 && data[1]["path"].isString());
    CHECK(inputsFor(legacy, *preset("hunyuan3d-2", true), 0, "a cat", {}, data, err) && data[0].asString() == "a cat");
}

TEST(hf_spaces_reordered_signature_finds_prompt_and_image) {
    for (const char* name : {"config-tencent-hunyuan3d-2mini-turbo.json", "config-tencent-hunyuan3d-2.json"}) {
        const Json config = reversed(fixture(name));
        Json data;
        std::string err;
        CHECK(inputsFor(config, *preset("hunyuan3d-2"), 0, {}, {}, data, err) && data.size() == 13);
        CHECK(data[11]["path"].asString() == "/tmp/gradio/up/input.png"); // "Image" is now input 11 of 13 (was 1)
        CHECK(data[12].isString() && data[12].asString().empty());       // and the prompt, last
        int images = 0;
        for (const auto& v : data.items()) images += v.isObject() && v["path"].isString();
        CHECK(images == 1);                                              // not the Front/Back/Left/Right views
        CHECK(inputsFor(config, *preset("hunyuan3d-2", true), 0, "a dragon", {}, data, err) && data[12].asString() == "a dragon");
    }
    // Reworded labels: the component type still finds the first image and the first textbox.
    const Json renamed = relabelled(fixture("config-tencent-hunyuan3d-2.json"));
    Json data;
    std::string err;
    CHECK(inputsFor(renamed, *preset("hunyuan3d-2"), 0, {}, {}, data, err) && data[1]["path"].asString() == "/tmp/gradio/up/input.png");
    CHECK(inputsFor(renamed, *preset("hunyuan3d-2", true), 0, "a dragon", {}, data, err) && data[0].asString() == "a dragon");
    // TRELLIS.2 reordered: the image and the state still go where they belong.
    const Json trellis = reversed(fixture("config-microsoft-trellis-2.json"));
    const Json pre = parsed(R"({"path":"/tmp/gradio/pre/image.png"})");
    CHECK(inputsFor(trellis, *preset("trellis-2"), 2, {}, pre, data, err) && data.size() == 15 && data[14]["path"].asString() == "/tmp/gradio/pre/image.png");
    CHECK(inputsFor(trellis, *preset("trellis-2"), 3, {}, {}, data, err) && data.size() == 3 && data[2].isNull());
}

TEST(hf_spaces_missing_slot_says_so) {
    const Json noImage = parsed(R"({"components":[{"id":1,"type":"textbox","props":{"label":"Text Prompt"}},{"id":2,"type":"file"}],
        "dependencies":[{"id":3,"api_name":"shape_generation","inputs":[1],"outputs":[2]}]})");
    Json data;
    std::string err;
    CHECK(!inputsFor(noImage, *preset("hunyuan3d-2"), 0, {}, {}, data, err));
    CHECK(err.find("no 'Image' input") != std::string::npos);
    int fn = 0;
    CHECK(!gradioBuildInputs(fixture("config-tencent-hunyuan3d-2.json"), "gone", {}, {}, data, fn, err) && err.find("'gone' endpoint") != std::string::npos);
}

TEST(hf_spaces_recorded_replies_give_a_model) {
    for (const char* sp : {"tencent-hunyuan3d-2mini-turbo", "tencent-hunyuan3d-2", "tencent-hunyuan3d-2-1"}) {
        const Json config = fixture(std::string("config-") + sp + ".json");
        std::vector<Json> outs;
        std::string failure;
        CHECK(gradioCompleted(fixture(std::string("reply-") + sp + ".json"), outs, failure));
        CHECK(outs.size() == 4); // file, html, json, slider
        const std::string url = gradioModelUrl(config, "shape_generation", outs, std::string("https://") + sp + ".hf.space");
        CHECK(url.rfind(std::string("https://") + sp + ".hf.space/file=/tmp/gradio/", 0) == 0 && url.ends_with("white_mesh.glb"));
    }
}

TEST(hf_spaces_quota_error_in_plain_words) {
    // Recorded 11-11 from microsoft-trellis-2 image_to_3d with no token.
    const Json message = parsed(R"({"msg":"process_completed","success":false,"output":{"error":"You have exceeded your ZeroGPU quota (120s requested vs. 0s left). Try again in 0:00:00. Authenticate with a Hugging Face token for more quota - https://huggingface.co/settings/tokens","duration":10,"visible":true,"title":"ZeroGPU quota exceeded"}})");
    std::vector<Json> outs;
    std::string error;
    CHECK(!gradioCompleted(message, outs, error));
    CHECK(error.find("free GPU time is used up") != std::string::npos && error.find("free Hugging Face token") != std::string::npos);
}
