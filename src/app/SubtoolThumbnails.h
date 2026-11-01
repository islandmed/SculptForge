// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// When the SubTool panel's thumbnails (#43) are rendered: one SubTool at a time, a new one at once, a changed one
// once it has settled (not every frame of a stroke), nothing while the app is busy with the mesh. The render itself
// draws the SubTool's existing GPU buffers (Renderer::renderSubtoolThumbnail); this is only the bookkeeping, pure so
// it can be unit tested.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sf {

class SubtoolThumbnailSchedule {
public:
    static constexpr double kSettleSeconds = 0.5; // a change waits this long without another before its render

    // SubTool `id` looks like `key` now (anything that changes when it does: its undo revision, size, visibility).
    void observe(uint32_t id, uint64_t key, double now) {
        Item& it = items_[id];
        if (it.seen != key || !it.observed) {
            it.seen = key;
            it.seenAt = now;
            it.observed = true;
        }
    }
    // The SubTool to render now, if any: one never rendered first, else the change that settled longest ago; none
    // while `busy` (a stroke or a background job owns the mesh). `wakeAt` gets when the next one is due (0: none
    // waiting), so a loop that sleeps when idle can come back for it.
    bool due(double now, bool busy, uint32_t& id, double& wakeAt) const {
        wakeAt = 0.0;
        bool found = false, fresh = false;
        double oldest = 0.0;
        for (const auto& [i, it] : items_) {
            if (it.rendered && it.shown == it.seen) continue;
            const double at = it.rendered ? it.seenAt + kSettleSeconds : it.seenAt;
            if (busy || at > now) {
                const double when = busy ? now + kSettleSeconds : at;
                wakeAt = wakeAt == 0.0 ? when : (when < wakeAt ? when : wakeAt);
                continue;
            }
            const bool isFresh = !it.rendered;
            if (!found || (isFresh && !fresh) || (isFresh == fresh && at < oldest)) {
                found = true;
                fresh = isFresh;
                oldest = at;
                id = i;
            }
        }
        return found;
    }
    // SubTool `id` was rendered as it was last observed (or couldn't be: it is tried again when it changes).
    void rendered(uint32_t id) {
        auto found = items_.find(id);
        if (found == items_.end()) return;
        found->second.shown = found->second.seen;
        found->second.rendered = true;
    }
    // Forgets the SubTools not in `ids` (deleted, or a new scene); returns them, so their textures can go.
    std::vector<uint32_t> keepOnly(const std::vector<uint32_t>& ids) {
        std::vector<uint32_t> gone;
        for (auto it = items_.begin(); it != items_.end();) {
            bool keep = false;
            for (uint32_t i : ids) keep = keep || i == it->first;
            if (keep) {
                ++it;
            } else {
                gone.push_back(it->first);
                it = items_.erase(it);
            }
        }
        return gone;
    }

private:
    struct Item {
        uint64_t seen = 0, shown = 0;
        double seenAt = 0.0;
        bool observed = false, rendered = false;
    };
    std::unordered_map<uint32_t, Item> items_;
};

} // namespace sf
