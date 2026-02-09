"""Analyze YOLO model outputs."""

from __future__ import annotations

import json
import logging
import time
from collections import defaultdict

import zmq

logger = logging.getLogger(__name__)


def log_analytics_summary(  # noqa: PLR0913 - All params needed for analytics
    elapsed: float,
    interval_frame_count: int,
    frame_count: int,
    total_objects: int,
    unique_track_ids: set,
    class_counts: dict,
) -> None:
    """Log periodic analytics summary.

    Args:
        elapsed: Time elapsed in the interval
        interval_frame_count: Number of frames in the interval
        frame_count: Total frames processed
        total_objects: Total objects detected
        unique_track_ids: Set of unique track IDs
        class_counts: Dictionary of class ID to detection count
    """
    avg_objects_per_frame = total_objects / frame_count if frame_count > 0 else 0
    processing_fps = interval_frame_count / elapsed

    logger.info("=" * 60)
    logger.info("📊 Analytics Summary (over %.1f seconds)", elapsed)
    logger.info("  Frames processed: %d", interval_frame_count)
    logger.info("  Processing rate: %.2f FPS", processing_fps)
    logger.info("  Total objects tracked: %d", len(unique_track_ids))
    logger.info("  Avg objects per frame: %.2f", avg_objects_per_frame)
    logger.info("  Class distribution:")
    for class_id, count in sorted(class_counts.items()):
        logger.info("    Class %d: %d detections", class_id, count)
    logger.info("=" * 60)


def log_final_summary(
    frame_count: int,
    total_objects: int,
    unique_track_ids: set,
) -> None:
    """Log final analytics summary on exit.

    Args:
        frame_count: Total frames processed
        total_objects: Total objects detected
        unique_track_ids: Set of unique track IDs
    """
    if frame_count > 0:
        avg_objects = total_objects / frame_count
        logger.info("")
        logger.info("📊 Final Analytics Summary")
        logger.info("  Total frames: %d", frame_count)
        logger.info("  Total objects: %d", total_objects)
        logger.info("  Unique tracks: %d", len(unique_track_ids))
        logger.info("  Avg objects per frame: %.2f", avg_objects)


def receive_and_analyze_metadata(
    port: int = 5555,
    analytics_interval_sec: int = 10,
) -> None:
    """Receive inference metadata via ZeroMQ and perform analytics.

    Args:
        port: ZeroMQ subscriber port to connect to
        analytics_interval_sec: Interval in seconds to log analytics summary
    """
    # Initialize ZeroMQ SUB socket
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect(f"tcp://localhost:{port}")

    # Subscribe to inference topic
    socket.setsockopt_string(zmq.SUBSCRIBE, "inference")

    logger.info("📊 Receiving Inference Metadata via ZeroMQ (Ctrl+C to stop)...")
    logger.info("📊 Subscribed to tcp://localhost:%s", port)

    # Analytics tracking
    frame_count = 0
    total_objects = 0
    unique_track_ids = set()
    class_counts: defaultdict[int, int] = defaultdict(int)
    interval_start_time = time.time()
    interval_frame_count = 0

    try:
        while True:
            # Receive message
            _topic, message = socket.recv_multipart()
            metadata = json.loads(message.decode("utf-8"))

            # Process metadata: {source_id: [detection1, detection2, ...]}
            for source_id, detections in metadata.items():
                frame_count += 1
                interval_frame_count += 1
                frame_object_count = len(detections)
                total_objects += frame_object_count

                logger.debug(
                    "[Source %s] Frame %d: %d objects",
                    source_id,
                    frame_count,
                    frame_object_count,
                )

                for detection in detections:
                    track_id = detection["track_id"]
                    class_id = detection["class_id"]
                    unique_track_ids.add(track_id)
                    class_counts[class_id] += 1

                    logger.debug(
                        "  └─ Track ID %s (class_id=%s) | Confidence: %.2f | Frame: %s",
                        track_id,
                        class_id,
                        detection["confidence"],
                        detection["frame_num"],
                    )

            # Log analytics summary periodically
            current_time = time.time()
            elapsed = current_time - interval_start_time

            if elapsed >= analytics_interval_sec:
                log_analytics_summary(
                    elapsed,
                    interval_frame_count,
                    frame_count,
                    total_objects,
                    unique_track_ids,
                    class_counts,
                )

                # Reset interval counters
                interval_start_time = current_time
                interval_frame_count = 0

    except KeyboardInterrupt:
        logger.info("🛑 Analytics stopped by user.")
    finally:
        # Final summary
        log_final_summary(frame_count, total_objects, unique_track_ids)

        socket.close()
        context.term()
