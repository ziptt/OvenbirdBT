#include "TorrentClient.hpp"

#include "PathUtil.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/config.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/time.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/write_resume_data.hpp>

namespace ovenbirdbt {
namespace lt = libtorrent;

namespace {

constexpr int kMaxSnapshotFiles = 50;

std::vector<std::string> const& defaultAdditionalTrackers() {
  static const std::vector<std::string> trackers{
      "http://tracker.tritan.gg:8080/announce",
      "udp://tracker.bluefrog.pw:2710/announce",
      "https://tracker.pmman.tech:443/announce",
  };
  return trackers;
}

std::vector<std::string> const& additionalTrackers(ClientConfig const& config) {
  if (config.libtorrent.additional_trackers) {
    return *config.libtorrent.additional_trackers;
  }
  return defaultAdditionalTrackers();
}

bool containsTracker(lt::add_torrent_params const& params, std::string const& tracker) {
  if (std::find(params.trackers.begin(), params.trackers.end(), tracker) !=
      params.trackers.end()) {
    return true;
  }

  if (params.ti) {
    auto const& torrent_trackers = params.ti->trackers();
    return std::any_of(torrent_trackers.begin(), torrent_trackers.end(),
                       [&tracker](lt::announce_entry const& entry) {
                         return entry.url == tracker;
                       });
  }

  return false;
}

int maxTrackerTier(lt::add_torrent_params const& params) {
  int max_tier = params.trackers.empty() ? -1 : 0;

  if (params.ti) {
    for (auto const& tracker : params.ti->trackers()) {
      max_tier = std::max(max_tier, static_cast<int>(tracker.tier));
    }
  }

  for (auto const tier : params.tracker_tiers) {
    max_tier = std::max(max_tier, tier);
  }

  return max_tier;
}

void appendAdditionalTrackers(lt::add_torrent_params& params, ClientConfig const& config) {
  auto const& trackers = additionalTrackers(config);
  if (trackers.empty()) {
    return;
  }

  auto has_tracker = [&params](std::string const& tracker) {
    return containsTracker(params, tracker);
  };
  if (std::all_of(trackers.begin(), trackers.end(), has_tracker)) {
    return;
  }

  int fill_tier = params.tracker_tiers.empty() ? 0 : params.tracker_tiers.back();
  while (params.tracker_tiers.size() < params.trackers.size()) {
    params.tracker_tiers.push_back(fill_tier);
  }

  int const additional_tier = maxTrackerTier(params) + 1;

  for (auto const& tracker : trackers) {
    if (tracker.empty() || has_tracker(tracker)) {
      continue;
    }
    params.trackers.push_back(tracker);
    params.tracker_tiers.push_back(additional_tier);
  }
}

lt::session_params makeSessionParams(ClientConfig const& config) {
  lt::session_params params;
  params.settings.set_str(lt::settings_pack::user_agent,
                          config.libtorrent.user_agent
                              ? *config.libtorrent.user_agent
                              : std::string("OvenbirdBT"));
  params.settings.set_int(lt::settings_pack::active_downloads, -1);
  params.settings.set_int(lt::settings_pack::active_seeds, -1);
  params.settings.set_int(lt::settings_pack::active_limit, -1);
  params.settings.set_int(lt::settings_pack::active_dht_limit, -1);
  params.settings.set_int(lt::settings_pack::active_tracker_limit, -1);
  params.settings.set_int(lt::settings_pack::active_lsd_limit, -1);
  params.settings.set_int(lt::settings_pack::alert_mask,
                          lt::alert_category::error | lt::alert_category::storage |
                              lt::alert_category::status | lt::alert_category::tracker);
  if (config.libtorrent.enable_dht) {
    params.settings.set_bool(lt::settings_pack::enable_dht, *config.libtorrent.enable_dht);
  }
  if (config.libtorrent.enable_lsd) {
    params.settings.set_bool(lt::settings_pack::enable_lsd, *config.libtorrent.enable_lsd);
  }
  if (config.libtorrent.enable_upnp) {
    params.settings.set_bool(lt::settings_pack::enable_upnp, *config.libtorrent.enable_upnp);
  }
  if (config.libtorrent.enable_natpmp) {
    params.settings.set_bool(lt::settings_pack::enable_natpmp,
                             *config.libtorrent.enable_natpmp);
  }
  if (config.libtorrent.i2p_hostname) {
    params.settings.set_str(lt::settings_pack::i2p_hostname,
                            *config.libtorrent.i2p_hostname);
  }
  if (config.libtorrent.i2p_port) {
    params.settings.set_int(lt::settings_pack::i2p_port, *config.libtorrent.i2p_port);
  }
  if (config.libtorrent.tracker_completion_timeout) {
    params.settings.set_int(lt::settings_pack::tracker_completion_timeout,
                            *config.libtorrent.tracker_completion_timeout);
  }
  if (config.libtorrent.tracker_receive_timeout) {
    params.settings.set_int(lt::settings_pack::tracker_receive_timeout,
                            *config.libtorrent.tracker_receive_timeout);
  }
  if (config.libtorrent.stop_tracker_timeout) {
    params.settings.set_int(lt::settings_pack::stop_tracker_timeout,
                            *config.libtorrent.stop_tracker_timeout);
  }
  if (config.libtorrent.peer_connect_timeout) {
    params.settings.set_int(lt::settings_pack::peer_connect_timeout,
                            *config.libtorrent.peer_connect_timeout);
  }
  if (config.libtorrent.request_timeout) {
    params.settings.set_int(lt::settings_pack::request_timeout,
                            *config.libtorrent.request_timeout);
  }
  if (config.libtorrent.listen_interfaces) {
    params.settings.set_str(lt::settings_pack::listen_interfaces,
                            *config.libtorrent.listen_interfaces);
  }
  if (config.libtorrent.upload_rate_limit) {
    params.settings.set_int(lt::settings_pack::upload_rate_limit,
                            *config.libtorrent.upload_rate_limit);
  }
  if (config.libtorrent.download_rate_limit) {
    params.settings.set_int(lt::settings_pack::download_rate_limit,
                            *config.libtorrent.download_rate_limit);
  }
  return params;
}

std::string trim(std::string value) {
  auto const first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  auto const last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
                      return std::isspace(ch) != 0;
                    }).base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

bool startsWithMagnet(std::string const& value) {
  constexpr char prefix[] = "magnet:?";
  if (value.size() < sizeof(prefix) - 1) {
    return false;
  }
  for (std::size_t index = 0; index < sizeof(prefix) - 1; ++index) {
    auto lhs = static_cast<unsigned char>(value[index]);
    auto rhs = static_cast<unsigned char>(prefix[index]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }
  return true;
}

std::string hexDigest(std::string const& bytes) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (unsigned char byte : bytes) {
    out << std::setw(2) << static_cast<int>(byte);
  }
  return out.str();
}

std::string infoHashText(lt::info_hash_t const& hashes) {
  std::ostringstream out;
  bool wrote_hash = false;

  if (hashes.has_v1()) {
    out << "v1 " << hexDigest(hashes.v1.to_string());
    wrote_hash = true;
  }
  if (hashes.has_v2()) {
    if (wrote_hash) {
      out << " / ";
    }
    out << "v2 " << hexDigest(hashes.v2.to_string());
    wrote_hash = true;
  }

  return wrote_hash ? out.str() : "unknown";
}

std::string storageModeName(lt::storage_mode_t mode) {
  switch (mode) {
    case lt::storage_mode_allocate:
      return "allocate";
    case lt::storage_mode_sparse:
      return "sparse";
  }
  return "unknown";
}

std::string stateName(lt::torrent_status const& status) {
  if (static_cast<bool>(status.flags & lt::torrent_flags::paused)) {
    return "paused";
  }

  switch (static_cast<int>(status.state)) {
    case 0:
      return "queued";
    case static_cast<int>(lt::torrent_status::checking_files):
      return "checking";
    case static_cast<int>(lt::torrent_status::downloading_metadata):
      return "metadata";
    case static_cast<int>(lt::torrent_status::downloading):
      return "downloading";
    case static_cast<int>(lt::torrent_status::finished):
      return "finished";
    case static_cast<int>(lt::torrent_status::seeding):
      return "seeding";
    case 6:
      return "allocating";
    case static_cast<int>(lt::torrent_status::checking_resume_data):
      return "resume";
  }

  return "unknown";
}

bool shouldLogAlert(lt::alert const* alert) {
  return lt::alert_cast<lt::add_torrent_alert>(alert) != nullptr ||
         lt::alert_cast<lt::file_error_alert>(alert) != nullptr ||
         lt::alert_cast<lt::listen_failed_alert>(alert) != nullptr ||
         lt::alert_cast<lt::metadata_failed_alert>(alert) != nullptr ||
         lt::alert_cast<lt::metadata_received_alert>(alert) != nullptr ||
         lt::alert_cast<lt::portmap_error_alert>(alert) != nullptr ||
         lt::alert_cast<lt::save_resume_data_failed_alert>(alert) != nullptr ||
         lt::alert_cast<lt::torrent_deleted_alert>(alert) != nullptr ||
         lt::alert_cast<lt::torrent_delete_failed_alert>(alert) != nullptr ||
         lt::alert_cast<lt::torrent_error_alert>(alert) != nullptr ||
         lt::alert_cast<lt::torrent_finished_alert>(alert) != nullptr ||
         lt::alert_cast<lt::torrent_paused_alert>(alert) != nullptr ||
         lt::alert_cast<lt::torrent_removed_alert>(alert) != nullptr ||
         lt::alert_cast<lt::torrent_resumed_alert>(alert) != nullptr ||
         lt::alert_cast<lt::tracker_error_alert>(alert) != nullptr;
}

std::vector<char> readBinaryFile(std::filesystem::path const& path, std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "could not open file";
    return {};
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool writeBinaryFileAtomically(std::filesystem::path const& path, std::vector<char> const& data,
                               std::string& error) {
  std::error_code fs_error;
  std::filesystem::create_directories(path.parent_path(), fs_error);
  if (fs_error) {
    error = fs_error.message();
    return false;
  }

  auto temp_path = path;
  temp_path += ".tmp";

  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "could not open temporary file";
      return false;
    }
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!output) {
      error = "could not write temporary file";
      return false;
    }
  }

  std::filesystem::rename(temp_path, path, fs_error);
  if (!fs_error) {
    return true;
  }

  std::filesystem::remove(path, fs_error);
  fs_error.clear();
  std::filesystem::rename(temp_path, path, fs_error);
  if (fs_error) {
    error = fs_error.message();
    std::filesystem::remove(temp_path);
    return false;
  }
  return true;
}

}  // namespace

TorrentClient::TorrentClient(ClientConfig config)
    : config_(std::move(config)), session_(makeSessionParams(config_)) {
  if (config_.default_save_path.empty()) {
    config_.default_save_path = defaultDownloadPath();
  }
  if (config_.state_path.empty()) {
    config_.state_path = defaultStatePath();
  }
  ensureStateDirectories();
  loadSavedTorrents();
}

TorrentClient::~TorrentClient() {
  try {
    saveStateForShutdown(std::chrono::seconds(3));
    for (auto const& entry : torrents_) {
      if (entry.handle.is_valid()) {
        entry.handle.flush_cache();
      }
    }
  } catch (...) {
  }
}

ClientActionResult TorrentClient::addTorrent(std::string source,
                                             std::filesystem::path save_path) {
  source = trim(std::move(source));
  if (source.empty()) {
    return {false, "Enter a magnet URI or .torrent path."};
  }

  if (save_path.empty()) {
    save_path = config_.default_save_path;
  }

  std::error_code fs_error;
  std::filesystem::create_directories(save_path, fs_error);
  if (fs_error) {
    return {false, "Could not create save path: " + fs_error.message()};
  }

  lt::add_torrent_params params;
  try {
    if (startsWithMagnet(source)) {
      lt::error_code parse_error;
      params = lt::parse_magnet_uri(source, parse_error);
      if (parse_error) {
        return {false, "Invalid magnet URI: " + parse_error.message()};
      }
    } else {
      std::filesystem::path torrent_path(source);
      if (!std::filesystem::exists(torrent_path, fs_error)) {
        return {false, "Torrent file does not exist."};
      }
      params = lt::load_torrent_file(pathToUtf8(torrent_path));
    }
  } catch (std::exception const& ex) {
    return {false, std::string("Could not read torrent: ") + ex.what()};
  }

  appendAdditionalTrackers(params, config_);
  params.save_path = pathToUtf8(save_path);
  params.flags |= lt::torrent_flags::duplicate_is_error;

  std::filesystem::path resume_file;
  int id = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = next_id_++;
    resume_file = makeResumeFilePath(id);
  }
  writeInitialResumeData(params, resume_file);

  lt::error_code add_error;
  auto handle = session_.add_torrent(std::move(params), add_error);
  if (add_error) {
    std::error_code remove_error;
    std::filesystem::remove(resume_file, remove_error);
    return {false, "Could not add torrent: " + add_error.message()};
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    torrents_.push_back({id, handle, source, resume_file});
  }

  std::ostringstream message;
  message << "Added torrent #" << id << '.';
  pushMessage(message.str());
  return {true, message.str()};
}

ClientActionResult TorrentClient::pauseTorrent(int id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto iter = std::find_if(torrents_.begin(), torrents_.end(), [id](TorrentEntry const& entry) {
    return entry.id == id;
  });
  if (iter == torrents_.end() || !iter->handle.is_valid()) {
    return {false, "No torrent is selected."};
  }

  try {
    iter->handle.unset_flags(lt::torrent_flags::auto_managed);
    iter->handle.pause();
    requestResumeDataForEntryLocked(*iter, false);
  } catch (std::exception const& ex) {
    return {false, std::string("Could not pause torrent: ") + ex.what()};
  }

  return {true, "Pause requested."};
}

ClientActionResult TorrentClient::resumeTorrent(int id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto iter = std::find_if(torrents_.begin(), torrents_.end(), [id](TorrentEntry const& entry) {
    return entry.id == id;
  });
  if (iter == torrents_.end() || !iter->handle.is_valid()) {
    return {false, "No torrent is selected."};
  }

  try {
    iter->handle.resume();
    requestResumeDataForEntryLocked(*iter, false);
  } catch (std::exception const& ex) {
    return {false, std::string("Could not resume torrent: ") + ex.what()};
  }

  return {true, "Resume requested."};
}

ClientActionResult TorrentClient::removeTorrent(int id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto iter = std::find_if(torrents_.begin(), torrents_.end(), [id](TorrentEntry const& entry) {
    return entry.id == id;
  });
  if (iter == torrents_.end() || !iter->handle.is_valid()) {
    return {false, "No torrent is selected."};
  }

  auto resume_file = iter->resume_file;
  try {
    session_.remove_torrent(iter->handle);
    torrents_.erase(iter);
  } catch (std::exception const& ex) {
    return {false, std::string("Could not remove torrent: ") + ex.what()};
  }

  std::error_code remove_error;
  std::filesystem::remove(resume_file, remove_error);
  return {true, "Torrent removed from session."};
}

ClientActionResult TorrentClient::setFileWanted(int id, int file_index, bool wanted) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto iter = std::find_if(torrents_.begin(), torrents_.end(), [id](TorrentEntry const& entry) {
    return entry.id == id;
  });
  if (iter == torrents_.end() || !iter->handle.is_valid()) {
    return {false, "No torrent is selected."};
  }

  try {
    auto status = iter->handle.status();
    auto torrent_file = status.torrent_file.lock();
    if (!torrent_file || !torrent_file->is_loaded()) {
      return {false, "File list is not available yet."};
    }
    if (file_index < 0 || file_index >= torrent_file->num_files()) {
      return {false, "No file is selected."};
    }

    iter->handle.file_priority(lt::file_index_t{file_index},
                               wanted ? lt::default_priority : lt::dont_download);
    requestResumeDataForEntryLocked(*iter, false);
  } catch (std::exception const& ex) {
    return {false, std::string("Could not update file selection: ") + ex.what()};
  }

  return {true, wanted ? "File selected for download." : "File deselected."};
}

ClientActionResult TorrentClient::setAllFilesWanted(int id, bool wanted) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto iter = std::find_if(torrents_.begin(), torrents_.end(), [id](TorrentEntry const& entry) {
    return entry.id == id;
  });
  if (iter == torrents_.end() || !iter->handle.is_valid()) {
    return {false, "No torrent is selected."};
  }

  try {
    auto status = iter->handle.status();
    auto torrent_file = status.torrent_file.lock();
    if (!torrent_file || !torrent_file->is_loaded()) {
      return {false, "File list is not available yet."};
    }

    std::vector<lt::download_priority_t> priorities(
        static_cast<std::size_t>(torrent_file->num_files()),
        wanted ? lt::default_priority : lt::dont_download);
    iter->handle.prioritize_files(priorities);
    requestResumeDataForEntryLocked(*iter, false);
  } catch (std::exception const& ex) {
    return {false, std::string("Could not update file selection: ") + ex.what()};
  }

  return {true, wanted ? "All files selected for download." : "All files deselected."};
}

std::vector<TorrentSnapshot> TorrentClient::snapshots(bool include_file_details,
                                                      bool include_all_file_details,
                                                      int file_details_torrent_id) {
  std::vector<TorrentEntry> entries;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    entries = torrents_;
  }

  std::vector<TorrentSnapshot> result;
  result.reserve(entries.size());

  for (auto const& entry : entries) {
    TorrentSnapshot snapshot;
    snapshot.id = entry.id;
    snapshot.source = entry.source;
    snapshot.resume_pending = entry.resume_pending;

    try {
      if (!entry.handle.is_valid()) {
        snapshot.name = "removed";
        snapshot.state = "removed";
        result.push_back(std::move(snapshot));
        continue;
      }

      auto status = entry.handle.status();
      snapshot.name = status.name.empty() ? entry.source : status.name;
      snapshot.save_path = status.save_path;
      snapshot.state = stateName(status);
      snapshot.current_tracker = status.current_tracker;
      snapshot.info_hash = infoHashText(status.info_hashes);
      snapshot.storage_mode = storageModeName(status.storage_mode);
      snapshot.progress = status.progress;
      snapshot.done_bytes = status.total_wanted_done;
      snapshot.wanted_bytes = status.total_wanted;
      snapshot.total_done_bytes = status.total_done;
      snapshot.total_bytes = status.total;
      snapshot.session_download_bytes = status.total_download;
      snapshot.session_upload_bytes = status.total_upload;
      snapshot.session_payload_download_bytes = status.total_payload_download;
      snapshot.session_payload_upload_bytes = status.total_payload_upload;
      snapshot.all_time_download_bytes = status.all_time_download;
      snapshot.all_time_upload_bytes = status.all_time_upload;
      snapshot.failed_bytes = status.total_failed_bytes;
      snapshot.redundant_bytes = status.total_redundant_bytes;
      snapshot.download_rate = status.download_rate;
      snapshot.upload_rate = status.upload_rate;
      snapshot.download_payload_rate = status.download_payload_rate;
      snapshot.upload_payload_rate = status.upload_payload_rate;
      snapshot.peers = status.num_peers;
      snapshot.seeds = status.num_seeds;
      snapshot.known_peers = status.list_peers;
      snapshot.known_seeds = status.list_seeds;
      snapshot.tracker_complete = status.num_complete;
      snapshot.tracker_incomplete = status.num_incomplete;
      snapshot.connect_candidates = status.connect_candidates;
      snapshot.uploads = status.num_uploads;
      snapshot.connections = status.num_connections;
      snapshot.uploads_limit = status.uploads_limit;
      snapshot.connections_limit = status.connections_limit;
      snapshot.up_bandwidth_queue = status.up_bandwidth_queue;
      snapshot.down_bandwidth_queue = status.down_bandwidth_queue;
      snapshot.queue_position = static_cast<int>(status.queue_position);
      snapshot.finished_pieces = status.num_pieces;
      snapshot.block_size = status.block_size;
      snapshot.distributed_full_copies = status.distributed_full_copies;
      snapshot.distributed_fraction = status.distributed_fraction;
      snapshot.distributed_copies = status.distributed_copies;
      snapshot.active_seconds = status.active_duration.count();
      snapshot.finished_seconds = status.finished_duration.count();
      snapshot.seeding_seconds = status.seeding_duration.count();
      snapshot.added_time = status.added_time;
      snapshot.completed_time = status.completed_time;
      snapshot.last_seen_complete = status.last_seen_complete;
      snapshot.paused = static_cast<bool>(status.flags & lt::torrent_flags::paused);
      snapshot.has_metadata = status.has_metadata;
      snapshot.is_finished = status.is_finished;
      snapshot.is_seeding = status.is_seeding;
      snapshot.need_save_resume = status.need_save_resume;
      snapshot.has_incoming = status.has_incoming;
      snapshot.moving_storage = status.moving_storage;
      snapshot.announcing_to_trackers = status.announcing_to_trackers;
      snapshot.announcing_to_lsd = status.announcing_to_lsd;
      snapshot.announcing_to_dht = status.announcing_to_dht;
      snapshot.has_error = static_cast<bool>(status.errc);
      if (snapshot.has_error) {
        snapshot.error = status.errc.message();
      }

      if (auto torrent_file = status.torrent_file.lock(); torrent_file && torrent_file->is_loaded()) {
        snapshot.file_count = torrent_file->num_files();
        snapshot.total_pieces = torrent_file->num_pieces();
        snapshot.piece_length = torrent_file->piece_length();
        snapshot.total_bytes = torrent_file->total_size();

        if (include_file_details &&
            (file_details_torrent_id == 0 || snapshot.id == file_details_torrent_id)) {
          auto progress = entry.handle.file_progress(lt::torrent_handle::piece_granularity);
          auto priorities = entry.handle.get_file_priorities();
          auto const& files = torrent_file->files();
          auto const file_limit =
              include_all_file_details ? snapshot.file_count
                                       : std::min(snapshot.file_count, kMaxSnapshotFiles);
          snapshot.files.reserve(static_cast<std::size_t>(file_limit));
          for (int file_index = 0; file_index < file_limit; ++file_index) {
            lt::file_index_t const index{file_index};
            TorrentFileSnapshot file;
            file.index = file_index;
            file.path = files.file_path(index);
            file.size = files.file_size(index);
            if (file_index < static_cast<int>(progress.size())) {
              file.done_bytes = progress[static_cast<std::size_t>(file_index)];
            }
            if (file_index < static_cast<int>(priorities.size())) {
              file.wanted = priorities[static_cast<std::size_t>(file_index)] != lt::dont_download;
            }
            snapshot.files.push_back(std::move(file));
          }
        }
      }
    } catch (std::exception const& ex) {
      snapshot.name = entry.source;
      snapshot.state = "error";
      snapshot.has_error = true;
      snapshot.error = ex.what();
    }

    result.push_back(std::move(snapshot));
  }

  return result;
}

std::vector<std::string> TorrentClient::recentMessages(std::size_t count) const {
  std::lock_guard<std::mutex> lock(mutex_);
  count = std::min(count, messages_.size());
  return {messages_.end() - static_cast<std::ptrdiff_t>(count), messages_.end()};
}

void TorrentClient::pumpAlerts() {
  std::vector<lt::alert*> alerts;
  session_.pop_alerts(&alerts);

  for (auto const* alert : alerts) {
    processAlert(alert);
  }
}

void TorrentClient::saveStateForShutdown(std::chrono::milliseconds timeout) {
  requestAllResumeData(false);

  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (pendingResumeCount() > 0) {
    auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      break;
    }

    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    auto wait_time = std::min<std::chrono::milliseconds>(remaining, std::chrono::milliseconds(250));
    session_.wait_for_alert(wait_time);

    std::vector<lt::alert*> alerts;
    session_.pop_alerts(&alerts);
    for (auto const* alert : alerts) {
      processAlert(alert);
    }
  }

  auto const pending = pendingResumeCount();
  if (pending > 0) {
    std::ostringstream message;
    message << "Timed out while saving resume data for " << pending << " torrent";
    if (pending != 1) {
      message << 's';
    }
    message << '.';
    pushMessage(message.str());
  }
}

std::filesystem::path const& TorrentClient::defaultSavePath() const noexcept {
  return config_.default_save_path;
}

std::filesystem::path const& TorrentClient::statePath() const noexcept {
  return config_.state_path;
}

lt::torrent_handle TorrentClient::findHandleLocked(int id) const {
  auto iter = std::find_if(torrents_.begin(), torrents_.end(), [id](TorrentEntry const& entry) {
    return entry.id == id;
  });
  if (iter == torrents_.end()) {
    return {};
  }
  return iter->handle;
}

std::filesystem::path TorrentClient::resumeDirectory() const {
  return config_.state_path / "resume";
}

std::filesystem::path TorrentClient::makeResumeFilePath(int id) const {
  auto const now = std::chrono::system_clock::now().time_since_epoch();
  auto const millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

  std::ostringstream name;
  name << "torrent-" << millis << '-' << id << ".fastresume";
  return resumeDirectory() / name.str();
}

void TorrentClient::ensureStateDirectories() {
  std::error_code fs_error;
  std::filesystem::create_directories(resumeDirectory(), fs_error);
  if (fs_error) {
    pushMessage("Could not create state directory: " + fs_error.message());
  }
}

void TorrentClient::loadSavedTorrents() {
  auto const directory = resumeDirectory();
  std::error_code fs_error;
  if (!std::filesystem::exists(directory, fs_error)) {
    return;
  }

  int restored = 0;
  for (auto const& entry : std::filesystem::directory_iterator(directory, fs_error)) {
    if (fs_error) {
      pushMessage("Could not read state directory: " + fs_error.message());
      break;
    }
    if (!entry.is_regular_file(fs_error) || entry.path().extension() != ".fastresume") {
      continue;
    }

    std::string io_error;
    auto buffer = readBinaryFile(entry.path(), io_error);
    if (!io_error.empty()) {
      pushMessage("Could not read " + pathToUtf8(entry.path().filename()) + ": " + io_error);
      continue;
    }
    if (buffer.empty()) {
      pushMessage("Skipping empty resume file: " + pathToUtf8(entry.path().filename()));
      continue;
    }

    lt::error_code read_error;
    auto params = lt::read_resume_data(lt::span<char const>(buffer), read_error);
    if (read_error) {
      pushMessage("Skipping invalid resume file " + pathToUtf8(entry.path().filename()) + ": " +
                  read_error.message());
      continue;
    }
    appendAdditionalTrackers(params, config_);
    params.flags |= lt::torrent_flags::duplicate_is_error;

    auto source = params.name.empty() ? pathToUtf8(entry.path().filename()) : params.name;

    lt::error_code add_error;
    auto handle = session_.add_torrent(std::move(params), add_error);
    if (add_error) {
      pushMessage("Could not restore " + source + ": " + add_error.message());
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      torrents_.push_back({next_id_++, handle, source, entry.path()});
    }
    ++restored;
  }

  if (restored > 0) {
    std::ostringstream message;
    message << "Restored " << restored << " torrent";
    if (restored != 1) {
      message << 's';
    }
    message << " from session state.";
    pushMessage(message.str());
  }
}

void TorrentClient::writeInitialResumeData(lt::add_torrent_params const& params,
                                           std::filesystem::path const& resume_file) {
  try {
    auto buffer = lt::write_resume_data_buf(params);
    std::string error;
    if (!writeBinaryFileAtomically(resume_file, buffer, error)) {
      pushMessage("Could not persist new torrent: " + error);
    }
  } catch (std::exception const& ex) {
    pushMessage(std::string("Could not persist new torrent: ") + ex.what());
  }
}

void TorrentClient::requestResumeDataForEntryLocked(TorrentEntry& entry, bool only_if_modified) {
  if (entry.resume_pending || !entry.handle.is_valid()) {
    return;
  }

  auto flags = lt::torrent_handle::flush_disk_cache | lt::torrent_handle::save_info_dict;
  if (only_if_modified) {
    flags |= lt::torrent_handle::only_if_modified;
  }
  entry.handle.save_resume_data(flags);
  entry.resume_pending = true;
}

std::size_t TorrentClient::requestAllResumeData(bool only_if_modified) {
  std::size_t requested = 0;
  std::vector<std::string> errors;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : torrents_) {
      try {
        auto const was_pending = entry.resume_pending;
        requestResumeDataForEntryLocked(entry, only_if_modified);
        if (!was_pending && entry.resume_pending) {
          ++requested;
        }
      } catch (std::exception const& ex) {
        errors.push_back(std::string("Could not request resume data: ") + ex.what());
      }
    }
  }
  for (auto& error : errors) {
    pushMessage(std::move(error));
  }
  return requested;
}

void TorrentClient::requestResumeDataForHandle(lt::torrent_handle const& handle,
                                               bool only_if_modified) {
  std::string error;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = std::find_if(torrents_.begin(), torrents_.end(),
                             [&handle](TorrentEntry const& entry) {
                               return entry.handle == handle;
                             });
    if (iter == torrents_.end()) {
      return;
    }
    try {
      requestResumeDataForEntryLocked(*iter, only_if_modified);
    } catch (std::exception const& ex) {
      error = std::string("Could not request resume data: ") + ex.what();
    }
  }
  if (!error.empty()) {
    pushMessage(error);
  }
}

std::size_t TorrentClient::pendingResumeCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::size_t>(std::count_if(torrents_.begin(), torrents_.end(),
                                                [](TorrentEntry const& entry) {
                                                  return entry.resume_pending;
                                                }));
}

void TorrentClient::processAlert(lt::alert const* alert) {
  if (auto const* resume = lt::alert_cast<lt::save_resume_data_alert>(alert);
      resume != nullptr) {
    storeResumeData(resume->handle, resume->params);
  } else if (auto const* failed = lt::alert_cast<lt::save_resume_data_failed_alert>(alert);
             failed != nullptr) {
    markResumeFailed(failed->handle);
  } else if (auto const* metadata = lt::alert_cast<lt::metadata_received_alert>(alert);
             metadata != nullptr) {
    requestResumeDataForHandle(metadata->handle, false);
  } else if (auto const* finished = lt::alert_cast<lt::torrent_finished_alert>(alert);
             finished != nullptr) {
    requestResumeDataForHandle(finished->handle, false);
  }

  if (shouldLogAlert(alert)) {
    pushMessage(alert->message());
  }
}

bool TorrentClient::storeResumeData(lt::torrent_handle const& handle,
                                    lt::add_torrent_params const& params) {
  std::filesystem::path resume_file;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = std::find_if(torrents_.begin(), torrents_.end(),
                             [&handle](TorrentEntry const& entry) {
                               return entry.handle == handle;
                             });
    if (iter == torrents_.end()) {
      return false;
    }
    iter->resume_pending = false;
    resume_file = iter->resume_file;
  }

  try {
    auto buffer = lt::write_resume_data_buf(params);
    std::string error;
    if (!writeBinaryFileAtomically(resume_file, buffer, error)) {
      pushMessage("Could not save resume data: " + error);
      return false;
    }
  } catch (std::exception const& ex) {
    pushMessage(std::string("Could not save resume data: ") + ex.what());
    return false;
  }

  return true;
}

bool TorrentClient::markResumeFailed(lt::torrent_handle const& handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto iter = std::find_if(torrents_.begin(), torrents_.end(),
                           [&handle](TorrentEntry const& entry) {
                             return entry.handle == handle;
                           });
  if (iter == torrents_.end()) {
    return false;
  }
  iter->resume_pending = false;
  return true;
}

void TorrentClient::pushMessage(std::string message) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (message.empty()) {
    return;
  }
  messages_.push_back(std::move(message));
  while (messages_.size() > 100) {
    messages_.pop_front();
  }
}

}  // namespace ovenbirdbt

