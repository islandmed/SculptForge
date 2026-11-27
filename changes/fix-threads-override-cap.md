---
whats_new: **Threads (next start)** counts every thread of your computer, even when an earlier start of the app saw fewer.
fixed: **A Threads setting above 16 capped at a stale count** (#184, thanks @HG4Science): the computer's thread count saved in the hardware profile was trusted over the live one, so a profile written while the app was limited (a restricted start, a container, a copied profile) held **Preferences > Performance > Threads** at that count for good. The count is now read again at every start, and on Windows it covers every processor group.
testers:
  - Windows with more than 64 threads, or any machine: **Preferences > Performance > Threads (next start)** to the thread count, restart; "Multithreading (N threads)" shows N.
---
Cap test: tests/test_hardware_profile.cpp hw_profile_threads_cap_is_live; real start: tests/threads_restart.py (stale 4 in the profile, 48 live).
