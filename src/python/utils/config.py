"""config."""

from __future__ import annotations

import tomllib
from pathlib import Path
from typing import Any


def read_config(path: str = "config.toml") -> dict[str, Any]:
    """Read config.toml file."""
    with Path(path).open("rb") as f:
        return tomllib.load(f)
