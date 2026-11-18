# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""SculptForge for Python: every editor command, the mesh as NumPy arrays, Parts and selection sets.

    import sculptforge as sf
    doc = sf.Document()
    sf.run(doc, "new_sphere", subdivisions=5)
    sf.run(doc, "export_glb", path="sphere.glb")

The engine runs in this process, with no window, no network port and nothing that starts on its own. Failures raise
sculptforge.CommandError; long operations release the GIL.
"""
from ._core import (  # noqa: F401
    CommandError,
    Document,
    __version__,
    commands,
    run,
    run_script,
    tool_schema,
)

# Shown as sculptforge.Document, not sculptforge._core.Document, in reprs, tracebacks and docs.
for _t in (CommandError, Document):
    try:
        _t.__module__ = __name__
    except (AttributeError, TypeError):
        pass
del _t

__all__ = ["CommandError", "Document", "__version__", "commands", "run", "run_script", "tool_schema"]
