---
fixed: **Drape**: cloth resting on a faceted body no longer creeps sideways
testers:
  - Drape a sheet or a sewn garment over a low-poly ball or body and let it settle: it comes to rest instead of turning
    slowly round the body.
---
Solver (src/core/Cloth.*): a particle resting near the edge between two faces went from one face to the next and back,
its contact push and friction turning each time, so cloth on a faceted body crept round it. While a particle is slow (under
3x the rest speed: cloth settling), its contact keeps last frame's triangle unless another is nearer by more than half
the contact thickness; moving cloth needs no keep. The rest check compares each particle's mean position
over the 10-frame window with the last window's (same 0.83 mm limit, same peak-speed limit), so a particle cycling in
place counts as still, and the cloth stops at the middle of the cycle; one moving steadily moves its mean as far as its
end, so the check is no looser for it. A bag draped over a ball turned round it until the 900-frame limit; it now rests
at 360, and run on with the rest check off its windows keep shrinking (0.01 mm by frame 900).
