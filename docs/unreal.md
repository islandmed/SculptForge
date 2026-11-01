# SculptForge → Unreal Engine

SculptForge can push a sculpt straight into a running Unreal Editor (5.x), or
write an Unreal-ready FBX for you to import by hand.

## What you get

| Your sculpt | In Unreal |
|---|---|
| No sculpt layers | **Static mesh** `SM_<Name>` with LOD0–LODn from **Game Ready → LODs** (one file, imported as LODs automatically) |
| Sculpt layers | **Skeletal mesh** `SK_<Name>` with one **morph target** per layer (Unreal adds a single root bone) |
| Several visible Parts | One FBX model per Part, combined into one asset on import (*Combine Meshes*). Static meshes get a LOD group per Part, each decimated on its own, and Unreal puts level *n* of every Part into LOD *n*; layers on any visible Part make the asset skeletal |
| Any | Material slot `M_Clay`, real-world size from **Height (cm)** |

Sending the same asset again **updates it in place** (same name and folder), so
references in your levels stay intact.

## One-time setup in Unreal

1. **Edit → Plugins**: enable **Python Editor Script Plugin**, then restart the editor.
2. **Edit → Project Settings → Plugins → Python**: tick **Enable Remote Execution?**

That's all when Unreal runs on the same computer as SculptForge.

### Unreal on another computer

On the Unreal machine, in the same **Python** settings page, open the
**Advanced** section and set:

- **Multicast Time-To-Live** = `1`
- **Multicast Bind Address** = `0.0.0.0`

In SculptForge, tick **Unreal is on another computer** (or pass `network=true`
on the command line). SculptForge finds the editor on the local network, the
editor connects back to SculptForge, and downloads the FBX from it through a
one-time link that only works during that transfer.

Both computers must be on the same network. The SculptForge computer's firewall
must allow incoming connections from the Unreal machine, because the editor
connects back to fetch the command and the file.

## Sending

- **Game Ready → Unreal Engine → Send to Unreal** (or **Ctrl+U**). Set the
  content folder (default `/Game/SculptForge`) and the height in centimetres.
- Command line / agents:

```sh
sculptforge-cli -c 'open path=rock.obj; unreal_send destination=/Game/Props height_cm=120 lods=4'
sculptforge-cli -c 'unreal_editors network=true'   # list editors on the network
```

If several editors are open, `project=<ProjectName>` picks one.

## Manual import

**File → Export FBX for Unreal...** writes the same file. Drag it into the
Content Browser:

- Static meshes: LODs are detected from the file's LOD group.
- Skeletal meshes: choose *Skeletal Mesh* and tick *Import Morph Targets*.

## How it works

Send to Unreal uses Unreal's built-in Python remote execution, the same
channel Epic's own tools use:

1. A UDP multicast discovery on `239.0.0.1:6766`.
2. A TCP connection that the editor opens back to SculptForge.
3. A short Python script that runs `AssetImportTask` with the FBX importer.

Nothing is installed into your project.

## Known limits

- Morph targets need a skeletal mesh in Unreal, so layered sculpts are sent
  without the LOD chain (Unreal can generate skeletal LODs itself).
- There are no UVs yet, so Unreal generates lightmap UVs on import.
- Checked against SculptForge's test suite, two independent FBX readers (ufbx
  and Assimp), and a stand-in editor that speaks the remote-execution
  protocol. It has not yet been run against a real Unreal Editor. If something
  fails, the editor's **Output Log** shows the importer's messages.
