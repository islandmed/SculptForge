---
added: Focus Detail on very dense Micro levels keeps up while you sculpt and orbit: after a stroke or a camera move the detail you left goes back to its coarse form in parallel and in small batches that fit the frame, instead of one long frame, so the pause after a big brush is gone.
---
Reviewers: src/core/FocusDetail.cpp/.h (batches sized to the frame budget and the recent cost per tile, update() gives way to a brush's promotion, edited tiles compressed and clean tiles' coarse grids filled in parallel, SF_FOCUS_TRACE=1 names slow calls), src/core/ThreadPool.cpp/.h (busy()), tests/test_focus_detail.cpp. Floor bench: results/00cb01af-focus.md.
