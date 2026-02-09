#include <iostream>
#include <string>
#include <utility>

#include "common/config.h"

int main(int argc, char **argv) {
  std::string config_path = "config.toml";

  if (argc > 1) {
    config_path = argv[1];
  }
  Config cfg = load_config(config_path);

  std::cout << "[config]\n";
  std::cout << "  max_sources: " << cfg.analytics.max_sources << "\n";
  std::cout << "  max_detections: " << cfg.analytics.max_detections << "\n";
  std::cout << "  zmq endpoint: " << cfg.zmq.endpoint << "\n";

  return 0;
}
