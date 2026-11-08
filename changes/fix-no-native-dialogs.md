---
changed: Headless runs (the offscreen or dummy video driver, UI test scripts, or `SF_NO_DIALOGS=1`) never open a
  system file dialog, a message box or the browser: every one goes through one wrapper, which logs the request and
  answers cancel (or a test's scripted path). A source check (`native_dialogs`) fails on any direct call.
---
Reviewers: src/app/NativeDialog.{h,cpp}; 48 call sites switched mechanically (SDL_ShowOpen/Save/FolderDialog,
SDL_ShowSimpleMessageBox, SDL_OpenURL); tests/check_native_dialogs.py; tests/ui/no_native_dialogs.sfui.
