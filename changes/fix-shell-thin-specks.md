---
fixed: **A thin shell no longer shows dark specks while you orbit.** Make Shell on a dense model and a thin Thickness: while the
  view moved (Fast Navigation) or the model drew its lighter preview, the two faces of the shell were treated as one surface, so
  the inner face's dark side showed through as specks, or the shell collapsed to a single sheet; they cleared up at rest. A thin
  shell now keeps its faces apart in the moving preview and draws in full where the lighter preview would merge them. A user report.
testers:
  - Any machine: Mask part of a sphere subdivided to level 6 or more, Tool > Parts > Make Shell with Thickness 0.004, Accept, then
    orbit with Preferences > Performance > Fast Navigation on (and a model over the proxy threshold, or on a floor-tier machine). The
    shell stays a clean, light surface while the view moves, no dark specks, same as at rest.
---
