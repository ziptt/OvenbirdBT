#include "Format.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include <ftxui/screen/string.hpp>

namespace ovenbirdbt {
namespace {

std::string formatScaled(double value, char const* const* units, std::size_t count) {
  std::size_t index = 0;
  while (value >= 1024.0 && index + 1 < count) {
    value /= 1024.0;
    ++index;
  }

  std::ostringstream out;
  if (value >= 100.0 || index == 0) {
    out << std::fixed << std::setprecision(0);
  } else if (value >= 10.0) {
    out << std::fixed << std::setprecision(1);
  } else {
    out << std::fixed << std::setprecision(2);
  }
  out << value << ' ' << units[index];
  return out.str();
}

std::size_t displayWidth(std::string const& value) {
  return static_cast<std::size_t>(std::max(0, ftxui::string_width(value)));
}

std::string truncateCells(std::string const& value, std::size_t width) {
  if (width == 0) {
    return {};
  }

  std::string truncated;
  std::size_t cells = 0;
  for (std::string const& glyph : ftxui::Utf8ToGlyphs(value)) {
    int const glyph_width = std::max(0, ftxui::string_width(glyph));
    if (cells + static_cast<std::size_t>(glyph_width) > width) {
      break;
    }
    truncated += glyph;
    cells += static_cast<std::size_t>(glyph_width);
  }
  return truncated;
}

bool isUtf8Continuation(unsigned char byte) {
  return byte >= 0x80 && byte <= 0xBF;
}

std::size_t utf8SequenceLength(unsigned char byte) {
  if (byte >= 0xC2 && byte <= 0xDF) {
    return 2;
  }
  if (byte >= 0xE0 && byte <= 0xEF) {
    return 3;
  }
  if (byte >= 0xF0 && byte <= 0xF4) {
    return 4;
  }
  return 0;
}

}  // namespace

std::string formatBytes(std::int64_t bytes) {
  static char const* const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
  return formatScaled(static_cast<double>(std::max<std::int64_t>(0, bytes)), units,
                      sizeof(units) / sizeof(units[0]));
}

std::string formatRate(int bytes_per_second) {
  return formatBytes(bytes_per_second) + "/s";
}

std::string formatPercent(float value) {
  value = std::clamp(value, 0.0F, 1.0F);
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << (value * 100.0F) << '%';
  return out.str();
}

std::string sanitizeDisplayText(std::string value) {
  std::string sanitized;
  sanitized.reserve(value.size());

  for (std::size_t index = 0; index < value.size(); ++index) {
    auto const byte = static_cast<unsigned char>(value[index]);

    if (byte < 0x20 || byte == 0x7F) {
      sanitized.push_back(' ');
      continue;
    }

    if (byte == 0xC2 && index + 1 < value.size()) {
      auto const next = static_cast<unsigned char>(value[index + 1]);
      if (next >= 0x80 && next <= 0x9F) {
        sanitized.push_back(' ');
        ++index;
        continue;
      }
    }

    auto const sequence_length = utf8SequenceLength(byte);
    if (sequence_length > 0 && index + sequence_length <= value.size()) {
      bool valid_sequence = true;
      for (std::size_t offset = 1; offset < sequence_length; ++offset) {
        if (!isUtf8Continuation(static_cast<unsigned char>(value[index + offset]))) {
          valid_sequence = false;
          break;
        }
      }
      if (valid_sequence) {
        sanitized.append(value, index, sequence_length);
        index += sequence_length - 1;
        continue;
      }
    }

    if (byte >= 0x80 && byte <= 0x9F) {
      sanitized.push_back(' ');
      continue;
    }

    sanitized.push_back(value[index]);
  }

  return sanitized;
}

std::string ellipsize(std::string value, std::size_t width) {
  value = sanitizeDisplayText(std::move(value));
  if (displayWidth(value) <= width) {
    return value;
  }
  if (width == 0) {
    return {};
  }
  if (width <= 3) {
    return truncateCells(value, width);
  }
  value = truncateCells(value, width - 3);
  value += "...";
  return value;
}

std::string fitColumn(std::string value, std::size_t width) {
  value = ellipsize(std::move(value), width);
  std::size_t const cells = displayWidth(value);
  if (cells < width) {
    value.append(width - cells, ' ');
  }
  return value;
}

}  // namespace ovenbirdbt

