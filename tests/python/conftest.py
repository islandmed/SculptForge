# SculptForge - Python package tests (pytest tests/python; the wheels run them on every platform).
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import shutil

import pytest


@pytest.fixture(autouse=True)
def private_data_home(tmp_path, monkeypatch):
    """Packs, macros and brushes go to a data folder of the test's own, never the user's."""
    home = tmp_path / "home"
    home.mkdir()
    for var in ("HOME", "USERPROFILE", "APPDATA", "LOCALAPPDATA", "XDG_DATA_HOME", "XDG_CONFIG_HOME"):
        monkeypatch.setenv(var, str(home))


@pytest.fixture
def cli():
    """sculptforge-cli for the comparisons with the CLI: SF_CLI or the one on PATH; the test skips without one."""
    path = os.environ.get("SF_CLI") or shutil.which("sculptforge-cli")
    if not path:
        pytest.skip("no sculptforge-cli (set SF_CLI)")
    return path
