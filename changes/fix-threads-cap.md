---
whats_new: **Threads above 16**: a thread count you set in **Preferences > Performance** (up to every thread your computer has) now applies at the next start; the default stays at 16.
fixed: **Thread count capped at 16** (#184): a Threads value set by hand (Preferences > Performance, or `set_performance_override threads`) above 16 is used at the next start, up to the computer's hardware threads; the default is still every hardware thread up to 16. The paint-layer, transform, multires and texture loops follow the same count.
testers:
  - A computer with more than 16 threads: Preferences > Performance, Threads (next start) = 32, restart; the Performance panel shows 32 threads. Leave it empty (auto): 16.
---
