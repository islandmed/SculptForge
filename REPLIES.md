# Reply drafts: RodTDai batch (#125-#132), for the coordinator to post

Post after the release they name ships (#125, #128 ride 0.11.2). No signature.

## #125 Micro mode very noisy on deep brushes (Sharp Crease)

Thanks for the clear repro, this one was ours. On a micro level, Pinch, Crease and Sharp Crease used to "sharpen"
the heights by pushing each point away from its neighbours' average, and that grows any tiny unevenness a little
more with every dab, so a hard stroke ended up as the noise in your screenshot. They now pull the height profile
together the way the brushes pull a mesh (the heights are resampled from where the pull brings the surface), so
nothing can build up. Intensity 100 with the pen pressed hard now gives a clean groove, with small and large brush
sizes, and there's a test that measures it.

The micro count is in too: the title bar reads e.g. "20.5k + 5.24M micro" while a visible Part has a micro level
(the classic look's ActiveTris readout gets a Micro entry as well).

Both ship in 0.11.2.

## #126 Middle click + drag to scroll parameter panes

Good news: this is already in, since 0.10.9. A middle-drag anywhere on a panel scrolls it (controls included), and
a left-drag on its empty space does too. I checked again on 0.11.x with the default look, the classic look and the
Blender keymap.

If it doesn't scroll for you, could you tell us which pane (the right tray's Tool palette, a floating palette, a
popup?) and whether you press the middle button on a mouse or on a pen? That will tell us what's catching the press.

## #127 Debug log mode to save on disk what it is doing

Part of this exists: SculptForge always writes sculptforge.log (Help > Open log folder), with the commands run and,
after a crash, what crashed and the command before it, and it keeps the previous session's log too. Attaching that
file here already helps.

What it doesn't record yet is timing: which operation took how long when the app slowed down. A "record a trace"
switch that writes those timings (long frames, strokes, uploads, background jobs) to a file you can attach is a
good idea, and we've put it on the list. No date yet.

## #128 Wires toggle not working with micro mode

Fixed: with a micro level, Wire now draws the edges of the level below it (the real level's wires, quads without
their diagonals, as on a normal mesh), lying on the sculpted micro surface so they follow the detail. Ships in
0.11.2.

## #129 Export panel simpler with a format dropdown

Agreed, it grew one section at a time. Today the File menu has one Export entry per format (OBJ, GLB, STL, USD,
FBX for Unreal, LOD chain), Tool > Export holds the game-ready set, and Tool > Geometry > Export Displacement writes
the micro level as an EXR or 16-bit PNG displacement map over the UVs.

So "OBJ + displacement" is possible now in two steps: File > Export OBJ (it holds the level below the micro level),
then Export Displacement. A single export panel with a format dropdown that shows only that format's options, with OBJ +
displacement as one preset, is on the list. No date yet.

## #130 Simpler update process

This is in 0.11.1: Help > Check for updates offers Update, which downloads the right build for your system and
packaging, installs it and restarts into the new version (and rolls back if the new one fails to start). Package
installs (AUR, Homebrew and the like) get their update command instead. Thanks for the push on this one.

## #131 Migrate a top subdivision level to a micro level

Not yet. Today a micro level can only be added on top of the highest subdivision level, so detail sculpted on high
subdivision levels can't move into one. Turning, say, levels 4-6 into a micro level on level 3 (fewer real
polygons, same detail) is a good fit for how micro levels work, and we've noted it next to #132, which is its
counterpart. No date yet.

## #132 Rebuild subdivision levels from micro geometry

Not yet either. For the record: Bake Micro (Tool > Geometry) turns the micro level into a real mesh, as a new Part
or in place, but that mesh has no subdivision levels. On a quad base, a micro level's grid is the same layout as
subdivision levels would be, so rebuilding real levels from it is doable. We've noted it together with #131. No
date yet.
