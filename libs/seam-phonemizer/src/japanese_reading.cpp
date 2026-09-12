#include "seam/phonemizer/japanese_reading.hpp"
#include "seam/core/sha256.hpp"
#include "seam/domain/note.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include <algorithm>

namespace seam::phonemizer {
namespace {
bool hex(std::string_view value, std::size_t size) {
  return value.size() == size && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
bool boundary(std::string_view source, std::size_t position) {
  return position == source.size() || (static_cast<unsigned char>(source[position]) & 0xc0U) != 0x80U;
}
bool whitespace(std::string_view text) {
  for (std::size_t i = 0U; i < text.size();) {
    if (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n') ++i;
    else if (text.substr(i, 3U) == "\xe3\x80\x80") i += 3U;
    else return false;
  }
  return true;
}
}
core::Result<void> validateJapaneseReading(std::string_view source, const JapaneseReadingResult& result,
    const JapaneseReadingIdentity& expected, std::stop_token stop) {
  const auto fail = [](const char* message) { return core::failure(core::ErrorCode::InvalidArgument, message); };
  if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Reading validation cancelled");
  if (source.empty() || source.size() > 4096U || source.find('\0') != std::string_view::npos ||
      !domain::fromUtf8(std::string(source))) return fail("Reading source must be valid nonempty UTF-8 within 4096 bytes");
  if (!hex(expected.engineRevision, 40U) || !hex(expected.dictionarySha256, 64U) || !hex(expected.helperSha256, 64U) || result.identity != expected ||
      result.sourceSha256 != core::sha256Hex(source)) return fail("Reading source or resource identity mismatch");
  if (result.tokens.size() > 4096U) return fail("Reading token count exceeds bounds");
  std::size_t end = 0U, textBudget = 65536U;
  for (const auto& token : result.tokens) {
    if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Reading validation cancelled");
    if (token.byteOffset < end || token.byteOffset > source.size() || token.byteLength == 0U ||
        token.byteLength > source.size() - token.byteOffset || !boundary(source, token.byteOffset) ||
        !boundary(source, token.byteOffset + token.byteLength) ||
        !whitespace(source.substr(end, token.byteOffset - end)) || token.surface != source.substr(token.byteOffset, token.byteLength))
      return fail("Reading token spans do not faithfully cover the source");
    end = token.byteOffset + token.byteLength;
    if (token.status != JapaneseReadingStatus::Known && token.status != JapaneseReadingStatus::Unknown &&
        token.status != JapaneseReadingStatus::MissingReading) return fail("Unknown reading status value");
    if (token.status == JapaneseReadingStatus::Known) {
      if (!token.lexicalReading || !token.pronunciation) return fail("Known token is missing reading data");
    } else if (token.lexicalReading || token.pronunciation) return fail("Unavailable reading must not contain invented reading data");
    for (const auto* value : {&token.lexicalReading, &token.pronunciation}) if (*value) {
      const auto& text = **value;
      if (text.empty() || text == "*" || text.size() > 4096U || text.size() > textBudget ||
          text.find('\0') != std::string::npos || !domain::fromUtf8(text)) return fail("Reading text exceeds encoding or size bounds");
      textBudget -= text.size();
    }
  }
  if (!whitespace(source.substr(end))) return fail("Reading result omitted non-whitespace source text");
  return core::success();
}

core::Result<std::vector<JapaneseReadingPhoneProjection>> projectJapaneseReadingPhones(
    const JapaneseReadingResult& result, std::stop_token stop) {
  using Output = std::vector<JapaneseReadingPhoneProjection>;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Reading phone projection cancelled");
  if (result.tokens.size() > 4096U) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Reading phone projection exceeds token bounds");
  Output output; output.reserve(result.tokens.size());
  for (std::size_t index = 0U; index < result.tokens.size(); ++index) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Reading phone projection cancelled");
    const auto& token = result.tokens[index];
    JapaneseReadingPhoneProjection projection; projection.readingTokenIndex = index;
    if (token.status == JapaneseReadingStatus::Known) {
      if (!token.pronunciation) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Known reading has no pronunciation");
      const auto text = domain::fromUtf8(*token.pronunciation);
      if (!text) return core::Result<Output>{text.error()};
      domain::VocalRegion region; region.id = domain::RegionId{1U};
      domain::LyricToken lyric; lyric.id = domain::LyricTokenId{1U}; lyric.surface = text.value(); lyric.language = domain::Language::Japanese;
      domain::Note note; note.id = domain::NoteId{1U}; note.durationTick = time::Tick{960}; note.lyricTokenId = lyric.id; note.midiKey = 60U;
      region.lyrics.push_back(std::move(lyric)); region.notes.push_back(std::move(note));
      const auto parsed = JapaneseKanaPhonemizer{}.phonemize(region, stop, 4096U);
      if (!parsed) return core::Result<Output>{parsed.error()};
      if (!parsed.value().warnings.empty()) return core::failure<Output>(core::ErrorCode::Unsupported,
          "Reading pronunciation is outside the SEAM Japanese phone inventory");
      for (const auto& phone : parsed.value().tokens) projection.phones.push_back(phone.symbol);
      if (projection.phones.empty()) return core::failure<Output>(core::ErrorCode::Unsupported, "Known reading produced no phones");
    }
    output.push_back(std::move(projection));
  }
  return output;
}
}
