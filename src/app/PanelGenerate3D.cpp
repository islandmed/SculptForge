// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generate 3D's flow: generate for form, remesh for topology, never one pass for both. Generate makes 1-3 candidates
// at once (each an ordinary request to the chosen service, on the user's own key or the free Spaces) and shows them
// side by side, a shaded thumbnail over a flat black silhouette, so a beginner picks the strongest form while it is
// cheap. The pick becomes a Part as generated; "Use as sculpting base" then rebuilds it as a clean, even mesh (Voxel
// Remesh, then Quad Remesh unless turned off) and keeps the generated model as a hidden high-detail Part, the source
// for Project Detail. The panel itself is App::drawImageTo3D (App.cpp).
#include "app/App.h"
#include "app/I18n.h"
#include "app/Ui.h"
#include "ai/ImageTo3DService.h"
#include "ai/SpaceHealth.h"
#include "ai/Tripo.h"
#include "app/NativeDialog.h"

#include "core/AuditLog.h"
#include "core/MeshPreview.h"
#include "render/GL.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace sf {

namespace {

constexpr int kPreviewSize = 160;
constexpr float kSameOutline = 0.02f; // silhouettes closer than this are the same shape

// Services billed per generation on the user's key: one candidate unless the user asks for more.
bool paidService(const std::string& id) {
    return id == "fal" || id == "meshy" || id == "hyper3d" || id == "tripo";
}
// Services on the user's own machine or server: one generation at a time (a local GPU runs one model at once).
bool localService(const std::string& id) { return id == "comfyui" || id == "command"; }

std::string letter(int i) { return std::string(1, char('A' + i)); }

std::filesystem::path u8path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

GLuint uploadPreview(const Image& img) {
    if (img.empty()) return 0;
    GLuint tex = 0;
    gl::GenTextures(1, &tex);
    gl::BindTexture(GL_TEXTURE_2D, tex);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    gl::BindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Deletes the downloaded file (and Hyper3D's private folder) once the model is read.
void removeDownload(const ai::GeneratorResult& r) {
    std::error_code ec;
    if (!r.outputPath.empty()) std::filesystem::remove(u8path(r.outputPath), ec);
    if (!r.privateDir.empty()) std::filesystem::remove_all(u8path(r.privateDir), ec);
}

} // namespace

int App::candidateCountForService() const {
    const std::string& svc = aiSettings_.generatorService;
    if (svc == "inflate") return 1; // offline and the same every time
    return std::clamp(paidService(svc) ? paidCandidateCount_ : candidateCount_, 1, 3);
}

std::string App::hfOffReason() const {
    if (aiSettings_.generatorService != "huggingface") return {};
    const ai::HfPreset& preset = ai::resolveHfPreset(aiSettings_, textMode_);
    const ai::health::Entry e = ai::health::of(preset.id);
    return e.state == ai::health::State::Off ? std::string(tr(ai::health::label(e.state))) + (e.note.empty() ? "" : ": " + e.note)
                                             : std::string();
}

void App::drawRecommendedModels() {
    if (!ImGui::TreeNodeEx(tr("Recommended models"), 0)) return; // (closed: the list is tall; one click opens it, and it stays open)
    const float scale = uiScale_;
    ImGui::TextDisabled("%s", tr("Start from an image, then make it yours."));
    int index = 0;
    for (const ai::RecommendedModel& m : ai::recommendedModels()) {
        if (textMode_ && !m.text) continue;
        ImGui::PushID(index++);
        const std::string svc = m.service, model = m.model;
        const bool free = svc == "huggingface";
        const bool current = aiSettings_.generatorService == svc &&
                             (model.empty() || (svc == "fal" ? std::string(falModel_) == model
                                                : svc == "tripo" ? ai::tripo::resolvedModel(aiSettings_.tripoModel) == model
                                                                 : aiSettings_.hfModel == model));
        const std::string price = std::string(m.price) == "Free" ? tr("Free") : m.price;
        std::string health;
        if (free) {
            const ai::health::Entry e = ai::health::of(model);
            if (e.state != ai::health::State::Up)
                health = std::string(tr(ai::health::label(e.state))) + (e.note.empty() ? "" : ": " + e.note);
        }
        const std::string line = health.empty() ? std::string(tr(m.note)) : health; // a free Space's health replaces the note
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float x0 = ImGui::GetCursorPosX(), width = ImGui::GetContentRegionAvail().x;
        const float wrap = std::max(width - 8 * scale, 40.0f * scale);
        const float lineH = ImGui::GetTextLineHeightWithSpacing();
        const float height = lineH + ImGui::CalcTextSize(line.c_str(), nullptr, false, wrap).y + 2 * scale;
        // One selectable covers the row; the label and the price sit on its first line, the note under them.
        if (ImGui::Selectable("##rec", current, 0, ImVec2(width, height))) {
            aiSettings_.generatorService = svc;
            if (svc == "fal") std::snprintf(falModel_, sizeof falModel_, "%s", m.model);
            else if (svc == "tripo") aiSettings_.tripoModel = m.model;
            else if (free) {
                aiSettings_.hfModel = m.model;
                aiSettings_.hfModelChosen = true;
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            std::string tip = std::string(tr(m.note)) +
                              (free ? "" : std::string("\n") + tr("Billed to your own account, at the provider's list price (it can change)."));
            if (!health.empty()) tip += "\n" + health;
            ImGui::SetTooltip("%s", tip.c_str());
        }
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(start.x + 4 * scale, start.y));
        ImGui::TextUnformatted(m.label);
        ImGui::SameLine(x0 + width - ImGui::CalcTextSize(price.c_str()).x - 4 * scale);
        ImGui::TextDisabled("%s", price.c_str());
        ImGui::SetCursorScreenPos(ImVec2(start.x + 4 * scale, start.y + lineH));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        if (!health.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.6f, 0.4f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::SetCursorScreenPos(after);
        ImGui::Dummy(ImVec2(0, 0)); // (an item after moving the cursor: ImGui's boundary check)
        if (*m.keyUrl) {
            const bool missing = svc == "tripo" ? aiSettings_.resolvedTripoKey().empty() && !tripoKey_[0]
                                 : svc == "fal" ? aiSettings_.resolvedFalKey().empty() && !falKey_[0]
                                                : aiSettings_.resolvedHfToken().empty() && !hfToken_[0];
            if (missing && ImGui::SmallButton(tr(free ? "Get a free token" : "Get a key"))) sf::dialogs::openUrl(m.keyUrl);
        }
        ImGui::Dummy(ImVec2(0, 4 * scale)); // (a gap, so a row's button reads as its own and not the next row's)
        ImGui::PopID();
    }
    ImGui::TextDisabled(tr("List prices as of %s; they may change."), ai::kPricesChecked);
    ImGui::TreePop();
}

std::string App::textOffKey() const {
    if (!textMode_ || aiSettings_.generatorService != "huggingface") return {};
    return ai::resolveHfPreset(aiSettings_, true).id;
}

void App::startGenerator() {
    if (const std::string key = textOffKey(); !key.empty() && textOffPresets_.count(key)) return; // greyed in the panel
    if (pendingGenerator_.valid() || pendingBase_.valid()) return;
    if (textMode_ ? textPrompt_[0] == '\0' : refImagePath_.empty()) return;
    aiSettings_.generatorCommand = generatorCommand_;
    aiSettings_.falKey = falKey_;
    aiSettings_.falModel = falModel_;
    aiSettings_.meshyKey = meshyKey_;
    aiSettings_.hyper3dKey = hyper3dKey_;
    aiSettings_.tripoKey = tripoKey_;
    aiSettings_.hfToken = hfToken_;
    aiSettings_.comfyUrl = comfyUrl_;
    if (!ai::generateKeyMissing(aiSettings_) &&
        !askToSend(ai::generateTarget(aiSettings_, textMode_), SendPanel::Generate3D, [this] { startGenerator(); }))
        return; // the first-use notice (PanelAiConsent.cpp): nothing sent before Allow
    // With no workflow chosen, ComfyUI runs the bundled one the panel shows (not saved, so updates reach it).
    ai::AiSettings settings = aiSettings_;
    settings.cutImageBackground = removeBackground_; // "Remove flat background" also cuts a plain backdrop out of the picture first
    const bool hunyuan = settings.generatorService == "huggingface"
                             ? ai::hunyuanModel(ai::resolveHfPreset(settings, textMode_).id)
                             : settings.generatorService == "fal" &&
                                   ai::hunyuanModel(textMode_ ? ai::falTextModel(settings.falModel) : settings.falModel);
    if (hunyuan) showHunyuanNoticeOnce();
    if (settings.comfyImageWorkflow.empty()) settings.comfyImageWorkflow = comfyDefaultWorkflow(false);
    if (settings.comfyTextWorkflow.empty()) settings.comfyTextWorkflow = comfyDefaultWorkflow(true);
    clearCandidates();
    generatorError_.clear();
    const int count = candidateCountForService();
    const std::string stem = [] {
        const auto p = (std::filesystem::temp_directory_path() / ("sculptforge_generated_" + std::to_string(SDL_GetTicksNS()))).u8string();
        return std::string(p.begin(), p.end());
    }();
    generatorTime_ = 0.0f;
    generatorProgress_ = std::make_shared<ai::GenerationProgress>();
    generatorProgress_->setStatus("Starting...");
    candidateProgress_.clear();
    for (int i = 0; i < count; ++i) {
        candidateProgress_.push_back(count == 1 ? generatorProgress_ : std::make_shared<ai::GenerationProgress>());
        candidateProgress_.back()->setStatus("Starting...");
        candidateProgress_.back()->candidate = i;
    }
    candidatesDone_ = std::make_shared<std::atomic<int>>(0);
    auto promise = std::make_shared<std::promise<std::vector<GeneratedCandidate>>>();
    pendingGenerator_ = promise->get_future();
    std::thread([promise, settings, in = refImagePath_, text = textMode_ ? std::string(textPrompt_) : std::string(), stem,
                 progress = candidateProgress_, done = candidatesDone_, dropBackdrop = removeBackground_] {
        const int n = int(progress.size());
        std::vector<GeneratedCandidate> out(static_cast<size_t>(n));
        // One candidate: generated, opened into one piece, its flat background dropped, pictured.
        auto make = [&](int i) {
            GeneratedCandidate& c = out[size_t(i)];
            ai::GenerationProgress* p = progress[size_t(i)].get();
            const std::string s = n == 1 ? stem : stem + "_" + letter(i);
            c.result = text.empty() ? ai::generate3D(settings, in, s, p) : ai::generate3DFromText(settings, text, s, p);
            if (c.result.ok && !p->cancel) {
                p->setStatus("Reading the model...");
                Document scratch;
                CommandRegistry commands;
                const CommandResult opened = commands.execute(scratch, "open", {{"path", c.result.outputPath}});
                if (opened.ok && scratch.partCount() > 1) commands.execute(scratch, "part_merge_visible", {});
                if (!opened.ok) c.error = opened.message;
                else if (scratch.mesh.triangleCount() == 0) c.error = "The service sent back an empty model";
                else {
                    if (dropBackdrop) removeFlatBackdrop(scratch.mesh, &c.backdrop);
                    c.shaded = renderMeshPreview(scratch.mesh, kPreviewSize, PreviewStyle::Shaded, 35.0f, 12.0f);
                    c.silhouette = renderMeshPreview(scratch.mesh, kPreviewSize, PreviewStyle::Silhouette);
                    c.mesh = std::make_shared<Mesh>(std::move(scratch.mesh));
                }
            } else {
                c.error = c.result.ok ? "Cancelled" : c.result.error;
            }
            removeDownload(c.result);
            p->setStatus(c.mesh ? "Ready" : "Failed");
            ++*done;
        };
        if (n == 1 || localService(settings.generatorService)) {
            for (int i = 0; i < n; ++i) make(i);
        } else { // hosted services queue the requests themselves: all at once, the wait is the slowest one
            std::vector<std::thread> workers;
            for (int i = 0; i < n; ++i) workers.emplace_back(make, i);
            for (auto& w : workers) w.join();
        }
        for (int i = 1; i < n; ++i) // a service that ignores the seed gives the same shape again: say so
            for (int j = 0; j < i && out[size_t(i)].sameAs < 0; ++j)
                if (out[size_t(i)].mesh && out[size_t(j)].mesh &&
                    silhouetteDifference(out[size_t(i)].silhouette, out[size_t(j)].silhouette) < kSameOutline)
                    out[size_t(i)].sameAs = j;
        promise->set_value(std::move(out));
    }).detach();
}

CommandResult App::startGenerateCommand(const std::string& name, const Args& args) {
    if (generateCommand_ || pendingGenerator_.valid()) return {false, "A 3D model is already being generated"};
    if (std::string why; audit::required() && !audit::writable(&why)) return {false, why}; // (as every AI command)
    auto g = std::make_unique<GenerateCommandJob>();
    CommandResult refused;
    if (!ai::prepareGenerateJob(name, args, aiSettings_, g->job, refused)) return refused;
    g->name = name;
    g->args = args;
    g->start = std::chrono::steady_clock::now();
    g->revision = doc_.undo.revision();
    g->progress = std::make_shared<ai::GenerationProgress>();
    g->progress->setStatus("Starting...");
    std::promise<ai::GeneratorResult> promise;
    g->done = promise.get_future();
    std::thread([promise = std::move(promise), work = g->job.work, progress = g->progress]() mutable {
        try {
            promise.set_value(work(progress.get()));
        } catch (const std::exception& e) { // (a worker's uncaught exception would end the whole app)
            promise.set_value({false, {}, std::string("Generating failed: ") + e.what()});
        }
    }).detach();
    generateCommand_ = std::move(g);
    commandResult_.reset();
    const std::string what = name == "text_to_3d" ? "Text to 3D" : "Image to 3D";
    return {true, what + ": generating in the background (the window stays usable; the result replaces the scene when it arrives)"};
}

void App::pollGenerateCommand() {
    if (!generateCommand_) return;
    GenerateCommandJob& g = *generateCommand_;
    if (g.done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        const int secs = int(std::chrono::duration<double>(std::chrono::steady_clock::now() - g.start).count());
        status_ = std::string(g.name == "text_to_3d" ? "Text to 3D" : "Image to 3D") + ": " + g.progress->status() + " (" +
                  std::to_string(secs) + " s)";
        return;
    }
    if (jobWaitsForStroke()) return; // (the result lands when the stroke ends)
    const std::unique_ptr<GenerateCommandJob> job = std::move(generateCommand_);
    ai::GeneratorResult generated = job->done.get();
    CommandResult r;
    if (generated.ok && doc_.undo.revision() != job->revision) {
        removeDownload(generated); // (open would clear the history: the work done since stays)
        r = {false, "The scene changed while the model was generating, so it was not opened: run it again"};
    } else {
        r = job->job.finish(doc_, generated);
    }
    ai::recordGenerateRun(job->name, job->args, r,
                          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - job->start).count());
    status_ = r.message;
    commandResult_ = r;
    console_.print(r.ok ? Console::Kind::Result : Console::Kind::Error, r.message);
    console_.seenStatus(status_);
    if (!r.ok) {
        SDL_Log("%s failed: %s", job->name.c_str(), r.message.c_str());
        return;
    }
    clearLods();
    playingLayer_ = -1;
    frameView(sceneBounds());
    updateTitle();
}

void App::pollGenerator() {
    pollGenerateCommand();
    if (!pendingGenerator_.valid() || pendingGenerator_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    candidates_ = pendingGenerator_.get();
    candidateProgress_.clear();
    int ready = 0, first = -1;
    std::string note, error;
    for (int i = 0; i < int(candidates_.size()); ++i) {
        GeneratedCandidate& c = candidates_[size_t(i)];
        if (note.empty()) note = c.result.note;
        if (!c.mesh) {
            if (error.empty()) error = c.error;
            continue;
        }
        if (first < 0) first = i;
        ++ready;
        c.shadedTexture = uploadPreview(c.shaded);
        c.silhouetteTexture = uploadPreview(c.silhouette);
        c.shaded = {};
        c.silhouette = {};
    }
    const std::string noteText = note.empty() ? std::string() : " - " + note;
    if (ready == 0) {
        clearCandidates();
        if (error.find("switched Text to 3D off") != std::string::npos)
            if (const std::string key = textOffKey(); !key.empty()) textOffPresets_.insert(key);
        generatorError_ = (textMode_ ? "Text to 3D failed: " : "Image to 3D failed: ") + error + noteText;
        status_ = generatorError_;
        SDL_Log("%s", generatorError_.c_str());
        return;
    }
    if (candidates_.size() == 1) { // nothing to compare: it lands at once
        pickCandidate(first);
        status_ += noteText;
        return;
    }
    scrollGenerateTo_ = 1;
    status_ = (ready == 1 ? std::string("1 candidate ready") : std::to_string(ready) + " candidates ready") +
              ": pick the strongest form (Generate 3D)" + noteText;
    if (ready < int(candidates_.size())) status_ += ". " + std::to_string(int(candidates_.size()) - ready) + " failed: " + error;
}

void App::clearCandidates() {
    for (auto& c : candidates_) {
        if (c.shadedTexture) gl::DeleteTextures(1, &c.shadedTexture);
        if (c.silhouetteTexture) gl::DeleteTextures(1, &c.silhouetteTexture);
    }
    candidates_.clear();
}

void App::pickCandidate(int index) {
    if (index < 0 || index >= int(candidates_.size()) || !candidates_[size_t(index)].mesh) return;
    if (doc_.sculptor.strokeActive()) endStroke();
    GeneratedCandidate& c = candidates_[size_t(index)];
    const size_t tris = c.mesh->triangleCount();
    // An untitled scene nobody has touched (the starting sphere): the model takes its place, as opening a file does.
    // Anything else keeps what is there and gains a Part beside it (on +X, as Append places one).
    glm::vec3 shift(0.0f);
    const bool replace = !doc_.dirty() && doc_.path.empty() && doc_.partCount() == 1;
    if (const AABB scene = doc_.visibleBounds(); !replace && scene.valid()) {
        const AABB b = c.mesh->bounds();
        shift = glm::vec3(scene.max.x + 0.1f * (b.max.x - b.min.x) - b.min.x, scene.center().y - b.center().y,
                          scene.center().z - b.center().z);
    }
    auto placed = [&shift](const Mesh& from) {
        Mesh m = from;
        if (shift != glm::vec3(0.0f))
            for (auto& p : m.positions) p += shift;
        return m;
    };
    if (replace) {
        doc_.setMesh(placed(*c.mesh), "Generated");
        doc_.modified = true;
    } else {
        doc_.addPart(placed(*c.mesh), "Generated");
    }
    generatedPart_ = doc_.activePartInfo().id;
    highDetailPart_ = 0;
    scrollGenerateTo_ = 2;
    const std::string name = doc_.activePartInfo().name;
    int kept = 0;
    if (keepOtherCandidates_) // in the same place, to compare by showing one at a time
        for (int i = 0; i < int(candidates_.size()); ++i) {
            if (i == index || !candidates_[size_t(i)].mesh) continue;
            const int at = doc_.addPart(placed(*candidates_[size_t(i)].mesh), "Option " + letter(i), false);
            doc_.parts[size_t(at)].visible = false;
            ++kept;
        }
    const bool several = candidates_.size() > 1;
    std::string backdrop = c.backdrop.planes ? ". " + backdropRemovedNote(c.backdrop) : std::string();
    clearCandidates();
    clearLods();
    frameView(sceneBounds());
    status_ = (several ? "Picked " + letter(index) + ": " : std::string("Generated model: ")) + "Part " + name + " (" +
              std::to_string(tris) + " tris, as generated)" + backdrop +
              (kept ? ", the others kept as hidden Parts" : std::string()) +
              ". Next: Use as sculpting base (Generate 3D) for a clean, even mesh";
    updateTitle();
}

void App::useAsSculptingBase() {
    if (pendingBase_.valid() || pendingGenerator_.valid()) return;
    const int index = doc_.partIndex(generatedPart_);
    if (index < 0) {
        generatedPart_ = 0;
        status_ = "The generated Part is gone: generate a model first";
        return;
    }
    if (doc_.sculptor.strokeActive()) endStroke();
    if (index != doc_.activePart) doc_.selectPart(index);
    baseSource_ = std::make_shared<Mesh>(doc_.mesh);
    baseTriangles_ = doc_.mesh.triangleCount();
    generatorTime_ = 0.0f;
    generatorProgress_ = std::make_shared<ai::GenerationProgress>();
    const auto mode = generatedRemesh_ == int(GeneratedRemesh::Voxel) ? GeneratedRemesh::Voxel : GeneratedRemesh::Quad;
    generatorProgress_->setStatus(mode == GeneratedRemesh::Quad ? "Sculpting base: Voxel Remesh, then Quad Remesh..."
                                                                : "Sculpting base: Voxel Remesh...");
    generatorProgress_->percent = mode == GeneratedRemesh::Quad ? 0 : -1;
    auto promise = std::make_shared<std::promise<SculptingBase>>();
    pendingBase_ = promise->get_future();
    std::thread([promise, source = baseSource_, mode, resolution = remeshResolution_, progress = generatorProgress_] {
        SculptingBase b;
        Mesh m;
        b.ok = remeshGenerated(*source, mode, resolution, 1, m, b.error, &b.stats, [progress](float f) {
            progress->percent = int(f * 100.0f);
            return !progress->cancel.load();
        });
        if (b.ok) b.result = std::make_shared<Mesh>(std::move(m));
        else if (progress->cancel) b.error = "cancelled";
        promise->set_value(std::move(b));
    }).detach();
}

void App::pollSculptingBase() {
    if (!pendingBase_.valid() || pendingBase_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    if (jobWaitsForStroke()) return; // (the result lands when the stroke ends)
    SculptingBase b = pendingBase_.get();
    std::shared_ptr<Mesh> source = std::move(baseSource_);
    if (!b.ok) {
        status_ = "Use as sculpting base: " + b.error;
        return;
    }
    const int index = doc_.partIndex(generatedPart_);
    if (index < 0 || doc_.partMesh(index).triangleCount() != baseTriangles_) {
        status_ = "Use as sculpting base skipped: the Part changed while it was being remeshed";
        return;
    }
    const auto mode = generatedRemesh_ == int(GeneratedRemesh::Voxel) ? GeneratedRemesh::Voxel : GeneratedRemesh::Quad;
    const JobPart part(*this, index);
    std::string kept;
    if (keepHighDetail_ && source) { // just below it, hidden: the source for Project Detail
        const std::string name = doc_.uniquePartName("Reference"); // short: the Parts list shows about 9 letters
        const int at = doc_.addPart(Mesh(*source), name, false, index + 1);
        doc_.parts[size_t(at)].visible = false;
        highDetailPart_ = doc_.parts[size_t(at)].id;
        kept = ". The model as generated is kept hidden as \"" + name + "\" for Project Detail";
    }
    doc_.replaceMeshUndoable(std::move(*b.result), "Sculpting base");
    const std::string notice = doc_.takeNotice();
    status_ = "Sculpting base ready: " + generatedRemeshNote(b.stats, mode) + kept + (notice.empty() ? "" : ". " + notice);
    lastCommandOk_ = true;
    clearLods();
    playingLayer_ = -1;
    updateTitle();
}

void App::drawGeneratedCandidates() {
    if (candidates_.empty()) return;
    const float scale = uiScale_;
    ImGui::SeparatorText(tr("Pick the strongest form"));
    if (scrollGenerateTo_ == 1) { // they arrived below the fold of a long panel: bring them up
        ImGui::SetScrollHereY(0.0f);
        scrollGenerateTo_ = 0;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", tr("Judge the outline (the black shapes), not the detail: a strong silhouette sculpts well. "
                                "The one you pick becomes a Part."));
    ImGui::PopStyleColor();
    const int n = int(candidates_.size());
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float w = std::min((ImGui::GetContentRegionAvail().x - gap * float(n - 1)) / float(n), 140.0f * scale);
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const ImVec2 picture(w - 2.0f * pad.x, w - 2.0f * pad.y);
    int pick = -1;
    if (ImGui::BeginTable("##candidates", n, ImGuiTableFlags_SizingFixedFit)) {
        for (int i = 0; i < n; ++i) ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, w);
        ImGui::TableNextRow();
        for (int i = 0; i < n; ++i) {
            ImGui::TableSetColumnIndex(i);
            const GeneratedCandidate& c = candidates_[size_t(i)];
            ImGui::PushID(i);
            if (c.mesh) {
                // Either picture picks it.
                if (ImGui::ImageButton("##shaded", ImTextureID(uintptr_t(c.shadedTexture)), picture))
                    pick = i;
                if (ImGui::ImageButton("##silhouette", ImTextureID(uintptr_t(c.silhouetteTexture)), picture))
                    pick = i;
                char label[64];
                std::snprintf(label, sizeof label, tr("Pick %s"), letter(i).c_str());
                if (ImGui::Button((std::string(label) + "##candidate." + letter(i)).c_str(), ImVec2(w, 0))) pick = i;
                if (c.sameAs >= 0) {
                    char same[96];
                    std::snprintf(same, sizeof same, tr("Same shape as %s"), letter(c.sameAs).c_str());
                    ImGui::TextDisabled("%s", same);
                } else {
                    ImGui::TextDisabled(tr("%zu tris"), c.mesh->triangleCount());
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.6f, 0.4f, 1));
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
                ImGui::TextWrapped("%s: %s", letter(i).c_str(), c.error.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Checkbox(tr("Keep the others as hidden Parts"), &keepOtherCandidates_);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("Off: the ones you don't pick are discarded. On: they stay in the Parts list, hidden, "
                                   "to compare or use later."));
    if (ImGui::SmallButton(tr("Discard all##candidates"))) {
        clearCandidates();
        status_ = "Candidates discarded";
    }
    if (pick >= 0) pickCandidate(pick);
}

void App::drawSculptingBase() {
    auto hint = [](const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    };
    ImGui::SeparatorText(tr("Sculpting base"));
    if (scrollGenerateTo_ == 2) { // picked: the next step is here
        ImGui::SetScrollHereY(0.0f);
        scrollGenerateTo_ = 0;
    }
    hint(tr("The AI gives the form; SculptForge gives the topology. Use as sculpting base rebuilds the picked model as a "
            "clean, even mesh to sculpt on."));
    bool quads = generatedRemesh_ != int(GeneratedRemesh::Voxel);
    if (ImGui::Checkbox(tr("Then Quad Remesh"), &quads)) generatedRemesh_ = int(quads ? GeneratedRemesh::Quad : GeneratedRemesh::Voxel);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("Voxel Remesh first makes one even, watertight surface. Quad Remesh then turns it into "
                                   "clean quads with one subdivision level holding the detail, ready to divide and "
                                   "sculpt. Off: the even triangle surface."));
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize(tr("Resolution")).x - ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SliderInt(tr("Resolution"), &remeshResolution_, 96, 400);
    if (quads) {
        char text[160];
        std::snprintf(text, sizeof text, tr("About %d quads, then 1 subdivision level"), generatedQuadTarget(remeshResolution_));
        hint(text);
    }
    ImGui::Checkbox(tr("Keep a high-detail copy (hidden)"), &keepHighDetail_);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tr("For experts: the model as generated stays as a hidden Part right below the sculpting "
                                   "base. Divide the base, then Project Detail from it to bring fine detail back."));
    const int part = doc_.partIndex(generatedPart_);
    ImGui::BeginDisabled(part < 0 || pendingBase_.valid() || pendingGenerator_.valid());
    if (ImGui::Button(tr("Use as sculpting base"), ImVec2(-1, 0))) useAsSculptingBase();
    ImGui::EndDisabled();
    if (part < 0 && !pendingGenerator_.valid())
        hint(tr("Generate a model and pick one first."));
    if (const int ref = doc_.partIndex(highDetailPart_); ref >= 0 && part >= 0 && !pendingBase_.valid()) {
        if (ImGui::Button(tr("Project Detail from the high-detail copy"), ImVec2(-1, 0))) {
            if (part != doc_.activePart) doc_.selectPart(part);
            runCommand("project_all", {{"source", doc_.parts[size_t(ref)].name}});
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", tr("Moves the sculpting base onto the generated surface. Best after a Divide or two."));
    }
}

} // namespace sf
