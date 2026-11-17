// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Generator3D.h"

#include <cstdlib>
#include <filesystem>

namespace sf::ai {

namespace {

std::string shellQuote(const std::string& s) {
#if defined(_WIN32)
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    return out + "\"";
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
#endif
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

} // namespace

std::string defaultGeneratorCommand(const std::string& cliPath) {
#if defined(_WIN32)
    return shellQuote(cliPath) +
           " --set input={input} --set output={output} -c \"image_inflate path=$input subdivisions=2; export_glb "
           "path=$output\"";
#else
    return shellQuote(cliPath) +
           " --set input={input} --set output={output} -c 'image_inflate path=$input subdivisions=2; export_glb "
           "path=$output'";
#endif
}

std::string expandGeneratorCommand(const std::string& commandTemplate, const std::string& input,
                                   const std::string& output) {
    std::string cmd = commandTemplate;
    replaceAll(cmd, "{input}", shellQuote(input));
    replaceAll(cmd, "{output}", shellQuote(output));
    return cmd;
}

GeneratorResult runGenerator(const std::string& commandTemplate, const std::string& imagePath,
                             const std::string& outputPath) {
    GeneratorResult r;
    if (commandTemplate.find("{input}") == std::string::npos || commandTemplate.find("{output}") == std::string::npos) {
        r.error = "The generator command must contain {input} and {output}";
        return r;
    }
    const std::filesystem::path out(std::u8string(outputPath.begin(), outputPath.end()));
    std::error_code ec;
    std::filesystem::remove(out, ec);
    std::string cmd = expandGeneratorCommand(commandTemplate, imagePath, outputPath);
#if defined(_WIN32)
    cmd = "\"" + cmd + "\""; // cmd.exe strips one level of outer quotes
#endif
#ifdef SF_IOS
    const int status = -1; // (iPadOS runs no other programs: std::system is unavailable there)
    r.error = "Local generator commands can't run on the iPad";
    return r;
#else
    const int status = std::system(cmd.c_str());
#endif
    if (status != 0) {
        r.error = "The generator exited with status " + std::to_string(status);
        return r;
    }
    if (!std::filesystem::exists(out, ec) || std::filesystem::file_size(out, ec) == 0) {
        r.error = "The generator finished but did not write " + outputPath;
        return r;
    }
    r.ok = true;
    r.outputPath = outputPath;
    return r;
}

} // namespace sf::ai
