#include <chrono>
#include <iostream>
#include <string>
#include <utility>

#include "common/config.h"
#include "include/rapidjson.hpp"
#include <zmq.hpp>

void parse_metadata(const zmq::message_t &payload) {
  rapidjson::Document doc;

  doc.Parse(static_cast<const char *>(payload.data()), payload.size());

  if (doc.HasParseError() || !doc.IsObject()) {
    return;
  }

  std::cout << "Parsed payload.size()=" << payload.size() << "\n";

  // Iterate through the JSON object and print track_id of each detection
  for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
    const auto &detections = it->value;

    if (!detections.IsArray()) {
      std::cerr << "Detections is not an array\n";
      continue;
    }

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

  while (true) {
    // I/O blocking recv
    if (!socket.recv(topic, zmq::recv_flags::none)) {
      std::cerr << "Failed to receive topic\n";
      break;
    }

    if (socket.recv(payload, zmq::recv_flags::none)) {
      std::cout << "Received size=" << payload.size() << "\n";
      parse_metadata(payload);
    } else {
      std::cout << "Failed to receive message\n";
      break;
    }
  }
  return 0;
}
