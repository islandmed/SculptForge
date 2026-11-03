// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real image-to-3D reconstruction through hosted models, with your own key:
//  - fal.ai: one key for TRELLIS.2 (Microsoft), Hunyuan3D 3.1 (Tencent),
//    Meshy 6 and other models, billed per generation
//  - Hugging Face Spaces: free public demos of TRELLIS.2, Hunyuan3D 2 / 2.1 and
//    Stable Fast 3D on shared GPUs (daily quota; a free HF token raises it)
//  - Meshy: direct API
// plus the local options (your own generator command, or offline silhouette
// inflation, which is only a rough blockout).
//
// The image is prepared first (subject cropped from a transparent background
// and centred, scaled to <= 1024 px) and sent inline as a data URI; the
// resulting GLB is downloaded for import. Nothing is uploaded unless a hosted
// service is selected.
#pragma once

#include "ai/BrushAssistant.h"
#include "ai/Generator3D.h"
#include "core/Json.h"

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sf::ai {

struct GenerationProgress {
    std::atomic<bool> cancel{false};
    std::atomic<int> percent{-1}; // -1 = unknown

    void setStatus(std::string text) {
        std::lock_guard lock(mutex_);
        status_ = std::move(text);
    }
    std::string status() const {
        std::lock_guard lock(mutex_);
        return status_;
    }

private:
    mutable std::mutex mutex_;
    std::string status_;
};

struct ServiceModel {
    const char* id;
    const char* label;
};
// Curated fal.ai image-to-3D models (any other fal model id also works).
const std::vector<ServiceModel>& falModels();

// Free models on Hugging Face Spaces (shared GPUs; a free HF token raises the daily quota).
struct HfStep {
    const char* api;
    std::vector<std::string> args; // "$IMG" = the uploaded image, "$OUT<n>" = first output of step n, else JSON
};
struct HfPreset {
    const char* id;
    const char* label;
    const char* space; // https://<space>.hf.space
    std::vector<HfStep> steps;
};
const std::vector<HfPreset>& hfPresets();
// A Hunyuan3D model (Hugging Face preset or fal.ai id): its licence's territory note is shown where it's chosen.
bool hunyuanModel(std::string_view id);
extern const char* kHunyuanLicenceNote;
// TRELLIS.2's Space needs a Hugging Face token; without one Hunyuan3D 2.1 runs instead, with this note.
extern const char* kTrellisNoTokenNote;
// The preset that runs for these settings (image presets, or text presets when `text`). An unknown id gets the
// first preset; TRELLIS.2 without a token falls back to Hunyuan3D 2.1 and sets `note` to kTrellisNoTokenNote.
const HfPreset& resolveHfPreset(const AiSettings& settings, bool text, std::string* note = nullptr);
// Free text-to-3D models ("$PROMPT" in step args is the description).
const std::vector<HfPreset>& hfTextPresets();

// Blocking; run on a worker thread. Writes `<outputStem>.glb` (or .obj) and
// returns its path in `outputPath`.
GeneratorResult generate3D(const AiSettings& settings, const std::string& imagePath, const std::string& outputStem,
                           GenerationProgress* progress = nullptr);

// Text to 3D: same services (Hugging Face, fal.ai, Meshy); untextured shapes for sculpting.
GeneratorResult generate3DFromText(const AiSettings& settings, const std::string& prompt, const std::string& outputStem,
                                   GenerationProgress* progress = nullptr);

// Pieces exposed for tests.
std::string meshyTextBody(const std::string& model, const std::string& prompt);
std::string falTextBody(const std::string& prompt);
// The image as sent to services: subject framed on a square, <= 1024 px, PNG.
bool prepareImagePng(const std::string& imagePath, std::string& png, std::string& error);
bool prepareImageDataUri(const std::string& imagePath, std::string& dataUri, std::string& error);
std::string falSubmitBody(const std::string& model, const std::string& dataUri);
std::string meshySubmitBody(const std::string& model, const std::string& dataUri);
// Finds the model file URL in a service result; `extension` gets ".glb" or ".obj".
std::string findModelUrl(const Json& result, std::string* extension);
std::string base64Encode(const uint8_t* data, size_t size);

} // namespace sf::ai
