#include "seam/synthesis/phrase_backend.hpp"
#include <algorithm>
#include <cmath>

namespace seam::synthesis {
core::Result<PhraseAudio> assemblePhraseOutput(PhraseFrameRange output,
    std::span<const PhraseAudioView> chunks, std::stop_token stopToken) {
  const auto valid = output.validate();
  if (!valid) return core::Result<PhraseAudio>{valid.error()};
  if (chunks.empty() || chunks.size() > 4096U) return core::failure<PhraseAudio>(
      core::ErrorCode::InvalidArgument, "Phrase assembly requires bounded nonempty chunks");
  std::vector<const PhraseAudioView*> ordered;
  ordered.reserve(chunks.size());
  for (const auto& chunk : chunks) ordered.push_back(&chunk);
  std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->startFrame < b->startFrame; });
  auto cursor = output.start;
  for (const auto* chunk : ordered) {
    if (stopToken.stop_requested()) return core::failure<PhraseAudio>(core::ErrorCode::Conflict, "Phrase assembly cancelled");
    if (chunk->startFrame != cursor || chunk->samples.empty() ||
        chunk->samples.size() > static_cast<std::size_t>(output.end - cursor)) {
      return core::failure<PhraseAudio>(core::ErrorCode::Conflict,
          "Phrase chunks overlap, have a gap or exceed owned output");
    }
    cursor += static_cast<time::SampleFrame>(chunk->samples.size());
  }
  if (cursor != output.end) return core::failure<PhraseAudio>(core::ErrorCode::Conflict, "Phrase output coverage is incomplete");
  PhraseAudio result{output.start, {}};
  result.samples.reserve(static_cast<std::size_t>(output.end - output.start));
  for (const auto* chunk : ordered) for (std::size_t i = 0; i < chunk->samples.size(); ++i) {
    if (i % 4096U == 0U && stopToken.stop_requested()) return core::failure<PhraseAudio>(
        core::ErrorCode::Conflict, "Phrase assembly cancelled");
    const auto sample = chunk->samples[i];
    if (!std::isfinite(sample)) return core::failure<PhraseAudio>(
        core::ErrorCode::InvariantViolation, "Phrase chunk contains non-finite PCM");
    result.samples.push_back(sample);
  }
  return result;
}

core::Result<void> PhraseFrameRange::validate() const {
  constexpr time::SampleFrame maximumAbsoluteFrame = time::SampleFrame{1} << 52;
  constexpr time::SampleFrame maximumContextFrames = 32LL * 1024LL * 1024LL;
  if (start < -maximumAbsoluteFrame || end > maximumAbsoluteFrame ||
      end <= start || end - start > maximumContextFrames) {
    return core::failure(core::ErrorCode::InvalidArgument, "Phrase frame range exceeds bounds");
  }
  return core::success();
}

core::Result<void> PhraseOutputContract::validate() const {
  const auto valid = context.validate();
  if (!valid) return valid;
  if (sampleRate < 8000U || sampleRate > 384000U ||
      owned.start < context.start || owned.end > context.end || owned.end <= owned.start) {
    return core::failure(core::ErrorCode::InvalidArgument, "Phrase output context or ownership range is invalid");
  }
  return core::success();
}

core::Result<std::vector<PhraseFrameRange>> planOwnedPhraseWindows(PhraseFrameRange output,
    std::uint32_t maximumChunkFrames, std::size_t maximumChunks) {
  using Output = std::vector<PhraseFrameRange>;
  const auto valid = output.validate();
  if (!valid) return core::Result<Output>{valid.error()};
  if (maximumChunkFrames == 0U || maximumChunkFrames > 32U * 1024U * 1024U ||
      maximumChunks == 0U || maximumChunks > 4096U) return core::failure<Output>(
          core::ErrorCode::InvalidArgument, "Phrase chunk configuration exceeds bounds");
  const auto length = static_cast<std::uint64_t>(output.end - output.start);
  const auto count = 1U + (length - 1U) / maximumChunkFrames;
  if (count > maximumChunks) return core::failure<Output>(core::ErrorCode::Unsupported,
      "Phrase output requires more chunks than the configured budget");
  Output result;
  result.reserve(static_cast<std::size_t>(count));
  for (auto start = output.start; start < output.end;) {
    const auto end = start + std::min<time::SampleFrame>(maximumChunkFrames, output.end - start);
    result.push_back({start, end});
    start = end;
  }
  return result;
}

core::Result<PhraseAudio> finalizePhraseBackendAudio(const PhraseOutputContract& contract,
    PhraseAudio audio, std::stop_token stopToken) {
  const auto valid = contract.validate();
  if (!valid) return core::Result<PhraseAudio>{valid.error()};
  if (audio.startFrame != contract.context.start || audio.samples.size() !=
      static_cast<std::size_t>(contract.context.end - contract.context.start)) {
    return core::failure<PhraseAudio>(core::ErrorCode::Conflict,
        "Backend audio does not cover its declared context exactly");
  }
  for (std::size_t i = 0; i < audio.samples.size(); ++i) {
    if (i % 4096U == 0U && stopToken.stop_requested()) return core::failure<PhraseAudio>(
        core::ErrorCode::Conflict, "Phrase output finalization cancelled");
    if (!std::isfinite(audio.samples[i])) return core::failure<PhraseAudio>(
        core::ErrorCode::InvariantViolation, "Backend context contains non-finite PCM");
  }
  const auto offset = static_cast<std::size_t>(contract.owned.start - contract.context.start);
  const auto count = static_cast<std::size_t>(contract.owned.end - contract.owned.start);
  // In-place forward copy is safe because the destination precedes the source.
  if (offset != 0U) for (std::size_t i = 0; i < count; ++i) {
    if (i % 4096U == 0U && stopToken.stop_requested()) return core::failure<PhraseAudio>(
        core::ErrorCode::Conflict, "Phrase output finalization cancelled");
    audio.samples[i] = audio.samples[offset + i];
  }
  audio.samples.resize(count);
  audio.startFrame = contract.owned.start;
  return audio;
}
}
