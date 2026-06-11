#include "PathUtil.hpp"

#include <cstdlib>

namespace ovenbirdbt {
namespace {

std::filesystem::path envPath(char const* name) {
  if (auto const* value = std::getenv(name); value != nullptr && *value != '\0') {
    return std::filesystem::path(value);
  }
  return {};
}

}  // namespace

std::filesystem::path defaultDownloadPath() {

#ifdef _WIN32
  auto home = envPath("USERPROFILE");
#else
  auto home = envPath("HOME");
#endif

  if (!home.empty()) {
    return home / "Downloads";
  }

  return std::filesystem::current_path() / "downloads";
}

std::filesystem::path defaultStatePath() {

#ifdef _WIN32
  if (auto app_data = envPath("APPDATA"); !app_data.empty()) {
    return app_data / "ovenbirdbt";
  }
  if (auto local_app_data = envPath("LOCALAPPDATA"); !local_app_data.empty()) {
    return local_app_data / "ovenbirdbt";
  }
  if (auto home = envPath("USERPROFILE"); !home.empty()) {
    return home / "AppData" / "Roaming" / "ovenbirdbt";
  }
#else
  if (auto state_home = envPath("XDG_STATE_HOME"); !state_home.empty()) {
    return state_home / "ovenbirdbt";
  }
  if (auto home = envPath("HOME"); !home.empty()) {
    return home / ".local" / "state" / "ovenbirdbt";
  }
#endif

  return std::filesystem::current_path() / ".ovenbirdbt";
}

std::string pathToUtf8(std::filesystem::path const& path) {
#if defined(__cpp_lib_char8_t)
  auto value = path.u8string();
  return std::string(value.begin(), value.end());
#else
  return path.u8string();
#endif
}

}  // namespace ovenbirdbt

