---
whats_new: **Español, 繁體中文 and 简体中文** now cover the whole interface: **Generate 3D** (tabs, services, hints, Quad Remesh, Generate 3D model, Quick shapes), the About box, Proof mode, the phone link, the brush gallery, Brush Lab, splat training and about 400 other labels that stayed in English.
fixed: **Translations**: about 430 labels showed in English in Spanish, Traditional and Simplified Chinese, most visibly the
  whole **Generate 3D** panel in zh-CN; they now go through the translator in every language. The i18n test now fails
  on any label that skips it (only product names may), so this can't come back unnoticed.
testers:
  - Preferences > Interface > 简体中文, then drop a picture on the window: the Generate 3D panel is Chinese except model and
    product names (Hunyuan3D, TRELLIS.2, ComfyUI, Rodin, Meshy). Same in Español and 繁體中文.
  - Switch languages with Generate 3D, the About box and Proof mode open: nothing shows as "?" boxes.
---
Reviewers: tools/i18n/coverage.py --raw now also sees ternaries, Combo item arrays, ProgressBar/BeginCombo/modal titles
and local show-as-is helpers (hint()); raw_allow.txt has no backlog and only takes names keep_english.txt keeps.
