"""Config."""

from __future__ import annotations

import argparse
from configparser import ConfigParser


class Arguments(argparse.Namespace):
    """Command-line arguments."""

    config: str


def parse_args() -> Arguments:
    """Parse arguments."""
    parser = argparse.ArgumentParser(description="Load configuration file")

    parser.add_argument(
        "--config",
        type=str,
        default="config.ini",
        help="Path to the configuration file",
    )

    return parser.parse_args(namespace=Arguments())


def read_config(path: str) -> ConfigParser:
    """Read config.ini file with typer."""
    config = ConfigParser()
    config.read(path)
    config.sections()

    return config


class Config:
    """Config load."""

    def __init__(self) -> None:
        self._args = parse_args()
        self._config_path = self._args.config
        self.config_parser = ConfigParser()
        self.config_parser.read(self._config_path)
