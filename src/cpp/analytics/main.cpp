#include <chrono>
#include <iostream>
#include <string>
#include <utility>

#include "common/config.h"
#include "include/rapidjson.hpp"
#include <zmq.hpp>

// ================= Metrics (Step 2) =================

struct NullMetrics {
  // cppcheck-suppress functionStatic
  inline void on_frame() {}
};

struct RealMetrics {
  uint64_t frames = 0;
  inline void on_frame() { frames++; }
};

#ifdef ENABLE_METRICS
using Metrics = RealMetrics;
#else
using Metrics = NullMetrics;
#endif

// ====================================================

void parse_metadata(const zmq::message_t &payload) {
  rapidjson::Document doc;

  doc.Parse(static_cast<const char *>(payload.data()), payload.size());

  if (doc.HasParseError() || !doc.IsObject()) {
    return;
  }

  // Iterate through the JSON object and print track_id of each detection
  for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
    const auto &detections = it->value;

    if (!detections.IsArray())
      continue;

    for (auto &det : detections.GetArray()) {
      int track_id = det["track_id"].GetInt();
      int class_id = det["class_id"].GetInt();

      (void)track_id;
      (void)class_id;
    }
  }
}

int main(int argc, char **argv) {
  // ---------- config ----------
  std::string config_path = "config.toml";

  if (argc > 1) {
    config_path = argv[1];
  }

  Config cfg = load_config(config_path);

  std::cout << "[config]\n";
  std::cout << "  max_sources: " << cfg.analytics.max_sources << "\n";
  std::cout << "  max_detections: " << cfg.analytics.max_detections << "\n";
  std::cout << "  zmq endpoint: " << cfg.zmq.endpoint << "\n";

  // ---------- zmq init ----------
  zmq::context_t ctx{1};

  zmq::socket_t socket(ctx, zmq::socket_type::sub);

  socket.set(zmq::sockopt::rcvhwm, cfg.zmq.rcvhwm);
  socket.set(zmq::sockopt::subscribe, cfg.zmq.subscribe);

  socket.connect(cfg.zmq.endpoint);

  std::cout << "Connected to " << cfg.zmq.endpoint << "\n";

  // ---------- recv test ----------
  zmq::message_t topic;
  zmq::message_t payload;

  Metrics metrics;
#ifdef ENABLE_METRICS
  auto start = std::chrono::steady_clock::now();
  auto last_report = start;
#endif

  while (true) {
    // I/O blocking recv
    if (!socket.recv(topic, zmq::recv_flags::none))
      break;
    if (!socket.recv(payload, zmq::recv_flags::none))
      break;

    // ---------- hot path ----------
    parse_metadata(payload);
    metrics.on_frame();
    // ------- end hot path ---------

#ifdef ENABLE_METRICS
    // ---------- cold path ----------
    auto now = std::chrono::steady_clock::now();
    if (now - last_report >= std::chrono::seconds(5)) {
      auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
              .count();
      double fps = metrics.frames * 1000.0 / elapsed;

      std::cerr << "[FPS] " << fps << "\n";
      last_report = now;
    }
    // ------- end cold path ---------
#endif
  }
  return 0;
}
