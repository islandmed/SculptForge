#pragma once
// The commit this binary was built from, refreshed on every build (cmake/BuildStamp.cmake writes BuildStamp.cpp), not at
// configure time: an incremental build after a commit shows the new commit.
namespace sf {
const char* buildCommit(); // short commit, "" when built outside a git checkout
const char* devCommit();   // the commit shown as "dev <sha>" in a developer's build; "" in release and CI builds
bool buildDirty();         // tracked files differed from the commit at build time
inline const char* commitOrUnknown() { return *buildCommit() ? buildCommit() : "unknown"; }
} // namespace sf
