#include "seam/domain/note.hpp"

#include <limits>

namespace seam::domain {

core::Result<void> Note::validate() const {
  if (!id.valid()) {
    return core::failure(core::ErrorCode::InvariantViolation, "Note ID must be valid");
  }
  if (startTick < time::Tick{0}) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Note start tick must not be negative", id.toString());
  }
  if (durationTick <= time::Tick{0}) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Note duration must be positive", id.toString());
  }
  if (midiKey > 127) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "MIDI key must be in the range 0..127", id.toString());
  }
  if (!lyricTokenId.valid()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Note must reference a lyric token", id.toString());
  }
  return core::success();
}

std::string toUtf8(const std::u32string& text) {
  std::string output;
  output.reserve(text.size());
  for (const auto codePoint : text) {
    const auto value = static_cast<std::uint32_t>(codePoint);
    if (value <= 0x7Fu) {
      output.push_back(static_cast<char>(value));
    } else if (value <= 0x7FFu) {
      output.push_back(static_cast<char>(0xC0u | (value >> 6u)));
      output.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    } else if (value <= 0xFFFFu) {
      output.push_back(static_cast<char>(0xE0u | (value >> 12u)));
      output.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3Fu)));
      output.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    } else if (value <= 0x10FFFFu) {
      output.push_back(static_cast<char>(0xF0u | (value >> 18u)));
      output.push_back(static_cast<char>(0x80u | ((value >> 12u) & 0x3Fu)));
      output.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3Fu)));
      output.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    }
  }
  return output;
}

core::Result<std::u32string> fromUtf8(const std::string& text) {
  std::u32string output;
  for (std::size_t index = 0; index < text.size();) {
    const auto first = static_cast<unsigned char>(text[index]);
    std::uint32_t value = 0;
    std::size_t length = 0;
    if ((first & 0x80u) == 0) {
      value = first;
      length = 1;
    } else if ((first & 0xE0u) == 0xC0u) {
      value = first & 0x1Fu;
      length = 2;
    } else if ((first & 0xF0u) == 0xE0u) {
      value = first & 0x0Fu;
      length = 3;
    } else if ((first & 0xF8u) == 0xF0u) {
      value = first & 0x07u;
      length = 4;
    } else {
      return core::failure<std::u32string>(core::ErrorCode::ParseError,
                                           "Invalid UTF-8 leading byte");
    }
    if (index + length > text.size()) {
      return core::failure<std::u32string>(core::ErrorCode::ParseError,
                                           "Truncated UTF-8 sequence");
    }
    for (std::size_t continuation = 1; continuation < length; ++continuation) {
      const auto byte = static_cast<unsigned char>(text[index + continuation]);
      if ((byte & 0xC0u) != 0x80u) {
        return core::failure<std::u32string>(core::ErrorCode::ParseError,
                                             "Invalid UTF-8 continuation byte");
      }
      value = (value << 6u) | (byte & 0x3Fu);
    }
    const bool overlong = (length == 2 && value < 0x80u) ||
                          (length == 3 && value < 0x800u) ||
                          (length == 4 && value < 0x10000u);
    if (overlong || value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) {
      return core::failure<std::u32string>(core::ErrorCode::ParseError,
                                           "Invalid UTF-8 code point");
    }
    output.push_back(static_cast<char32_t>(value));
    index += length;
  }
  return core::success(std::move(output));
}

}  // namespace seam::domain
