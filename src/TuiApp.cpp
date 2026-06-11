#include "TuiApp.hpp"

#include "Format.hpp"
#include "PathUtil.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace ovenbirdbt {
namespace {

constexpr int kGapWidth = 2;
constexpr int kIdWidth = 3;
constexpr int kSizeWidth = 11;
constexpr int kStateWidth = 11;
constexpr int kProgressWidth = 8;
constexpr int kEtaWidth = 11;
constexpr int kRateWidth = 11;
constexpr int kUploadedWidth = 11;
constexpr int kRatioWidth = 6;
constexpr int kAddedOnWidth = 10;
constexpr int kCountWidth = 5;
constexpr int kInfoLabelWidth = 18;
constexpr int kFileWantedWidth = 4;
constexpr int kFilePathWidth = 54;
constexpr int kFileSizeWidth = 11;
constexpr int kFilePercentWidth = 7;
constexpr int kInfoTabCount = 4;

enum class InfoTab {
  Overview = 0,
  Transfer = 1,
  Swarm = 2,
  Files = 3,
};

std::string formatDate(std::time_t timestamp);
std::string formatEta(TorrentSnapshot const& torrent);
std::string formatRatio(std::int64_t upload_bytes, std::int64_t download_bytes);

ftxui::Element hideCursor(ftxui::Element element) {
  using namespace ftxui;
  return dbox({
      std::move(element),
      focus(emptyElement()),
  });
}

ftxui::Element gap() {
  using namespace ftxui;
  return emptyElement() | size(WIDTH, EQUAL, kGapWidth);
}

ftxui::Element leftCell(std::string value, int width) {
  using namespace ftxui;
  return text(ellipsize(std::move(value), static_cast<std::size_t>(width))) |
         size(WIDTH, EQUAL, width);
}

ftxui::Element flexibleLeftCell(std::string value) {
  using namespace ftxui;
  return text(sanitizeDisplayText(std::move(value))) | xflex;
}

ftxui::Element rightCell(std::string value, int width) {
  using namespace ftxui;
  value = ellipsize(std::move(value), static_cast<std::size_t>(width));
  return hbox({filler(), text(std::move(value))}) | size(WIDTH, EQUAL, width);
}

ftxui::Element torrentColumns(ftxui::Elements cells) {
  using namespace ftxui;
  Elements row;
  row.reserve(cells.empty() ? 0 : cells.size() * 2 - 1);
  for (std::size_t i = 0; i < cells.size(); ++i) {
    if (i != 0) {
      row.push_back(gap());
    }
    row.push_back(std::move(cells[i]));
  }
  return hbox(std::move(row));
}

ftxui::Element torrentHeaderRow() {
  using namespace ftxui;
  return torrentColumns({
      //rightCell("ID", kIdWidth),
      flexibleLeftCell("Name"),
      rightCell("Size", kSizeWidth),
      //leftCell("State", kStateWidth),
      rightCell("Progress", kProgressWidth),
      rightCell("Seeds", kCountWidth),
      rightCell("Peers", kCountWidth),
      rightCell("Down", kRateWidth),
      rightCell("Up", kRateWidth),
      rightCell("ETA", kEtaWidth),
      rightCell("Ratio", kRatioWidth),
      rightCell("Added On", kAddedOnWidth),
      //rightCell("Uploaded", kUploadedWidth),
  });
}

ftxui::Element torrentMenuRow(TorrentSnapshot const& torrent,
                              bool active,
                              bool focused) {
  using namespace ftxui;
  auto row = torrentColumns({
      //rightCell(std::to_string(torrent.id), kIdWidth),
      flexibleLeftCell(torrent.name),
      rightCell(formatBytes(torrent.total_bytes), kSizeWidth),
      //leftCell(torrent.state, kStateWidth),
      rightCell(torrent.paused ? "Paused" : formatPercent(torrent.progress), kProgressWidth),
      rightCell(std::to_string(torrent.seeds), kCountWidth),
      rightCell(std::to_string(torrent.peers), kCountWidth),
      rightCell(formatRate(torrent.download_rate), kRateWidth),
      rightCell(formatRate(torrent.upload_rate), kRateWidth),
      rightCell(formatEta(torrent), kEtaWidth),
      rightCell(formatRatio(torrent.all_time_upload_bytes, torrent.all_time_download_bytes),
                kRatioWidth),
      rightCell(formatDate(torrent.added_time), kAddedOnWidth),
      //rightCell(formatBytes(torrent.all_time_upload_bytes), kUploadedWidth),
  });

  if (active) {
    row |= inverted;
  }
  if (focused) {
    row |= focus;
  }
  return row;
}

bool parseMenuIndex(std::string const& label, std::size_t& index) {
  index = 0;
  if (label.empty()) {
    return false;
  }
  for (char character : label) {
    if (!std::isdigit(static_cast<unsigned char>(character))) {
      return false;
    }
    index = index * 10 + static_cast<std::size_t>(character - '0');
  }
  return true;
}

ftxui::Element field(std::string label, ftxui::Element value) {
  using namespace ftxui;
  return hbox({
      text(std::move(label)) | size(WIDTH, EQUAL, 13) | dim,
      std::move(value) | flex,
  });
}

ftxui::Element infoField(std::string label, ftxui::Element value) {
  using namespace ftxui;
  return hbox({
      text(std::move(label)) | size(WIDTH, EQUAL, kInfoLabelWidth) | dim,
      std::move(value) | flex,
  });
}

ftxui::Element infoTextField(std::string label, std::string value) {
  using namespace ftxui;
  return infoField(std::move(label), text(ellipsize(std::move(value), 110)));
}

std::string yesNo(bool value) {
  return value ? "yes" : "no";
}

std::string valueOrUnknown(std::string value) {
  return value.empty() ? "unknown" : std::move(value);
}

std::string formatTimestamp(std::time_t timestamp) {
  if (timestamp <= 0) {
    return "unknown";
  }

  std::tm local_time{};
#if defined(_WIN32)
  if (localtime_s(&local_time, &timestamp) != 0) {
    return "unknown";
  }
#else
  if (localtime_r(&timestamp, &local_time) == nullptr) {
    return "unknown";
  }
#endif

  std::ostringstream out;
  out << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

std::string formatDate(std::time_t timestamp) {
  auto formatted = formatTimestamp(timestamp);
  if (formatted == "unknown") {
    return formatted;
  }
  return formatted.substr(0, 10);
}

std::string formatDuration(std::int64_t total_seconds) {
  total_seconds = std::max<std::int64_t>(0, total_seconds);
  auto const days = total_seconds / 86400;
  total_seconds %= 86400;
  auto const hours = total_seconds / 3600;
  total_seconds %= 3600;
  auto const minutes = total_seconds / 60;
  auto const seconds = total_seconds % 60;

  std::ostringstream out;
  if (days > 0) {
    out << days << "d ";
  }
  if (hours > 0 || days > 0) {
    out << hours << "h ";
  }
  if (minutes > 0 || hours > 0 || days > 0) {
    out << minutes << "m ";
  }
  out << seconds << 's';
  return out.str();
}

std::string formatEta(TorrentSnapshot const& torrent) {
  if (torrent.is_finished || torrent.is_seeding || torrent.progress >= 1.0F) {
    return "Done";
  }
  if (!torrent.has_metadata || torrent.wanted_bytes <= 0) {
    return "Unknown";
  }

  auto const remaining_bytes =
      std::max<std::int64_t>(0, torrent.wanted_bytes - torrent.done_bytes);
  if (remaining_bytes == 0) {
    return "Done";
  }
  if (torrent.paused) {
    return "Paused";
  }
  if (torrent.download_rate <= 0) {
    return "n/a";
  }

  auto const seconds =
      (remaining_bytes + static_cast<std::int64_t>(torrent.download_rate) - 1) /
      static_cast<std::int64_t>(torrent.download_rate);
  return formatDuration(seconds);
}

std::string formatOptionalCount(int value) {
  if (value < 0) {
    return "unknown";
  }
  return std::to_string(value);
}

std::string formatLimit(int value) {
  if (value <= 0) {
    return "unlimited";
  }
  return std::to_string(value);
}

std::string formatQueuePosition(int value) {
  if (value < 0) {
    return "not queued";
  }
  return std::to_string(value);
}

std::string formatRatio(std::int64_t upload_bytes, std::int64_t download_bytes) {
  if (download_bytes <= 0) {
    return upload_bytes > 0 ? "inf" : "n/a";
  }

  std::ostringstream out;
  out << std::fixed << std::setprecision(2)
      << static_cast<double>(upload_bytes) / static_cast<double>(download_bytes);
  return out.str();
}

std::string formatAvailability(TorrentSnapshot const& torrent) {
  if (torrent.distributed_copies < 0.0F) {
    return "unknown";
  }

  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << torrent.distributed_copies << " copies";
  if (torrent.distributed_full_copies >= 0) {
    out << " (" << torrent.distributed_full_copies << " full + "
        << formatPercent(static_cast<float>(torrent.distributed_fraction) / 1000.0F) << ')';
  }
  return out.str();
}

ftxui::Element fileHeaderRow() {
  return torrentColumns({
      leftCell("Path", kFilePathWidth),
      rightCell("Size", kFileSizeWidth),
      rightCell("Done", kFileSizeWidth),
      rightCell("Done", kFilePercentWidth),
  });
}

ftxui::Element fileInfoRow(TorrentFileSnapshot const& file) {
  auto const progress =
      file.size <= 0
          ? 1.0F
          : static_cast<float>(std::clamp<std::int64_t>(file.done_bytes, 0, file.size)) /
                static_cast<float>(file.size);
  return torrentColumns({
      leftCell(file.path, kFilePathWidth),
      rightCell(formatBytes(file.size), kFileSizeWidth),
      rightCell(formatBytes(file.done_bytes), kFileSizeWidth),
      rightCell(formatPercent(progress), kFilePercentWidth),
  });
}

ftxui::Element partialFileHeaderRow() {
  return torrentColumns({
      leftCell("Get", kFileWantedWidth),
      leftCell("Path", kFilePathWidth),
      rightCell("Size", kFileSizeWidth),
      rightCell("Done", kFileSizeWidth),
      rightCell("Done", kFilePercentWidth),
  });
}

ftxui::Element partialFileMenuRow(TorrentFileSnapshot const& file,
                                  bool active,
                                  bool focused) {
  using namespace ftxui;

  auto const progress =
      file.size <= 0
          ? 1.0F
          : static_cast<float>(std::clamp<std::int64_t>(file.done_bytes, 0, file.size)) /
                static_cast<float>(file.size);
  auto row = torrentColumns({
      leftCell(file.wanted ? "[x]" : "[ ]", kFileWantedWidth),
      leftCell(file.path, kFilePathWidth),
      rightCell(formatBytes(file.size), kFileSizeWidth),
      rightCell(formatBytes(file.done_bytes), kFileSizeWidth),
      rightCell(formatPercent(progress), kFilePercentWidth),
  });

  if (active) {
    row |= inverted;
  }
  if (focused) {
    row |= focus;
  }
  return row;
}

void appendErrorRows(ftxui::Elements& rows, TorrentSnapshot const& torrent) {
  using namespace ftxui;
  if (!torrent.has_error) {
    return;
  }

  rows.push_back(separator());
  rows.push_back(infoTextField("Error", torrent.error) | color(Color::Red));
}

ftxui::Element overviewTab(TorrentSnapshot const& torrent) {
  using namespace ftxui;
  auto const remaining_bytes =
      std::max<std::int64_t>(0, torrent.wanted_bytes - torrent.done_bytes);

  Elements rows;
  rows.push_back(infoTextField("ID", std::to_string(torrent.id)));
  rows.push_back(infoTextField("State", torrent.state));
  rows.push_back(infoField("Progress", hbox({
                                           gauge(torrent.progress) | flex,
                                           text(" " + formatPercent(torrent.progress)),
                                       })));
  rows.push_back(infoTextField("Downloaded", formatBytes(torrent.done_bytes) + " / " +
                                                 formatBytes(torrent.wanted_bytes)));
  rows.push_back(infoTextField("Remaining", formatBytes(remaining_bytes)));
  rows.push_back(infoTextField("Total size", formatBytes(torrent.total_bytes)));
  rows.push_back(infoTextField("Queue", formatQueuePosition(torrent.queue_position)));
  rows.push_back(infoTextField("Paused", yesNo(torrent.paused)));
  rows.push_back(infoTextField("Metadata", yesNo(torrent.has_metadata)));
  rows.push_back(infoTextField("Finished", yesNo(torrent.is_finished)));
  rows.push_back(infoTextField("Seeding", yesNo(torrent.is_seeding)));
  rows.push_back(separator());
  rows.push_back(infoTextField("Save path", valueOrUnknown(torrent.save_path)));
  rows.push_back(infoTextField("Source", valueOrUnknown(torrent.source)));
  rows.push_back(infoTextField("Added", formatTimestamp(torrent.added_time)));
  rows.push_back(infoTextField("Completed", formatTimestamp(torrent.completed_time)));
  rows.push_back(infoTextField("Last complete", formatTimestamp(torrent.last_seen_complete)));
  rows.push_back(infoTextField("Tracker", valueOrUnknown(torrent.current_tracker)));
  rows.push_back(infoTextField("Info hash", torrent.info_hash));
  appendErrorRows(rows, torrent);
  return vbox(std::move(rows));
}

ftxui::Element transferTab(TorrentSnapshot const& torrent) {
  using namespace ftxui;
  Elements rows;
  rows.push_back(infoTextField("Download rate", formatRate(torrent.download_rate) + " total, " +
                                                    formatRate(torrent.download_payload_rate) +
                                                    " payload"));
  rows.push_back(infoTextField("Upload rate", formatRate(torrent.upload_rate) + " total, " +
                                                  formatRate(torrent.upload_payload_rate) +
                                                  " payload"));
  rows.push_back(separator());
  rows.push_back(infoTextField("Session down", formatBytes(torrent.session_download_bytes)));
  rows.push_back(infoTextField("Session up", formatBytes(torrent.session_upload_bytes)));
  rows.push_back(infoTextField("Payload down", formatBytes(torrent.session_payload_download_bytes)));
  rows.push_back(infoTextField("Payload up", formatBytes(torrent.session_payload_upload_bytes)));
  rows.push_back(infoTextField("All-time down", formatBytes(torrent.all_time_download_bytes)));
  rows.push_back(infoTextField("All-time up", formatBytes(torrent.all_time_upload_bytes)));
  rows.push_back(infoTextField("Share ratio", formatRatio(torrent.all_time_upload_bytes,
                                                          torrent.all_time_download_bytes)));
  rows.push_back(separator());
  rows.push_back(infoTextField("Failed bytes", formatBytes(torrent.failed_bytes)));
  rows.push_back(infoTextField("Redundant bytes", formatBytes(torrent.redundant_bytes)));
  rows.push_back(infoTextField("Active time", formatDuration(torrent.active_seconds)));
  rows.push_back(infoTextField("Finished time", formatDuration(torrent.finished_seconds)));
  rows.push_back(infoTextField("Seeding time", formatDuration(torrent.seeding_seconds)));
  rows.push_back(infoTextField("Resume data", torrent.resume_pending
                                                  ? "saving"
                                                  : (torrent.need_save_resume ? "changed"
                                                                              : "saved")));
  appendErrorRows(rows, torrent);
  return vbox(std::move(rows));
}

ftxui::Element swarmTab(TorrentSnapshot const& torrent) {
  using namespace ftxui;
  Elements rows;
  rows.push_back(infoTextField("Peers", std::to_string(torrent.peers)));
  rows.push_back(infoTextField("Seeds", std::to_string(torrent.seeds)));
  rows.push_back(infoTextField("Known peers", std::to_string(torrent.known_peers)));
  rows.push_back(infoTextField("Known seeds", std::to_string(torrent.known_seeds)));
  rows.push_back(infoTextField("Tracker seeds", formatOptionalCount(torrent.tracker_complete)));
  rows.push_back(infoTextField("Tracker leeches", formatOptionalCount(torrent.tracker_incomplete)));
  rows.push_back(infoTextField("Candidates", std::to_string(torrent.connect_candidates)));
  rows.push_back(separator());
  rows.push_back(infoTextField("Connections", std::to_string(torrent.connections)));
  rows.push_back(infoTextField("Upload slots", std::to_string(torrent.uploads)));
  rows.push_back(infoTextField("Upload limit", formatLimit(torrent.uploads_limit)));
  rows.push_back(infoTextField("Connection limit", formatLimit(torrent.connections_limit)));
  rows.push_back(infoTextField("Down queue", std::to_string(torrent.down_bandwidth_queue)));
  rows.push_back(infoTextField("Up queue", std::to_string(torrent.up_bandwidth_queue)));
  rows.push_back(separator());
  rows.push_back(infoTextField("Availability", formatAvailability(torrent)));
  rows.push_back(infoTextField("Incoming", yesNo(torrent.has_incoming)));
  rows.push_back(infoTextField("Trackers", yesNo(torrent.announcing_to_trackers)));
  rows.push_back(infoTextField("DHT", yesNo(torrent.announcing_to_dht)));
  rows.push_back(infoTextField("Local peers", yesNo(torrent.announcing_to_lsd)));
  rows.push_back(infoTextField("Current tracker", valueOrUnknown(torrent.current_tracker)));
  appendErrorRows(rows, torrent);
  return vbox(std::move(rows));
}

ftxui::Element filesTab(TorrentSnapshot const& torrent) {
  using namespace ftxui;
  Elements rows;
  rows.push_back(infoTextField("Metadata", yesNo(torrent.has_metadata)));
  rows.push_back(infoTextField("Files", std::to_string(torrent.file_count)));
  rows.push_back(infoTextField("Total size", formatBytes(torrent.total_bytes)));
  rows.push_back(infoTextField("Pieces", std::to_string(torrent.total_pieces)));
  rows.push_back(infoTextField("Finished pieces", std::to_string(torrent.finished_pieces)));
  rows.push_back(infoTextField("Piece size", formatBytes(torrent.piece_length)));
  rows.push_back(infoTextField("Block size", formatBytes(torrent.block_size)));
  rows.push_back(infoTextField("Storage", valueOrUnknown(torrent.storage_mode)));
  rows.push_back(infoTextField("Moving storage", yesNo(torrent.moving_storage)));

  rows.push_back(separator());
  if (!torrent.has_metadata) {
    rows.push_back(text("File details will appear after metadata is downloaded.") | dim);
    appendErrorRows(rows, torrent);
    return vbox(std::move(rows));
  }
  if (torrent.files.empty()) {
    rows.push_back(text("No file details are available yet.") | dim);
    appendErrorRows(rows, torrent);
    return vbox(std::move(rows));
  }

  if (torrent.file_count > static_cast<int>(torrent.files.size())) {
    rows.push_back(text("Showing first " + std::to_string(torrent.files.size()) + " of " +
                        std::to_string(torrent.file_count) + " files.") |
                   dim);
  }
  rows.push_back(fileHeaderRow() | dim);
  for (auto const& file : torrent.files) {
    rows.push_back(fileInfoRow(file));
  }
  appendErrorRows(rows, torrent);
  return vbox(std::move(rows));
}

ftxui::Element infoTabBody(TorrentSnapshot const& torrent, int selected_info_tab) {
  switch (static_cast<InfoTab>(selected_info_tab)) {
    case InfoTab::Transfer:
      return transferTab(torrent);
    case InfoTab::Swarm:
      return swarmTab(torrent);
    case InfoTab::Files:
      return filesTab(torrent);
    case InfoTab::Overview:
    default:
      return overviewTab(torrent);
  }
}

ftxui::Element emptyWindow(std::string title, std::string message) {
  using namespace ftxui;
  return window(text(std::move(title)) | bold,
                vbox({
                    text(std::move(message)) | dim,
                })) |
         flex;
}

ftxui::Element torrentInfoView(TorrentSnapshot const* torrent,
                               int selected_info_tab,
                               ftxui::Element tab_bar) {
  using namespace ftxui;
  if (torrent == nullptr) {
    return emptyWindow(" Torrent information ", "No torrent selected.");
  }

  Elements rows;
  rows.push_back(hbox({
      text("#" + std::to_string(torrent->id) + " ") | dim,
      text(ellipsize(torrent->name, 110)) | bold,
  }));
  rows.push_back(separator());
  rows.push_back(std::move(tab_bar));
  rows.push_back(separator());
  rows.push_back(infoTabBody(*torrent, selected_info_tab) | yframe | flex);

  return window(text(" Torrent information ") | bold, vbox(std::move(rows))) |
         flex;
}

// ftxui::Element messagesView(std::vector<std::string> const& messages,
//                             std::string const& status_message) {
//   using namespace ftxui;
//   Elements rows;
//   if (!status_message.empty()) {
//     rows.push_back(text(ellipsize(status_message, 110)) | bold);
//   }
//   for (auto const& message : messages) {
//     rows.push_back(text(ellipsize(message, 110)) | dim);
//   }
//   if (rows.empty()) {
//     rows.push_back(text("Ready") | dim);
//   }
//   return vbox(std::move(rows)) | border;
// }

bool isCtrlC(ftxui::Event const& event) {
  return event == ftxui::Event::Special(std::string(1, '\x03'));
}

bool isClipboardPaste(ftxui::Event const& event) {
#if defined(_WIN32)
  return event == ftxui::Event::CtrlV;
#else
  (void)event;
  return false;
#endif
}

bool isKey(ftxui::Event const& event, char key) {
  if (!event.is_character()) {
    return false;
  }
  auto const character = event.character();
  return character.size() == 1 &&
         std::tolower(static_cast<unsigned char>(character.front())) == key;
}

#if defined(_WIN32)
void stripTrailingLineEndings(std::string& text) {
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
    text.pop_back();
  }
}
#endif

std::optional<std::string> readClipboardText() {
#if defined(_WIN32)
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(nullptr)) {
    return std::nullopt;
  }

  struct ClipboardGuard {
    ~ClipboardGuard() { CloseClipboard(); }
  } clipboard_guard;

  HANDLE const handle = GetClipboardData(CF_UNICODETEXT);
  if (handle == nullptr) {
    return std::nullopt;
  }

  auto const* wide_text = static_cast<wchar_t const*>(GlobalLock(handle));
  if (wide_text == nullptr) {
    return std::nullopt;
  }

  struct GlobalLockGuard {
    HANDLE handle;
    ~GlobalLockGuard() { GlobalUnlock(handle); }
  } global_lock_guard{handle};

  int wide_length = 0;
  while (wide_text[wide_length] != L'\0') {
    ++wide_length;
  }
  if (wide_length == 0) {
    return std::string();
  }

  int const utf8_length =
      WideCharToMultiByte(CP_UTF8, 0, wide_text, wide_length, nullptr, 0,
                          nullptr, nullptr);
  if (utf8_length <= 0) {
    return std::nullopt;
  }

  std::string text(static_cast<std::size_t>(utf8_length), '\0');
  int const converted =
      WideCharToMultiByte(CP_UTF8, 0, wide_text, wide_length, text.data(),
                          utf8_length, nullptr, nullptr);
  if (converted <= 0) {
    return std::nullopt;
  }

  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  stripTrailingLineEndings(text);
  return text;
#else
  return std::nullopt;
#endif
}

}  // namespace

TuiApp::TuiApp(ClientConfig config) : client_(std::move(config)) {
  save_path_input_ = pathToUtf8(client_.defaultSavePath());
}

void TuiApp::addStartupTorrent(std::string source) {
  startup_sources_.push_back(std::move(source));
}

int TuiApp::run() {
  for (auto const& source : startup_sources_) {
    auto result = client_.addTorrent(source, save_path_input_);
    status_message_ = result.message;
  }

  using namespace ftxui;

  auto screen = ScreenInteractive::Fullscreen();
  auto source_input = Input(&source_input_, "magnet URI or .torrent file");
  auto save_input = Input(&save_path_input_, "download directory");
  auto menu_option = MenuOption::Vertical();
  menu_option.entries_option.transform = [&](EntryState const& state) {
    std::size_t index = 0;
    if (!parseMenuIndex(state.label, index) || index >= snapshots_.size()) {
      return emptyElement();
    }
    return torrentMenuRow(snapshots_[index], state.active, state.focused);
  };
  auto menu = Menu(&menu_row_keys_, &selected_, menu_option);

  auto partial_file_option = MenuOption::Vertical();
  partial_file_option.entries_option.transform = [&](EntryState const& state) {
    std::size_t index = 0;
    auto const* torrent = partialSnapshot();
    if (torrent == nullptr || !parseMenuIndex(state.label, index) ||
        index >= torrent->files.size()) {
      return emptyElement();
    }
    return partialFileMenuRow(torrent->files[index], state.active, state.focused);
  };
  auto partial_file_menu =
      Menu(&partial_file_keys_, &selected_partial_file_, partial_file_option);

  std::vector<std::string> info_tab_labels = {
      "[1] Overview",
      "[2] Transfer",
      "[3] Swarm",
      "[4] Files",
  };
  auto info_tab_option = MenuOption::Horizontal();
  info_tab_option.entries_option.transform = [](EntryState const& state) {
    auto item = text(" " + state.label + " ");
    if (state.active) {
      item |= bold;
      item |= inverted;
    } else {
      item |= dim;
    }
    if (state.focused) {
      item |= focus;
    }
    return item;
  };
  auto info_tab_menu = Menu(&info_tab_labels, &selected_info_tab_, info_tab_option);

  auto show_main = [&] {
    active_tab_ = static_cast<int>(ViewMode::Main);
    menu->TakeFocus();
  };
  auto show_add = [&] {
    source_input_.clear();
    save_path_input_ = pathToUtf8(client_.defaultSavePath());
    active_tab_ = static_cast<int>(ViewMode::Add);
    source_input->TakeFocus();
  };
  auto show_info = [&] {
    refreshModel();
    if (selectedTorrentId() == 0) {
      status_message_ = "No torrent selected.";
      show_main();
      return;
    }
    active_tab_ = static_cast<int>(ViewMode::Info);
    info_tab_menu->TakeFocus();
  };
  auto show_partial = [&] {
    refreshModel();
    auto const torrent_id = selectedTorrentId();
    if (torrent_id == 0) {
      status_message_ = "No torrent selected.";
      show_main();
      return;
    }

    partial_torrent_id_ = torrent_id;
    selected_partial_file_ = 0;
    active_tab_ = static_cast<int>(ViewMode::Partial);
    refreshModel(true, true, partial_torrent_id_);
    partial_file_menu->TakeFocus();
  };
  auto submit_add = [&] {
    if (addCurrentInput()) {
      show_main();
    }
  };
  auto paste_clipboard_into_add_form = [&] {
    auto clipboard_text = readClipboardText();
    if (!clipboard_text) {
      status_message_ = "Clipboard does not contain text.";
      return true;
    }
    if (clipboard_text->empty()) {
      status_message_ = "Clipboard is empty.";
      return true;
    }

    *clipboard_text = sanitizeDisplayText(std::move(*clipboard_text));
    auto const paste_event = Event::Character(*clipboard_text);
    if (save_input->Focused()) {
      return save_input->OnEvent(paste_event);
    }
    return source_input->OnEvent(paste_event);
  };
  bool confirm_exit = false;
  bool confirm_delete = false;
  int pending_delete_torrent_id = 0;
  std::string pending_delete_torrent_name;
  int active_layer = 0;
  auto exit_loop = screen.ExitLoopClosure();
  auto cancel_exit = [&] {
    confirm_exit = false;
    active_layer = 0;
  };
  auto confirm_exit_now = [&] { exit_loop(); };
  auto cancel_delete = [&] {
    confirm_delete = false;
    pending_delete_torrent_id = 0;
    pending_delete_torrent_name.clear();
    active_layer = 0;
  };
  auto confirm_delete_now = [&] {
    if (pending_delete_torrent_id != 0) {
      auto result = client_.removeTorrent(pending_delete_torrent_id);
      status_message_ = result.message;
      refreshModel();
    }
    cancel_delete();
    show_main();
  };

  auto exit_yes_button = Button("Yes ", confirm_exit_now);
  auto exit_no_button = Button(" No ", cancel_exit);
  auto exit_buttons = Container::Horizontal({
      exit_yes_button,
      exit_no_button,
  });
  auto delete_yes_button = Button("Yes ", confirm_delete_now);
  auto delete_no_button = Button(" No ", cancel_delete);
  auto delete_buttons = Container::Horizontal({
      delete_yes_button,
      delete_no_button,
  });
  auto request_exit = [&] {
    confirm_exit = true;
    active_layer = 1;
    exit_yes_button->TakeFocus();
  };
  auto request_delete = [&] {
    refreshModel();
    auto const* torrent = selectedSnapshot();
    if (torrent == nullptr) {
      status_message_ = "No torrent selected.";
      return;
    }

    pending_delete_torrent_id = torrent->id;
    pending_delete_torrent_name = torrent->name;
    confirm_delete = true;
    active_layer = 2;
    delete_yes_button->TakeFocus();
  };

  auto main_buttons = Container::Horizontal({
      Button("[a] Add", show_add),
      Button("[i] Info", show_info),
      Button("[t] Partial", show_partial),
      Button("[p] Pause", [&] { pauseSelected(); }),
      Button("[d] Delete", request_delete),
      Button("[q] Quit", request_exit),
  });
  auto add_buttons = Container::Horizontal({
      Button("[Enter] Add", submit_add),
      Button("[Esc] Back", show_main),
  });
  auto info_buttons = Container::Horizontal({
      Button("[Esc] Back", show_main),
      Button("[q] Quit", request_exit),
  });
  auto partial_buttons = Container::Horizontal({
      Button("[Space] Toggle", [&] {
        toggleSelectedPartialFile();
        partial_file_menu->TakeFocus();
      }),
      Button("[a] Select all", [&] {
        setAllPartialFiles(true);
        partial_file_menu->TakeFocus();
      }),
      Button("[n] Select none", [&] {
        setAllPartialFiles(false);
        partial_file_menu->TakeFocus();
      }),
      Button("[Esc] Back", show_main),
  });

  auto main_view = Container::Vertical({
      menu,
      main_buttons,
  });
  auto add_view = Container::Vertical({
      source_input,
      save_input,
      add_buttons,
  });
  auto info_view = Container::Vertical({
      info_tab_menu,
      info_buttons,
  });
  auto partial_view = Container::Vertical({
      partial_file_menu,
      partial_buttons,
  });
  auto app_layout = Container::Tab({
      main_view,
      add_view,
      info_view,
      partial_view,
  }, &active_tab_);
  auto confirm_exit_view = Container::Vertical({
      exit_buttons,
  });
  auto confirm_delete_view = Container::Vertical({
      delete_buttons,
  });
  auto layout = Container::Tab({
      app_layout,
      confirm_exit_view,
      confirm_delete_view,
  }, &active_layer);

  std::atomic<bool> running = true;
  std::thread ticker([&] {
    while (running.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      screen.PostEvent(Event::Custom);
    }
  });

  auto renderer = Renderer(layout, [&] {
    auto const view_mode = static_cast<ViewMode>(active_tab_);
    auto const include_file_details =
        view_mode == ViewMode::Partial ||
        (view_mode == ViewMode::Info &&
         selected_info_tab_ == static_cast<int>(InfoTab::Files));
    refreshModel(include_file_details, view_mode == ViewMode::Partial,
                 view_mode == ViewMode::Partial ? partial_torrent_id_ : 0);

    auto table_header = torrentHeaderRow() | dim;
    auto torrent_list = vbox({
                            table_header,
                            separator(),
                            menu->Render() | flex,
                        });
    auto torrent_window = window(text(" Torrents ") | bold, torrent_list) | flex;

    auto main_screen = [&] {
      return vbox({
          torrent_window,
          main_buttons->Render(),
          // messagesView(client_.recentMessages(4), status_message_),
      });
    };

    auto add_screen = [&] {
      auto form = vbox({
          field("Source", source_input->Render() | flex),
          separator(),
          field("Save to", save_input->Render() | flex),
          separator(),
          add_buttons->Render(),
      });

      return vbox({
          filler(),
          window(text(" Add torrent ") | bold, form) |
              size(WIDTH, GREATER_THAN, 72) | center,
          filler(),
          // messagesView(client_.recentMessages(3), status_message_),
      });
    };

    auto info_screen = [&] {
      return vbox({
          torrentInfoView(selectedSnapshot(), selected_info_tab_,
                          info_tab_menu->Render()) |
              flex,
          info_buttons->Render(),
          // messagesView(client_.recentMessages(3), status_message_),
      });
    };

    auto partial_screen = [&] {
      auto const* torrent = partialSnapshot();
      Element body;
      if (torrent == nullptr) {
        body = emptyWindow(" Partial download ", "No torrent selected.") | flex;
      } else if (!torrent->has_metadata) {
        body = emptyWindow(" Partial download ",
                           "File list will appear after metadata is downloaded.") |
               flex;
      } else if (torrent->files.empty()) {
        body = emptyWindow(" Partial download ", "No file details are available yet.") |
               flex;
      } else {
        auto heading = hbox({
            text("#" + std::to_string(torrent->id) + " ") | dim,
            text(ellipsize(torrent->name, 110)) | bold,
        });
        auto file_list = vbox({
            std::move(heading),
            separator(),
            partialFileHeaderRow() | dim,
            separator(),
            partial_file_menu->Render() | yframe | flex,
        });
        body = window(text(" Partial download ") | bold, file_list) | flex;
      }

      return vbox({
          std::move(body),
          partial_buttons->Render(),
          // messagesView(client_.recentMessages(3), status_message_),
      });
    };

    Element screen_body;
    switch (view_mode) {
      case ViewMode::Add:
        screen_body = add_screen();
        break;
      case ViewMode::Info:
        screen_body = info_screen();
        break;
      case ViewMode::Partial:
        screen_body = partial_screen();
        break;
      case ViewMode::Main:
      default:
        screen_body = main_screen();
        break;
    }

    if (!confirm_exit && !confirm_delete) {
      return view_mode == ViewMode::Add ? screen_body
                                        : hideCursor(std::move(screen_body));
    }

    Element dialog;
    if (confirm_exit) {
      dialog = window(text(" Exit ovenbirdbt ") | bold,
                      vbox({
                          text("Are you sure you want to exit?"),
                          separator(),
                          exit_buttons->Render() | hcenter,
                      })) |
               size(WIDTH, GREATER_THAN, 38) | clear_under | center;
    } else {
      dialog = window(text(" Delete torrent ") | bold,
                      vbox({
                          text("Are you sure you want to delete this torrent?"),
                          text(ellipsize(pending_delete_torrent_name, 70)) | dim,
                          separator(),
                          delete_buttons->Render() | hcenter,
                      })) |
               size(WIDTH, GREATER_THAN, 48) | clear_under | center;
    }

    return hideCursor(dbox({
        std::move(screen_body) | dim,
        std::move(dialog),
    }));
  });

  auto root = CatchEvent(renderer, [&](Event event) {
    if (isCtrlC(event)) {
      screen.ExitLoopClosure()();
      return true;
    }

    if (confirm_exit) {
      if (event == Event::Escape) {
        cancel_exit();
        return true;
      }
      return false;
    }
    if (confirm_delete) {
      if (event == Event::Escape) {
        cancel_delete();
        return true;
      }
      return false;
    }

    switch (static_cast<ViewMode>(active_tab_)) {
      case ViewMode::Main:
        if (isKey(event, 'a')) {
          show_add();
          return true;
        }
        if (isKey(event, 'i') || event == Event::Return) {
          show_info();
          return true;
        }
        if (isKey(event, 't')) {
          show_partial();
          return true;
        }
        if (isKey(event, 'p')) {
          pauseSelected();
          return true;
        }
        if (isKey(event, 'd')) {
          request_delete();
          return true;
        }
        if (isKey(event, 'q')) {
          request_exit();
          return true;
        }
        break;
      case ViewMode::Add:
        if (isClipboardPaste(event)) {
          return paste_clipboard_into_add_form();
        }
        if (event.is_character()) {
          auto sanitized = sanitizeDisplayText(event.character());
          if (sanitized != event.character()) {
            if (sanitized.empty()) {
              return true;
            }
            auto const sanitized_event = Event::Character(std::move(sanitized));
            if (save_input->Focused()) {
              return save_input->OnEvent(sanitized_event);
            }
            return source_input->OnEvent(sanitized_event);
          }
        }
        if (event == Event::Escape) {
          show_main();
          return true;
        }
        if (event == Event::Return) {
          submit_add();
          return true;
        }
        break;
      case ViewMode::Info:
        if (isKey(event, '1')) {
          selected_info_tab_ = static_cast<int>(InfoTab::Overview);
          info_tab_menu->TakeFocus();
          return true;
        }
        if (isKey(event, '2')) {
          selected_info_tab_ = static_cast<int>(InfoTab::Transfer);
          info_tab_menu->TakeFocus();
          return true;
        }
        if (isKey(event, '3')) {
          selected_info_tab_ = static_cast<int>(InfoTab::Swarm);
          info_tab_menu->TakeFocus();
          return true;
        }
        if (isKey(event, '4')) {
          selected_info_tab_ = static_cast<int>(InfoTab::Files);
          info_tab_menu->TakeFocus();
          return true;
        }
        if (event == Event::Escape) {
          show_main();
          return true;
        }
        if (isKey(event, 'q')) {
          request_exit();
          return true;
        }
        break;
      case ViewMode::Partial:
        if (event == Event::Escape) {
          show_main();
          return true;
        }
        if (event == Event::Return || event == Event::Character(" ")) {
          toggleSelectedPartialFile();
          partial_file_menu->TakeFocus();
          return true;
        }
        if (isKey(event, 'a')) {
          setAllPartialFiles(true);
          partial_file_menu->TakeFocus();
          return true;
        }
        if (isKey(event, 'n')) {
          setAllPartialFiles(false);
          partial_file_menu->TakeFocus();
          return true;
        }
        break;
    }

    return false;
  });

  screen.Loop(root);
  running = false;
  ticker.join();
  return 0;
}

void TuiApp::refreshModel(bool include_file_details,
                          bool include_all_file_details,
                          int file_details_torrent_id) {
  client_.pumpAlerts();
  snapshots_ = client_.snapshots(include_file_details, include_all_file_details,
                                 file_details_torrent_id);

  if (selected_ < 0) {
    selected_ = 0;
  }
  if (selected_ >= static_cast<int>(snapshots_.size())) {
    selected_ = snapshots_.empty() ? 0 : static_cast<int>(snapshots_.size()) - 1;
  }
  if (selected_info_tab_ < 0) {
    selected_info_tab_ = 0;
  }
  if (selected_info_tab_ >= kInfoTabCount) {
    selected_info_tab_ = kInfoTabCount - 1;
  }

  menu_row_keys_.clear();
  menu_row_keys_.reserve(snapshots_.size());
  for (std::size_t index = 0; index < snapshots_.size(); ++index) {
    menu_row_keys_.push_back(std::to_string(index));
  }

  partial_file_keys_.clear();
  auto const* partial = partialSnapshot();
  if (partial != nullptr) {
    partial_file_keys_.reserve(partial->files.size());
    for (std::size_t index = 0; index < partial->files.size(); ++index) {
      partial_file_keys_.push_back(std::to_string(index));
    }
  }
  if (selected_partial_file_ < 0) {
    selected_partial_file_ = 0;
  }
  if (selected_partial_file_ >= static_cast<int>(partial_file_keys_.size())) {
    selected_partial_file_ =
        partial_file_keys_.empty() ? 0 : static_cast<int>(partial_file_keys_.size()) - 1;
  }
}

bool TuiApp::addCurrentInput() {
  auto result = client_.addTorrent(source_input_, save_path_input_);
  status_message_ = result.message;
  if (result.ok) {
    source_input_.clear();
  }
  return result.ok;
}

void TuiApp::pauseSelected() {
  refreshModel();
  auto const* torrent = selectedSnapshot();
  if (torrent == nullptr) {
    status_message_ = "No torrent selected.";
    return;
  }

  auto result = torrent->paused ? client_.resumeTorrent(torrent->id)
                                : client_.pauseTorrent(torrent->id);
  status_message_ = result.message;
  refreshModel();
}

void TuiApp::removeSelected() {
  auto result = client_.removeTorrent(selectedTorrentId());
  status_message_ = result.message;
  refreshModel();
}

void TuiApp::toggleSelectedPartialFile() {
  auto const* torrent = partialSnapshot();
  if (torrent == nullptr) {
    status_message_ = "No torrent selected.";
    return;
  }
  if (!torrent->has_metadata || torrent->files.empty()) {
    status_message_ = "File list is not available yet.";
    return;
  }
  if (selected_partial_file_ < 0 ||
      selected_partial_file_ >= static_cast<int>(torrent->files.size())) {
    status_message_ = "No file is selected.";
    return;
  }

  auto const& file = torrent->files[static_cast<std::size_t>(selected_partial_file_)];
  auto result = client_.setFileWanted(torrent->id, file.index, !file.wanted);
  status_message_ = result.message;
  refreshModel(true, true, partial_torrent_id_);
}

void TuiApp::setAllPartialFiles(bool wanted) {
  auto const* torrent = partialSnapshot();
  if (torrent == nullptr) {
    status_message_ = "No torrent selected.";
    return;
  }

  auto result = client_.setAllFilesWanted(torrent->id, wanted);
  status_message_ = result.message;
  refreshModel(true, true, partial_torrent_id_);
}

TorrentSnapshot const* TuiApp::selectedSnapshot() const {
  if (snapshots_.empty() || selected_ < 0 || selected_ >= static_cast<int>(snapshots_.size())) {
    return nullptr;
  }
  return &snapshots_[static_cast<std::size_t>(selected_)];
}

TorrentSnapshot const* TuiApp::partialSnapshot() const {
  if (partial_torrent_id_ == 0) {
    return selectedSnapshot();
  }

  auto const iter = std::find_if(snapshots_.begin(), snapshots_.end(),
                                 [this](TorrentSnapshot const& torrent) {
                                   return torrent.id == partial_torrent_id_;
                                 });
  if (iter == snapshots_.end()) {
    return nullptr;
  }
  return &*iter;
}

int TuiApp::selectedTorrentId() const {
  auto const* torrent = selectedSnapshot();
  if (torrent == nullptr) {
    return 0;
  }
  return torrent->id;
}

}  // namespace ovenbirdbt

