# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""SculptForge for Python: every editor command, the mesh as NumPy arrays, Parts and selection sets.

    import sculptforge as sf
    doc = sf.Document()
    doc.new_sphere(5)
    doc.stroke("Clay", [[-0.5, 0.2, 1], [0.5, 0.2, 1]], radius=0.2, strength=0.6)
    doc.save("sphere.glb")                      # or sf.run(doc, "export_glb", path="sphere.glb"): any command

The engine runs in this process, with no window, no network port and nothing that starts on its own. Failures raise
sculptforge.CommandError; long operations release the GIL.
"""
from ._core import (  # noqa: F401
    CommandError,
    __version__,
    commands,
    run,
    run_script,
    tool_schema,
)
from ._document import Document, command  # noqa: F401

# Shown as sculptforge.Document, not sculptforge._core.Document, in reprs, tracebacks and docs.
for _t in (CommandError, Document):
    try:
        _t.__module__ = __name__
    except (AttributeError, TypeError):
        pass
del _t

__all__ = ["CommandError", "Document", "__version__", "command", "commands", "run", "run_script", "tool_schema"]
