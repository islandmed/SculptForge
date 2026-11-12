---
whats_new: **Two-key shortcuts are gone** (the "A then S" shortcuts of 0.12.8 to 0.12.10, and the Two-key window setting). Every key acts at once again: a key that was part of a two-key shortcut used to wait about 0.4 s before acting; now it acts in about 20 to 40 ms like every other key. Saved two-key shortcuts are dropped on the first start, with a note.
removed: **Two-key shortcuts** ("A then S", added in 0.12.8) and the **Two-key window** setting (Preferences > Input). A key used in a two-key shortcut waited about 0.4 s before acting (measured on Windows 11: 405 to 412 ms); now it acts in about 20 to 40 ms like every other key. Shortcuts saved with two keys lose them on the first start (a one-line note says how many; a shortcut that had other keys keeps them); the saved Two-key window setting is ignored. Normal shortcuts and every key style are unchanged.
testers:
  - Windows: bind a shortcut, quit, then start 0.12.11 over a settings folder that has an "A then S" shortcut: a short note says how many lost it, the other keys still work.
  - Any OS: press V, C or any key you had in a two-key shortcut: it acts at once.
---
