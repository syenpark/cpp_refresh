#pragma once
#include <string>
#include <utility>

struct AnalyticsConfig {
  [[maybe_unused]] int max_sources;
  [[maybe_unused]] int max_detections;
};

struct ZmqConfig {
  [[maybe_unused]] std::string endpoint;
  [[maybe_unused]] std::string socket_type;
  [[maybe_unused]] std::string subscribe;
  [[maybe_unused]] int rcvhwm;
};

struct Config {
  [[maybe_unused]] AnalyticsConfig analytics;
  [[maybe_unused]] ZmqConfig zmq;
};

Config load_config(const std::string &path);
