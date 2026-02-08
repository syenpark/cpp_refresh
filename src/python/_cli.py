"""Command-line interface for Dummy YOLO."""

from __future__ import annotations

import logging
from dataclasses import dataclass
from typing import Annotated
from typing import Any

import typer

from python.utils import config as custom_config
from python.utils import logging as custom_logging
from python.yolo.inference.metadata import run_simulation

# Configure logging
logger = custom_logging.SetLogger().logger

app = typer.Typer(
    help="Dummy YOLO Command-Line Interface",
    rich_markup_mode=None,
)


@dataclass
class CommonArgs:
    config_data: Annotated[dict[str, Any], "Configuration data from config.toml"]


def set_configuration(config: str, *, debug: bool = False) -> dict[str, Any]:
    """Set configurations.

    Args:
        config: config.ini path
        debug: debug log level or not
    """
    if debug:
        logger.setLevel(logging.DEBUG)

        for handler in logger.handlers:
            handler.setLevel(logging.DEBUG)

        logger.debug("Debug logging enabled")

    return custom_config.read_config(config)


@app.callback(invoke_without_command=True)
def main(
    ctx: typer.Context,
    config: Annotated[
        str, typer.Option("--config", "-c", help="Path to config file")
    ] = "config.toml",
    *,
    debug: Annotated[
        bool, typer.Option("--debug", help="Enable debug logging")
    ] = False,
) -> None:
    # Set configuration
    config_data = set_configuration(config, debug=debug)

    # Store configuration
    ctx.obj = CommonArgs(
        config_data=config_data,
    )

    if ctx.invoked_subcommand is not None:
        logger.info("Store common configurations and invoke %s", ctx.invoked_subcommand)

        return

    run_simulation(config_data["yolo"]["fps"])
