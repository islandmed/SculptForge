---
whats_new: **Saves that fail tell you**: if your settings, layout, brushes or recent projects can't be written, the status bar names the file instead of staying quiet.
fixed: On Windows, saving could fail with "Access is denied" and your settings, shelf layout, brush edits or recent projects were silently lost on the next start (reports from noved_ and rand_gogh on Discord). Start-up no longer keeps the settings file open while it saves it, and every save that replaces a file now waits and retries for about a second when the file is busy (a virus scanner, a sync client) or marked read-only, and finally writes over it directly. If a save still can't land, the status bar says which file.
testers:
  - Windows 10/11: change a setting, put a brush on the shelf, edit a custom brush, quit and start again: all of it is still there (also with OneDrive or an antivirus running).
  - Windows: mark ui.json in the settings folder read-only (file properties), change a setting, restart: the change is kept.
---
Core: replaceFile (rename, retry, clear read-only, copy over) for the five atomic writers; initProject closes project.json before saving it.
