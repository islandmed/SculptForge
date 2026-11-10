---
whats_new: **Your language on the first start**: if your computer is in Spanish, Traditional Chinese or Simplified Chinese, SculptForge starts in it, with a note to change it. Any other language starts in English and the welcome has a **Language** row with all four, each in its own script.
highlight: The interface starts in your language
added: **Language on the first start** (#44): the first start reads the operating system's interface language (Windows display
  language, macOS preferred language, `LC_ALL` / `LC_MESSAGES` / `LANG` on Linux) and, for Spanish, Traditional Chinese (zh-TW,
  zh-HK, zh-MO, zh-Hant) and Simplified Chinese, starts in it with a one-time note, "SculptForge is in Español. Change", in that
  language and in English. Any other language stays English, and the welcome (also Help > Welcome) has a Language row with the
  four names in their own script that switches live. Never on a headless display, in tests, renders or demos, or twice.
testers:
  - Windows with the display language set to Español (Settings > Time & language), a fresh settings folder: the first start is in Spanish with the note at the bottom; Change opens Preferences > Interface.
  - macOS with Spanish or Chinese first in System Settings > Language & Region, fresh settings folder: same.
  - Linux with LANG=es_ES.UTF-8 (or zh_TW.UTF-8) and a fresh settings folder; with LANG=pt_BR.UTF-8 it starts in English and the welcome's Language row switches it live.
  - Any OS: restart after the first start; the language is kept and no note comes back.
---
Windows and macOS packages (zip, dmg, winget) have no installer screen of their own, so there is nothing to translate there;
the first start of the app is where the language is offered. Override for QA: `SF_OS_LANGUAGE=es-MX`.
