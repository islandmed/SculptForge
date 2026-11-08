#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh's beta path, run for real in a sandbox (ctest release_script_beta): a shared clone of this
# checkout (with its uncommitted changes), a bare repository as origin, and stand-ins for gh, curl, cmake and ctest,
# so nothing reaches GitHub, the website or the build. It releases 9.0.0-beta.1 (steps bump check status: members
# only, nothing public),
# then promotes it to 9.0.0 (bump check site) with the beta tag present, and checks what each one may and may not
# touch (docs/releasing.md, "Betas"). Those two take the old path (--rebuild: bump pushed to main). Then build once,
# promote: 9.0.1 staged (--stage) and promoted, with the refusals (a dispatched or a stranger's staging run, the
# signing key at repository level).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
# CI containers check the repository out as another user ("dubious ownership"); HOME changes below, so not --global.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0='*'
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
quiet() { "$@" >"$S/out.log" 2>&1 || { cat "$S/out.log" >&2; fail "$*"; }; }

# --- the sandbox -----------------------------------------------------------------------------------------------------
git clone -q --shared --no-checkout "$ROOT" "$S/repo"
cd "$S/repo"
git sparse-checkout set --no-cone /.github/workflows/ /CMakeLists.txt /CHANGELOG.md /CITATION.cff /packaging/ /docs/ /tools/ /website/ \
    /scripts/ /changes/ /install/ /tests/ui/whats_new.sfui
git checkout -q -B main "$(git -C "$ROOT" rev-parse HEAD)"
# This checkout's uncommitted changes (the tree being tested), then one commit on main.
paths=(.github/workflows CMakeLists.txt CHANGELOG.md packaging docs tools website scripts changes tests/ui/whats_new.sfui)
(cd "$ROOT" && git diff --name-only HEAD -- "${paths[@]}" && git ls-files --others --exclude-standard -- "${paths[@]}") |
    while read -r f; do
        if [ -e "$ROOT/$f" ]; then mkdir -p "$(dirname "$f")" && cp "$ROOT/$f" "$f"; else rm -f "$f"; fi
    done
git config user.name test && git config user.email test@example.org
# The app's compiled credits as step bump leaves them: onboarding updates website/members-live.json on main at once,
# while founding.json and supporters.txt wait for the next release's bump, so main's copies can trail it.
python3 tools/guide/build_site.py --app-credits >/dev/null
rm -f changes/[!R]*.md # this branch's own fragments: the test writes its own
mkdir -p changes
cat > changes/beta-test.md <<'EOF'
---
whats_new: **Beta test feature**: something new to try.
highlight: First highlight of the beta
highlight: Second highlight of the beta
highlight: Third highlight of the beta
added: **Beta test feature**: something new to try.
testers:
  - Windows, 150% scaling: open the beta test feature and try it.
---
EOF
# The real tree's What's New may still hold {{NAME}} placeholders for numbers not measured yet; this test is about the
# steps, so the sandbox fills them (the placeholder check itself is tested below).
sed -i 's/{{[A-Z0-9_]*}}/measured/g' docs/guide/whats-new.md CHANGELOG.md
git add -A . && git commit -q -m "sandbox"
git init -q --bare "$S/origin.git"
git remote set-url origin "$S/origin.git"
# One root commit with the whole tree: CI's shallow checkout can't be pushed to a fresh bare repository.
git reset -q --soft "$(git commit-tree "HEAD^{tree}" -m sandbox)"
git push -q origin main

# Stand-ins: gh answers the few questions the tested steps ask; curl serves the sandbox's own site.
mkdir -p "$S/bin" "$S/home" "$S/build"
touch "$S/build/CTestTestfile.cmake"
cat > "$S/bin/gh" <<EOF
#!/usr/bin/env bash
# staging's Packages run for the asked commit (tools/release/staged_run.py): STAGED_EVENT, STAGED_ACTOR vary it.
# Listings by branch (runs of other commits with the same build-input key) are empty here.
runs() {
  sha=\$(sed -n 's/.*head_sha=\([0-9a-f]*\).*/\1/p' <<<"\$1")
  [ -n "\$sha" ] || { echo '{"workflow_runs": []}'; return; } # (the branch listings: no other commit's run)
  printf '{"workflow_runs": [{"id": 77, "path": ".github/workflows/packages.yml", "head_sha": "%s", "head_branch": "staging",
    "event": "%s", "status": "completed", "conclusion": "success", "created_at": "%s",
    "head_repository": {"full_name": "islandmed/SculptForge"}, "repository": {"full_name": "islandmed/SculptForge"},
    "actor": {"login": "%s"}, "triggering_actor": {"login": "islandmed"}}]}\n' \
    "\$sha" "\${STAGED_EVENT:-push}" "\$(date -u +%FT%TZ)" "\${STAGED_ACTOR:-islandmed}"
}
case "\$*" in
  "auth status"*) echo "Logged in to github.com account islandmed" ;;
  "api users/"*) exit 0 ;; # every credited handle is an account (tools/release/check_credits.py)
  "secret list --env"*) printf 'INSIDERS_REPO_TOKEN\t2026-11-05T00:00:00Z\nUPDATE_SIGNING_KEY\t2026-11-05T00:00:00Z\n' ;;
  "secret list"*) printf 'DISCORD_MEMBERS_WEBHOOK\t2026-11-05T00:00:00Z\n'; [ -z "\${SECRET_AT_REPO:-}" ] || printf 'UPDATE_SIGNING_KEY\t2026-11-05T00:00:00Z\n' ;;
  "api repos/islandmed/SculptForge/actions/workflows/packages.yml/runs"*) runs "\$2" ;;
  "api repos/islandmed/SculptForge/actions/runs/77/artifacts"*)
    python3 -c 'import json; print(json.dumps({"artifacts": [{"name": n} for n in "linux-x86_64 linux-arm64 flatpak-x86_64 flatpak-aarch64 vfx-vfx2026-x86_64 vfx-vfx2026-arm64 vfx-vfx2027-x86_64 vfx-vfx2027-arm64 macos windows staged-sums".split()]}))' ;;
  "api repos/islandmed/SculptForge/git/ref/tags/"*) exit 1 ;; # not pushed yet
  "api repos/islandmed/sculptforge-brushes/git/trees/main"*) python3 "$S/repo/tools/brush-gallery/check_published.py" --tree-json ;; # (published as is)
  "run list"*) echo 7 ;;
  "run view"*"-q .status"*) echo completed ;;
  "run view"*"-q .conclusion"*) echo success ;;
  "repo view sculptforge/sculptforge-insiders"*) echo "sculptforge/sculptforge-insiders" ;;
  "release view"*) printf 'SHA256SUMS.txt\nSHA256SUMS.txt.sig\n' # (signed: the app has an update key)
    # what install/install.sh and install.ps1 download (tools/release/check_install_assets.py)
    for f in x86_64.AppImage aarch64.AppImage darwin-universal.dmg windows-x86_64.zip windows-x86_64.exe; do
      echo "SculptForge-\${3#v}-\$f"; done ;;
  "release download"*) exit 1 ;;
  *homebrew-tap*) printf '  version "0.10.10"\n' | base64 ;;
  "pr view 1800"*) echo "OPEN islandmed sculptforge-bump" ;;
  *omarchy-pkgs/contents*) printf "sha256sums=('%064d')\n" 0 | base64 ;;
  *) echo "gh stand-in: unexpected gh \$*" >&2; exit 1 ;;
esac
EOF
cat > "$S/bin/curl" <<EOF
#!/usr/bin/env bash
for a in "\$@"; do
  case "\$a" in
    https://sculptforge.app/latest.json*) git -C "$S/origin.git" show main:website/latest.json; exit ;;
    https://sculptforge.app/guide/whats-new.html*) git -C "$S/origin.git" show main:docs/guide/whats-new.md; exit ;;
  esac
done
echo "curl stand-in: unexpected curl \$*" >&2; exit 1
EOF
printf '#!/bin/sh\nexit 0\n' > "$S/bin/cmake"
printf '#!/bin/sh\nexit 0\n' > "$S/bin/ctest"
chmod +x "$S/bin/"*
# The public gallery as published (no GitHub API call: shared CI runners hit its rate limit): this tree's own files.
python3 -c 'import json, sys; sys.path.insert(0, sys.argv[1]); import check_published as c; print(json.dumps(c.local_tree()))' \
    "$ROOT/tools/brush-gallery" > "$S/gallery.json"
export PATH="$S/bin:$PATH" HOME="$S/home" BUILD_DIR="$S/build" YES=1 SF_GALLERY_PUBLIC="$S/gallery.json"
unset HIGHLIGHTS

before() { git show "$1:$2"; }
stable_files="CITATION.cff packaging/rez/package.py packaging/linux/org.sculptforge.SculptForge.metainfo.xml website/index.html packaging/arch/PKGBUILD"

# --- usage ---------------------------------------------------------------------------------------------------------
for bad in 9.0.0-beta 9.0.0-beta.x 9.0.0-alpha.1 v9.0.0-beta.1 9.0; do
    if scripts/release.sh "$bad" --only status >/dev/null 2>&1; then fail "release.sh accepted '$bad'"; fi
done

# --- the beta ------------------------------------------------------------------------------------------------------
start=$(git rev-parse HEAD)
echo 'placeholder: {{SOME_NUMBER}}' >>CHANGELOG.md && git commit -q -am placeholder
scripts/release.sh 9.0.0-beta.1 --rebuild --only bump >"$S/out.log" 2>&1 && fail "bump passed with an unfilled placeholder"
grep -q 'unfilled placeholders: CHANGELOG.md' "$S/out.log" || fail "no placeholder stop: $(cat "$S/out.log")"
git reset -q --hard HEAD^
scripts/release.sh 9.0.0-beta.1 --only bump >"$S/out.log" 2>&1 && fail "bump on main without --rebuild (promote mode) bumped"
grep -q -- '--stage' "$S/out.log" || fail "promote mode's bump doesn't point at --stage: $(cat "$S/out.log")"
quiet scripts/release.sh 9.0.0-beta.1 --rebuild --only bump
grep -q '^project(SculptForge VERSION 9.0.0 ' CMakeLists.txt || fail "beta bump: CMakeLists.txt is not at 9.0.0"
grep -qx 'set(SCULPTFORGE_PRERELEASE "beta.1")' CMakeLists.txt || fail "beta bump: no prerelease beta.1"
for f in $stable_files; do
    [ "$(before "$start" "$f")" = "$(cat "$f")" ] || fail "beta bump changed $f (a beta leaves it to the stable release)"
done
grep -q '^## \[9.0.0\]' CHANGELOG.md && fail "beta bump made a CHANGELOG [9.0.0] section"
grep -q 'Beta test feature' CHANGELOG.md || fail "beta bump: the fragment's entry is not in CHANGELOG [Unreleased]"
grep -q '^## Version 9.0.0$' docs/guide/whats-new.md || fail "beta bump: no What's New 9.0.0"
[ ! -e changes/beta-test.md ] || fail "beta bump: the fragment is still there"
grep -q 'open the beta test feature' release-testers-9.0.0-beta.1.md || fail "beta bump: no testers checklist"
[ "$(git rev-parse HEAD)" = "$(git -C "$S/origin.git" rev-parse main)" ] || fail "beta bump: main not pushed"
grep -qx 'env SF_GUIDE_VERSION 9.0.0' tests/ui/whats_new.sfui && grep -qx 'expect_text "Version 9.0.0"' tests/ui/whats_new.sfui ||
    fail "beta bump didn't pin tests/ui/whats_new.sfui to 9.0.0"
# Every STOPPED message ends with the exact command that resumes, --yes included.
echo "# uncommitted" >> tests/ui/whats_new.sfui
scripts/release.sh 9.0.0-beta.1 --only check >"$S/out.log" 2>&1 && fail "check passed with uncommitted changes"
grep -q "then: BUILD_DIR=$BUILD_DIR scripts/release.sh 9.0.0-beta.1 --from check --yes$" "$S/out.log" || fail "STOPPED without the resume command: $(cat "$S/out.log")"
git checkout -q -- tests/ui/whats_new.sfui

quiet scripts/release.sh 9.0.0-beta.1 --only check
grep -q 'CMakeLists.txt says 9.0.0 beta.1' "$S/out.log" || fail "beta check: $(cat "$S/out.log")"
quiet scripts/release.sh 9.0.0-beta.1 --rebuild --only bump # again: nothing left to do
grep -q 'already says' "$S/out.log" || fail "beta bump is not idempotent: $(cat "$S/out.log")"

# A beta is members only: nothing of the site or the update feed changes.
[ "$(git rev-parse HEAD)" = "$(git -C "$S/origin.git" rev-parse main)" ] || fail "beta check pushed something"
git diff --quiet "$start" HEAD -- website/latest.json website/index.html || fail "the beta touched the update feed or the home page"
scripts/release.sh 9.0.0-beta.1 --only preview >/dev/null 2>&1 && fail "a beta still has a preview (public feed) step"
quiet scripts/release.sh 9.0.0-beta.1 --only status
grep -q 'release-testers-9.0.0-beta.1.md' "$S/out.log" || fail "beta status names no testers checklist"
grep -q 'sculptforge/sculptforge-insiders/releases/tag/v9.0.0-beta.1' "$S/out.log" || fail "beta status doesn't name the members' repository"
grep -q 'scripts/release.sh 9.0.0 --yes' "$S/out.log" || fail "beta status doesn't say how to promote"
git tag v9.0.0-beta.1 "$(git rev-parse HEAD)" # what step tag would have pushed

# --- promoting it: the stable 9.0.0 with the beta tag present ------------------------------------------------------
# People give the version its tagline before the stable release (the home page card needs it).
python3 - <<'EOF'
p = "docs/guide/whats-new.md"
t = open(p, encoding="utf-8").read()
head = "## Version 9.0.0\n\n```features\ndate: unreleased\n"
assert head in t, "no 9.0.0 features block"
open(p, "w", encoding="utf-8").write(t.replace(head, head + "tagline: The beta, finished.\n", 1))
EOF
grep -q '^tagline: The beta, finished.$' docs/guide/whats-new.md || fail "test setup: couldn't add the tagline"
git commit -q -am "What's New 9.0.0 tagline" && git push -q origin main
quiet scripts/release.sh 9.0.0 --rebuild --only bump
grep -qx 'set(SCULPTFORGE_PRERELEASE "")' CMakeLists.txt || fail "stable bump didn't clear the prerelease"
grep -q '^## \[9.0.0\] - ' CHANGELOG.md || fail "stable bump: no CHANGELOG [9.0.0]"
grep -qx 'version: 9.0.0' CITATION.cff || fail "stable bump: CITATION.cff not at 9.0.0"
grep -qF '<small>new in 9.0.0</small>' website/index.html || fail "stable bump: no home page card"
quiet scripts/release.sh 9.0.0 --only check
grep -q 'say 9.0.0 at' "$S/out.log" || fail "stable check after a beta: $(cat "$S/out.log")"
printf 'One\nTwo\nThree\n' >"$S/highlights.txt"
HIGHLIGHTS=$S/highlights.txt quiet scripts/release.sh 9.0.0 --only site
python3 - <<'EOF' || fail "stable site: wrong feed"
import json
feed = json.load(open("website/latest.json"))
assert feed["version"] == "9.0.0" and feed["highlights"] == ["One", "Two", "Three"], feed
assert "preview" not in feed.get("channels", {}), "the passed beta is still on the Preview channel"
EOF

git push -q origin main

# --- build once, promote: 9.0.1 staged, then promoted ----------------------------------------------------------------
cat > changes/promote-test.md <<'EOF'
---
whats_new: **Promote test**: built once.
highlight: First highlight of 9.0.1
highlight: Second highlight of 9.0.1
highlight: Third highlight of 9.0.1
fixed: **Promote test**: built once.
---
EOF
python3 - <<'EOF'
p = "docs/guide/whats-new.md"
t = open(p, encoding="utf-8").read()
i = t.index("## Version 9.0.0")
open(p, "w", encoding="utf-8").write(t[:i] + "## Version 9.0.1\n\n```features\ndate: unreleased\ntagline: Built once.\n```\n\n" + t[i:])
EOF
git add changes docs/guide/whats-new.md && git commit -q -m "9.0.1 notes" && git push -q origin main
# The merger's batch checkout (detached, main merged in); a stand-in merge queue lands it on staging and main.
cat > "$S/bin/mq" <<EOF
#!/usr/bin/env bash
echo "\$*" > "$S/mq.args"
git push -q origin "\${@: -1}:refs/heads/staging" "\${@: -1}:refs/heads/main"
EOF
chmod +x "$S/bin/mq"
git switch -q --detach
MERGE_QUEUE=$S/bin/mq STAGE_WITH= quiet scripts/release.sh 9.0.1 --stage
staged=$(git rev-parse HEAD)
[ "$(git -C "$S/origin.git" rev-parse staging)" = "$staged" ] || fail "--stage didn't put the bump on staging"
grep -q "^staging $staged\$" "$S/mq.args" || fail "--stage ran the merge queue as: $(cat "$S/mq.args")"
grep -q '^## \[9.0.1\]' CHANGELOG.md || fail "--stage: no CHANGELOG [9.0.1] in the staged commit"
git switch -q main && git merge -q --ff-only origin/main
quiet scripts/release.sh 9.0.1 --only bump # promoting: main already carries it
grep -q 'already carries 9.0.1' "$S/out.log" || fail "promote bump: $(cat "$S/out.log")"
SECRET_AT_REPO=1 scripts/release.sh 9.0.1 --only check >"$S/out.log" 2>&1 && fail "check passed with UPDATE_SIGNING_KEY at repository level"
grep -q 'UPDATE_SIGNING_KEY is a repository secret' "$S/out.log" || fail "no signing-key stop: $(cat "$S/out.log")"
quiet scripts/release.sh 9.0.1 --only check
STAGED_EVENT=workflow_dispatch scripts/release.sh 9.0.1 --only ci >"$S/out.log" 2>&1 && fail "ci accepted a dispatched staging run"
grep -q 'no Packages run on staging' "$S/out.log" || fail "dispatched run: $(cat "$S/out.log")"
STAGED_ACTOR=mallory scripts/release.sh 9.0.1 --only ci >"$S/out.log" 2>&1 && fail "ci accepted a stranger's staging run"
grep -q "can't be promoted: .*mallory" "$S/out.log" || fail "stranger's run: $(cat "$S/out.log")"
STAGED_ACTOR=mallory scripts/release.sh 9.0.1 --only tag >"$S/out.log" 2>&1 && fail "tag accepted a stranger's staging run"
git -C "$S/origin.git" rev-parse -q --verify v9.0.1 >/dev/null && fail "a refused promotion pushed the tag"
quiet scripts/release.sh 9.0.1 --only ci
grep -q "Packages run 77 built and tested 9.0.1" "$S/out.log" || fail "promote ci: $(cat "$S/out.log")"
quiet scripts/release.sh 9.0.1 --only tag
[ "$(git -C "$S/origin.git" rev-parse 'v9.0.1^{commit}')" = "$staged" ] || fail "v9.0.1 doesn't tag the staged commit"
echo "release.sh beta, promotion, build once: ok"
