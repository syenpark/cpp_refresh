#include <iostream>
#include <utility>

#include "common/config.h"

int main() {
  Config cfg = load_config("config.toml");

  std::cout << "max_sources: " << cfg.analytics.max_sources << "\n";
  std::cout << "zmq endpoint: " << cfg.zmq.endpoint << "\n";

  return 0;
}
