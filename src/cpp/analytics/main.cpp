#include <iostream>
#include <string>
#include <utility>

#include <zmq.hpp>

#include "common/config.h"

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
  auto result = socket.recv(msg, zmq::recv_flags::none);

  if (result) {
    std::cout << "Received message size=" << msg.size() << "\n";
  } else {
    std::cout << "Failed to receive message\n";
  }

  return 0;
}
