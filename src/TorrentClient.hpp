#pragma once

#include "Config.hpp"

#include <cstdint>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>

namespace ovenbirdbt {

struct ClientActionResult {
  bool ok = false;
  std::string message;
};

struct TorrentFileSnapshot {
  int index = 0;
  std::string path;
  std::int64_t size = 0;
  std::int64_t done_bytes = 0;
  bool wanted = true;
};

struct TorrentSnapshot {
  int id = 0;
  std::string name;
  std::string source;
  std::string save_path;
  std::string state;
  std::string error;
  std::string current_tracker;
  std::string info_hash;
  std::string storage_mode;
  float progress = 0.0F;
  std::int64_t done_bytes = 0;
  std::int64_t wanted_bytes = 0;
  std::int64_t total_done_bytes = 0;
  std::int64_t total_bytes = 0;
  std::int64_t session_download_bytes = 0;
  std::int64_t session_upload_bytes = 0;
  std::int64_t session_payload_download_bytes = 0;
  std::int64_t session_payload_upload_bytes = 0;
  std::int64_t all_time_download_bytes = 0;
  std::int64_t all_time_upload_bytes = 0;
  std::int64_t failed_bytes = 0;
  std::int64_t redundant_bytes = 0;
  int download_rate = 0;
  int upload_rate = 0;
  int download_payload_rate = 0;
  int upload_payload_rate = 0;
  int peers = 0;
  int seeds = 0;
  int known_peers = 0;
  int known_seeds = 0;
  int tracker_complete = -1;
  int tracker_incomplete = -1;
  int connect_candidates = 0;
  int uploads = 0;
  int connections = 0;
  int uploads_limit = 0;
  int connections_limit = 0;
  int up_bandwidth_queue = 0;
  int down_bandwidth_queue = 0;
  int queue_position = -1;
  int finished_pieces = 0;
  int total_pieces = 0;
  int piece_length = 0;
  int block_size = 0;
  int file_count = 0;
  int distributed_full_copies = -1;
  int distributed_fraction = 0;
  float distributed_copies = 0.0F;
  std::int64_t active_seconds = 0;
  std::int64_t finished_seconds = 0;
  std::int64_t seeding_seconds = 0;
  std::time_t added_time = 0;
  std::time_t completed_time = 0;
  std::time_t last_seen_complete = 0;
  bool paused = false;
  bool has_metadata = false;
  bool has_error = false;
  bool is_finished = false;
  bool is_seeding = false;
  bool need_save_resume = false;
  bool resume_pending = false;
  bool has_incoming = false;
  bool moving_storage = false;
  bool announcing_to_trackers = false;
  bool announcing_to_lsd = false;
  bool announcing_to_dht = false;
  std::vector<TorrentFileSnapshot> files;
};

class TorrentClient {
 public:
  explicit TorrentClient(ClientConfig config);
  TorrentClient(TorrentClient const&) = delete;
  TorrentClient& operator=(TorrentClient const&) = delete;
  ~TorrentClient();

  ClientActionResult addTorrent(std::string source, std::filesystem::path save_path = {});
  ClientActionResult pauseTorrent(int id);
  ClientActionResult resumeTorrent(int id);
  ClientActionResult removeTorrent(int id);
  ClientActionResult setFileWanted(int id, int file_index, bool wanted);
  ClientActionResult setAllFilesWanted(int id, bool wanted);

  std::vector<TorrentSnapshot> snapshots(bool include_file_details = false,
                                         bool include_all_file_details = false,
                                         int file_details_torrent_id = 0);
  std::vector<std::string> recentMessages(std::size_t count) const;
  void pumpAlerts();
  void saveStateForShutdown(std::chrono::milliseconds timeout = std::chrono::seconds(5));

  std::filesystem::path const& defaultSavePath() const noexcept;
  std::filesystem::path const& statePath() const noexcept;

 private:
  struct TorrentEntry {
    int id = 0;
    libtorrent::torrent_handle handle;
    std::string source;
    std::filesystem::path resume_file;
    bool resume_pending = false;
  };

  libtorrent::torrent_handle findHandleLocked(int id) const;
  std::filesystem::path resumeDirectory() const;
  std::filesystem::path makeResumeFilePath(int id) const;
  void ensureStateDirectories();
  void loadSavedTorrents();
  void writeInitialResumeData(libtorrent::add_torrent_params const& params,
                              std::filesystem::path const& resume_file);
  void requestResumeDataForEntryLocked(TorrentEntry& entry, bool only_if_modified);
  void requestResumeDataForHandle(libtorrent::torrent_handle const& handle,
                                  bool only_if_modified);
  std::size_t requestAllResumeData(bool only_if_modified);
  std::size_t pendingResumeCount() const;
  void processAlert(libtorrent::alert const* alert);
  bool storeResumeData(libtorrent::torrent_handle const& handle,
                       libtorrent::add_torrent_params const& params);
  bool markResumeFailed(libtorrent::torrent_handle const& handle);
  void pushMessage(std::string message);

  ClientConfig config_;
  libtorrent::session session_;
  mutable std::mutex mutex_;
  std::vector<TorrentEntry> torrents_;
  std::deque<std::string> messages_;
  int next_id_ = 1;
};

}  // namespace ovenbirdbt

