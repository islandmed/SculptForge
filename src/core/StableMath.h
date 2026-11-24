// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sine and cosine from + and * only, so every operating system gives the same bits (each standard library's sin and
// cos round their last bit their own way, and a drape that starts a bit apart ends up visibly apart). Accurate to a
// few units in the last place of a double; for placement, not for long sums.
#pragma once

#include <cmath>

namespace sf {

inline void stableSinCos(double x, double& s, double& c) {
    // x = k pi/2 + r, |r| <= pi/4 (pi/2 split in two so k pi/2 is exact for |k| < 2^20)
    constexpr double kTwoOverPi = 0.63661977236758134308;
    constexpr double kPio2Hi = 1.5707963267341256e+00, kPio2Lo = 6.0771005065061922e-11;
    const double k = std::floor(x * kTwoOverPi + 0.5);
    const double r = (x - k * kPio2Hi) - k * kPio2Lo;
    const double r2 = r * r;
    // Taylor series to r^17 / r^16 (the next terms are under 1e-17 on |r| <= pi/4)
    const double sr = r * (1.0 + r2 * (-1.0 / 6.0 + r2 * (1.0 / 120.0 + r2 * (-1.0 / 5040.0 + r2 * (1.0 / 362880.0 +
                      r2 * (-1.0 / 39916800.0 + r2 * (1.0 / 6227020800.0 + r2 * (-1.0 / 1307674368000.0 +
                      r2 * (1.0 / 355687428096000.0)))))))));
    const double cr = 1.0 + r2 * (-0.5 + r2 * (1.0 / 24.0 + r2 * (-1.0 / 720.0 + r2 * (1.0 / 40320.0 +
                      r2 * (-1.0 / 3628800.0 + r2 * (1.0 / 479001600.0 + r2 * (-1.0 / 87178291200.0 +
                      r2 * (1.0 / 20922789888000.0))))))));
    const long q = long(k) & 3;
    s = q == 0 ? sr : q == 1 ? cr : q == 2 ? -sr : -cr;
    c = q == 0 ? cr : q == 1 ? -sr : q == 2 ? -cr : sr;
}

inline double stableSin(double x) {
    double s, c;
    stableSinCos(x, s, c);
    return s;
}
inline double stableCos(double x) {
    double s, c;
    stableSinCos(x, s, c);
    return c;
}
inline float stableSin(float x) { return float(stableSin(double(x))); }
inline float stableCos(float x) { return float(stableCos(double(x))); }

} // namespace sf
