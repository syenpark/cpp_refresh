"""Analyze YOLO model outputs."""

from __future__ import annotations

import json
import logging
import time
from collections import defaultdict

import zmq

from python.yolo.analytics.metrics import PerformanceMetrics

logger = logging.getLogger(__name__)

# ruff: noqa: PLR0913 - All params needed for analytics


def log_analytics_summary(
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


def process_detections(
    detections: list,
    unique_track_ids: set,
    class_counts: defaultdict[int, int],
    metrics: PerformanceMetrics | None,
    track_id_cache: set[int],
) -> int:
    """Process detections from a single frame.

    Args:
        detections: List of detection dictionaries
        unique_track_ids: Set to track unique track IDs
        class_counts: Counter for class distribution
        metrics: Optional performance metrics tracker
        track_id_cache: Cache for tracking seen track IDs

    Returns:
        Number of objects in the frame
    """
    for detection in detections:
        track_id = detection["track_id"]
        class_id = detection["class_id"]

        # Track cache hits/misses for track IDs
        if metrics:
            if track_id in track_id_cache:
                metrics.record_cache_hit()
            else:
                metrics.record_cache_miss()
                track_id_cache.add(track_id)

        unique_track_ids.add(track_id)
        class_counts[class_id] += 1

        logger.debug(
            "  └─ Track ID %s (class_id=%s) | Confidence: %.2f | Frame: %s",
            track_id,
            class_id,
            detection["confidence"],
            detection["frame_num"],
        )

    return len(detections)


def should_log_summary(
    current_time: float, interval_start: float, interval_sec: int
) -> bool:
    """Check if it's time to log the analytics summary.

    Args:
        current_time: Current timestamp
        interval_start: Start time of the interval
        interval_sec: Interval duration in seconds

    Returns:
        True if summary should be logged
    """
    return (current_time - interval_start) >= interval_sec


def receive_and_analyze_metadata(
    port: int = 5555,
    analytics_interval_sec: int = 10,
    *,
    enable_performance_metrics: bool = False,
) -> None:
    """Receive inference metadata via ZeroMQ and perform analytics.

    Args:
        port: ZeroMQ subscriber port to connect to
        analytics_interval_sec: Interval in seconds to log analytics summary
        enable_performance_metrics: Enable detailed performance metrics tracking
    """
    # Initialize ZeroMQ SUB socket
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect(f"tcp://localhost:{port}")

    # Subscribe to inference topic
    socket.setsockopt_string(zmq.SUBSCRIBE, "inference")

    logger.info("📊 Receiving Inference Metadata via ZeroMQ (Ctrl+C to stop)...")
    logger.info("📊 Subscribed to tcp://localhost:%s", port)

    if enable_performance_metrics:
        logger.info("🔍 Performance metrics enabled")

    # Analytics tracking
    frame_count = 0
    total_objects = 0
    unique_track_ids = set()
    class_counts: defaultdict[int, int] = defaultdict(int)
    interval_start_time = time.time()
    interval_frame_count = 0

    # Performance metrics
    metrics = PerformanceMetrics() if enable_performance_metrics else None

    # Track cache for seen track IDs (for cache hit/miss tracking)
    track_id_cache: set[int] = set()

    try:
        while True:
            if metrics:
                metrics.start_frame()

            # Receive message
            _topic, message = socket.recv_multipart()
            metadata = json.loads(message.decode("utf-8"))

            # Process metadata: {source_id: [detection1, detection2, ...]}
            for source_id, detections in metadata.items():
                frame_count += 1
                interval_frame_count += 1

                logger.debug(
                    "[Source %s] Frame %d: %d objects",
                    source_id,
                    frame_count,
                    len(detections),
                )

                # Process all detections
                frame_object_count = process_detections(
                    detections,
                    unique_track_ids,
                    class_counts,
                    metrics,
                    track_id_cache,
                )
                total_objects += frame_object_count

            if metrics:
                metrics.end_frame()

            # Log analytics summary periodically
            current_time = time.time()

            if should_log_summary(
                current_time, interval_start_time, analytics_interval_sec
            ):
                elapsed = current_time - interval_start_time

                log_analytics_summary(
                    elapsed,
                    interval_frame_count,
                    frame_count,
                    total_objects,
                    unique_track_ids,
                    class_counts,
                )

                # Log performance metrics if enabled
                if metrics:
                    metrics.log_metrics()

                # Reset interval counters
                interval_start_time = current_time
                interval_frame_count = 0

    except KeyboardInterrupt:
        logger.info("🛑 Analytics stopped by user.")
    finally:
        # Final summary
        log_final_summary(frame_count, total_objects, unique_track_ids)

        # Final performance metrics
        if metrics:
            logger.info("")
            logger.info("🔍 Final Performance Metrics")
            metrics.log_metrics()

        socket.close()
        context.term()
