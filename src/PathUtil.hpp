#pragma once

#include <filesystem>
#include <string>

namespace ovenbirdbt {

std::filesystem::path defaultDownloadPath();
std::filesystem::path defaultStatePath();
std::string pathToUtf8(std::filesystem::path const& path);

}  // namespace ovenbirdbt

