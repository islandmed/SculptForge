// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bring-your-own image-to-3D generator. SculptForge runs a user-configured
// command with {input} (the image) and {output} (where to write a .glb/.obj),
// then imports the result for sculpting. Point it at a local open-source model
// (TRELLIS, Hunyuan3D-2, TripoSR, Stable Fast 3D...) or a script that calls a
// cloud service. The built-in default inflates the image's silhouette using
// sculptforge-cli, so the pipeline works out of the box with no GPU or network.
#pragma once

#include <string>

namespace sf::ai {

struct GeneratorResult {
    bool ok = false;
    std::string outputPath;
    std::string error;
    std::string note; // shown with the result, e.g. which model ran instead of the chosen one
};

std::string defaultGeneratorCommand(const std::string& cliPath);
// Placeholders {input} and {output} are replaced with shell-quoted paths.
std::string expandGeneratorCommand(const std::string& commandTemplate, const std::string& input,
                                   const std::string& output);
// Blocking; run on a worker thread.
GeneratorResult runGenerator(const std::string& commandTemplate, const std::string& imagePath,
                             const std::string& outputPath);

} // namespace sf::ai
