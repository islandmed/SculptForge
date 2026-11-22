---
changed: A UI-test check pins the Classic layout's defaults (menu rows, shelves, tray panels, colour cells, Interface preferences), so a change that moves one fails the tests until it is made on purpose.
---
Test-only: the dump_ui verb (UiTest.cpp), tests/ui/classic_defaults.sfui and its golden file. No user-visible change.
