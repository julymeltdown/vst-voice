#include "seam/domain/phoneme.hpp"

#include <sstream>

namespace seam::domain {

std::string PhonemeKey::toString() const {
  std::ostringstream stream;
  stream << noteId.toString() << ':' << ordinal;
  return stream.str();
}

core::Result<void> PhonemeTiming::validate() const {
  if (startOffset.has_value() && endOffset.has_value() &&
      *startOffset >= *endOffset) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Phoneme start offset must be before its end offset");
  }
  constexpr time::Microseconds kMaxAbsoluteOffset = 60'000'000;
  if ((startOffset.has_value() &&
       (*startOffset < -kMaxAbsoluteOffset || *startOffset > kMaxAbsoluteOffset)) ||
      (endOffset.has_value() &&
       (*endOffset < -kMaxAbsoluteOffset || *endOffset > kMaxAbsoluteOffset))) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Phoneme timing offset exceeds the supported range");
  }
  return core::success();
}

core::Result<void> PhonemeOverride::validate() const {
  if (!key.noteId.valid()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Phoneme override must reference a valid note");
  }
  if (symbol.has_value() && symbol->empty()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Explicit phoneme symbol must not be empty",
                         key.toString());
  }
  return timing.validate();
}

core::Result<void> PhonemeToken::validate() const {
  if (!key.noteId.valid()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Phoneme token must reference a valid note");
  }
  if (symbol.empty()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Phoneme symbol must not be empty",
                         key.toString());
  }
  return timing.validate();
}

std::string_view phonemeRoleName(PhonemeRole role) noexcept {
  switch (role) {
    case PhonemeRole::Onset: return "onset";
    case PhonemeRole::Nucleus: return "nucleus";
    case PhonemeRole::Coda: return "coda";
    case PhonemeRole::Geminate: return "geminate";
    case PhonemeRole::Breath: return "breath";
    case PhonemeRole::Silence: return "silence";
  }
  return "nucleus";
}

PhonemeRole parsePhonemeRole(std::string_view value) noexcept {
  if (value == "onset") return PhonemeRole::Onset;
  if (value == "coda") return PhonemeRole::Coda;
  if (value == "geminate") return PhonemeRole::Geminate;
  if (value == "breath") return PhonemeRole::Breath;
  if (value == "silence") return PhonemeRole::Silence;
  return PhonemeRole::Nucleus;
}

}  // namespace seam::domain
