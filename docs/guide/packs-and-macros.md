# Packs and macros

A **pack** is one file (`.sfpack`) that brings brushes, alphas, Lit Spheres, Material Recipes, VDM brushes,
keymaps, pie menus and macros at once. A **macro** is a list of SculptForge commands you can run with one click,
one key or one pie-menu slot. Both are plain data: SculptForge never runs code from a pack or a macro.

## Install a pack

Use **File > Install Pack...**, drop a `.sfpack` file on the window, or click **Install Pack...** in
**Library > Packs**. Nothing is installed yet: a review shows exactly what is inside first.

| The review shows | |
|---|---|
| **Name, version and author** | As the pack's maker wrote them. **Member pack** marks the monthly pack or an Insider drop. |
| **License** | The license name, and the license text from the pack on the right. |
| **Contains** | How many brushes, alphas, Lit Spheres, Material Recipes, VDMs, keymaps, pie menus and macros, with every item listed by name. |
| **Replaces the installed version** | When an older version of the same pack is installed. |

Click **Install** to add it, or **Cancel** to leave everything as it was. When a pack fails a check, the review
says which file and why (for example *"../evil.sfbrush" leaves its folder*), and nothing is installed.

Installed packs live in SculptForge's own data folder, each in its own folder. A pack never replaces a built-in
brush or one of your own: a pack brush called like an existing one shows the pack's name after its own, for
example **Clay (Member pack)**.

## Where pack items show up

| Item | Where |
|---|---|
| Brushes and VDMs | The brush lists, the Brush picker and **Library > Brush**, tagged with the pack's name. |
| Alphas | The Alpha picker (a **Packs** section) and **Library > Alpha**, as **Name (Pack)**. |
| Lit Spheres | **Render > Materials** and **Library > Material**, as **Name (Pack)**. |
| Material Recipes | The **Material Recipe** list under **Tool > Paint Layers > Fill**, as **Name (Pack)**. |
| Keymaps | **Library > Packs > Details > Apply keymap**, and **Edit > Preferences > Input > Pack keymaps**. |
| Pie menus | **Library > Packs > Details > Add pie menu** (it becomes one of your radial menus). |
| Macros | <kbd>F3</kbd>, the key editor and the pie menus, as **Name (Pack)**. |

Keymaps and pie menus change how SculptForge responds, so they are only applied when you click **Apply keymap**
or **Add pie menu**. **Store Config** keeps the result.

## Library > Packs

Open the Library (<kbd>,</kbd>) and click **Packs**. Each installed pack has a row:

- The **check box** turns the pack off or on. Off, its items leave every list; nothing is deleted.
- **Details** shows its description, folder, keymaps, pie menus and macros.
- **Remove** deletes the pack and everything it brought. Your own files are never touched.

Start SculptForge with `--no-packs` to leave every pack out for one session (safe mode), for example to check
whether a problem comes from a pack. `sculptforge-cli --no-packs` does the same for scripts.

## Make a pack

**Library > Packs > New Pack...** lists what you made yourself: brushes in your library, images in your
Library's Alphas and MatCaps folders, the active Part's Fill layers as a Material Recipe, your VDMs, your pie
menus, your changed shortcuts (as a keymap) and your macros. Tick what goes in, fill in the name, author, license
and description, and click **Save Pack...**. The file is checked exactly as an install would check it before it
is written.

Tick **Member pack (insider channel)** for the monthly pack or an Insider drop. It only changes the label shown in the
review and the pack list; anyone can install the file.

To build a pack from a folder in a script, lay the folder out like a pack (`brushes/`, `alphas/`, `matcaps/`,
`materials/`, `vdms/`, `keymaps/`, `piemenus/`, `macros/`, plus `LICENSE.txt` and `preview.png`) and run:

```
sculptforge-cli -c 'pack_create folder=MyPack path=MyPack.sfpack id=my-pack name="My Pack" author=Me license=CC-BY-4.0'
```

The file format is described in [the pack format](https://github.com/islandmed/SculptForge/blob/main/docs/pack-format.md).

## Record a macro

1. In **Library > Packs**, click **Record Macro**. A red **Recording macro** note shows at the top of the window.
2. Work as usual: every command you run from a menu, a button, the console or a script line is kept.
3. Click **Stop Recording**. The **Macro Editor** opens with the commands; give the macro a name and click **Save**.

Commands a macro may not run (see below) are left out, and the status line says how many. A file inside the
project folder is kept as a path relative to it; any other file becomes `?`, which asks for a file when the macro
runs.

## Write or change a macro

**Macro Editor** (in **Library > Packs**, or <kbd>F3</kbd>) is a plain text editor. A macro is the command
language of the console and `sculptforge-cli`, one command per line, `#` for comments:

```
# name: Clean Up Scan
# description: Remesh, smooth and export a GLB next to the project
remesh resolution=256
mask_clear
export_glb path=exports/clean.glb
```

The line under the text checks it as you type: every command must exist, be allowed in a macro and have the
values it needs. **Save** writes it to your macros folder; **Run** runs it.

## Run a macro

Every saved macro (yours and those of enabled packs) is an action:

- <kbd>F3</kbd>: type its name and press <kbd>Enter</kbd>.
- **Edit > Preferences > Hotkeys**: find it with the filter and give it a key, like any other action (see
  [Assign shortcuts](interface.md#assign-shortcuts)).
- **Edit > Preferences > Input > Radial Menus**: put it in a pie menu slot.
- **Library > Packs**: **Run** next to your macros, or in a pack's **Details**.

The first time a macro runs, SculptForge shows its whole command list and asks. Click **Run** to go on; you are
not asked again for that macro unless its text changes. A macro's edits on one Part are **one undo step**
(<kbd>Ctrl</kbd>+<kbd>Z</kbd> takes back the whole macro).

## What a macro can do

A macro only runs SculptForge's own commands, in order. It can't run programs or scripts, and it can't:

- reach the network (AI services, the brush gallery, Unreal, the phone link) or send to Blender, Maya or ZBrush;
- install, remove or switch packs, or run another macro;
- name a file outside the project folder. File values are either a path inside the folder of the saved project
  (`exports/clean.glb`) or `?`, which opens a file dialog when the macro runs. Absolute paths, `..`, `~` and drive
  letters are refused.

Save the project first when a macro uses relative paths: they are relative to the project's folder.

## Scripts and Python

The same rules apply outside the app:

| Command | What it does |
|---|---|
| `pack_install path=Pack.sfpack` | Checks and installs a pack (the app shows the review first). |
| `pack_list` | Lists the installed packs: id, name, version, author, license, contents, on or off. |
| `pack_remove id=my-pack` | Removes a pack. |
| `pack_enable id=...` / `pack_disable id=...` | Turns a pack on or off. |
| `pack_create folder=... path=...` | Makes a pack from a folder laid out like one. |
| `macro_run name=... ` or `path=...` | Runs a macro (`project=` sets the folder its relative paths use). |
| `macro_list` / `macro_check` | Lists the macros, or checks one without running it. |

In Python, `sculptforge.run(doc, "pack_install", path="Pack.sfpack")` and the other commands work the same way.
A command you type into `sculptforge-cli` or Python is your own explicit request, so it runs without the
confirmation the app shows. AI agents connected through `sculptforge-cli --mcp` can't install, remove or switch
packs.
