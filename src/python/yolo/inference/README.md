# Running the CLI

## Using uv

The CLI can be run using `uv` in several ways:

For first, after installing `uv`,

```bash
uv venv
source .venv/bin/activate

uv sync --all-groups
uv run pre-commit install
```

### 1. As a Python module

```bash
uv run python -m python
```

With options:

```bash
# Use default config.toml
uv run python -m python

# Specify a config file
uv run python -m python --config path/to/config.toml
uv run python -m python -c path/to/config.toml

# Enable debug logging
uv run python -m python --debug
```

### 2. Using the installed script

After installation, you can use the `dummy_yolo` command:

```bash
uv run dummy_yolo
```

With options:

```bash
# Use default config.toml
uv run dummy_yolo

# Specify a config file
uv run dummy_yolo --config path/to/config.toml
uv run dummy_yolo -c path/to/config.toml

# Enable debug logging
uv run dummy_yolo --debug
```

### 3. Direct module execution

```bash
# From the project root
uv run --directory . python -m python

# With options
uv run --directory . python -m python --config config.toml --debug
```

## Options

- `--config, -c`: Path to config file (default: `config.toml`)
- `--debug`: Enable debug logging

## Building and Installing

### Build the project

```bash
uv build
```

This creates distribution files in the `dist/` directory:

- `dist/cpp_refresh-0.1.0-py3-none-any.whl` (wheel file)
- `dist/cpp_refresh-0.1.0.tar.gz` (source distribution)

### Install the wheel file

```bash
# Install the wheel file with uv
uv pip install dist/cpp_refresh-0.1.0-py3-none-any.whl

# Or install in editable mode for development
uv pip install -e .
```

After installation, the `dummy_yolo` command will be available globally in your environment.

```bash
dummy_yolo
```