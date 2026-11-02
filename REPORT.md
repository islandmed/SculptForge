# feat/release-fast

What: scripts/release.sh is one command (`HIGHLIGHTS=f CARD="title|body" scripts/release.sh X.Y.Z --yes`).
- New first step `bump`: sets CMakeLists, CITATION.cff (+date), rez package.py, metainfo `<release>`, CHANGELOG
  [Unreleased] -> [X.Y.Z] - date; optional CARD writes the home page card + lede; rebuilds the guide; commits as
  islandmed; pushes main. Idempotent; writes nothing unless every check passes; refuses without What's New section,
  its sfui expect, or CHANGELOG entries.
- `check` (and bump) run `preflight`: card + lede (shared `card_ok` with step site), HIGHLIGHTS 3-5 lines (until the
  feed says X.Y.Z), site version-text anchors, PKGBUILD format, setup-channels.sh, Homebrew cask readable, Omarchy PR
  open with a sha256sums line, tools installed, gh account.
- pipefail sweep: gh auth/release/brew/omarchy `| grep -q`, replies `| head -3` and no-match grep in `$(...)`.
- Live-site waits: feed 10 min, guide 5 min (was: fail on first check). CI run lookup waits 3 min after bump's push.
- docs/releasing.md updated.

Tests: bash -n; temp clone + temp bare remote, version 9.9.9: stops in <1 s without What's New, without card,
with 2-line or unset HIGHLIGHTS, with bad CARD, with empty [Unreleased] (nothing written); with all inputs bump
pushed to the bare remote in 3.5 s, check passed in 3.4 s, bump re-run was a no-op. Not run for real.
