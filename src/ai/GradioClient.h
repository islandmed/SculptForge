// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal client for Gradio apps (Hugging Face Spaces), speaking Gradio's
// queue protocol directly: upload a file, join the queue with a session hash
// (so multi-step apps keep their state), and read the server-sent events
// until the result arrives. Works with Gradio 4.x and 5/6.x apps.
#pragma once

#include "core/Json.h"

#include <functional>
#include <string>
#include <vector>

namespace sf::ai {

// An input found by what it is, not where it sits: the first input whose label is `label` (any case), else the first
// not yet claimed input of component type `type` ("image", "textbox"). Spaces reorder and add inputs (Hunyuan3D's
// shape_generation grew from 2 to 13), so presets name the slots they must fill and leave the rest to the Space's defaults.
struct GradioSlot {
    std::string label, type;
    Json value;
    bool optional = false; // no such input: skip it instead of failing (a Space that dropped it)
};

// The `data` array for endpoint `apiName` of the Space whose /config is `config`: state inputs keep their config value
// (the server holds them per session), `slots` fill the inputs they name, `args` fill the remaining non-state inputs in
// order, and every other input takes the Space's own default. False with `error` when the endpoint or a slot is missing.
bool gradioBuildInputs(const Json& config, const std::string& apiName, const std::vector<Json>& args,
                       const std::vector<GradioSlot>& slots, Json& data, int& fnIndex, std::string& error);

class GradioClient {
public:
    // `spaceUrl`, e.g. https://tencent-hunyuan3d-2-1.hf.space. `token` (optional) is a
    // Hugging Face access token; on ZeroGPU Spaces it raises the free GPU quota.
    bool connect(const std::string& spaceUrl, const std::string& token, std::string& error);

    // Uploads a file and returns its Gradio FileData object.
    bool upload(const std::string& bytes, const std::string& fileName, Json& fileData, std::string& error);

    // Runs the endpoint `apiName`. `slots` fill the inputs they name (gradioBuildInputs), `args` the remaining non-state
    // inputs in order (buttons included: their value is their label); state inputs come from the session and any
    // missing inputs take the app's own defaults. `outputs` receives every output
    // value; `onStatus` gets queue/progress text. A Space that doesn't start within kStartSeconds, or doesn't finish within
    // kRunSeconds of starting, is given up on ("isn't answering"); `cancelled` (polled with each message) stops it sooner.
    static constexpr int kStartSeconds = 240, kRunSeconds = 420;
    bool call(const std::string& apiName, const std::vector<Json>& args, std::vector<Json>& outputs, std::string& error,
              const std::function<void(const std::string&)>& onStatus = {}, const std::vector<GradioSlot>& slots = {},
              const std::function<bool()>& cancelled = {});

    // The first 3D model / file output of the last call, as a downloadable URL.
    std::string lastModelUrl() const { return lastModelUrl_; }
    // Downloads `url` (with the token, if any).
    bool download(const std::string& url, std::string& bytes, std::string& error);

    const std::string& root() const { return root_; }

private:
    std::vector<std::string> headers(bool json) const;
    std::string fileUrl(const Json& value) const;

    std::string root_, prefix_, token_, sessionHash_;
    Json config_;
    std::string lastModelUrl_;
};

// A process_completed queue message: true with the output values, or false with a readable `error`.
bool gradioCompleted(const Json& message, std::vector<Json>& outputs, std::string& error);
// The first 3D model file among `outputs` of endpoint `apiName` (config = the Space's /config), as a download URL under
// `base` (root + api prefix); empty when the Space returned none. Reads FileData objects, bare URLs and lists.
std::string gradioModelUrl(const Json& config, const std::string& apiName, const std::vector<Json>& outputs,
                           const std::string& base);

// Turns Gradio/ZeroGPU failures into advice for artists.
std::string friendlyGradioError(const std::string& raw);
// A failure of the Space itself (down, asleep, not answering, connection lost), where another Space may still work;
// not a quota, a missing token or the artist's own cancel.
bool gradioSpaceUnavailable(const std::string& error);

} // namespace sf::ai
