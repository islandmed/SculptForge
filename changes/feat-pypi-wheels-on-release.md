---
whats_new: **Python package**: download the wheel for your platform from the release page and `pip install` it: the sculpting engine in your own Python, for procedural assets, batch exports and pipeline tests (PyPI coming).
added: **Python wheels on the release page**: every release carries the `sculptforge` wheels (CPython 3.12-3.14,
  Windows x64, macOS universal2, Linux x86_64/aarch64), checked against their recorded checksums and build attestations
  and listed in the signed SHA256SUMS.txt; `pip install <wheel link>`.
testers:
  - Windows, macOS or Linux with Python 3.12-3.14: `python -m venv v`, then `v/bin/pip install <the wheel's link from
    the release page>` (Windows: `v\Scripts\pip`); `v/bin/python -c "import sculptforge as sf; d = sf.Document();
    sf.run(d, 'new_sphere'); print(d.vertices.shape)"` prints the vertex count.
---
