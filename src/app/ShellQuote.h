// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace sf {

// One argument for a popen() command line, so a file name can never run commands: POSIX single quotes (a ' inside
// becomes '\''); on Windows double quotes, and an empty result (refuse) for names with " % ! ^ & | < > in them.
inline std::string shellQuote(const std::string& arg) {
#if defined(_WIN32)
    if (arg.find_first_of("\"%!^&|<>\r\n") != std::string::npos) return {};
    return "\"" + arg + "\"";
#else
    std::string out = "'";
    for (char c : arg) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
#endif
}

} // namespace sf
