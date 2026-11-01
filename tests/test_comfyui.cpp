// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ComfyUI client: the sf_* workflow contract, format detection, /history and
// /prompt reply parsing. No server needed.
#include "TestHarness.h"

#include "ai/BrushAssistant.h"
#include "ai/ComfyUI.h"
#include "ai/Http.h"

#include <fstream>

using namespace sf;
using namespace sf::ai;

namespace {

Json parsed(const std::string& text) {
    Json j;
    std::string err;
    CHECK(Json::parse(text, j, err));
    return j;
}

// Hunyuan3D-style image-to-3D graph, as "Export (API)" writes it.
const char* kImageWorkflow = R"j({
  "3": {"class_type": "KSampler", "inputs": {"seed": 1125899906842624, "steps": 30, "model": ["4", 0]}, "_meta": {"title": "KSampler"}},
  "7": {"class_type": "LoadImage", "inputs": {"image": "example.png"}, "_meta": {"title": "Load Image"}},
  "8": {"class_type": "LoadImage", "inputs": {"image": "mask.png"}, "_meta": {"title": " SF_Image "}},
  "9": {"class_type": "SaveGLB", "inputs": {"filename_prefix": "mesh/ComfyUI", "mesh": ["3", 0]}, "_meta": {"title": "Save GLB"}}
})j";

} // namespace

TEST(comfy_url_normalised) {
    CHECK(comfy::normalizeUrl("") == "http://127.0.0.1:8188");
    CHECK(comfy::normalizeUrl(" localhost:8188/ ") == "http://localhost:8188");
    CHECK(comfy::normalizeUrl("https://gpu.studio.lan/comfy//") == "https://gpu.studio.lan/comfy");
}

TEST(comfy_image_goes_to_titled_nodes_only) {
    Json wf = parsed(kImageWorkflow);
    comfy::ContractInputs in;
    in.image = "sculptforge_abc.png";
    comfy::ContractReport rep;
    std::string err;
    CHECK(comfy::applyContract(wf, in, err, &rep));
    CHECK(rep.imageNodes.size() == 1 && rep.imageNodes[0] == "8"); // title match is case and space tolerant
    CHECK(wf["8"]["inputs"]["image"].asString() == "sculptforge_abc.png");
    CHECK(wf["7"]["inputs"]["image"].asString() == "example.png"); // untitled LoadImage left alone
    CHECK(wf["9"]["inputs"]["mesh"][0].asString() == "3");         // links survive
}

TEST(comfy_image_falls_back_to_first_load_image) {
    Json wf = parsed(R"j({
      "2": {"class_type": "LoadImageMask", "inputs": {"image": "m.png", "channel": "alpha"}},
      "5": {"class_type": "LoadImage", "inputs": {"image": "a.png"}},
      "6": {"class_type": "LoadImage", "inputs": {"image": "b.png"}}
    })j");
    comfy::ContractInputs in;
    in.image = "up/in.png";
    comfy::ContractReport rep;
    std::string err;
    CHECK(comfy::applyContract(wf, in, err, &rep));
    CHECK(rep.imageNodes.size() == 1 && rep.imageNodes[0] == "5");
    CHECK(wf["5"]["inputs"]["image"].asString() == "up/in.png");
    CHECK(wf["6"]["inputs"]["image"].asString() == "b.png");
    CHECK(wf["2"]["inputs"]["image"].asString() == "m.png");

    // Node-pack look-alikes count when there is no stock LoadImage.
    Json custom = parsed(R"j({"1": {"class_type": "LoadImageFromUrlOrPath", "inputs": {"image": "x"}}})j");
    CHECK(comfy::applyContract(custom, in, err, &rep) && rep.imageNodes[0] == "1");

    Json none = parsed(R"j({"1": {"class_type": "CLIPTextEncode", "inputs": {"text": "x"}}})j");
    CHECK(!comfy::applyContract(none, in, err));
    CHECK(err.find("Load Image") != std::string::npos);
}

TEST(comfy_prompt_contract) {
    // sf_prompt: text, then prompt, then string inputs.
    Json wf = parsed(R"j({
      "1": {"class_type": "CLIPTextEncode", "inputs": {"text": "old", "clip": ["4", 1]}, "_meta": {"title": "Positive"}},
      "2": {"class_type": "MyPromptNode", "inputs": {"prompt": "old"}, "_meta": {"title": "sf_prompt"}},
      "3": {"class_type": "StringConcat", "inputs": {"string": "old", "other": "x"}, "_meta": {"title": "sf_prompt"}},
      "4": {"class_type": "PrimitiveStringMultiline", "inputs": {"value": "old"}, "_meta": {"title": "sf_prompt"}}
    })j");
    comfy::ContractInputs in;
    in.prompt = "a stylised dragon head";
    comfy::ContractReport rep;
    std::string err;
    CHECK(comfy::applyContract(wf, in, err, &rep));
    CHECK(rep.promptNodes.size() == 3);
    CHECK(wf["2"]["inputs"]["prompt"].asString() == "a stylised dragon head");
    CHECK(wf["3"]["inputs"]["string"].asString() == "a stylised dragon head");
    CHECK(wf["3"]["inputs"]["other"].asString() == "x");
    CHECK(wf["4"]["inputs"]["value"].asString() == "a stylised dragon head");
    CHECK(wf["1"]["inputs"]["text"].asString() == "old"); // titled nodes win over the fallback

    // No sf_prompt: the first text encoder that isn't the negative prompt.
    Json plain = parsed(R"j({
      "6": {"class_type": "CLIPTextEncode", "inputs": {"text": "blurry"}, "_meta": {"title": "Negative Prompt"}},
      "7": {"class_type": "CLIPTextEncode", "inputs": {"text": "a cat"}, "_meta": {"title": "CLIP Text Encode (Prompt)"}}
    })j");
    CHECK(comfy::applyContract(plain, in, err, &rep));
    CHECK(rep.promptNodes.size() == 1 && rep.promptNodes[0] == "7");
    CHECK(plain["6"]["inputs"]["text"].asString() == "blurry");

    Json bad = parsed(R"j({"1": {"class_type": "Note", "inputs": {"width": 3}, "_meta": {"title": "sf_prompt"}}})j");
    CHECK(!comfy::applyContract(bad, in, err));
    CHECK(err.find("sf_prompt") != std::string::npos && err.find("node 1") != std::string::npos);

    Json nowhere = parsed(R"j({"1": {"class_type": "LoadImage", "inputs": {"image": "a.png"}}})j");
    CHECK(!comfy::applyContract(nowhere, in, err));
    CHECK(err.find("sf_prompt") != std::string::npos);
}

TEST(comfy_seed_contract) {
    Json wf = parsed(R"j({
      "1": {"class_type": "KSampler", "inputs": {"seed": 5, "steps": 20}},
      "2": {"class_type": "SamplerCustom", "inputs": {"noise_seed": 5}},
      "3": {"class_type": "PrimitiveInt", "inputs": {"value": 5}, "_meta": {"title": "sf_seed"}},
      "4": {"class_type": "KSampler", "inputs": {"seed": ["3", 0]}},
      "5": {"class_type": "PrimitiveInt", "inputs": {"value": 5}},
      "6": {"class_type": "SamplerBoth", "inputs": {"seed": 5, "noise_seed": 6, "cfg": 7}}
    })j");
    comfy::ContractInputs in;
    in.seed = 123456789012345ull; // above 1e15 would print as 1.2e+15 and fail ComfyUI's INT check
    comfy::ContractReport rep;
    std::string err;
    CHECK(comfy::applyContract(wf, in, err, &rep));
    CHECK(rep.seedNodes.size() == 4);
    CHECK(wf["1"]["inputs"]["seed"].asNumber() == 123456789012345.0);
    // Both seeds of one node (setting the first replaced the node the second was read from: a use-after-free).
    CHECK(wf["6"]["inputs"]["seed"].asNumber() == 123456789012345.0);
    CHECK(wf["6"]["inputs"]["noise_seed"].asNumber() == 123456789012345.0);
    CHECK(wf["6"]["inputs"]["cfg"].asNumber() == 7.0);
    CHECK(wf["2"]["inputs"]["noise_seed"].asNumber() == 123456789012345.0);
    CHECK(wf["3"]["inputs"]["value"].asNumber() == 123456789012345.0);
    CHECK(wf["4"]["inputs"]["seed"].isArray()); // linked seed stays linked
    CHECK(wf["5"]["inputs"]["value"].asNumber() == 5.0);
    CHECK(wf.dump().find("123456789012345") != std::string::npos);
    CHECK(parsed(kImageWorkflow).dump().find("1125899906842624") != std::string::npos); // big exported seeds stay integers

    // Nothing requested: nothing touched.
    Json same = parsed(kImageWorkflow);
    CHECK(comfy::applyContract(same, {}, err));
    CHECK(same.dump() == parsed(kImageWorkflow).dump());
}

TEST(comfy_detects_ui_format) {
    const char* ui = R"j({"last_node_id": 9, "last_link_id": 9, "nodes": [{"id": 7, "type": "LoadImage", "widgets_values": ["a.png", "image"]}],
                         "links": [], "version": 0.4})j";
    CHECK(comfy::isUiFormat(parsed(ui)));
    CHECK(!comfy::isUiFormat(parsed(kImageWorkflow)));
    Json wf;
    std::string err;
    CHECK(!comfy::parseWorkflow(ui, wf, err));
    CHECK(err.find("Export (API)") != std::string::npos);

    CHECK(comfy::parseWorkflow(kImageWorkflow, wf, err));
    CHECK(wf["9"]["class_type"].asString() == "SaveGLB");
    // A /prompt request body works too.
    CHECK(comfy::parseWorkflow(std::string("{\"prompt\": ") + kImageWorkflow + ", \"client_id\": \"x\"}", wf, err));
    CHECK(wf["7"]["class_type"].asString() == "LoadImage");

    CHECK(!comfy::parseWorkflow("{\"a\": 1}", wf, err) && err.find("class_type") != std::string::npos);
    CHECK(!comfy::parseWorkflow("{}", wf, err));
    CHECK(!comfy::parseWorkflow("not json", wf, err) && err.find("JSON") != std::string::npos);

    const std::string path = tempPath("sf_comfy_ui_format.json");
    std::ofstream(path) << ui;
    CHECK(!comfy::loadWorkflow(path, wf, err));
    CHECK(err.find("sf_comfy_ui_format.json") != std::string::npos && err.find("Export (API)") != std::string::npos);
    CHECK(!comfy::loadWorkflow(tempPath("sf_comfy_missing.json"), wf, err));
}

TEST(comfy_workflow_kind) {
    CHECK(comfy::workflowKind(parsed(kImageWorkflow)) == "image");
    CHECK(comfy::workflowKind(parsed(R"j({"1": {"class_type": "CLIPTextEncode", "inputs": {"text": "x"}}})j")) == "text");
    CHECK(comfy::workflowKind(parsed(R"j({"1": {"class_type": "Foo", "inputs": {}, "_meta": {"title": "sf_prompt"}}})j")) == "text");
    CHECK(comfy::workflowKind(parsed(R"j({"1": {"class_type": "SaveGLB", "inputs": {}}})j")).empty());
}

TEST(comfy_history_output_shapes) {
    // SaveGLB ("3d" objects), Preview3D ("result" strings), images, text.
    const char* history = R"j({"abc": {
      "prompt": [3, "abc", {"9": {"class_type": "SaveImage", "inputs": {}}, "12": {"class_type": "SaveGLB", "inputs": {}, "_meta": {"title": "Save Mesh"}}}, {}, ["9", "12"]],
      "outputs": {
        "9": {"images": [{"filename": "ComfyUI_00001_.png", "subfolder": "", "type": "output"}]},
        "11": {"text": ["hello"]},
        "12": {"3d": [{"filename": "ComfyUI_00003_.glb", "subfolder": "mesh", "type": "output"}], "camera_info": [null], "model_3d_info": []}
      },
      "status": {"status_str": "success", "completed": true, "messages": [["execution_start", {"prompt_id": "abc"}]]}}})j";
    comfy::HistoryEntry h = comfy::parseHistory(parsed(history), "abc");
    CHECK(h.found && h.completed && !h.failed);
    CHECK(h.files.size() == 2);
    const comfy::OutputFile* mesh = comfy::firstMesh(h.files);
    CHECK(mesh && mesh->filename == "ComfyUI_00003_.glb" && mesh->subfolder == "mesh" && mesh->nodeId == "12" && mesh->key == "3d");
    CHECK(comfy::viewUrl("localhost:8188/", *mesh) ==
          "http://localhost:8188/view?filename=ComfyUI_00003_.glb&subfolder=mesh&type=output");
    CHECK(!comfy::parseHistory(parsed(history), "other").found);

    // Preview3D-style strings, an annotated name, an absolute path inside output/, and nested lists.
    comfy::HistoryEntry s = comfy::parseHistory(parsed(R"j({"p": {"outputs": {
        "4": {"result": ["mesh/out 1.OBJ", {"position": [1, 2]}, null]},
        "5": {"meshes": [["sub/a.ply [temp]"]]},
        "6": {"files": ["C:\\ComfyUI\\output\\3d\\b.stl", "/srv/comfy/notes.txt"]}},
      "status": {"status_str": "success", "completed": true, "messages": []}}})j"), "p");
    CHECK(s.files.size() == 3);
    CHECK(s.files[0].filename == "out 1.OBJ" && s.files[0].subfolder == "mesh" && s.files[0].type == "output" && s.files[0].isMesh());
    CHECK(s.files[1].filename == "a.ply" && s.files[1].subfolder == "sub" && s.files[1].type == "temp");
    CHECK(s.files[2].filename == "b.stl" && s.files[2].subfolder == "3d" && s.files[2].type == "output");
    CHECK(comfy::viewUrl("http://h:1", s.files[0]) == "http://h:1/view?filename=out%201.OBJ&subfolder=mesh&type=output");

    // Old servers: no status block, outputs mean done.
    CHECK(comfy::parseHistory(parsed(R"j({"q": {"outputs": {}}})j"), "q").completed);
}

TEST(comfy_history_without_mesh_lists_outputs) {
    const char* history = R"j({"abc": {
      "prompt": [3, "abc", {"9": {"class_type": "SaveImage", "inputs": {}, "_meta": {"title": "Save Image"}}}, {}, ["9"]],
      "outputs": {"9": {"images": [{"filename": "a.png", "subfolder": "", "type": "output"},
                                   {"filename": "b.png"}, {"filename": "c.png"}, {"filename": "d.png"}]}},
      "status": {"status_str": "success", "completed": true, "messages": []}}})j";
    comfy::HistoryEntry h = comfy::parseHistory(parsed(history), "abc");
    CHECK(h.completed && h.files.size() == 4 && !comfy::firstMesh(h.files));
    const std::string d = comfy::describeOutputs(parsed(history), "abc");
    CHECK(d == "node 9 (SaveImage) 'images': a.png, b.png, c.png (+1 more)");
    CHECK(comfy::describeOutputs(parsed(R"j({"x": {"outputs": {}}})j"), "x") == "none");
}

TEST(comfy_history_errors) {
    comfy::HistoryEntry e = comfy::parseHistory(parsed(R"j({"abc": {"outputs": {}, "status": {"status_str": "error", "completed": false,
      "messages": [["execution_start", {}], ["execution_error", {"node_id": "5", "node_type": "Hy3DGenerateMesh",
        "exception_message": "CUDA out of memory.\n", "exception_type": "torch.OutOfMemoryError"}]]}}})j"), "abc");
    CHECK(e.found && e.failed && !e.completed);
    CHECK(e.error.find("Hy3DGenerateMesh (node 5) failed: CUDA out of memory.") == 0);
    CHECK(e.error.find("ran out of memory") != std::string::npos);

    comfy::HistoryEntry i = comfy::parseHistory(parsed(R"j({"abc": {"outputs": {}, "status": {"status_str": "error", "completed": false,
      "messages": [["execution_interrupted", {"node_id": "3"}]]}}})j"), "abc");
    CHECK(i.failed && i.error.find("interrupted") != std::string::npos);
}

TEST(comfy_prompt_errors) {
    Json wf = parsed(kImageWorkflow);
    const std::string missing = comfy::promptError(R"j({"error": {"type": "missing_node_type",
        "message": "Node 'Hy3DGenerateMesh' not found. The custom node may not be installed.", "details": "",
        "extra_info": {"node_id": "9", "class_type": "SaveGLB", "node_title": "Save GLB"}}, "node_errors": {}})j", wf);
    CHECK(missing.find("node 9 (SaveGLB)") != std::string::npos);
    CHECK(missing.find("Install Missing Custom Nodes") != std::string::npos);

    const std::string nodeErrors = comfy::promptError(R"j({"error": {"type": "prompt_outputs_failed_validation",
        "message": "Prompt outputs failed validation", "details": ""}, "node_errors": {"7": {"errors": [{"type": "value_not_in_list",
        "message": "Value not in list", "details": "ckpt_name: 'hunyuan3d-dit-v2.safetensors' not in []", "extra_info": {}}],
        "dependent_outputs": ["9"], "class_type": "LoadImage"}}})j", wf);
    CHECK(nodeErrors.find("node 7 (LoadImage) - Value not in list: ckpt_name") != std::string::npos);
    CHECK(nodeErrors.find("model file") != std::string::npos);
    const std::string badChoice = comfy::promptError(R"j({"error": {"type": "prompt_outputs_failed_validation", "message": "x"},
        "node_errors": {"2": {"errors": [{"type": "value_not_in_list", "message": "Value not in list",
        "details": "upscale_method: 'bogus' not in ['nearest-exact', 'bilinear']"}], "class_type": "ImageScale"}}})j", wf);
    CHECK(badChoice == "ComfyUI rejected the workflow: node 2 - Value not in list: upscale_method: 'bogus' not in ['nearest-exact', 'bilinear']");

    CHECK(comfy::promptError(R"j({"error": {"type": "prompt_no_outputs", "message": "Prompt has no outputs", "details": ""},
        "node_errors": []})j", wf).find("no output node") != std::string::npos);
    CHECK(comfy::promptError("<html>Bad gateway</html>", wf).find("Bad gateway") != std::string::npos);
    CHECK(comfy::promptError(R"j({"error": {"type": "invalid_prompt", "message": "Cannot execute", "details": "Node ID '#5'"}})j", wf) ==
          "ComfyUI rejected the workflow: Cannot execute (Node ID '#5')");

    const auto list = comfy::missingNodeTypes(wf, parsed(R"j({"LoadImage": {}, "SaveGLB": {}})j"));
    CHECK(list.size() == 1 && list[0] == "KSampler");
}

TEST(comfy_queue_position) {
    const Json q = parsed(R"j({"queue_running": [[10, "run", {}, {}, []]],
                              "queue_pending": [[13, "c", {}, {}, []], [11, "a", {}, {}, []], [12, "b", {}, {}, []]]})j");
    CHECK(comfy::queuePosition(q, "run") == 0);
    CHECK(comfy::queuePosition(q, "a") == 1);
    CHECK(comfy::queuePosition(q, "c") == 3);
    CHECK(comfy::queuePosition(q, "zzz") == -1);
}

TEST(comfy_system_stats) {
    comfy::ServerInfo info;
    CHECK(comfy::parseSystemStats(parsed(R"j({"system": {"os": "posix", "comfyui_version": "0.3.40", "python_version": "3.12.3 (main) [GCC]",
        "pytorch_version": "2.7.0+cu128"}, "devices": [{"name": "cuda:0 NVIDIA GeForce RTX 4090 : cudaMallocAsync", "type": "cuda",
        "index": 0, "vram_total": 25757220864}]})j"), info));
    CHECK(info.summary() == "ComfyUI 0.3.40 - NVIDIA GeForce RTX 4090 (24.0 GB)");
    CHECK(info.pythonVersion == "3.12.3");
    CHECK(comfy::parseSystemStats(parsed(R"j({"system": {}, "devices": [{"name": "cpu", "type": "cpu", "vram_total": 3e10}]})j"), info));
    CHECK(info.summary() == "ComfyUI - cpu");
    CHECK(!comfy::parseSystemStats(parsed(R"j({"detail": "Not Found"})j"), info));
}

TEST(comfy_unreachable_server_is_explained) {
    comfy::ServerInfo info;
    std::string err;
    CHECK(!comfy::checkServer("127.0.0.1:9", info, err, 3));
    CHECK(err.find("http://127.0.0.1:9") != std::string::npos);
#if !defined(_WIN32)
    CHECK(err.find("Nothing is answering") != std::string::npos);
#endif
}

TEST(comfy_settings_round_trip) {
    AiSettings s;
    CHECK(s.comfyUrl == "http://127.0.0.1:8188");
    s.generatorService = "comfyui";
    s.comfyUrl = "http://gpu-box:8188";
    s.comfyImageWorkflow = "/w/hunyuan.json";
    s.comfyTextWorkflow = "/w/text.json";
    AiSettings r = AiSettings::fromJson(s.toJson());
    CHECK(r.generatorService == "comfyui" && r.comfyUrl == "http://gpu-box:8188");
    CHECK(r.comfyImageWorkflow == "/w/hunyuan.json" && r.comfyTextWorkflow == "/w/text.json");
}

TEST(multipart_body_layout) {
    std::string header;
    const std::string body = multipartBody({{"image", "PNGDATA", "in.png", "image/png"}, {"overwrite", "true", {}, {}}}, header);
    const std::string boundary = header.substr(header.find("boundary=") + 9);
    CHECK(header.starts_with("Content-Type: multipart/form-data; boundary=----sculptforge"));
    CHECK(body == "--" + boundary + "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"in.png\"\r\nContent-Type: image/png\r\n\r\n"
                  "PNGDATA\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--" + boundary + "--\r\n");
}
