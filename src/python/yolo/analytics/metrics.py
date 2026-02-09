"""Performance metrics for analytics evaluation."""

from __future__ import annotations

import logging
import time
from collections import deque
from typing import Any

import psutil

logger = logging.getLogger(__name__)


class PerformanceMetrics:
    """Track performance metrics for analytics processing.

    Metrics tracked:
    - FPS (frames per second as throughput)
    - Memory usage (RSS, VMS)
    - CPU usage (percentage)
    - Latency (processing time per frame)
    - Cache hits/misses (for tracking data lookups)
    """

    def __init__(self, window_size: int = 100) -> None:
        """Initialize performance metrics tracker.

        Args:
            window_size: Number of recent samples to keep for rolling statistics
        """
        self.window_size = window_size

        # FPS tracking
        self.frame_times: deque[float] = deque(maxlen=window_size)
        self.total_frames = 0

        # Latency tracking
        self.latencies: deque[float] = deque(maxlen=window_size)
        self.frame_start_time: float | None = None

        # Cache tracking
        self.cache_hits = 0
        self.cache_misses = 0

        # Process handle for CPU/Memory
        self.process = psutil.Process()

        # Timing
        self.start_time = time.time()
        self.last_frame_time = time.time()

    def start_frame(self) -> None:
        """Mark the start of frame processing."""
        self.frame_start_time = time.time()

    def end_frame(self) -> None:
        """Mark the end of frame processing and update metrics."""
        if self.frame_start_time is None:
            return

        current_time = time.time()

        # Calculate latency for this frame
        latency = current_time - self.frame_start_time
        self.latencies.append(latency)

        # Calculate time since last frame for FPS
        frame_interval = current_time - self.last_frame_time
        self.frame_times.append(frame_interval)
        self.last_frame_time = current_time

        self.total_frames += 1
        self.frame_start_time = None

    def record_cache_hit(self) -> None:
        """Record a cache hit."""
        self.cache_hits += 1

    def record_cache_miss(self) -> None:
        """Record a cache miss."""
        self.cache_misses += 1

    def get_fps(self) -> float:
        """Get current FPS based on recent frame times.

        Returns:
            Current FPS (frames per second)
        """
        if not self.frame_times:
            return 0.0

        avg_frame_time = sum(self.frame_times) / len(self.frame_times)
        return 1.0 / avg_frame_time if avg_frame_time > 0 else 0.0

    def get_average_fps(self) -> float:
        """Get average FPS over entire runtime.

        Returns:
            Average FPS since start
        """
        elapsed = time.time() - self.start_time
        return self.total_frames / elapsed if elapsed > 0 else 0.0

    def get_latency_stats(self) -> dict[str, float]:
        """Get latency statistics.

        Returns:
            Dictionary with min, max, avg, p95, p99 latencies in milliseconds
        """
        if not self.latencies:
            return {
                "min_ms": 0.0,
                "max_ms": 0.0,
                "avg_ms": 0.0,
                "p95_ms": 0.0,
                "p99_ms": 0.0,
            }

        sorted_latencies = sorted(self.latencies)
        n = len(sorted_latencies)
        p95_idx = int(n * 0.95)
        p99_idx = int(n * 0.99)

        return {
            "min_ms": min(sorted_latencies) * 1000,
            "max_ms": max(sorted_latencies) * 1000,
            "avg_ms": (sum(sorted_latencies) / n) * 1000,
            "p95_ms": sorted_latencies[p95_idx] * 1000 if p95_idx < n else 0.0,
            "p99_ms": sorted_latencies[p99_idx] * 1000 if p99_idx < n else 0.0,
        }

    def get_memory_usage(self) -> dict[str, float]:
        """Get current memory usage.

        Returns:
            Dictionary with RSS and VMS in MB
        """
        mem_info = self.process.memory_info()
        return {
            "rss_mb": mem_info.rss / (1024 * 1024),  # Resident Set Size
            "vms_mb": mem_info.vms / (1024 * 1024),  # Virtual Memory Size
        }

    def get_cpu_usage(self) -> float:
        """Get CPU usage percentage.

        Returns:
            CPU usage as percentage (0-100)
        """
        return self.process.cpu_percent(interval=0.1)

    def get_cache_stats(self) -> dict[str, float]:
        """Get cache hit/miss statistics.

        Returns:
            Dictionary with hits, misses, total, and hit rate
        """
        total = self.cache_hits + self.cache_misses
        hit_rate = self.cache_hits / total if total > 0 else 0.0

        return {
            "hits": self.cache_hits,
            "misses": self.cache_misses,
            "total": total,
            "hit_rate": hit_rate,
        }

    def get_all_metrics(self) -> dict[str, Any]:
        """Get all metrics as a dictionary.

        Returns:
            Dictionary containing all current metrics
        """
        return {
            "fps": {
                "current": self.get_fps(),
                "average": self.get_average_fps(),
            },
            "latency": self.get_latency_stats(),
            "memory": self.get_memory_usage(),
            "cpu_percent": self.get_cpu_usage(),
            "cache": self.get_cache_stats(),
            "total_frames": self.total_frames,
            "uptime_seconds": time.time() - self.start_time,
        }

    def log_metrics(self) -> None:
        """Log all metrics at INFO level."""
        metrics = self.get_all_metrics()

        logger.info("=" * 70)
        logger.info("🔍 Performance Metrics")
        logger.info("-" * 70)

        # FPS
        fps = metrics["fps"]
        logger.info(
            "FPS: Current=%.2f | Average=%.2f",
            fps["current"],
            fps["average"],
        )

        # Latency
        latency = metrics["latency"]
        logger.info(
            "Latency (ms): Min=%.2f | Avg=%.2f | P95=%.2f | P99=%.2f | Max=%.2f",
            latency["min_ms"],
            latency["avg_ms"],
            latency["p95_ms"],
            latency["p99_ms"],
            latency["max_ms"],
        )

        # Memory
        memory = metrics["memory"]
        logger.info(
            "Memory: RSS=%.2f MB | VMS=%.2f MB",
            memory["rss_mb"],
            memory["vms_mb"],
        )

        # CPU
        logger.info("CPU Usage: %.2f%%", metrics["cpu_percent"])

        # Cache
        cache = metrics["cache"]
        logger.info(
            "Cache: Hits=%d | Misses=%d | Hit Rate=%.2f%%",
            cache["hits"],
            cache["misses"],
            cache["hit_rate"] * 100,
        )

        # Summary
        logger.info(
            "Summary: %d frames in %.1f seconds",
            metrics["total_frames"],
            metrics["uptime_seconds"],
        )
        logger.info("=" * 70)

    def reset(self) -> None:
        """Reset all metrics."""
        self.frame_times.clear()
        self.latencies.clear()
        self.total_frames = 0
        self.cache_hits = 0
        self.cache_misses = 0
        self.start_time = time.time()
        self.last_frame_time = time.time()
        self.frame_start_time = None
