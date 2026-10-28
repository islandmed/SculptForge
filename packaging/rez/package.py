# Rez package for studio deployment (https://github.com/AcademySoftwareFoundation/rez).
#
#   cd packaging/rez && rez-build --install          # build into your local packages
#   rez-env sculptforge -- sculptforge                # run it
#   rez-env sculptforge python-3.13 -- python -c "import sculptforge"
name = "sculptforge"

version = "0.9.2"

description = "Free, open-source digital sculpting with game and animation pipelines (OpenUSD, FBX, glTF, Send to Unreal)."

authors = ["The SculptForge contributors"]

help = [["Website", "https://sculptforge.app/"], ["Docs", "https://github.com/islandmed/SculptForge#readme"]]

tools = ["sculptforge", "sculptforge-cli"]

# Builds with the CMake and compiler on PATH (GCC 14 for the VFX Reference Platform). The Python module is built
# for the first python3 found; set SCULPTFORGE_PYTHON to a specific interpreter to choose.
build_command = "bash {root}/build.sh {install}"


def commands():
    env.PATH.prepend("{root}/bin")
    env.PYTHONPATH.prepend("{root}/python")
    env.SCULPTFORGE_ROOT = "{root}"
    # Optional studio brush library (read-only, see docs/studios.md): set SCULPTFORGE_STUDIO_BRUSHES to a shared
    # folder of .sfbrush files and every artist gets those brushes.
    if defined("SCULPTFORGE_STUDIO_BRUSHES"):
        env.SCULPTFORGE_BRUSH_PATH.append(getenv("SCULPTFORGE_STUDIO_BRUSHES"))
