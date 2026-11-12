---
whats_new: **Crisp Flatten** only cuts now: it lowers what is above its plane and never raises or adds clay, and the edges of its facets are clean; pen pressure no longer changes how deep it cuts.
fixed: Crisp Flatten only cuts down now and never adds clay (thanks Hameron on Discord). Pen pressure no longer scales the Crisp Flatten cut depth (it left terraces across a facet), so a light-pressure stroke lands on the same plane. Surface below the plane is left alone at any depth, which also removes the scattered spikes, debris and stair-steps along the edges of its facets. The regular Flatten brush is still the one that fills dips. The brush's shelf preview is a little wider so the cut shows.
testers:
  - Any OS: Brush > Hard Surface > Crisp Flatten, drag across a sphere or a head; the facet is flat, nothing rises past the original surface, and the facet's edge has no spikes (view it from a grazing angle).
  - Same stroke with Flatten: it still fills dips.
---
Cut-only: a Flatten plane with Crisp edge no longer fills (Sculpt.cpp, dabSingle's fill factor); a Fill plane recipe still fills. No new option.
