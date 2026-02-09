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

# Run the inference command
uv run python -m python inference
uv run python -m python --debug inference
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

# Run the inference command
uv run dummy_yolo inference
uv run dummy_yolo --debug inference
uv run dummy_yolo --config config.toml inference
```

### 3. Direct module execution

```bash
# From the project root
uv run --directory . python -m python

# With options
uv run --directory . python -m python --config config.toml --debug

# Run inference command
uv run --directory . python -m python inference
uv run --directory . python -m python --debug inference
```

## Commands

### Default (no command)

Runs the simulation with default behavior.

```bash
uv run dummy_yolo
```

### `inference`

Explicitly runs the inference simulation and sends metadata via ZeroMQ.

```bash
uv run dummy_yolo inference
uv run dummy_yolo --debug inference
```

## Options

- `--config, -c`: Path to config file (default: `config.toml`)
- `--debug`: Enable debug logging (shows frame-by-frame detection details)

## Configuration

The CLI reads settings from `config.toml`. Example configuration:

```toml
[stream]
fps = 25
source_id = 0
uri = "rtsp://camera/stream"

[zmq]
port = 5555
```

**Configuration parameters:**

- `stream.fps`: Frames per second for simulation (default: 25)
- `stream.source_id`: Source identifier for metadata (default: 0)
- `stream.uri`: Stream URI for metadata tagging (default: "rtsp://camera/stream")
- `zmq.port`: ZeroMQ publisher port (default: 5555)

## Metadata Format

The inference simulation sends DeepStream-style metadata via ZeroMQ. Each detection includes:

```python
{
    "uri": "rtsp://camera/stream",
    "class_id": 0,              # Object class (0-2)
    "track_id": 123,            # Unique tracking ID
    "confidence": 0.95,         # Detection confidence
    "bbox": {                   # Bounding box
        "left": 100.0,
        "top": 150.0,
        "width": 50.0,
        "height": 100.0,
        "border_width": 0,
        "has_bg_color": 0
    },
    "frame_num": 42             # Frame number
}
```

The metadata is published on ZeroMQ with topic `b"inference"` in the format:

```python
{source_id: [detection1, detection2, ...]}
```

## Example Usage

### Running with debug output

```bash
uv run dummy_yolo --debug inference
```

Output:

```text
📡 Sending Live Stream Metadata via ZeroMQ (Ctrl+C to stop)...
📡 Publishing on tcp://*:5555
[Frame 1] Active Objects: 2
  └─ ID 234 (class_id=0) | Confidence: 0.92 | BBox: {'left': 120, 'top': 200, ...}
  └─ ID 456 (class_id=1) | Confidence: 0.88 | BBox: {'left': 300, 'top': 150, ...}
[Frame 2] Active Objects: 3
  └─ ID 234 (class_id=0) | Confidence: 0.91 | BBox: {'left': 122, 'top': 203, ...}
  └─ ID 456 (class_id=1) | Confidence: 0.89 | BBox: {'left': 302, 'top': 148, ...}
  └─ ID 789 (class_id=2) | Confidence: 0.95 | BBox: {'left': 450, 'top': 300, ...}
^C🛑 Stream stopped by user.
```


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
