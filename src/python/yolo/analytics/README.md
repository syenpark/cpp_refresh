# Analytics

The analytics module receives inference metadata from the inference service via ZeroMQ and performs real-time analysis on object detection data.

## Overview

The analytics receiver subscribes to inference metadata published by the inference service and provides:

- **Real-time processing metrics** (FPS, frame counts)
- **Object tracking statistics** (unique tracks, total objects)
- **Class distribution analysis** (detections per class)
- **Periodic summaries** (configurable intervals)
- **Final aggregate statistics** (on exit)

## Architecture

```text
┌─────────────────┐         ZeroMQ          ┌─────────────────┐
│   Inference     │    tcp://localhost:5555 │   Analytics     │
│   (Publisher)   │ ──────────────────────> │   (Subscriber)  │
│                 │   Topic: "inference"    │                 │
└─────────────────┘                         └─────────────────┘
        │                                            │
        │ Sends metadata:                            │ Analyzes:
        │ - Track IDs                                │ - Processing FPS
        │ - Bounding boxes                           │ - Unique tracks
        │ - Class IDs                                │ - Class distribution
        │ - Confidence scores                        │ - Object counts
        └────────────────────────────────────────────┘
```

## Running Analytics

### Prerequisites

Start the inference service first to publish metadata:

```bash
# Terminal 1: Start inference publisher
uv run dummy_yolo inference
```

### Start Analytics Receiver

```bash
# Terminal 2: Start analytics subscriber
uv run dummy_yolo analytics
```

### With Debug Output

```bash
# See detailed per-frame and per-detection information
uv run dummy_yolo --debug analytics
```

### With Performance Metrics

```bash
# Enable detailed performance metrics tracking (FPS, latency, memory, CPU, cache stats)
uv run dummy_yolo --metrics analytics
```

### With Custom Configuration

```bash
# Use a custom config file
uv run dummy_yolo --config custom_config.toml analytics
```

## Configuration

Analytics uses the following settings from `config.toml`:

```toml
[stream]
fps_check_interval_sec = 10  # Analytics summary interval (seconds)

[zmq]
port = 5555                  # ZeroMQ subscriber port
```

## Metrics Tracked

### Analytics Metrics (Always Tracked)

| Metric | Description |
| ------ | ----------- |
| **Frames processed** | Total number of frames received in the interval |
| **Processing rate** | Average FPS at which frames are being processed |
| **Total objects tracked** | Number of unique track IDs observed |
| **Avg objects per frame** | Mean number of detections per frame |
| **Class distribution** | Count of detections per class ID |

### Performance Metrics (With `--metrics` flag)

When enabled with `--metrics`, detailed performance metrics are logged periodically:

| Metric | Description |
| ------ | ----------- |
| **FPS (Current/Average)** | Real-time and average frames per second |
| **Latency** | Min, max, average, P95, and P99 frame processing time (ms) |
| **Memory (RSS/VMS)** | Resident Set Size and Virtual Memory Size in MB |
| **CPU Usage** | Process CPU utilization percentage |
| **Cache Stats** | Hit/miss counts and hit rate for internal data lookups |
| **Uptime** | Total runtime in seconds |

### Example Output

```text
📊 Receiving Inference Metadata via ZeroMQ (Ctrl+C to stop)...
📊 Subscribed to tcp://localhost:5555
============================================================
📊 Analytics Summary (over 10.0 seconds)
  Frames processed: 250
  Processing rate: 25.00 FPS
  Total objects tracked: 15
  Avg objects per frame: 2.34
  Class distribution:
    Class 0: 120 detections
    Class 1: 85 detections
    Class 2: 45 detections
============================================================
```

### Debug Output

With `--debug` enabled, you'll see detailed per-detection logs:

```text
[Source 0] Frame 1: 3 objects
  └─ Track ID 234 (class_id=0) | Confidence: 0.92 | Frame: 1
  └─ Track ID 456 (class_id=1) | Confidence: 0.88 | Frame: 1
  └─ Track ID 789 (class_id=2) | Confidence: 0.95 | Frame: 1
[Source 0] Frame 2: 3 objects
  └─ Track ID 234 (class_id=0) | Confidence: 0.91 | Frame: 2
  └─ Track ID 456 (class_id=1) | Confidence: 0.89 | Frame: 2
  └─ Track ID 789 (class_id=2) | Confidence: 0.94 | Frame: 2
```

### Performance Metrics Output

With `--metrics` enabled, you'll see periodic performance metrics summaries:

```text
🔍 Performance Metrics
----------------------------------------------------------------------
FPS: Current=25.30 | Average=24.95
Latency (ms): Min=35.42 | Avg=38.64 | P95=42.15 | P99=45.33 | Max=52.18
Memory: RSS=156.32 MB | VMS=2841.64 MB
CPU Usage: 18.50%
Cache: Hits=1245 | Misses=89 | Hit Rate=93.34%
Summary: 250 frames in 10.0 seconds
======================================================================
```

## Metadata Format

The analytics receiver expects metadata in the following format (published by inference):

```python
{
    source_id: [
        {
            "uri": "rtsp://camera/stream",
            "class_id": 0,
            "track_id": 123,
            "confidence": 0.95,
            "bbox": {
                "left": 100.0,
                "top": 150.0,
                "width": 50.0,
                "height": 100.0,
                "border_width": 0,
                "has_bg_color": 0
            },
            "frame_num": 42
        },
        # ... more detections
    ]
}
```

## Use Cases

### 1. Performance Monitoring

Monitor the processing rate to ensure the system can keep up with the inference stream:

```bash
uv run dummy_yolo analytics
```

Watch for the "Processing rate" metric to stay close to the configured FPS.

### 2. Object Tracking Analysis

Analyze how many unique objects are being tracked over time:

```bash
uv run dummy_yolo --debug analytics
```

Look at "Total objects tracked" to understand scene occupancy.

### 3. Performance Analysis

Monitor detailed performance metrics including latency, memory, and CPU:

```bash
uv run dummy_yolo --metrics analytics
```

Use this to identify bottlenecks and monitor resource consumption over time.

### 4. Class Distribution Analysis

Understand which object classes are most commonly detected:

```bash
uv run dummy_yolo analytics
```

Review the "Class distribution" section in periodic summaries.

### 5. System Integration Testing

Verify end-to-end data flow from inference to analytics:

```bash
# Terminal 1
uv run dummy_yolo inference

# Terminal 2
uv run dummy_yolo --debug analytics
```

Confirm that detections from inference appear in analytics logs.

## Stopping Analytics

Press `Ctrl+C` to gracefully stop the analytics receiver. A final summary will be displayed:

```text
^C🛑 Analytics stopped by user.

📊 Final Analytics Summary
  Total frames: 1250
  Total objects: 2925
  Unique tracks: 42
  Avg objects per frame: 2.34
```

## Troubleshooting

### No data being received

**Problem:** Analytics starts but shows no activity.

**Solutions:**

1. Verify inference service is running: `ps aux | grep python`
2. Check that both services use the same port in `config.toml`
3. Ensure no firewall is blocking localhost connections
4. Check that inference is publishing: enable `--debug` on inference

### Slow processing rate

**Problem:** Processing FPS is lower than expected.

**Solutions:**

1. Check system CPU usage: `top` or `htop`
2. Reduce `fps_check_interval_sec` to see more frequent updates
3. Enable `--debug` to see if logging is the bottleneck
4. Consider the overhead of JSON parsing for large detection sets

### Port already in use

**Problem:** Cannot start analytics because port is busy.

**Solutions:**

1. Find the process: `lsof -i :5555`
2. Kill the process or use a different port in `config.toml`
3. Stop the inference service first, then restart both

## API Reference

### `receive_and_analyze_metadata(port, analytics_interval_sec)`

Main function that subscribes to ZeroMQ and analyzes metadata.

**Parameters:**

- `port` (int): ZeroMQ subscriber port (default: 5555)
- `analytics_interval_sec` (int): Seconds between summary logs (default: 10)

**Returns:** None (runs until interrupted)

### `log_analytics_summary(...)`

Logs periodic analytics summary with current metrics.

### `log_final_summary(...)`

Logs final aggregate statistics on exit.
