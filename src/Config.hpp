#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ovenbirdbt {

struct LibtorrentConfig {
  std::optional<std::string> user_agent;
  std::optional<bool> enable_dht;
  std::optional<bool> enable_lsd;
  std::optional<bool> enable_upnp;
  std::optional<bool> enable_natpmp;
  std::optional<std::string> i2p_hostname;
  std::optional<int> i2p_port;
  std::optional<int> tracker_completion_timeout;
  std::optional<int> tracker_receive_timeout;
  std::optional<int> stop_tracker_timeout;
  std::optional<int> peer_connect_timeout;
  std::optional<int> request_timeout;
  std::optional<std::string> listen_interfaces;
  std::optional<int> upload_rate_limit;
  std::optional<int> download_rate_limit;
  std::optional<std::vector<std::string>> additional_trackers;
};

struct ClientConfig {
  std::filesystem::path default_save_path;
  std::filesystem::path state_path;
  LibtorrentConfig libtorrent;
};

std::filesystem::path defaultConfigPath();
ClientConfig loadConfig(std::filesystem::path const& config_path, bool require_exists);

}  // namespace ovenbirdbt

