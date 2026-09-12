#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"
#include <stop_token>
#include <span>
#include <vector>

namespace seam::synthesis {

struct PhraseAudio final {
  time::SampleFrame startFrame{0};
  std::vector<float> samples;
};
struct PhraseAudioView final {
  time::SampleFrame startFrame{0};
  std::span<const float> samples;
};

// Absolute, half-open output frames. Context may precede project frame zero;
// timeline clipping belongs to the publishing layer, not this contract.
struct PhraseFrameRange final {
  time::SampleFrame start{0};
  time::SampleFrame end{0};
  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const PhraseFrameRange&, const PhraseFrameRange&) = default;
};

[[nodiscard]] core::Result<std::vector<PhraseFrameRange>> planOwnedPhraseWindows(
    PhraseFrameRange output, std::uint32_t maximumChunkFrames, std::size_t maximumChunks = 4096U);
struct PhraseOutputContract final {
  std::uint32_t sampleRate{48000U};
  PhraseFrameRange context;
  PhraseFrameRange owned;
  [[nodiscard]] core::Result<void> validate() const;
};

// Requires complete context output, finite PCM and exact frame coordinates.
// Publishes only owned samples; never pads or shifts a short backend result.
[[nodiscard]] core::Result<PhraseAudio> finalizePhraseBackendAudio(
    const PhraseOutputContract& contract, PhraseAudio audio,
    std::stop_token stopToken = {});

// Accepts unordered chunks, but requires exact once-only coverage. Caller
// separately validates scheduler revision/cache identities before assembly.
[[nodiscard]] core::Result<PhraseAudio> assemblePhraseOutput(
    PhraseFrameRange output, std::span<const PhraseAudioView> chunks,
    std::stop_token stopToken = {});

}
