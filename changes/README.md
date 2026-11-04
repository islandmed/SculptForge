# Note fragments

Every branch that changes something users notice adds **one file here**, `changes/<branch>.md` (e.g.
`changes/fix-wacom-hotplug.md`), instead of editing `CHANGELOG.md` and `docs/guide/whats-new.md`. Two branches
never touch the same file, so release notes never conflict. `scripts/release.sh X.Y.Z` (step bump) runs
`tools/release/assemble_notes.py X.Y.Z --apply`: the entries go into CHANGELOG `[Unreleased]` and What's New
`Version X.Y.Z`, and the fragments are deleted in the release commit.

```markdown
---
whats_new: **Gizmo size**: **Draw > Camera > Gizmo size** sets Small, Medium or Large.
highlight: Corner axes in three sizes
added: **Gizmo size** (Draw > Camera): the corner axes come in Small, Medium (as before, the default) and Large,
  on top of the UI scale. Saved in the preferences.
testers:
  - Windows at 150% scaling: Draw > Camera > Gizmo size > Large; click each axis end, the view snaps to that side.
  - Any OS: restart; the size is kept.
---
Anything after the front matter is for reviewers and is not copied anywhere.
```

| key | goes to | |
|---|---|---|
| `whats_new` | a bullet in the guide's What's New (the app shows it) | what users get, plain words, menu paths in bold |
| `highlight` | the version's feature block and the update feed | optional; 3-5 per release in all, one short line |
| `added` `changed` `fixed` `removed` `security` (`deprecated`) | `CHANGELOG.md` `[Unreleased]`, that section | the full entry, issue numbers and credits |
| `testers` | the #testers checklist (`assemble_notes.py X.Y.Z --testers`) | what to try, where, what "good" looks like |

Every key may repeat. A value continues on the next lines indented by two spaces; each `testers` item starts with
`  - `. The version's tagline and feature cards (title, line, image, Try it) stay hand-written in the ```features block of
`docs/guide/whats-new.md` (release.sh stops if the version has no tagline).
`ctest -R release_notes` checks that every fragment parses.
