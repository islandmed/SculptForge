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

class GradioClient {
public:
    // `spaceUrl`, e.g. https://tencent-hunyuan3d-2-1.hf.space. `token` (optional) is a
    // Hugging Face access token; on ZeroGPU Spaces it raises the free GPU quota.
    bool connect(const std::string& spaceUrl, const std::string& token, std::string& error);

    // Uploads a file and returns its Gradio FileData object.
    bool upload(const std::string& bytes, const std::string& fileName, Json& fileData, std::string& error);

    // Runs the endpoint `apiName`. `args` fill the non-state inputs in order (buttons
    // included: their value is their label); state inputs come from the session and any
    // missing trailing inputs take the app's own defaults. `outputs` receives every output
    // value; `onStatus` gets queue/progress text.
    bool call(const std::string& apiName, const std::vector<Json>& args, std::vector<Json>& outputs, std::string& error,
              const std::function<void(const std::string&)>& onStatus = {});

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

// Turns Gradio/ZeroGPU failures into advice for artists.
std::string friendlyGradioError(const std::string& raw);

} // namespace sf::ai
