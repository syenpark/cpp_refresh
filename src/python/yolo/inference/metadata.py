"""Simulated YOLO Inference Metadata Generator for Live Tracking."""

from __future__ import annotations

import logging
import random
import time
from typing import TYPE_CHECKING
from typing import Any

if TYPE_CHECKING:
    from collections.abc import Generator

logger = logging.getLogger(__name__)

# ruff: noqa: S311 - Allow use of random for simulation purposes
# Constants for simulation probabilities
NEW_OBJECT_PROBABILITY = 0.1
OBJECT_EXIT_PROBABILITY = 0.05


def live_stream_tracker_simulation(fps: int = 30) -> Generator[dict[str, Any]]:
    """Infinite generator simulating a YOLO live tracking stream."""
    # Active tracks: {track_id: [x, y, w, h, class_id]}
    active_tracks = {}
    frame_count = 0
    labels = ["person", "bicycle", "car"]

    while True:
        frame_count += 1
        start_time = time.perf_counter()

        # 1. Randomly add a new object (simulating someone entering the scene)
        if random.random() < NEW_OBJECT_PROBABILITY:
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

            # Package into YOLO inference metadata format
            frame_detections.append(
                {
                    "track_id": tid,
                    "box_xyxy": [
                        coords[0],
                        coords[1],
                        coords[0] + coords[2],
                        coords[1] + coords[3],
                    ],
                    "conf": round(random.uniform(0.8, 0.98), 2),
                    "label": labels[coords[4]],
                }
            )

            # 5% chance an object leaves the frame
            if random.random() < OBJECT_EXIT_PROBABILITY:
                to_remove.append(tid)

        for tid in to_remove:
            del active_tracks[tid]

        # 3. Simulate real-world FPS timing
        elapsed = time.perf_counter() - start_time
        time.sleep(max(0, (1 / fps) - elapsed))

        yield {
            "timestamp": time.time(),
            "frame_id": frame_count,
            "detections": frame_detections,
            "active_count": len(active_tracks),
        }


def run_simulation(fps: int = 30) -> None:
    """Run the live stream tracker simulation."""
    # --- Consumer Loop (The 'Live' System) ---
    logger.info("📡 Receiving Live Stream Metadata (Ctrl+C to stop)...")

    try:
        for metadata in live_stream_tracker_simulation(fps=fps):
            logger.debug(
                "[%s] Active Objects: %s",
                metadata["frame_id"],
                metadata["active_count"],
            )
            for det in metadata["detections"]:
                logger.debug(
                    "  └─ ID %s: %s | Box: %s",
                    det["track_id"],
                    det["label"],
                    det["box_xyxy"],
                )
    except KeyboardInterrupt:
        logger.info("🛑 Stream stopped by user.")
