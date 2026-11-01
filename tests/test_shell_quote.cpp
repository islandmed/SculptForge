// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// File names reach ffmpeg's command line through shellQuote: nothing in a name may run (timelapse, proof mode,
// --record).
#include "TestHarness.h"

#include "app/ShellQuote.h"

#include <cstdio>
#include <string>

using namespace sf;

TEST(shell_quote_names_never_run) {
#if !defined(_WIN32)
    const std::string names[] = {"plain.mp4", "with space.mp4", "it's.mp4", "a$(touch x).mp4", "b`id`.mp4",
                                 "c\"d\".mp4", "e;rm -rf nothing.mp4", "f$HOME\\n.mp4", "'''.mp4"};
    for (const std::string& n : names) {
        FILE* p = popen(("printf %s " + shellQuote(n)).c_str(), "r");
        CHECK(p != nullptr);
        std::string out;
        char buf[256];
        size_t got;
        while ((got = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, got);
        pclose(p);
        CHECK(out == n); // the shell gave the name back unchanged: nothing expanded or ran
    }
#else
    CHECK(shellQuote("a\"b.mp4").empty());
    CHECK(shellQuote("50%.mp4").empty());
    CHECK(shellQuote("ok name.mp4") == "\"ok name.mp4\"");
#endif
}
