#pragma once

#include "TorrentClient.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ovenbirdbt {

class TuiApp {
 public:
  explicit TuiApp(ClientConfig config);

  void addStartupTorrent(std::string source);
  int run();

 private:
  enum class ViewMode {
    Main = 0,
    Add = 1,
    Info = 2,
    Partial = 3,
  };

  void refreshModel(bool include_file_details = false,
                    bool include_all_file_details = false,
                    int file_details_torrent_id = 0);
  bool addCurrentInput();
  void pauseSelected();
  void removeSelected();
  void toggleSelectedPartialFile();
  void setAllPartialFiles(bool wanted);
  TorrentSnapshot const* selectedSnapshot() const;
  TorrentSnapshot const* partialSnapshot() const;
  int selectedTorrentId() const;

  TorrentClient client_;
  std::string source_input_;
  std::string save_path_input_;
  std::string status_message_;
  std::vector<std::string> startup_sources_;
  std::vector<TorrentSnapshot> snapshots_;
  std::vector<std::string> menu_row_keys_;
  std::vector<std::string> partial_file_keys_;
  int selected_ = 0;
  int selected_partial_file_ = 0;
  int partial_torrent_id_ = 0;
  int active_tab_ = static_cast<int>(ViewMode::Main);
  int selected_info_tab_ = 0;
};

}  // namespace ovenbirdbt

