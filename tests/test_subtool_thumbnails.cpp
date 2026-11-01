// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// When SubTool thumbnails (#43) are rendered: a new SubTool at once, a change once it has settled, one at a time,
// nothing while busy, and deleted SubTools forgotten.
#include "TestHarness.h"

#include "app/SubtoolThumbnails.h"

using namespace sf;

TEST(subtool_thumbnails_new_ones_render_at_once) {
    SubtoolThumbnailSchedule s;
    s.observe(1, 100, 0.0);
    s.observe(2, 200, 0.0);
    uint32_t id = 0;
    double wake = 0.0;
    CHECK(!s.due(0.0, true, id, wake)); // busy: nothing, but come back
    CHECK(wake > 0.0);
    CHECK(s.due(0.0, false, id, wake));
    const uint32_t first = id;
    s.rendered(first);
    CHECK(s.due(0.0, false, id, wake) && id != first); // the other one, the next frame
    s.rendered(id);
    CHECK(!s.due(0.0, false, id, wake));
    CHECK(wake == 0.0); // nothing waiting: the loop may sleep
}

TEST(subtool_thumbnails_changes_wait_to_settle) {
    SubtoolThumbnailSchedule s;
    uint32_t id = 0;
    double wake = 0.0;
    s.observe(7, 1, 0.0);
    CHECK(s.due(0.0, false, id, wake) && id == 7);
    s.rendered(7);
    // A stroke changes it every frame: no render until it stops changing for a while.
    for (int f = 1; f <= 30; ++f) {
        s.observe(7, uint64_t(1 + f), f * 0.016);
        CHECK(!s.due(f * 0.016, false, id, wake));
        CHECK(wake > f * 0.016);
    }
    const double last = 30 * 0.016;
    s.observe(7, 31, last + 0.1); // the same key: still waiting from the last change
    CHECK(!s.due(last + 0.1, false, id, wake));
    CHECK(s.due(last + SubtoolThumbnailSchedule::kSettleSeconds + 0.01, false, id, wake) && id == 7);
    s.rendered(7);
    CHECK(!s.due(10.0, false, id, wake));
}

TEST(subtool_thumbnails_forget_deleted) {
    SubtoolThumbnailSchedule s;
    s.observe(1, 1, 0.0);
    s.observe(2, 1, 0.0);
    s.observe(3, 1, 0.0);
    const std::vector<uint32_t> gone = s.keepOnly({1, 3});
    CHECK(gone.size() == 1 && gone[0] == 2);
    uint32_t id = 0;
    double wake = 0.0;
    int rendered = 0;
    while (s.due(0.0, false, id, wake)) {
        CHECK(id != 2);
        s.rendered(id);
        ++rendered;
    }
    CHECK(rendered == 2);
}
