#include "common/config.h"

#include <iostream>
#include <string>

#include "toml++/toml.h"

Config load_config(const std::string &path) {
  Config cfg;

  try {
    auto tbl = toml::parse_file(path);

    cfg.analytics.max_sources = tbl["analytics"]["max_sources"].value_or(1);
    cfg.analytics.max_detections =
        tbl["analytics"]["max_detections"].value_or(16);

    cfg.zmq.endpoint = tbl["zmq"]["endpoint"].value_or("tcp://127.0.0.1:5555");
    cfg.zmq.socket_type = tbl["zmq"]["socket_type"].value_or("sub");
    cfg.zmq.subscribe = tbl["zmq"]["subscribe"].value_or("");
    cfg.zmq.rcvhwm = tbl["zmq"]["rcvhwm"].value_or(1000);
  } catch (const toml::parse_error &e) {
    std::cerr << "Failed to load config: " << path << "\n";
    std::cerr << e.description() << "\n";
    std::exit(1);
  }

  return cfg;
}
