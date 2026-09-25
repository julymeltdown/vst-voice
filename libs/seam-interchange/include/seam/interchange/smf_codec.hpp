#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <limits>
#include <vector>

namespace seam::interchange {

struct SmfLimits final {
  std::size_t maximumBytes{64U * 1024U * 1024U};
  std::size_t maximumTracks{64U};
  // Source events accepted by decodeSmf, and diagnostic amplification budget.
  std::size_t maximumEvents{1'000'000U};
  // Canonical events admitted by encodeSmf. The default allows one synthesized
  // note-off per admitted missing-off note beyond the source-event ceiling.
  std::size_t maximumSerializedEvents{1'200'000U};
  std::size_t maximumNotes{200'000U};
  std::size_t maximumTextBytes{1U * 1024U * 1024U};
  std::int64_t maximumTick{std::numeric_limits<std::int64_t>::max() / 4};
};

enum class SmfIssueSeverity { Warning, Loss };

struct SmfIssue final {
  SmfIssueSeverity severity{SmfIssueSeverity::Warning};
  time::Tick tick{time::Tick{0}};
  std::string message;
  friend bool operator==(const SmfIssue&, const SmfIssue&) = default;
};

struct SmfTempo final {
  time::Tick tick{time::Tick{0}};
  double bpm{120.0};
  friend bool operator==(const SmfTempo&, const SmfTempo&) = default;
};

struct SmfMeter final {
  time::Tick tick{time::Tick{0}};
  std::uint8_t numerator{4U};
  std::uint8_t denominatorPower{2U};
  friend bool operator==(const SmfMeter&, const SmfMeter&) = default;
};

struct SmfText final {
  time::Tick tick{time::Tick{0}};
  std::string text;
  bool lyric{true};
  std::uint16_t track{0U};
  friend bool operator==(const SmfText&, const SmfText&) = default;
};

struct SmfNote final {
  time::Tick start{time::Tick{0}};
  time::Tick duration{time::Tick{1}};
  std::uint8_t midi{60U};
  std::uint8_t velocity{100U};
  std::uint8_t channel{0U};
  std::uint16_t track{0U};
  friend bool operator==(const SmfNote&, const SmfNote&) = default;
};

struct SmfTrack final {
  std::string name;
  friend bool operator==(const SmfTrack&, const SmfTrack&) = default;
};

struct SmfScore final {
  std::uint16_t ppq{480U};
  // Empty for constructed scores means one default track. Decoded files retain
  // source track order and names, including empty/conductor-only tracks.
  std::vector<SmfTrack> tracks;
  std::vector<SmfTempo> tempos;
  std::vector<SmfMeter> meters;
  std::vector<SmfText> texts;
  std::vector<SmfNote> notes;
  std::vector<SmfIssue> issues;

  [[nodiscard]] core::Result<void> validate(const SmfLimits& limits = {}) const;
  friend bool operator==(const SmfScore&, const SmfScore&) = default;
};

// Imports Type 0/1 PPQ Standard MIDI Files. SMPTE timing, executable/meta
// payloads and unbounded declared lengths are rejected before allocation.
[[nodiscard]] core::Result<SmfScore> decodeSmf(
    std::span<const std::uint8_t> bytes, SmfLimits limits = {});

// Emits a deterministic Type-1 PPQ file. Unsupported score metadata is never
// silently encoded: callers receive a bounded loss report through `issues`.
[[nodiscard]] core::Result<std::vector<std::uint8_t>> encodeSmf(
    const SmfScore& score, SmfLimits limits = {});

}  // namespace seam::interchange
