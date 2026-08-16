#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/ids.hpp"
#include "seam/time/tick.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace seam::domain {

enum class Language { Unspecified, Japanese, Korean, English };

enum class NoteArticulation { Normal, Legato, Staccato };

struct LyricToken final {
  LyricTokenId id;
  std::u32string surface;
  Language language{Language::Unspecified};

  friend bool operator==(const LyricToken&, const LyricToken&) = default;
};

struct Note final {
  NoteId id;
  time::Tick startTick;
  time::Tick durationTick{time::Tick{time::kDefaultPpq}};
  std::uint8_t midiKey{60};
  LyricTokenId lyricTokenId;
  NoteArticulation articulation{NoteArticulation::Normal};
  std::optional<std::uint64_t> slurGroup;

  [[nodiscard]] time::Tick endTick() const noexcept { return startTick + durationTick; }
  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const Note&, const Note&) = default;
};

[[nodiscard]] std::string toUtf8(const std::u32string& text);
[[nodiscard]] core::Result<std::u32string> fromUtf8(const std::string& text);

}  // namespace seam::domain
