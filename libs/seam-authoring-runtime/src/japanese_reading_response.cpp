#include "seam/authoring/japanese_reading_response.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/core/sha256.hpp"
#include "seam/domain/note.hpp"

namespace seam::authoring {
core::Result<phonemizer::JapaneseReadingResult> decodeJapaneseReadingResponse(
    std::string_view source, std::string_view response,
    const phonemizer::JapaneseReadingIdentity& verifiedIdentity, std::stop_token stop) {
  using Output = phonemizer::JapaneseReadingResult;
  const auto fail = [](const char* message) { return core::failure<Output>(core::ErrorCode::InvalidArgument, message); };
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Reading response decoding cancelled"); };
  if (stop.stop_requested()) return cancelled();
  if (source.empty() || source.size() > 4096U || source.find('\0') != std::string_view::npos ||
      !domain::fromUtf8(std::string(source))) return fail("Reading request exceeds UTF-8 input limits");
  if (response.empty() || response.size() > 1048576U) return fail("Reading response exceeds byte limit");
  const auto parsed = formats::parseJson(response, {.maximumInputBytes = 1048576U, .maximumDepth = 8U,
      .maximumNodes = 32780U, .maximumStringBytes = 4096U, .maximumCollectionEntries = 4096U});
  if (!parsed) return core::Result<Output>{parsed.error()};
  if (stop.stop_requested()) return cancelled();
  const auto& root = parsed.value();
  if (!root.isObject() || root.asObject().size() != 3U) return fail("Reading response root schema mismatch");
  const auto* version = root.find("schemaVersion"); const auto* echoed = root.find("source"); const auto* tokens = root.find("tokens");
  if (!version || !version->isInteger() || version->asInt64() != 1 || !echoed || !echoed->isString() ||
      echoed->asString() != source || !tokens || !tokens->isArray()) return fail("Reading response version or source mismatch");
  Output output{verifiedIdentity, core::sha256Hex(source), {}}; output.tokens.reserve(tokens->asArray().size());
  for (const auto& entry : tokens->asArray()) {
    if (stop.stop_requested()) return cancelled();
    if (!entry.isObject() || entry.asObject().size() != 6U) return fail("Reading token schema mismatch");
    const auto* offset = entry.find("byteOffset"); const auto* length = entry.find("byteLength");
    const auto* surface = entry.find("surface"); const auto* status = entry.find("status");
    const auto* reading = entry.find("reading"); const auto* pronunciation = entry.find("pronunciation");
    if (!offset || !offset->isInteger() || offset->asInt64() < 0 || offset->asInt64() > 4096 ||
        !length || !length->isInteger() || length->asInt64() <= 0 || length->asInt64() > 4096 ||
        !surface || !surface->isString() || !status || !status->isString() ||
        !reading || (!reading->isString() && !reading->isNull()) ||
        !pronunciation || (!pronunciation->isString() && !pronunciation->isNull())) return fail("Reading token field types or bounds mismatch");
    phonemizer::JapaneseReadingStatus state;
    if (status->asString() == "known") state = phonemizer::JapaneseReadingStatus::Known;
    else if (status->asString() == "unknown") state = phonemizer::JapaneseReadingStatus::Unknown;
    else if (status->asString() == "missing-reading") state = phonemizer::JapaneseReadingStatus::MissingReading;
    else return fail("Reading token status is unsupported");
    output.tokens.push_back({static_cast<std::size_t>(offset->asInt64()), static_cast<std::size_t>(length->asInt64()),
        surface->asString(), state, reading->isNull() ? std::nullopt : std::optional{reading->asString()},
        pronunciation->isNull() ? std::nullopt : std::optional{pronunciation->asString()}});
  }
  const auto valid = phonemizer::validateJapaneseReading(source, output, verifiedIdentity, stop);
  if (!valid) return core::Result<Output>{valid.error()};
  return output;
}
}
