// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Environment maths for the PBR viewport (PBR texturing phase 1B; scaffolding only). Will hold the CPU side:
// SH9 irradiance, a split-sum BRDF lookup table, procedural environments and the SCULPTFORGE_HDRI_PATH library.
// The GPU prefilter (render/Environment) and the renderer's PBR shader bit wait for the renderer's upload
// rewrite; until then the viewport shows material channels one at a time (channel_view).
#pragma once

namespace sf {

constexpr int kBrdfLutSize = 64;
constexpr const char* kHdriPathVariable = "SCULPTFORGE_HDRI_PATH";

} // namespace sf
