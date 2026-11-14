---
whats_new: **Start from an image, then make it yours**: a **Recommended models** list in the Generate 3D window shows what each model costs per generation, what it is good at and a **Get a key** button. **Tripo v3.1** (about $0.20 a picture) and **Stability SPAR3D** (about $0.04, a quick look in seconds) are new, with your own key. A free model that is switched off is greyed out with the reason, and a paid service that is down falls back to the free models you have allowed instead of hanging.
highlight: Recommended 3D models with prices, Tripo and SPAR3D with your own key
added: **Tripo and Stability SPAR3D in Generate 3D** (with your own key): Tripo v3.1 / v3.0 from an image or a description
  (shape only, about $0.20 / $0.10) and Stability AI's Stable Point Aware 3D from a picture (about $0.04). Keys live in the
  system keychain only; each service has one pinned address, a timeout, a capped wait and plain error messages; the model
  file is accepted only from the provider's own servers and checked before import (docs/ai-services.md).
  **Recommended models** list: price per generation, a quality note and a link to the provider's key page for each.
  **Free models that heal themselves**: the update check's feed now says which free Hugging Face Spaces are up, slow to
  start or switched off (no extra request); a switched-off Space is greyed out with the reason and a run moves to one that
  is up. When a paid service can't be reached or times out, the free models you already allowed run instead (never
  another paid service). **Remove flat background** now also cuts a plain backdrop (a flat colour, a vignette, a rounded
  icon plate) out of the picture before it is sent, so the models don't build the backdrop too; a picture with a busy
  background gets the tip "Best results: a single object on a plain or transparent background."
testers:
  - Any OS, no key: Generate 3D > Recommended models lists Tripo v3.1, Stability SPAR3D, TRELLIS.2, Hunyuan3D 3.1, Rodin 2.5 and the free models with a price each; clicking one picks the service and model; **Get a key** opens the provider's page.
  - Any OS, if you have a Tripo key (developers.tripo3d.ai): one generation, Generate 3D > Tripo > From image; report the time it took, the result (does the shape import?) and the cost your Tripo account shows. A wrong key says "Tripo rejected the API key".
  - Any OS, if you have a Stability key: one generation, Generate 3D > Stability SPAR3D > From image; report the time, the result and the cost shown. From text says it needs a picture.
---
Reviewers: the feed's `ai3d` block is written by scripts/ai3d-health.py (a daily timer, not Claude); the app only reads it.
