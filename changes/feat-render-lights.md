---
whats_new: **Lights**: add up to four lights of your own in **Material > Light > Lights**, a Key, Fill or Rim each with its
  direction, colour and intensity, in the Lit Sphere, studio and PBR views; and a **Shadow strength** slider for deeper
  cast shadows. Requested by members.
highlight: Up to four lights of your own, and deeper shadows
added: **Lights** (Material > Light > Lights, `lights` command): up to four directional lights on top of the material's light
  or the PBR environment. Add a **Key** (warm, from the upper left; it casts the view's shadows), a **Fill** (cool and soft,
  from the right) or a **Rim** (from behind, along the silhouette), then drag each one's direction on its own pad (past the
  edge of the ball sends a light round the back), pick its colour and intensity, and tick which one casts the shadows
  (Shadows & AO). In the PBR view they light the material channels with their roughness and metallic; in the Lit Sphere and
  studio views they add to the material. None by default, so the view draws as before until you add one; saved in the
  project. Requested by members.
added: **Shadow strength** (Material > Viewport Shading and Preferences > Display, under Shadows & AO): how dark the cast
  shadows get, 1 being what they were, towards 2 they deepen to black; `view_shadows shadow_strength=`. Saved with the
  preferences. Requested by a member.
changed: **Material > Light**: dragging past the edge of the light's ball now takes the light round the back of the model
  (shown as a ring), where it only used to stop at the edge.
member_request: true
member_credit: KKolombre, fxrtst, Artizmo
testers:
  - Any OS, Material > Light > Lights: click Key, then Fill, then Rim. The head gets a warm key from the upper left, a cool
    lift on the right and a bright edge from behind; the shadows (Shadows & AO on) fall away from the key light. Four lights at
    their starting intensities run bright on a white Lit Sphere: turn a light's Intensity down to taste (presets get a trim next).
  - Drag a light's ball past its edge: the dot turns into a ring and the light comes from behind the model. Double-click the
    ball puts the light back where its role starts.
  - Tick Shadows on the Fill: the cast shadows swing to its side; the Key's tick goes off.
  - Material > Viewport Shading, PBR: drag Shadow strength to 2: the nose's shadow on the cheek goes black; at 1 it is as
    it was in 0.13.
  - Save the project, reopen it: the lights, their colours and intensities are back.
---
Reviewer notes: render/Renderer.cpp mesh shader (every variant, micro levels included) gets `uLightCount` + two vec3[4]
arrays; count 0 leaves every shading path's arithmetic as it was (one `if (uLightCount > 0)` per branch), so the default
picture is unchanged. PBR: GGX (height-correlated Smith, Schlick) + Lambert per light, before the cavity term, exposure
and tone map; Lit Sphere / studio: Lambert in the surface colour (0.55) + a small Blinn lobe. Cast shadows: the added
light with `shadows` on replaces the shadow light in Renderer::prepareOcclusion (direction + its share of the light), the
existing key stays otherwise; Shadow strength multiplies the share (shadowDarknessFor). Lights live in RenderSettings and
the project JSON (`render.lights`), Shadow strength in UiPrefs. UI test tests/ui/render_lights.sfui (+ 1440 / 2880 variants).
Member consent for the credits: feedback/feature-queue.md rows (consent yes). Floor-tier frame times with 0 / 1 / 4 lights are in the
branch's status note (laptop, 5M-triangle sphere: within 0.2 ms of each other). Render matrix: lights=1 cells on every mesh path.
