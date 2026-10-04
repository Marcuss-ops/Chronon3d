import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from chronon3d import _lib  # noqa: E402


def pytest_collection_modifyitems(config, items):
    try:
        _lib.load()
    except OSError as e:
        skip = pytest.mark.skip(reason=f"libchronon3d_c unavailable: {e}")
        for item in items:
            item.add_marker(skip)
