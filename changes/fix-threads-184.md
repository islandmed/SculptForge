---
whats_new: **More threads, for real**: a **Threads** value above 16 (Preferences > Performance) now reaches the brushes and the remesher after a restart.
fixed: **Threads above 16** (#184, thanks @HG4Science): the saved count was read after the built-in stamps had already started the thread pool at the default 16, so a 48-thread machine kept using 16. The saved count is now read before anything starts the pool.
testers:
  - A machine with more than 16 threads: Preferences > Performance > Manual overrides > Threads = all of them, restart; Preferences shows "Multithreading N threads" with that N, and the system monitor shows more than 16 cores busy during Cloth or a remesh.
---
Cause: App::App() builds the Document, whose built-in VDM stamps run on ThreadPool::shared(); that fixed the pool size before App::init applied the profile.
