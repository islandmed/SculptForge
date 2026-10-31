# SculptForge on FlatPark

[FlatPark](https://flatpark.org/) is a community Flatpak remote (not Flathub) for apps that publish an official
Linux download. It never builds or hosts the app: its package pins the vendor's file by URL, size and SHA-256,
and Flatpak downloads it from GitHub on the user's machine (Flatpak "extra-data").

`org.sculptforge.SculptForge/` is that package for SculptForge, laid out as FlatPark's registry wants it
(`registry/<app-id>/` in [github.com/flatpark/flatpark](https://github.com/flatpark/flatpark)). It wraps the
release tarballs `SculptForge-<version>-linux-x86_64.tar.gz` and `-linux-arm64.tar.gz` unchanged:

| File | What it does |
|---|---|
| `flatpark.yml` | FlatPark's descriptor: name, category, both arches, the update resolver |
| `org.sculptforge.SculptForge.yml` | The Flatpak manifest: freedesktop 26.08, the same sandbox as `../org.sculptforge.SculptForge.yml`, the pinned tarballs |
| `apply_extra.sh` | Unpacks the tarball to `/app/extra/sculptforge` at install time (offline, `--no-same-owner`) |
| `sculptforge-wrapper` | `/app/bin/sculptforge`: starts the tarball's binary |
| `resolve-update.sh` | Finds the newest GitHub release and its two tarballs; FlatPark re-pins on each release |
| `.desktop`, `.metainfo.xml`, `.png` | What the desktop and software centres show |

FlatPark's resolver picks up each new release by itself (twice a day), so nothing here changes per release.

## Submitting

Listing needs a pull request to github.com/flatpark/flatpark from a GitHub account, and no other sign-up:

1. Fork flatpark/flatpark, copy `org.sculptforge.SculptForge/` to `registry/`, on a branch `add/org.sculptforge.SculptForge`.
2. Build and check it with their scripts (`node scripts/audit-descriptor.mjs registry/org.sculptforge.SculptForge/flatpark.yml`,
   `./scripts/check-apply-extra.sh org.sculptforge.SculptForge`, `./scripts/publish.sh --verify org.sculptforge.SculptForge`).
3. Commit as `feat(sculptforge): add SculptForge, digital sculpting (org.sculptforge.SculptForge)` and open the PR with
   the What / Packaging / Sandbox / Tested sections their template asks for.
4. Opened from the SculptForge account, the PR is itself the developer's approval: add
   `upstream_approved: true` under `catalog:` and a row in their `docs/reference/upstream-approvals.md`.
