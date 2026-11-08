---
whats_new: **Surface Polish**: **Tool > Geometry > Surface Polish** smooths the surface while hard edges stay sharp,
  with a live preview, Apply and Cancel.
whats_new: **Noise and Symmetrize** steps in **Tool > Reshape > Stack**: smooth random bumps along any axis, and one
  side copied onto the other (or both evened out) across X, Y or Z.
highlight: Surface Polish keeps hard edges sharp
added: **Surface Polish** (Tool > Geometry): smooths the active Part's surface while creases sharper than the Edges
  angle stay crisp. A Polish amount from 0 to 1; the Part shows the result as you drag (live up to 250,000 points,
  on release above), Apply keeps it as one undo step, Cancel takes it back. The mask holds what it covers.
  `surface_polish amount= edge=` for scripts. Thanks foirgairwolf on Discord.
added: **Noise** and **Symmetrize** in Tool > Reshape > Stack, each on the X, Y or Z axes, with the preview and
  the mask like every step. Noise: smooth random bumps along the axes, with a Scale for how many. Symmetrize: the +
  side copied onto the - side across the symmetry plane (a negative amount: the other way), or with Average both
  sides evened out to meet halfway; the two sides need not share topology. Also `deform_noise` and
  `deform_symmetrize`. Thanks foirgairwolf on Discord.
testers:
  - Any OS: Tool > Geometry > Surface Polish: drag Polish on a bumpy Part; it smooths as you drag and the corners of a
    box stay sharp. Cancel puts it back; Apply then one Ctrl+Z puts it back too.
  - Any OS: Tool > Reshape > Stack > Add step > Noise, then Symmetrize on X: the bumps match left and right; tick
    Average and both sides meet halfway. Mask one side first: the masked part stays.
---
Credit per brief: "foirgairwolf on Discord".
