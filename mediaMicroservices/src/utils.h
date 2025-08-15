#ifndef MEDIA_MICROSERVICES_UTILS_H
#define MEDIA_MICROSERVICES_UTILS_H

#include <string>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <nlohmann/json.hpp>

#include "logger.h"

namespace media_service{
using json = nlohmann::json;

int load_config_file(const std::string &file_name, json *config_json) {
  std::ifstream json_file;
  json_file.open(file_name);
  if (json_file.is_open()) {
    json_file >> *config_json;
    json_file.close();
    return 0;
  }
  else {
    LOG(error) << "Cannot open service-config.json";
    return -1;
  }
};

int ParseExtraLatency() {
  const char* extra_latency_env = std::getenv("EXTRA_LATENCY");
  if (extra_latency_env == nullptr) {
    return 0;
  }
  
  std::string latency_str(extra_latency_env);
  
  // Remove "ms" suffix if present
  if (latency_str.length() >= 2 && 
      latency_str.substr(latency_str.length() - 2) == "ms") {
    latency_str = latency_str.substr(0, latency_str.length() - 2);
  }
  
  try {
    int latency_ms = std::stoi(latency_str);
    if (latency_ms < 0) {
      LOG(warning) << "EXTRA_LATENCY cannot be negative, setting to 0";
      return 0;
    }
    LOG(info) << "EXTRA_LATENCY set to " << latency_ms << "ms";
    return latency_ms;
  } catch (const std::exception& e) {
    LOG(warning) << "Invalid EXTRA_LATENCY value: " << extra_latency_env 
                 << ", setting to 0";
    return 0;
  }
}

void ApplyExtraLatency(int extra_latency_ms) {
  if (extra_latency_ms > 0) {
    LOG(debug) << "Adding extra latency of " << extra_latency_ms << "ms";
    std::this_thread::sleep_for(std::chrono::milliseconds(extra_latency_ms));
  }
}

} //namespace media_service

#endif //MEDIA_MICROSERVICES_UTILS_H
