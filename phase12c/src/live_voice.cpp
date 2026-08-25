#include "seam/phase12c/live_voice.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::phase12c {
namespace {
constexpr double kPi = 3.14159265358979323846;

float interpolate(const LiveVoicebankResource& resource,
                  double position,
                  bool loop,
                  const UnitSpan& unit) noexcept {
  if (resource.mono.empty() || unit.end <= unit.begin) {
    return 0.0F;
  }

  if (loop) {
    const auto loopBegin = std::clamp(unit.loopBegin, unit.begin, unit.end - 1);
    const auto loopEnd = std::clamp(unit.loopEnd, loopBegin + 1, unit.end);
    if (position >= static_cast<double>(loopEnd)) {
      position = static_cast<double>(loopBegin) +
                 std::fmod(position - static_cast<double>(loopBegin),
                           static_cast<double>(loopEnd - loopBegin));
    }
  }

  if (position < static_cast<double>(unit.begin)) {
    position = static_cast<double>(unit.begin);
  }
  if (position >= static_cast<double>(unit.end)) {
    position = static_cast<double>(unit.end - 1);
  }

  const auto first = std::min<std::size_t>(
      static_cast<std::size_t>(position), resource.mono.size() - 1);
  const auto second = std::min(first + 1, resource.mono.size() - 1);
  const auto fraction = static_cast<float>(position - static_cast<double>(first));
  return resource.mono[first] +
         (resource.mono[second] - resource.mono[first]) * fraction;
}
}  // namespace

std::size_t LiveVoicebankResource::bytes() const noexcept {
  return mono.size() * sizeof(float) + units.size() * sizeof(UnitSpan) +
         contentHash.size();
}

bool LiveVoicebankResource::valid() const noexcept {
  if (!trusted || sampleRate < 8000 || sampleRate > 192000 || mono.empty() ||
      bytes() > kMaxResourceBytes) {
    return false;
  }
  for (const auto& unit : units) {
    if (unit.begin >= unit.end || unit.end > mono.size() ||
        unit.loopBegin < unit.begin || unit.loopEnd > unit.end ||
        unit.loopBegin >= unit.loopEnd) {
      return false;
    }
  }
  return true;
}

bool ResourcePublisher::publish(
    std::shared_ptr<const LiveVoicebankResource> resource) noexcept {
  if (!resource || !resource->valid()) {
    clear();
    return false;
  }

  const auto current = published_.load(std::memory_order_acquire);
  const auto audio = audio_.load(std::memory_order_acquire);
  int slot = -1;
  for (int candidate = 0; candidate < 3; ++candidate) {
    if (candidate != current && candidate != audio) {
      slot = candidate;
      break;
    }
  }
  if (slot < 0) {
    return false;
  }

  slots_[static_cast<std::size_t>(slot)] = std::move(resource);
  published_.store(slot, std::memory_order_release);
  generation_.fetch_add(1, std::memory_order_release);
  return true;
}

void ResourcePublisher::clear() noexcept {
  published_.store(-1, std::memory_order_release);
  generation_.fetch_add(1, std::memory_order_release);
}

const LiveVoicebankResource* ResourcePublisher::acquireForAudio() noexcept {
  const auto slot = published_.load(std::memory_order_acquire);
  if (slot < 0) {
    return nullptr;
  }
  audio_.store(slot, std::memory_order_release);
  return slots_[static_cast<std::size_t>(slot)].get();
}

void ResourcePublisher::releaseFromAudio() noexcept {
  audio_.store(-1, std::memory_order_release);
}

std::uint64_t ResourcePublisher::generation() const noexcept {
  return generation_.load(std::memory_order_acquire);
}

LiveVoiceEngine::LiveVoiceEngine(bool enableEmbeddedFixture) {
  static_cast<void>(enableEmbeddedFixture);
  configure(48000, 2);
}

void LiveVoiceEngine::configure(std::uint32_t sampleRate,
                                std::uint32_t outputChannels) noexcept {
  sampleRate_ = std::clamp(sampleRate, 8000u, 192000u);
  outputChannels_ = std::clamp(outputChannels, 1u, 8u);
}

bool LiveVoiceEngine::publishResource(
    std::shared_ptr<const LiveVoicebankResource> resource) noexcept {
  return publisher_.publish(std::move(resource));
}

void LiveVoiceEngine::clearResource() noexcept {
  publisher_.clear();
}

void LiveVoiceEngine::reset() noexcept {
  for (auto& voice : voices_) {
    voice = {};
  }
  channelBend_.fill(0.0F);
  stats_ = {};
  resource_ = nullptr;
  resourceGeneration_ = 0;
}

std::size_t LiveVoiceEngine::activeVoiceCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(
      voices_.begin(), voices_.end(),
      [](const Voice& voice) { return voice.active; }));
}

LiveStats LiveVoiceEngine::stats() const noexcept { return stats_; }

const UnitSpan* LiveVoiceEngine::choose(UnitKind kind,
                                        int key,
                                        int from,
                                        int to) const noexcept {
  if (!resource_) {
    return nullptr;
  }
  const UnitSpan* best = nullptr;
  auto bestDistance = std::numeric_limits<int>::max();
  for (const auto& unit : resource_->units) {
    if (unit.kind != kind) {
      continue;
    }
    if (kind == UnitKind::Transition) {
      if (from >= 0 && unit.fromKey >= 0 && unit.fromKey != from) {
        continue;
      }
      if (to >= 0 && unit.toKey >= 0 && unit.toKey != to) {
        continue;
      }
    }
    const auto distance = std::abs(static_cast<int>(unit.rootKey) - key);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = &unit;
    }
  }
  return best;
}

LiveVoiceEngine::Voice* LiveVoiceEngine::findLegatoSource(
    std::int16_t channel) noexcept {
  Voice* latest = nullptr;
  for (auto& voice : voices_) {
    if (voice.active && !voice.releasing && voice.channel == channel &&
        (!latest || voice.age > latest->age)) {
      latest = &voice;
    }
  }
  return latest;
}

LiveVoiceEngine::Voice* LiveVoiceEngine::allocateVoice() noexcept {
  for (auto& voice : voices_) {
    if (!voice.active) {
      return &voice;
    }
  }

  auto* candidate = &voices_.front();
  for (auto& voice : voices_) {
    if (voice.envelope < candidate->envelope ||
        (voice.envelope == candidate->envelope && voice.age < candidate->age)) {
      candidate = &voice;
    }
  }

  candidate->tailSample = candidate->lastSample;
  candidate->tailLength = std::max(
      64u, static_cast<std::uint32_t>(std::ceil(sampleRate_ * 0.0015)));
  candidate->tailRemaining = candidate->tailLength;
  ++stats_.steals;
  return candidate;
}

void LiveVoiceEngine::beginRelease(Voice& voice, bool choke) noexcept {
  voice.releasing = true;
  if (choke) {
    voice.tailSample = voice.lastSample;
    voice.tailLength = std::max(
        64u, static_cast<std::uint32_t>(std::ceil(sampleRate_ * 0.0015)));
    voice.tailRemaining = voice.tailLength;
    voice.envelope = 0.0F;
    voice.releasing = true;
    voice.stage = VoiceStage::Release;
    if (!voice.current) {
      voice.current = voice.sustain;
      voice.position = voice.current ? voice.current->begin : 0.0;
    }
    return;
  }

  if (voice.release) {
    voice.current = voice.release;
    voice.position = voice.release->begin;
    voice.stage = VoiceStage::Release;
  } else {
    voice.stage = VoiceStage::Release;
  }
}

void LiveVoiceEngine::applyEvent(
    const LiveEvent& event,
    const LiveVoicebankResource* resource) noexcept {
  resource_ = resource;
  if (!resource) {
    return;
  }

  const auto channel = static_cast<std::int16_t>(
      std::clamp<int>(event.channel, 0, 15));

  if (event.type == EventType::Midi1) {
    ++stats_.midiEvents;
    const auto status = event.midi[0] & 0xF0u;
    const auto midiChannel = static_cast<std::int16_t>(event.midi[0] & 0x0Fu);
    if (status == 0x90u && event.midi[2] != 0) {
      auto translated = event;
      translated.type = EventType::NoteOn;
      translated.channel = midiChannel;
      translated.key = event.midi[1];
      translated.value = static_cast<float>(event.midi[2]) / 127.0F;
      applyEvent(translated, resource);
    } else if (status == 0x80u ||
               (status == 0x90u && event.midi[2] == 0)) {
      auto translated = event;
      translated.type = EventType::NoteOff;
      translated.channel = midiChannel;
      translated.key = event.midi[1];
      applyEvent(translated, resource);
    } else if (status == 0xE0u) {
      const auto value = static_cast<int>(event.midi[1]) |
                         (static_cast<int>(event.midi[2]) << 7);
      channelBend_[static_cast<std::size_t>(midiChannel)] =
          static_cast<float>(value - 8192) / 8192.0F * 2.0F;
    } else if (status == 0xD0u) {
      for (auto& voice : voices_) {
        if (voice.active && voice.channel == midiChannel) {
          voice.pressure = static_cast<float>(event.midi[1]) / 127.0F;
        }
      }
    } else if (status == 0xB0u && event.midi[1] == 74) {
      for (auto& voice : voices_) {
        if (voice.active && voice.channel == midiChannel) {
          voice.timbre = static_cast<float>(event.midi[2]) / 127.0F;
        }
      }
    }
    return;
  }

  if (event.type == EventType::PitchBend) {
    channelBend_[static_cast<std::size_t>(channel)] =
        std::clamp(event.value, -48.0F, 48.0F);
    ++stats_.expressionEvents;
    return;
  }

  if (event.type == EventType::Pressure || event.type == EventType::Timbre ||
      event.type == EventType::Brightness) {
    for (auto& voice : voices_) {
      if (!voice.active ||
          (event.noteId >= 0 && voice.noteId != event.noteId)) {
        continue;
      }
      if (event.type == EventType::Pressure) {
        voice.pressure = std::clamp(event.value, 0.0F, 1.0F);
      } else if (event.type == EventType::Timbre) {
        voice.timbre = std::clamp(event.value, 0.0F, 1.0F);
      } else {
        voice.brightness = std::clamp(event.value, 0.0F, 1.0F);
      }
    }
    ++stats_.expressionEvents;
    return;
  }

  if (event.type == EventType::NoteOn && event.value > 0.0F) {
    auto* legatoSource = findLegatoSource(channel);
    auto* voice = allocateVoice();

    const auto savedTail = voice->tailSample;
    const auto savedTailRemaining = voice->tailRemaining;
    const auto savedTailLength = voice->tailLength;
    *voice = {};
    voice->tailSample = savedTail;
    voice->tailRemaining = savedTailRemaining;
    voice->tailLength = savedTailLength;
    voice->active = true;
    voice->noteId = event.noteId;
    voice->channel = channel;
    voice->key = event.key;
    voice->velocity = std::clamp(event.value, 0.0F, 1.0F);
    voice->age = ++ageCounter_;
    voice->attack = choose(UnitKind::Attack, event.key);
    voice->sustain = choose(UnitKind::Sustain, event.key);
    voice->release = choose(UnitKind::Release, event.key);
    voice->transition = legatoSource
                            ? choose(UnitKind::Transition, event.key,
                                     legatoSource->key, event.key)
                            : nullptr;

    if (voice->transition) {
      voice->current = voice->transition;
      voice->position = voice->transition->begin;
      voice->stage = VoiceStage::Transition;
      ++stats_.transitionHits;
    } else if (voice->attack) {
      voice->current = voice->attack;
      voice->position = voice->attack->begin;
      voice->stage = VoiceStage::Attack;
    } else {
      voice->current = voice->sustain;
      voice->position = voice->sustain ? voice->sustain->begin : 0.0;
      voice->stage = VoiceStage::Sustain;
    }

    if (legatoSource && legatoSource != voice) {
      const auto crossfade = std::max(
          64u, static_cast<std::uint32_t>(std::ceil(sampleRate_ * 0.0015)));
      legatoSource->legatoFadeLength = crossfade;
      legatoSource->legatoFadeOutRemaining = crossfade;
      voice->legatoFadeLength = crossfade;
      voice->legatoFadeInRemaining = crossfade;
      if (!voice->transition) {
        ++stats_.transitionFallbacks;
      }
    }

    ++stats_.noteOns;
    return;
  }

  if (event.type == EventType::NoteOff ||
      event.type == EventType::NoteChoke ||
      (event.type == EventType::NoteOn && event.value <= 0.0F)) {
    for (auto& voice : voices_) {
      if (!voice.active) {
        continue;
      }
      const auto match = event.noteId >= 0
                             ? voice.noteId == event.noteId
                             : (voice.channel == channel && voice.key == event.key);
      if (match) {
        beginRelease(voice, event.type == EventType::NoteChoke);
      }
    }
    ++stats_.noteOffs;
  }
}

void LiveVoiceEngine::advanceStage(Voice& voice) noexcept {
  if (voice.stage == VoiceStage::Attack ||
      voice.stage == VoiceStage::Transition) {
    if (voice.sustain) {
      voice.current = voice.sustain;
      voice.position = voice.sustain->begin;
      voice.stage = VoiceStage::Sustain;
    } else {
      beginRelease(voice, false);
    }
  } else if (voice.stage == VoiceStage::Release) {
    voice.active = false;
    voice.stage = VoiceStage::Inactive;
  }
}

float LiveVoiceEngine::renderVoice(
    Voice& voice,
    const LiveVoicebankResource& resource) noexcept {
  if (!voice.active || !voice.current) {
    return 0.0F;
  }

  if (voice.position >= static_cast<double>(voice.current->end)) {
    advanceStage(voice);
    if (!voice.active || !voice.current) {
      return 0.0F;
    }
  }

  const auto bend = channelBend_[static_cast<std::size_t>(
      std::clamp<int>(voice.channel, 0, 15))];
  voice.increment =
      std::pow(2.0,
               (static_cast<double>(voice.key) - voice.current->rootKey + bend) /
                   12.0) *
      static_cast<double>(resource.sampleRate) / sampleRate_;

  if (voice.stage == VoiceStage::Release || voice.releasing) {
    voice.envelope = std::max(
        0.0F, voice.envelope -
                  1.0F / std::max(64.0F, static_cast<float>(sampleRate_) * 0.025F));
  } else {
    voice.envelope = std::min(
        1.0F, voice.envelope +
                  1.0F / std::max(64.0F, static_cast<float>(sampleRate_) * 0.008F));
  }

  if (voice.envelope <= 0.0F &&
      (voice.stage == VoiceStage::Release || voice.releasing) &&
      voice.tailRemaining == 0) {
    voice.active = false;
    voice.stage = VoiceStage::Inactive;
    return 0.0F;
  }

  const auto shouldLoop = voice.stage == VoiceStage::Sustain;
  auto sample = interpolate(resource, voice.position, shouldLoop, *voice.current);
  voice.position += voice.increment;

  const auto pressureGain = 0.75F + 0.25F * voice.pressure;
  const auto timbreGain = 0.9F + 0.1F * voice.timbre;
  const auto brightnessGain = 0.9F + 0.1F * voice.brightness;
  auto result = sample * voice.envelope * voice.velocity * pressureGain *
                timbreGain * brightnessGain;

  if (voice.legatoFadeInRemaining && voice.legatoFadeLength) {
    const auto progress = 1.0F -
                          static_cast<float>(voice.legatoFadeInRemaining) /
                              voice.legatoFadeLength;
    result *= std::sin(progress * static_cast<float>(kPi) * 0.5F);
    --voice.legatoFadeInRemaining;
  }

  if (voice.legatoFadeOutRemaining && voice.legatoFadeLength) {
    const auto progress = 1.0F -
                          static_cast<float>(voice.legatoFadeOutRemaining) /
                              voice.legatoFadeLength;
    result *= std::cos(progress * static_cast<float>(kPi) * 0.5F);
    --voice.legatoFadeOutRemaining;
    if (!voice.legatoFadeOutRemaining) {
      beginRelease(voice, false);
    }
  }

  if (voice.tailRemaining && voice.tailLength) {
    const auto ratio = static_cast<float>(voice.tailRemaining) /
                       voice.tailLength;
    result += voice.tailSample *
              std::sin(ratio * static_cast<float>(kPi) * 0.5F);
    --voice.tailRemaining;
  }

  voice.lastSample = result;
  return result;
}

void LiveVoiceEngine::renderRange(
    float* const* outputs,
    std::uint32_t channels,
    std::uint32_t begin,
    std::uint32_t end,
    const LiveVoicebankResource* resource) noexcept {
  for (auto frame = begin; frame < end; ++frame) {
    auto mixed = 0.0F;
    if (resource) {
      for (auto& voice : voices_) {
        mixed += renderVoice(voice, *resource);
      }
    }
    mixed = std::clamp(mixed, -1.0F, 1.0F);
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      if (outputs[channel]) {
        outputs[channel][frame] = mixed;
      }
    }
    ++stats_.renderedFrames;
  }
}

void LiveVoiceEngine::process(
    std::span<const LiveEvent> events,
    float* const* outputs,
    std::uint32_t channels,
    std::uint32_t frames) noexcept {
  channels = std::clamp(channels, 1u, 8u);
  for (std::uint32_t channel = 0; channel < channels; ++channel) {
    if (outputs[channel]) {
      std::fill(outputs[channel], outputs[channel] + frames, 0.0F);
    }
  }

  resource_ = publisher_.acquireForAudio();
  const auto generation = publisher_.generation();
  if (generation != resourceGeneration_) {
    for (auto& voice : voices_) voice = {};
    resourceGeneration_ = generation;
  }
  if (!resource_ || !resource_->valid()) {
    stats_.silentFramesNoResource += frames;
    publisher_.releaseFromAudio();
    return;
  }

  if (events.size() > kMaxEventsPerBlock) {
    ++stats_.eventOverflows;
    for (auto& voice : voices_) voice = {};
    events = events.first(kMaxEventsPerBlock);
  }

  auto current = 0u;
  for (const auto& event : events) {
    const auto offset = std::clamp(event.sampleOffset, current, frames);
    renderRange(outputs, channels, current, offset, resource_);
    applyEvent(event, resource_);
    current = offset;
  }
  renderRange(outputs, channels, current, frames, resource_);
  publisher_.releaseFromAudio();
}

void LiveVoiceEngine::dispatch(const LiveEvent& event) noexcept {
  resource_ = publisher_.acquireForAudio();
  const auto generation = publisher_.generation();
  if (generation != resourceGeneration_) {
    for (auto& voice : voices_) voice = {};
    resourceGeneration_ = generation;
  }
  applyEvent(event, resource_);
  publisher_.releaseFromAudio();
}

void LiveVoiceEngine::noteOn(std::int32_t noteId,
                             std::int32_t key,
                             float velocity) noexcept {
  resource_ = publisher_.acquireForAudio();
  const auto generation = publisher_.generation();
  if (generation != resourceGeneration_) {
    for (auto& voice : voices_) voice = {};
    resourceGeneration_ = generation;
  }
  const auto compatibilityVelocity =
      std::clamp(velocity, 0.001F, 1.0F);
  applyEvent({0, EventType::NoteOn, noteId, 0,
              static_cast<std::int16_t>(key), compatibilityVelocity, {}},
             resource_);
  publisher_.releaseFromAudio();
}

void LiveVoiceEngine::noteOff(std::int32_t noteId,
                              std::int32_t key) noexcept {
  resource_ = publisher_.acquireForAudio();
  const auto generation = publisher_.generation();
  if (generation != resourceGeneration_) {
    for (auto& voice : voices_) voice = {};
    resourceGeneration_ = generation;
  }
  applyEvent({0, EventType::NoteOff, noteId, 0,
              static_cast<std::int16_t>(key), 0.0F, {}},
             resource_);
  publisher_.releaseFromAudio();
}

void LiveVoiceEngine::choke(std::int32_t noteId,
                            std::int32_t key) noexcept {
  resource_ = publisher_.acquireForAudio();
  const auto generation = publisher_.generation();
  if (generation != resourceGeneration_) {
    for (auto& voice : voices_) voice = {};
    resourceGeneration_ = generation;
  }
  applyEvent({0U, EventType::NoteChoke, noteId, 0,
              static_cast<std::int16_t>(key), 0.0F, {}},
             resource_);
  for (auto& voice : voices_) {
    if (voice.active && voice.noteId == noteId && voice.key == key) {
      voice = {};
    }
  }
  publisher_.releaseFromAudio();
}

float LiveVoiceEngine::renderSample() noexcept {
  resource_ = publisher_.acquireForAudio();
  const auto generation = publisher_.generation();
  if (generation != resourceGeneration_) {
    for (auto& voice : voices_) voice = {};
    resourceGeneration_ = generation;
  }
  auto mixed = 0.0F;
  if (resource_ && resource_->valid()) {
    for (auto& voice : voices_) {
      mixed += renderVoice(voice, *resource_);
    }
  }
  publisher_.releaseFromAudio();
  return std::clamp(mixed, -1.0F, 1.0F);
}

}  // namespace seam::phase12c
