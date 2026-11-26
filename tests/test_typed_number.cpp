// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// A number typed into a picker pop-up (app/TypedNumber.h): '.' or ',' as the decimal separator, and the Intensity rule.
#include "TestHarness.h"

#include "app/TypedNumber.h"

#include <cmath>

using namespace sf;

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }
float num(const char* t) { const auto n = parseTypedNumber(t); return n ? n->value : -9999.0f; }
bool intensityIs(const char* t, float want) { const auto v = typedIntensity(t); return v && near(*v, want); }
} // namespace

TEST(typed_number_decimal_comma_equals_point) {
    CHECK(near(num("0,5"), 0.5f));
    CHECK(near(num("0.5"), 0.5f));
    CHECK(near(num(",5"), 0.5f));
    CHECK(near(num(".5"), 0.5f));
    CHECK(near(num("1,"), 1.0f));
    CHECK(near(num("1."), 1.0f));
    CHECK(near(num("-2,5"), -2.5f));
    CHECK(near(num("-,5"), -0.5f));
    CHECK(near(num("30"), 30.0f));
    const auto pct = parseTypedNumber("50%");
    CHECK(pct && pct->percent && near(pct->value, 50.0f));
}

TEST(typed_number_rejects_ambiguous_and_empty) {
    CHECK(!parseTypedNumber("1,000.5"));
    CHECK(!parseTypedNumber("1.000,5"));
    CHECK(!parseTypedNumber("1,000,000"));
    CHECK(!parseTypedNumber("1.2.3"));
    CHECK(!parseTypedNumber(""));
    CHECK(!parseTypedNumber("."));
    CHECK(!parseTypedNumber(","));
    CHECK(!parseTypedNumber("-"));
    CHECK(!parseTypedNumber("%"));
    CHECK(!parseTypedNumber("1-2"));
    CHECK(!parseTypedNumber("abc"));
}

TEST(typed_intensity_rule) {
    CHECK(intensityIs("0,5", 0.5f));  // the bug: 0,5 read as 0
    CHECK(intensityIs("0.5", 0.5f));
    CHECK(intensityIs(",5", 0.5f));
    CHECK(intensityIs("0,05", 0.05f));
    CHECK(intensityIs("1,", 0.01f));  // 1 is 1%
    CHECK(intensityIs("1", 0.01f));
    CHECK(intensityIs("1,0", 0.01f));
    CHECK(intensityIs("30", 0.30f));
    CHECK(intensityIs("100", 1.0f));
    CHECK(intensityIs("50%", 0.5f));
    CHECK(intensityIs("0,5%", 0.005f)); // an explicit % is always a percentage
    CHECK(!typedIntensity("1,000.5"));
    CHECK(!typedIntensity("."));
}
