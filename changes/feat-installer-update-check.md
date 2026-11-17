---
whats_new: **Windows installer**: a "Check for updates once a day" box (on by default) says what the check sends; the choice sets **Edit > Preferences > Updates**, which you can still change any time.
added: **Windows installer**: a "Check for updates once a day" page (default on; silent installs keep it on). Its choice is applied to Edit > Preferences > Updates the first start after each install, and the installer's privacy line names what the check sends.
testers:
  - Windows: run the installer, untick "Check for updates once a day", finish, start SculptForge; Edit > Preferences > Updates shows the box unticked and no update request is made.
  - Windows: reinstall with the box ticked; after the next start it is ticked again. Turn it off in Preferences; it stays off until the next install.
---
Part of the SignPath Foundation application (their terms want the privacy statement shown at installation with an option to switch the check off).
