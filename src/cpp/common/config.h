#pragma once
#include <string>
#include <utility>

struct AnalyticsConfig {
  int max_sources;
  int max_detections;
};

struct ZmqConfig {
  std::string endpoint;
  std::string socket_type;
  std::string subscribe;
  int rcvhwm;
};

struct Config {
  AnalyticsConfig analytics;
  ZmqConfig zmq;
};

Config load_config(const std::string &path);
