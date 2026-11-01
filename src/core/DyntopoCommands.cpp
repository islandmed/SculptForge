// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Commands.h"
#include "core/Dyntopo.h"

#include <algorithm>
#include <cmath>

namespace sf {

std::string dyntopoSummary(const DyntopoSettings& s) {
    const int detail = int(std::lround(std::clamp(s.detail, 0.0f, 1.0f) * 100.0f));
    return std::string("Dynamic topology ") + (s.enabled ? "on" : "off") + ": Detail " + std::to_string(detail) +
           (s.constantDetail ? " (one size for the whole mesh)" : " (follows the brush size)") +
           (s.smoothRemesh <= 0.005f ? "; Smooth leaves the topology alone"
            : s.smoothRemesh >= 0.995f
                ? ""
                : "; Smooth remeshes " + std::to_string(int(std::lround(s.smoothRemesh * 100.0f))) + "%");
}

void registerDyntopoCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"dyntopo",
                  "Dynamic topology (ZBrush's Sculptris Pro): while on, brush strokes add triangles where they need "
                  "detail and remove them where they don't, so shapes can be pulled out without stretching. Needs a "
                  "mesh without subdivision levels; quads under the brush become triangles and UVs are dropped (undo "
                  "brings them back). Without arguments, reports the settings.",
                  {{"enabled", T::Bool, "true = on, false = off.", false},
                   {"detail", T::Number, "Detail 0-100: how small the triangles get (default 50).", false},
                   {"constant", T::Bool,
                    "false (default) = the triangle size follows the brush size on screen (zoom in or shrink the brush "
                    "for finer detail); true = one size for the whole mesh, set by Detail.",
                    false},
                   {"smooth_remesh", T::Number,
                    "0-100: how much Smooth (Shift) remeshes while on. 0 = smooth only, the topology stays; 100 "
                    "(default) = it simplifies toward larger triangles as it smooths.",
                    false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      DyntopoSettings& s = doc.sculptor.dyntopo;
                      const bool enable = argBool(a, "enabled", s.enabled);
                      if (enable && !s.enabled) {
                          const std::string why = dyntopoBlocker(doc.mesh);
                          if (!why.empty()) return {false, why};
                      }
                      if (a.count("detail")) s.detail = float(std::clamp(argNumber(a, "detail", 50.0), 0.0, 100.0) / 100.0);
                      s.constantDetail = argBool(a, "constant", s.constantDetail);
                      if (a.count("smooth_remesh"))
                          s.smoothRemesh = float(std::clamp(argNumber(a, "smooth_remesh", 100.0), 0.0, 100.0) / 100.0);
                      s.enabled = enable;
                      return {true, dyntopoSummary(s)};
                  }});
}

} // namespace sf
