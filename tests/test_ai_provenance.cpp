// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The AI provenance tag (core/AiProvenance.h, areas/ai.md #3): what Generate 3D records (host, model, date, pass),
// that it survives a project round trip, follows copies and splits of the Part, rides in exports whose format has a
// place for it (glTF extras, USD customData, OBJ comments) and stays out of files of Parts no AI made.
#include "TestHarness.h"

#include "ai/AiCommands.h"
#include "ai/BrushAssistant.h"
#include "ai/Consent.h"
#include "ai/ImageTo3DService.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/Parts.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "io/GltfIO.h"
#include "io/ObjIO.h"
#include "io/UsdIO.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sf;

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

AiProvenance tag() {
    AiProvenance p;
    p.host = "tencent-hunyuan3d-2-1.hf.space";
    p.model = "Hunyuan3D 2.1 (Tencent)";
    p.date = "2026-11-23";
    p.role = AiProvenance::kGenerated;
    return p;
}

bool same(const AiProvenance& a, const AiProvenance& b) {
    return a.host == b.host && a.model == b.model && a.date == b.date && a.role == b.role;
}

struct StubGuard {
    ~StubGuard() { ai::setGeneratorStubForTests({}); }
};

} // namespace

TEST(ai_provenance_line_date_and_json) {
    CHECK(AiProvenance().empty() && AiProvenance().line().empty());
    AiProvenance p = tag();
    p.role = AiProvenance::kSculptingBase;
    CHECK(p.line() == "Hunyuan3D 2.1 (Tencent), tencent-hunyuan3d-2-1.hf.space, 2026-11-23 (sculpting base)");
    p.role.clear();
    CHECK(p.line() == "Hunyuan3D 2.1 (Tencent), tencent-hunyuan3d-2-1.hf.space, 2026-11-23");
    const std::string today = aiProvenanceToday(); // YYYY-MM-DD
    CHECK(today.size() == 10 && today[4] == '-' && today[7] == '-' && std::isdigit(static_cast<unsigned char>(today[0])));
    const Json j = aiProvenanceToJson(tag());
    CHECK(j["host"].asString() == "tencent-hunyuan3d-2-1.hf.space" && j["role"].asString() == "generated");
    CHECK(same(aiProvenanceFromJson(j), tag()));
    CHECK(aiProvenanceFromJson(Json()).empty());         // missing key: no tag
    CHECK(aiProvenanceFromJson(Json("text")).empty());   // the wrong type: no tag, no crash
    Json partial = Json::object();
    partial.set("model", "X");
    CHECK(aiProvenanceFromJson(partial).model == "X" && aiProvenanceFromJson(partial).host.empty());
}

TEST(ai_provenance_round_trips_in_projects) {
    Document doc = makeDoc(makeIcosphere(2));
    doc.addPart(makeCube(2, 1.0f), "Generated");
    doc.activePartInfo().ai = tag();
    doc.activePartInfo().ai.role = AiProvenance::kSculptingBase;
    const int ref = doc.addPart(makeCube(3, 1.0f), "Reference", false);
    doc.parts[size_t(ref)].ai = tag();
    doc.parts[size_t(ref)].ai.role = AiProvenance::kReference;
    doc.parts[size_t(ref)].visible = false;
    const int link = doc.linkedDuplicate(1, glm::vec3(2.0f, 0.0f, 0.0f)); // a linked copy (#94) carries the tag too
    CHECK(doc.parts[size_t(link)].ai.role == AiProvenance::kSculptingBase);
    doc.selectPart(1);
    const std::string path = tempPath("sf_ai_provenance.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.partCount() == 4);
    CHECK(back.parts[0].ai.empty()); // the sphere nobody generated
    AiProvenance expectBase = tag(), expectRef = tag();
    expectBase.role = AiProvenance::kSculptingBase;
    expectRef.role = AiProvenance::kReference;
    CHECK(same(back.parts[1].ai, expectBase));
    CHECK(same(back.parts[2].ai, expectBase)); // the link (linkedDuplicate puts it right below its source)
    CHECK(same(back.parts[3].ai, expectRef) && !back.parts[3].visible);
    // The key is optional: a manifest without it loads as before (older files), one with it in the sphere's place is ignored.
    std::filesystem::remove(path);
}

TEST(ai_provenance_follows_copies_and_splits) {
    Document doc = makeDoc(makeIcosphere(3));
    doc.activePartInfo().ai = tag();
    CommandRegistry commands;
    CHECK(commands.execute(doc, "part_duplicate", {}).ok);
    CHECK(doc.partCount() == 2 && same(doc.activePartInfo().ai, tag()));
    doc.selectPart(0);
    doc.mesh.ensureHidden();
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t) doc.mesh.hidden[t] = doc.mesh.positions[doc.mesh.triangles[t].x].x > 0.0f;
    CHECK(commands.execute(doc, "part_split_hidden", {}).ok);
    CHECK(doc.partCount() == 3);
    int tagged = 0;
    for (int i = 0; i < doc.partCount(); ++i) tagged += same(doc.parts[size_t(i)].ai, tag());
    CHECK(tagged == 3); // the piece that left is still AI-made
    // scene_overview says so.
    const CommandResult over = commands.execute(doc, "scene_overview", {});
    CHECK(over.ok && over.message.find("AI: Hunyuan3D 2.1 (Tencent), tencent-hunyuan3d-2-1.hf.space, 2026-11-23 (generated)") != std::string::npos);
}

TEST(ai_provenance_rides_in_exports_where_the_format_allows) {
    Document doc = makeDoc(makeIcosphere(2));
    doc.addPart(makeCube(2, 1.0f), "Generated");
    doc.activePartInfo().ai = tag();
    doc.activePartInfo().ai.role = AiProvenance::kSculptingBase;
    CommandRegistry commands;
    std::string err;
    const std::string glb = tempPath("sf_ai_provenance.glb"), usd = tempPath("sf_ai_provenance.usda"), obj = tempPath("sf_ai_provenance.obj");
    CHECK(commands.execute(doc, "export_glb", {{"path", glb}}).ok);
    const std::string glbText = slurp(glb);
    CHECK(glbText.find("\"extras\":{\"sculptforge_ai\":{\"host\":\"tencent-hunyuan3d-2-1.hf.space\",\"model\":\"Hunyuan3D 2.1 (Tencent)\","
                       "\"date\":\"2026-11-23\",\"role\":\"sculpting base\"}}") != std::string::npos);
    CHECK(glbText.find("\"name\":\"Sculpt\"") != std::string::npos || glbText.find("\"name\":\"Generated\"") != std::string::npos);
    // The glTF still opens; one node per Part.
    std::vector<MeshObject> objects;
    CHECK(loadGltf(glb, objects, err) && objects.size() == 2);

    CHECK(commands.execute(doc, "export_usd", {{"path", usd}}).ok);
    const std::string usdText = slurp(usd);
    CHECK(usdText.find("dictionary sculptforge_ai = {") != std::string::npos);
    CHECK(usdText.find("string model = \"Hunyuan3D 2.1 (Tencent)\"") != std::string::npos);
    CHECK(usdText.find("string role = \"sculpting base\"") != std::string::npos);
    Mesh usdMesh;
    SplatCloud splats;
    CHECK(loadUsd(usd, usdMesh, splats, err) && usdMesh.triangleCount() > 0); // our own reader takes the file back

    CHECK(commands.execute(doc, "save_obj", {{"path", obj}}).ok);
    const std::string objText = slurp(obj);
    CHECK(objText.find("o Generated\n# sculptforge_ai host=tencent-hunyuan3d-2-1.hf.space model=Hunyuan3D 2.1 (Tencent) date=2026-11-23 role=sculpting base\n") != std::string::npos);
    Mesh objMesh;
    CHECK(loadObj(obj, objMesh, err) && objMesh.triangleCount() > 0);
    // Only the tagged Part: the sphere's node, prim and object carry nothing.
    CHECK(glbText.find("sculptforge_ai") == glbText.rfind("sculptforge_ai"));
    CHECK(usdText.find("sculptforge_ai") == usdText.rfind("sculptforge_ai"));
    CHECK(objText.find("sculptforge_ai") == objText.rfind("sculptforge_ai"));
    for (const std::string& p : {glb, usd, obj}) std::filesystem::remove(p);
}

TEST(ai_provenance_names_the_host_and_model_of_the_settings) {
    ai::AiSettings s = ai::AiSettings::defaults(ai::AiSettings::Provider::Free);
    s.hfToken.clear();
    AiProvenance p = ai::generateProvenance(s, false); // the default: the free Hunyuan3D 2.1 Space
    CHECK(p.host == "tencent-hunyuan3d-2-1.hf.space");
    CHECK(p.model == "Hunyuan3D 2.1 (Tencent)");
    CHECK(p.date == aiProvenanceToday() && p.role == AiProvenance::kGenerated);
    s.generatorService = "fal";
    s.falModel = "fal-ai/trellis-2";
    p = ai::generateProvenance(s, false);
    CHECK(p.host == "queue.fal.run" && p.model == "fal-ai/trellis-2");
    CHECK(ai::generateProvenance(s, true).model == ai::falTextModel("fal-ai/trellis-2")); // text runs another model
    s.generatorService = "tripo";
    CHECK(ai::generateProvenance(s, false).host == "openapi.tripo3d.ai" && ai::generateProvenance(s, false).model.rfind("Tripo ", 0) == 0);
    s.generatorService = "comfyui";
    s.comfyUrl = "http://gpu-box:8188";
    s.comfyImageWorkflow = "/home/me/workflows/owl v2.json";
    p = ai::generateProvenance(s, false);
    CHECK(p.host == "gpu-box" && p.model == "ComfyUI workflow owl v2");
    s.generatorService = "command";
    p = ai::generateProvenance(s, false);
    CHECK(p.host == "this computer" && p.model == "your generator command");
    s.generatorService = "inflate";
    CHECK(ai::generateProvenance(s, false).empty()); // offline geometry: not AI, no tag
}

TEST(ai_provenance_set_by_the_image_to_3d_command) {
    StubGuard guard;
    ai::setGeneratorStubForTests([](int, const std::string& stem) {
        ai::GeneratorResult r;
        r.outputPath = stem + ".obj";
        r.ok = saveObj(r.outputPath, makeIcosphere(2, 1.0f), r.error);
        return r;
    });
    ai::AiSettings s = ai::AiSettings::defaults(ai::AiSettings::Provider::Free);
    ai::allowSend(s, ai::generateTarget(s, false).id);
    for (const char* remesh : {"false", "voxel"}) {
        ai::GenerateJob job;
        CommandResult refused;
        CHECK(ai::prepareGenerateJob("image_to_3d", {{"path", "any.png"}, {"remesh", std::string(remesh)}}, s, job, refused));
        ai::GeneratorResult generated = job.work(nullptr);
        Document doc;
        CHECK(job.finish(doc, generated).ok);
        const AiProvenance& p = doc.activePartInfo().ai;
        CHECK(p.host == "tencent-hunyuan3d-2-1.hf.space" && p.model == "Hunyuan3D 2.1 (Tencent)" && p.date == aiProvenanceToday());
        CHECK(p.role == (std::string(remesh) == "false" ? AiProvenance::kGenerated : AiProvenance::kSculptingBase));
    }
}
