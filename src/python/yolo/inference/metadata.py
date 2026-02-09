"""Simulated YOLO Inference Metadata Generator for Live Tracking."""

from __future__ import annotations

import json
import logging
import random
import time
from typing import TYPE_CHECKING
from typing import Any

import zmq

if TYPE_CHECKING:
    from collections.abc import Generator

logger = logging.getLogger(__name__)

# ruff: noqa: S311 - Allow use of random for simulation purposes


def rect_params_to_dict(left: float, top: float, width: float, height: float) -> dict:
    """Convert rectangle parameters to JSON serializable dict."""
    return {
        "left": left,
        "top": top,
        "width": width,
        "height": height,
        "border_width": 0,
        "has_bg_color": 0,
    }


def format_obj_meta(  # noqa: PLR0913 - Mimicking DeepStream API structure
    uri: str,
    frame_num: int,
    track_id: int,
    class_id: int,
    confidence: float,
    bbox: dict,
) -> dict:
    """Format object metadata to json to send.

    Mimics DeepStream NvDsObjectMeta structure.
    """
    return {
        "uri": uri,
        "class_id": class_id,
        "track_id": track_id,
        "confidence": confidence,
        "bbox": bbox,
        "frame_num": frame_num,
    }


def send_metadata(
    socket: zmq.Socket,
    source_id: int,
    frame_objects: list[dict[str, Any]],
) -> None:
    """Send metadata via ZeroMQ (PUB socket)."""
    metadata = {source_id: frame_objects}
    socket.send_multipart([b"inference", json.dumps(metadata).encode("utf-8")])


def live_stream_tracker_simulation(
    fps: int = 30,
    uri: str = "rtsp://camera/stream",
    new_object_probability: float = 0.1,
    object_exit_probability: float = 0.05,
) -> Generator[dict[str, Any]]:
    """Infinite generator simulating a YOLO live tracking stream.

    Uses DeepStream-like metadata format.
    """
    # Active tracks: {track_id: [x, y, w, h, class_id]}
    active_tracks = {}
    frame_count = 0

    while True:
        frame_count += 1
        start_time = time.perf_counter()

        # 1. Randomly add a new object (simulating someone entering the scene)
        if random.random() < new_object_probability:
            new_id = random.randint(100, 999)
            active_tracks[new_id] = [
                random.randint(0, 500),
                random.randint(0, 500),
                50,
                100,  # w, h
                random.randint(0, 2),
            ]

        # 2. Update existing tracks & remove those that "leave"
        frame_detections = []
        to_remove = []

        for tid, coords in active_tracks.items():
            # Simulate slight movement
            coords[0] += random.randint(-3, 3)
            coords[1] += random.randint(-3, 3)

            # Format using DeepStream-like structure
            bbox = rect_params_to_dict(
                left=coords[0],
                top=coords[1],
                width=coords[2],
                height=coords[3],
            )

            obj_meta = format_obj_meta(
                uri=uri,
                frame_num=frame_count,
                track_id=tid,
                class_id=coords[4],
                confidence=round(random.uniform(0.8, 0.98), 2),
                bbox=bbox,
            )

            frame_detections.append(obj_meta)

            # 5% chance an object leaves the frame
            if random.random() < object_exit_probability:
                to_remove.append(tid)

        for tid in to_remove:
            del active_tracks[tid]

        # 3. Simulate real-world FPS timing
        elapsed = time.perf_counter() - start_time
        time.sleep(max(0, (1 / fps) - elapsed))

        yield {
            "timestamp": time.time(),
            "frame_num": frame_count,
            "detections": frame_detections,
            "active_count": len(active_tracks),
        }


def run_simulation(
    config_data: dict[str, Any],
) -> None:
    """Run the live stream tracker simulation and send via ZeroMQ."""
    # Extract configuration parameters
    fps = config_data["stream"]["fps"]
    source_id = config_data["stream"].get("source_id", 0)
    uri = config_data["stream"].get("uri", "rtsp://camera/stream")
    port = config_data["zmq"].get("port", 5555)
    fps_check_interval_sec = config_data["stream"].get("fps_interval_sec", 10)
    new_object_probability = config_data["simulation"].get(
        "new_object_probability", 0.1
    )
    object_exit_probability = config_data["simulation"].get(
        "object_exit_probability", 0.05
    )

    # Initialize ZeroMQ PUB socket
    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    socket.bind(f"tcp://*:{port}")

    logger.info("📡 Sending Live Stream Metadata via ZeroMQ (Ctrl+C to stop)...")
    logger.info("📡 Publishing on tcp://*:%s", port)

    # FPS tracking
    interval_start_time = time.time()
    interval_frame_count = 0

    try:
        for metadata in live_stream_tracker_simulation(
            fps=fps,
            uri=uri,
            new_object_probability=new_object_probability,
            object_exit_probability=object_exit_probability,
        ):
            # Send metadata via ZeroMQ
            send_metadata(
                socket=socket,
                source_id=source_id,
                frame_objects=metadata["detections"],
            )

            # Track FPS
            interval_frame_count += 1
            current_time = time.time()
            elapsed = current_time - interval_start_time

            # Log average FPS every 10 seconds
            if elapsed >= fps_check_interval_sec:
                avg_fps = interval_frame_count / elapsed
                logger.info(
                    "📊 Average FPS: %.2f/%02d (over %.1f seconds)",
                    avg_fps,
                    fps,
                    elapsed,
                )
                # Reset counters
                interval_start_time = current_time
                interval_frame_count = 0

            logger.debug(
                "[Frame %s] Active Objects: %s",
                metadata["frame_num"],
                metadata["active_count"],
            )
            for det in metadata["detections"]:
                logger.debug(
                    "  └─ ID %s (class_id=%s) | Confidence: %.2f | BBox: %s",
                    det["track_id"],
                    det["class_id"],
                    det["confidence"],
                    det["bbox"],
                )
    except KeyboardInterrupt:
        logger.info("🛑 Stream stopped by user.")
    finally:
        socket.close()
        context.term()
