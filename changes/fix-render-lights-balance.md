---
whats_new: **Lights** stay balanced as you add them: a second, third or fourth light shares one light's worth of exposure with the
  others instead of piling on. Requested by members.
added: **Lights** (Material > Light > Lights): the added lights together add one light's worth of exposure. With one light
  nothing changes (it is exactly what its Intensity says); with more, their colours are scaled so their intensities sum to the
  strongest light's own intensity (at least 1), so Key + Fill + Rim at their starting values look balanced instead of washing the
  model out. A deliberately bright single light stays bright. Requested by members.
member_request: true
member_credit: KKolombre, fxrtst, Artizmo
testers:
  - Any OS, Material > Light > Lights: add a Key, then a Fill, then a Rim and look at the head between clicks: it gets a fill and
    a rim without getting brighter overall; with four lights the white Lit Sphere still reads as clay, not white.
  - Set one light alone to Intensity 1.5: it is brighter than at 1 (a single light is not scaled).
---
Follow-up to feat/render-lights (artist-qa note on 781331c28: four preset lights washed the Lit Sphere out). CPU-side only:
viewLightsBalance scales the uploaded colours in Renderer::setLightUniforms; the shader is unchanged, count 0 keeps the old path.
UI test tests/ui/render_lights_balance.sfui (+ 1440 / 2880). Member consent: feedback/feature-queue.md rows (yes).
