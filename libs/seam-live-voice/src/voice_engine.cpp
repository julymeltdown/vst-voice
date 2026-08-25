#include "seam/live_voice/voice_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace seam::live_voice {
namespace {

std::atomic<std::uint64_t> callPathCounter{0U};

phase12c::UnitKind mapRole(LiveSegmentRole role) noexcept {
  switch (role) {
    case LiveSegmentRole::Attack: return phase12c::UnitKind::Attack;
    case LiveSegmentRole::Transition: return phase12c::UnitKind::Transition;
    case LiveSegmentRole::Sustain: return phase12c::UnitKind::Sustain;
    case LiveSegmentRole::Release: return phase12c::UnitKind::Release;
    case LiveSegmentRole::Breath: return phase12c::UnitKind::Attack;
  }
  return phase12c::UnitKind::Sustain;
}

core::Result<std::shared_ptr<const phase12c::LiveVoicebankResource>>
convertResources(const LiveVoicebankResources& resources) {
  if (resources.units.empty() || resources.decodedBytes == 0U) {
    return core::failure<
        std::shared_ptr<const phase12c::LiveVoicebankResource>>(
        core::ErrorCode::InvalidArgument,
        "Live resource publication requires decoded units");
  }
  auto result = std::make_shared<phase12c::LiveVoicebankResource>();
  result->trusted = true;
  result->contentHash = resources.identity.contentHash;
  result->sampleRate = resources.units.front().sourceSampleRate;
  std::size_t frameCount = 0U;
  std::unordered_map<const std::vector<float>*, std::uint32_t> bases;
  for (const auto& unit : resources.units) {
    if (unit.mono == nullptr || unit.sourceEnd <= unit.sourceStart ||
        unit.sourceEnd > unit.mono->size() || unit.loopStart < unit.sourceStart ||
        unit.loopEnd > unit.sourceEnd || unit.loopStart >= unit.loopEnd ||
        unit.releaseStart < unit.loopEnd || unit.releaseStart > unit.sourceEnd) {
      return core::failure<
          std::shared_ptr<const phase12c::LiveVoicebankResource>>(
          core::ErrorCode::InvariantViolation,
          "Live resource unit bounds are invalid", unit.unitId);
    }
    if (!bases.contains(unit.mono.get())) {
      if (frameCount > std::numeric_limits<std::size_t>::max() -
                           unit.mono->size()) {
        return core::failure<
            std::shared_ptr<const phase12c::LiveVoicebankResource>>(
            core::ErrorCode::Unsupported,
            "Live resource frame count overflows");
      }
      bases.emplace(unit.mono.get(), static_cast<std::uint32_t>(frameCount));
      frameCount += unit.mono->size();
    }
  }
  result->mono.reserve(frameCount);
  for (const auto& unit : resources.units) {
    const auto base = bases.at(unit.mono.get());
    if (result->mono.size() <
        static_cast<std::size_t>(base) + unit.mono->size()) {
      result->mono.insert(result->mono.end(), unit.mono->begin(),
                          unit.mono->end());
    }
    result->units.push_back(phase12c::UnitSpan{
        .kind = mapRole(unit.role),
        .begin = base + unit.sourceStart,
        .end = base + unit.sourceEnd,
        .loopBegin = base + unit.loopStart,
        .loopEnd = base + unit.loopEnd,
        .rootKey = static_cast<std::int16_t>(unit.rootMidi),
        .fromKey = -1,
        .toKey = -1,
    });
  }
  if (!result->valid()) {
    return core::failure<
        std::shared_ptr<const phase12c::LiveVoicebankResource>>(
        core::ErrorCode::InvariantViolation,
        "Converted live resource failed validation");
  }
  return std::shared_ptr<const phase12c::LiveVoicebankResource>{
      std::move(result)};
}

}

VoiceEngine::VoiceEngine() : engine_(false) {}

void VoiceEngine::resetCallPathEvidence() noexcept {
  callPathCounter.store(0U, std::memory_order_release);
}

std::uint64_t VoiceEngine::callPathEvidence() noexcept {
  return callPathCounter.load(std::memory_order_acquire);
}

void VoiceEngine::configure(std::uint32_t sampleRate,
                            std::uint32_t outputChannels) noexcept {
  engine_.configure(sampleRate, outputChannels);
}

void VoiceEngine::setOutputSampleRate(double sampleRate) noexcept {
  if (std::isfinite(sampleRate) && sampleRate >= 8000.0 &&
      sampleRate <= 192000.0) {
    configure(static_cast<std::uint32_t>(sampleRate), 2U);
  }
}

bool VoiceEngine::publishResource(
    std::shared_ptr<const LiveVoicebankResource> resource) noexcept {
  return engine_.publishResource(std::move(resource));
}

bool VoiceEngine::publishVoicebankResource(
    std::shared_ptr<const LiveVoicebankResource> resource) noexcept {
  return publishResource(std::move(resource));
}

bool VoiceEngine::publishResources(
    std::shared_ptr<const LiveVoicebankResources> resources) noexcept {
  if (!resources) {
    engine_.clearResource();
    return false;
  }
  const auto converted = convertResources(*resources);
  if (!converted) {
    engine_.clearResource();
    return false;
  }
  return engine_.publishResource(converted.value());
}

void VoiceEngine::clearResource() noexcept { engine_.clearResource(); }

void VoiceEngine::clearVoicebankResource() noexcept { clearResource(); }

void VoiceEngine::process(std::span<const LiveEvent> events,
                          float* const* outputs,
                          std::uint32_t channels,
                          std::uint32_t frames) noexcept {
  callPathCounter.fetch_add(1U, std::memory_order_relaxed);
  engine_.process(events, outputs, channels, frames);
}

void VoiceEngine::dispatch(const LiveEvent& event) noexcept {
  callPathCounter.fetch_add(1U, std::memory_order_relaxed);
  engine_.dispatch(event);
}

void VoiceEngine::dispatchLiveEvent(const LiveEvent& event) noexcept {
  dispatch(event);
}

void VoiceEngine::renderLiveRange(float* const* outputs,
                                  std::uint32_t channels,
                                  std::uint32_t beginFrame,
                                  std::uint32_t endFrame) noexcept {
  if (outputs == nullptr || channels == 0U || endFrame <= beginFrame) return;
  std::array<float*, phase12c::kMaxVoices / 4U> sliced{};
  const auto boundedChannels = std::min<std::uint32_t>(channels, sliced.size());
  for (std::uint32_t channel = 0U; channel < boundedChannels; ++channel) {
    sliced[channel] = outputs[channel] + beginFrame;
  }
  process({}, sliced.data(), boundedChannels, endFrame - beginFrame);
}

void VoiceEngine::reset() noexcept { engine_.reset(); }

void VoiceEngine::noteOn(std::int32_t noteId, std::int32_t key,
                         float velocity) noexcept {
  callPathCounter.fetch_add(1U, std::memory_order_relaxed);
  engine_.noteOn(noteId, key, velocity);
}

void VoiceEngine::noteOff(std::int32_t noteId, std::int32_t key) noexcept {
  callPathCounter.fetch_add(1U, std::memory_order_relaxed);
  engine_.noteOff(noteId, key);
}

void VoiceEngine::choke(std::int32_t noteId, std::int32_t key) noexcept {
  callPathCounter.fetch_add(1U, std::memory_order_relaxed);
  engine_.choke(noteId, key);
}

float VoiceEngine::renderSample() noexcept {
  callPathCounter.fetch_add(1U, std::memory_order_relaxed);
  return engine_.renderSample();
}

std::size_t VoiceEngine::activeVoiceCount() const noexcept {
  return engine_.activeVoiceCount();
}

LiveStats VoiceEngine::stats() const noexcept { return engine_.stats(); }

}
