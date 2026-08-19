#include "seam/clap_editor/editor_runtime.hpp"
#include "human_vowel_data.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace seam::clap_editor {
namespace {
constexpr std::uint32_t kSourceSampleRate = asset::kSampleRate;
constexpr double kSourceRootMidi = 67.0;
float sampleAt(double position) noexcept {
  if (asset::kFrameCount < 2U) return 0.0F;
  const auto loopStart = std::min(asset::kLoopStart, asset::kFrameCount - 2U);
  const auto loopEnd = std::max(loopStart + 2U,
                                std::min(asset::kLoopEnd, asset::kFrameCount));
  if (!std::isfinite(position) || position < 0.0) position = 0.0;
  if (position >= static_cast<double>(loopEnd)) {
    const auto loopLength = static_cast<double>(loopEnd - loopStart);
    position = static_cast<double>(loopStart) +
               std::fmod(position - static_cast<double>(loopStart), loopLength);
  }
  const auto first = std::min(static_cast<std::size_t>(position),
                              asset::kFrameCount - 1U);
  const auto second = std::min(first + 1U, asset::kFrameCount - 1U);
  const auto fraction = static_cast<float>(position - static_cast<double>(first));
  const auto left = static_cast<float>(asset::kPcm[first]) / 32768.0F;
  const auto right = static_cast<float>(asset::kPcm[second]) / 32768.0F;
  return left + (right - left) * fraction;
}
}  // namespace

std::string_view previewStatusName(PreviewStatus status) noexcept {
  switch (status) {
    case PreviewStatus::Empty: return "empty";
    case PreviewStatus::Ready: return "ready";
    case PreviewStatus::VoicebankMissing: return "voicebank-missing";
    case PreviewStatus::VoicebankVersionMismatch: return "voicebank-version-mismatch";
    case PreviewStatus::VoicebankContentHashMissing: return "voicebank-content-hash-missing";
    case PreviewStatus::VoicebankContentMismatch: return "voicebank-content-mismatch";
    case PreviewStatus::VoicebankUntrusted: return "voicebank-untrusted";
    case PreviewStatus::Failed: return "failed";
  }
  return "unknown";
}


RealtimePreviewPublication::ReadHandle::ReadHandle(
    const RealtimePreviewPublication* owner, std::size_t slot,
    const RenderedPreview* value) noexcept
    : owner_(owner), slot_(slot), value_(value) {}

RealtimePreviewPublication::ReadHandle::ReadHandle(ReadHandle&& other) noexcept
    : owner_(other.owner_), slot_(other.slot_), value_(other.value_) {
  other.owner_ = nullptr;
  other.value_ = nullptr;
}

RealtimePreviewPublication::ReadHandle&
RealtimePreviewPublication::ReadHandle::operator=(ReadHandle&& other) noexcept {
  if (this == &other) return *this;
  release();
  owner_ = other.owner_;
  slot_ = other.slot_;
  value_ = other.value_;
  other.owner_ = nullptr;
  other.value_ = nullptr;
  return *this;
}

RealtimePreviewPublication::ReadHandle::~ReadHandle() { release(); }

void RealtimePreviewPublication::ReadHandle::release() noexcept {
  if (owner_ != nullptr) {
    owner_->slots_[slot_].readers.fetch_sub(1U, std::memory_order_release);
  }
  owner_ = nullptr;
  value_ = nullptr;
}

RealtimePreviewPublication::RealtimePreviewPublication() {
  slots_[0].preview = RenderedPreview{};
}

RealtimePreviewPublication::ReadHandle
RealtimePreviewPublication::acquire() const noexcept {
  for (;;) {
    const auto slot = published_.load(std::memory_order_acquire);
    slots_[slot].readers.fetch_add(1U, std::memory_order_acquire);
    if (slot == published_.load(std::memory_order_acquire)) {
      return ReadHandle{this, slot, &slots_[slot].preview};
    }
    slots_[slot].readers.fetch_sub(1U, std::memory_order_release);
  }
}

bool RealtimePreviewPublication::publish(RenderedPreview preview) {
  std::scoped_lock lock(writerMutex_);
  const auto current = published_.load(std::memory_order_acquire);
  for (std::size_t offset = 1U; offset < kSlotCount; ++offset) {
    const auto candidate = (current + offset) % kSlotCount;
    if (slots_[candidate].readers.load(std::memory_order_acquire) != 0U) {
      continue;
    }
    slots_[candidate].preview = std::move(preview);
    published_.store(candidate, std::memory_order_release);
    return true;
  }
  return false;
}

void LiveSampleInstrument::reset() noexcept {
  voices_ = {};
}

void LiveSampleInstrument::setOutputSampleRate(double sampleRate) noexcept {
  if (std::isfinite(sampleRate) && sampleRate >= 8000.0 &&
      sampleRate <= 192000.0) {
    outputSampleRate_ = sampleRate;
  }
}

LiveVoice* LiveSampleInstrument::allocateVoice() noexcept {
  const auto free = std::find_if(voices_.begin(), voices_.end(),
                                 [](const LiveVoice& voice) {
                                   return !voice.active;
                                 });
  if (free != voices_.end()) return &*free;
  return &*std::min_element(voices_.begin(), voices_.end(),
                            [](const LiveVoice& lhs, const LiveVoice& rhs) {
                              return lhs.envelope < rhs.envelope;
                            });
}

void LiveSampleInstrument::noteOn(std::int32_t noteId, std::int16_t key,
                                  float velocity) noexcept {
  auto* voice = allocateVoice();
  *voice = LiveVoice{
      .active = true,
      .releasing = false,
      .noteId = noteId,
      .key = key,
      .samplePosition = 0.0,
      .pitchRatio =
          std::pow(2.0, (static_cast<double>(key) - kSourceRootMidi) / 12.0) *
          static_cast<double>(kSourceSampleRate) / outputSampleRate_,
      .envelope = 0.0F,
      .velocity = std::clamp(velocity, 0.0F, 1.0F),
  };
}

void LiveSampleInstrument::noteOff(std::int32_t noteId,
                                   std::int16_t key) noexcept {
  for (auto& voice : voices_) {
    if (!voice.active) continue;
    if ((noteId >= 0 && voice.noteId == noteId) ||
        (noteId < 0 && voice.key == key)) {
      voice.releasing = true;
    }
  }
}

void LiveSampleInstrument::choke(std::int32_t noteId,
                                 std::int16_t key) noexcept {
  for (auto& voice : voices_) {
    if (!voice.active) continue;
    if ((noteId >= 0 && voice.noteId == noteId) ||
        (noteId < 0 && voice.key == key)) {
      voice = LiveVoice{};
    }
  }
}

float LiveSampleInstrument::renderSample() noexcept {
  float mixed = 0.0F;
  for (auto& voice : voices_) {
    if (!voice.active) continue;
    if (voice.releasing) {
      voice.envelope = std::max(0.0F, voice.envelope - 0.0018F);
    } else {
      voice.envelope = std::min(1.0F, voice.envelope + 0.0045F);
    }
    if (voice.releasing && voice.envelope <= 0.0F) {
      voice.active = false;
      continue;
    }
    mixed += sampleAt(voice.samplePosition) * voice.envelope *
             voice.velocity * 0.35F;
    voice.samplePosition += voice.pitchRatio;
  }
  return std::clamp(mixed, -1.0F, 1.0F);
}

std::size_t LiveSampleInstrument::activeVoiceCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(
      voices_.begin(), voices_.end(),
      [](const LiveVoice& voice) { return voice.active; }));
}

}  // namespace seam::clap_editor
