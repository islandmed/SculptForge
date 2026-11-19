---
whats_new: **Timelapse and video export** say what went wrong: ffmpeg missing (and how to get it), ffmpeg stopped (with its own message), a folder that can't be written. No more empty black window on Windows, and ffmpeg is found in Homebrew's and winget's folders.
fixed: **Timelapse export and `--record`** (ffmpeg): every failure now ends in a status message and one log line naming the cause (ffmpeg not found and how to install it; ffmpeg's exit code and last words; the file or folder that can't be written; no frames). On Windows the encoder is started directly, no `cmd.exe` console window that stays black and empty, and a stuck ffmpeg is stopped after a minute instead of hanging the export. ffmpeg is also found where Homebrew (macOS), winget, Chocolatey and Scoop (Windows) put it, which a desktop-launched app's PATH misses. The video is written beside the target and put in place at the end, so a failed export no longer leaves a half file or loses the previous one.
testers:
  - Windows: Edit > Export timelapse... with ffmpeg not installed: the status bar says it needs ffmpeg and how to install it. After `winget install Gyan.FFmpeg`, export again without restarting: no console window opens and the mp4 is written (try a folder with a space in its name).
  - macOS: with `brew install ffmpeg`, launch SculptForge from the Dock (not a terminal) and export a timelapse: the mp4 is written.
  - Any OS: pick a folder you can't write to: the status bar names the folder.
---
Every launch of ffmpeg (timelapse, --record, --ui-record, the benchmark video) goes through ai/VideoEncoder.h now.
