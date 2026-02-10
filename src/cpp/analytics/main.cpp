#include <rapidjson/document.h>

#include <chrono>
#include <iostream>
#include <string>
#include <utility>

#include "common/config.h"
#include <zmq.hpp>

void test_parse(const zmq::message_t &msg) {
  rapidjson::Document doc;

  doc.Parse(static_cast<const char *>(msg.data()), msg.size());

  std::cout << "Parsed msg.size()=" << msg.size() << "\n";

  if (!doc.IsObject()) {
    std::cerr << "Invalid JSON\n";
    return;
  }

  for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
    const auto &detections = it->value;
    for (auto &d : detections.GetArray()) {
      int track_id = d["track_id"].GetInt();
      (void)track_id;
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
  zmq::message_t msg;

  while (true) {
    // I/O blocking recv
    auto result = socket.recv(msg, zmq::recv_flags::none);

    if (result) {
      std::cout << "Received size=" << msg.size() << "\n";
      test_parse(msg);
    } else {
      std::cout << "Failed to receive message\n";
      break;
    }
  }
  return 0;
}
