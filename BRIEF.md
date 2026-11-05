# Translation coverage: Spanish + Traditional Chinese — rides 0.11.2 if READY in time, else 0.11.3
Read ~/Work/CLAUDE.md. pc.sh builds/tests; offscreen; look at screenshots; check_marks (ZERO third-party marks in any language).
1. tools/i18n/coverage.py: list every translatable UI string (tr("...") and the other label paths I18n.h uses) and,
   per language, which are missing from src/app/I18nSpanish.cpp / I18nTraditionalChinese.cpp. Print counts + percent.
2. Fill the gaps in both tables, same style and terms as each table already uses (Spanish: neutral Latin American/
   Spain-friendly; zh-TW: Taiwan terms, product names in English, see zh-hant REPORT). Keep printf conversions, "##"
   ids, icon prefixes; length <= 1.6x. Don't change existing entries unless wrong.
3. test_i18n: a test that fails when coverage for es or zh-TW drops below 100% of the strings (an allow-list file for
   strings that should stay English: product names, units). So new features can't ship untranslated labels.
4. UI shots in both languages of the newest panels (Quick Remesh, UV Editor Pack, VDM rotation, Preferences > Input,
   Help menu): look for clipping/overflow and fix layout if needed.
CHANGELOG Unreleased one line. T0 green, push, REPORT.md (before/after coverage %) READY <sha>, tell input + merge0109.
