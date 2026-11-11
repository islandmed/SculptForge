---
whats_new: "**Alpha pool and jitter**: **Sculpt > Alpha** has **Scale jitter** and **Position jitter**, and right-clicking an alpha or a folder of images in the Library gives **Add to alpha pool** / **Use folder as alpha pool**: each dab takes a random alpha, for wood grain and scans that never repeat. Three procedural wood grains are included. Requested by a member."
highlight: Alpha pool and jitter for non-repeating detail
added: "**Alpha pool and jitter** (Sculpt > Alpha): **Scale jitter** and **Position jitter** per dab, kept per brush, and an alpha pool (`brush_alpha_pool`): a Library folder of images or a few picked alphas, one drawn at random for each dab or Drag Stamp, repeatable per stroke. Three procedural wood-grain alphas (Procedural Wood Straight, Cathedral, Weathered) are named as procedural, not scans. Each stroke now starts its Angle jitter from its own seed, so two strokes no longer jitter identically. Thanks Hameron (FXiT)."
testers:
  - "Any OS: Library > Alpha, make a sub-folder with 3-4 images, right-click it > Use folder as alpha pool; Sculpt > Alpha shows 'Alpha pool: N'. Stroke across a sphere: the dabs differ."
  - "Any OS: Scale jitter 40 and Position jitter 50 with the Standard brush and Angle jitter 180: no two dabs alike; Clear puts the brush's own alpha back."
---
Smallest version of the wood-detail request: per-stamp jitter (angle existed; scale and position added), a random alpha from a
pool (folder or picked), the Library's own sub-folders for alphas, and procedural wood grains labelled as such. We cannot
make scans; the member was asked (via sfmonitor) for scans he can share under a licence. Pool is not saved per brush or in
the project (next: keep it with the brush, and a 1024+ px alpha for 4K scans).
