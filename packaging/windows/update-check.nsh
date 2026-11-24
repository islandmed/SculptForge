; SculptForge installer: the "Check for updates once a day" box (an NSIS page, included through CPACK_NSIS_DEFINES in
; CMakeLists.txt). Writes bin\installer.json ({"check_updates": true|false}); the app applies it once per installer run and
; keeps Edit > Preferences > Updates in step (core/UpdateCheck.h, applyInstallerChoice). A silent install (/S) shows no
; page and keeps the default, on. The uninstaller removes the file.
!include nsDialogs.nsh
!include LogicLib.nsh

Var SfUpdateBox
Var SfUpdateState

Function SfUpdatePage
  !insertmacro MUI_HEADER_TEXT "Updates" "SculptForge can tell you when a new version is out."
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 36u "Once a day SculptForge asks sculptforge.app which version is newest. It sends nothing about you or your work (the request carries your IP address, as any web request does). There is no telemetry and no crash upload."
  Pop $0
  ${NSD_CreateCheckbox} 0 44u 100% 12u "Check for updates once a day"
  Pop $SfUpdateBox
  ${NSD_Check} $SfUpdateBox
  ${NSD_CreateLabel} 0 64u 100% 24u "You can change this at any time in Edit > Preferences > Updates. Privacy policy: https://sculptforge.app/code-signing.html#privacy"
  Pop $0
  nsDialogs::Show
FunctionEnd

Function SfUpdatePageLeave
  ${NSD_GetState} $SfUpdateBox $SfUpdateState
FunctionEnd

; Inside the install section, after the files are in place.
!macro SfWriteInstallerJson
  CreateDirectory '$INSTDIR\bin'
  FileOpen $0 '$INSTDIR\bin\installer.json' w
  ${If} $SfUpdateState == 0
    FileWrite $0 '{$\"check_updates$\": false}$\r$\n'
  ${Else}
    FileWrite $0 '{$\"check_updates$\": true}$\r$\n'
  ${EndIf}
  FileClose $0
!macroend

; Inside the uninstall section.
!macro SfRemoveInstallerJson
  Delete '$INSTDIR\bin\installer.json'
!macroend
