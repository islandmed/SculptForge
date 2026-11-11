---
whats_new: "**Save Visible Parts As**: **File > Save Visible Parts As...** writes a copy of the project with only the Parts you can see, keeping their materials, layers and the scene's cameras and settings. The open project is untouched."
highlight: Save only the visible Parts
added: "**Save Visible Parts As...** (File): saves a new .sfproj with only the visible Parts (their materials, layers, folders and linked copies kept, the cameras and settings too). The open project and its file stay as they are. Parts with streamed detail need Save As for now. Requested by phoenix."
testers:
  - Any OS: hide one Part, File > Save Visible Parts As..., open the saved file; the hidden Part is not in it and the rest is as it was. The first project is unchanged.
---
Command: save_visible_parts. Streamed-detail Parts refuse (saveAs would move the open project's detail file).
