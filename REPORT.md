feat/discord-announce a1dce77: .github/workflows/discord-announce.yml (release + workflow_run on Release + workflow_dispatch),
.github/announcements/{0.10.6,0.10.7-new-look-preview,membership}.md, website/media/looks/forge-tokyo-night.png (PNG8, 237 KB),
docs/releasing.md Discord line. Tests: yaml.safe_load; script vs local echo server (dispatch, path escapes, bad image,
release, workflow_run new/old, feed timeout, no secret). Release.yml publishes with GITHUB_TOKEN, so `release` never fires
for it; workflow_run covers that. Highlights are read from main's website/latest.json (written by release.sh site after the tag).
