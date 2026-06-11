#pragma once

#include <cstdint>
#include <string>

namespace ovenbirdbt {

std::string formatBytes(std::int64_t bytes);
std::string formatRate(int bytes_per_second);
std::string formatPercent(float value);
std::string sanitizeDisplayText(std::string value);
std::string ellipsize(std::string value, std::size_t width);
std::string fitColumn(std::string value, std::size_t width);

}  // namespace ovenbirdbt

