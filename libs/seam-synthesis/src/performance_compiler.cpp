#include "seam/synthesis/performance_compiler.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

namespace seam::synthesis {
core::Result<std::vector<domain::VocalRegion>> projectScoreVoices(
    const domain::Project& project, const domain::VocalRegion& region,
    std::uint32_t sampleRate, std::span<const domain::PhonemeToken> phonemes, std::size_t maximumVoices) {
  using Output = std::vector<domain::VocalRegion>;
  if (sampleRate < 8000U || sampleRate > 384000U || phonemes.size() > 16384U) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Voice performance input exceeds bounds");
  }
  const auto plan = allocateScoreVoices(region, maximumVoices);
  if (!plan) return core::Result<Output>{plan.error()};
  if (!phonemes.empty()) {
    const auto timing = compilePhonemeTimingPlan(project, region, phonemes, sampleRate);
    if (!timing) return core::Result<Output>{timing.error()};
    std::unordered_set<domain::NoteId> covered;
    for (const auto& phone : phonemes) covered.insert(phone.key.noteId);
    if (covered.size() != region.notes.size()) return core::failure<Output>(core::ErrorCode::Conflict,
        "Voice performance phonemes must cover every score note");
  }
  // Filtering tokens must not silently shorten a forced unit or give an
  // incoming seam a different predecessor, even when the symbols match.
  if (plan.value().voices.size() > 1U) {
    std::unordered_map<domain::NoteId, std::size_t> membership;
    for (std::size_t i = 0; i < plan.value().voices.size(); ++i) {
      for (const auto id : plan.value().voices[i]) membership.emplace(id, i);
    }
    const auto indexOf = [&](const domain::PhonemeKey& key) {
      return static_cast<std::size_t>(std::find_if(phonemes.begin(), phonemes.end(),
          [&](const auto& phone) { return phone.key == key; }) - phonemes.begin());
    };
    for (const auto& edit : region.unitSelectionOverrides) {
      if (edit.unresolved) continue;
      const auto start = indexOf(edit.startKey);
      if (start == phonemes.size() || edit.tokenCount > phonemes.size() - start) {
        return core::failure<Output>(core::ErrorCode::Conflict,
            "Voice allocation requires the complete forced-unit phoneme span", edit.unitId);
      }
      const auto owner = membership.at(edit.startKey.noteId);
      for (std::size_t i = start; i < start + edit.tokenCount; ++i) {
        if (membership.at(phonemes[i].key.noteId) != owner) {
          return core::failure<Output>(core::ErrorCode::Conflict,
              "Forced unit crosses allocated score voices", edit.unitId);
        }
      }
    }
    for (const auto& edit : region.seamOverrides) {
      if (edit.unresolved) continue;
      const auto incoming = indexOf(edit.incomingStartKey);
      if (incoming == 0U || incoming == phonemes.size() ||
          membership.at(phonemes[incoming - 1U].key.noteId) !=
              membership.at(edit.incomingStartKey.noteId)) {
        return core::failure<Output>(core::ErrorCode::Conflict,
            "Seam predecessor is missing or crosses allocated score voices",
            edit.incomingStartKey.noteId.toString());
      }
    }
  }
  Output result;
  std::size_t totalPoints = 0U;
  for (const auto& ids : plan.value().voices) {
    const std::unordered_set<domain::NoteId> selected(ids.begin(), ids.end());
    auto voice = region;
    std::erase_if(voice.notes, [&](const auto& note) { return !selected.contains(note.id); });
    std::sort(voice.notes.begin(), voice.notes.end(), [](const auto& a, const auto& b) {
      return a.startTick == b.startTick ? a.id < b.id : a.startTick < b.startTick;
    });
    std::erase_if(voice.phonemeOverrides, [&](const auto& edit) { return !selected.contains(edit.key.noteId); });
    std::erase_if(voice.unitSelectionOverrides, [&](const auto& edit) { return !selected.contains(edit.startKey.noteId); });
    std::erase_if(voice.seamOverrides, [&](const auto& edit) { return !selected.contains(edit.incomingStartKey.noteId); });
    const auto outside = [&](const domain::PerformanceScope& scope) {
      const auto* id = std::get_if<domain::NoteId>(&scope);
      return id && !selected.contains(*id);
    };
    std::erase_if(voice.performance.ownership, [&](const auto& owner) { return outside(owner.scope); });
    std::erase_if(voice.performance.accepted, [&](const auto& accepted) { return outside(accepted.scope); });
    // Only accepted takes affect evaluation; preserve full originals in the
    // project, not a duplicated proposal history in every compiled voice.
    std::erase_if(voice.performance.takes, [&](const auto& take) {
      return std::none_of(voice.performance.accepted.begin(), voice.performance.accepted.end(),
          [&](const auto& accepted) { return accepted.takeId == take.id; });
    });
    std::size_t points = voice.pitchAutomation.points().size() + voice.dynamicsAutomation.points().size();
    for (const auto& take : voice.performance.takes) for (const auto& lane : take.lanes) points += lane.points.size();
    constexpr std::size_t maximumCompiledPoints = 1U << 20;
    if (points > maximumCompiledPoints - totalPoints) return core::failure<Output>(core::ErrorCode::Unsupported,
        "Compiled voices exceed aggregate performance point budget");
    totalPoints += points;
    result.push_back(std::move(voice));
  }
  return result;
}

core::Result<std::vector<CompiledVoicePerformance>> compileScoreVoices(
    const domain::Project& project, const domain::VocalRegion& region,
    std::uint32_t sampleRate, std::span<const domain::PhonemeToken> phonemes, std::size_t maximumVoices,
    std::span<const std::vector<domain::PhonemeToken>> independentPhonemes) {
  using Output = std::vector<CompiledVoicePerformance>;
  const auto voices = projectScoreVoices(project, region, sampleRate, phonemes, maximumVoices);
  if (!voices) return core::Result<Output>{voices.error()};
  if (!independentPhonemes.empty()) {
    if (independentPhonemes.size() != voices.value().size()) return core::failure<Output>(
        core::ErrorCode::Conflict, "Independent pronunciation must cover every allocated voice");
    std::size_t totalTokens = 0U;
    for (std::size_t i = 0; i < independentPhonemes.size(); ++i) {
      const auto& tokens = independentPhonemes[i];
      if (tokens.empty() || tokens.size() > 16384U - totalTokens) return core::failure<Output>(
          core::ErrorCode::InvalidArgument, "Independent voice pronunciation exceeds aggregate bounds");
      totalTokens += tokens.size();
      const auto& voice = voices.value()[i];
      const auto timing = compilePhonemeTimingPlan(project, voice, tokens, sampleRate);
      if (!timing) return core::Result<Output>{timing.error()};
      std::unordered_set<domain::NoteId> covered;
      for (const auto& token : tokens) covered.insert(token.key.noteId);
      if (covered.size() != voice.notes.size()) return core::failure<Output>(
          core::ErrorCode::Conflict, "Independent pronunciation must cover every note in its allocated voice");
    }
  }
  const bool resolveVoices = independentPhonemes.empty() && voices.value().size() > 1U && !phonemes.empty();
  if (resolveVoices) {
    const auto original = phonemizer::resolvePronunciation(region);
    if (!original) return core::Result<Output>{original.error()};
    if (!std::equal(phonemes.begin(), phonemes.end(),
        original.value().pronunciation.tokens.begin(), original.value().pronunciation.tokens.end())) {
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "Multi-voice pronunciation requires the current Japanese resolver sequence; custom voices need independent context");
    }
  }
  Output result;
  for (const auto& voice : voices.value()) {
    std::vector<domain::NoteId> ids;
    std::unordered_set<domain::NoteId> selected;
    for (const auto& note : voice.notes) { ids.push_back(note.id); selected.insert(note.id); }
    std::vector<domain::PhonemeToken> voicePhones;
    if (!independentPhonemes.empty()) {
      voicePhones = independentPhonemes[result.size()];
    } else if (resolveVoices) {
      const auto resolved = phonemizer::resolvePronunciation(voice);
      if (!resolved) return core::Result<Output>{resolved.error()};
      voicePhones = resolved.value().pronunciation.tokens;
    } else {
      for (const auto& phone : phonemes) if (selected.contains(phone.key.noteId)) voicePhones.push_back(phone);
    }
    const auto compiled = compileScorePerformance(project, voice, sampleRate, voicePhones);
    if (!compiled) return core::Result<Output>{compiled.error()};
    result.push_back({ids, compiled.value()});
  }
  return result;
}

core::Result<ScoreVoicePlan> allocateScoreVoices(const domain::VocalRegion& region, std::size_t maximumVoices) {
  if (maximumVoices == 0U || maximumVoices > 64U) {
    return core::failure<ScoreVoicePlan>(core::ErrorCode::InvalidArgument, "Score voice allocation exceeds bounds");
  }
  if (region.notes.size() > kMaximumScoreVoiceAllocationNotes)
    return core::failure<ScoreVoicePlan>(core::ErrorCode::InvalidArgument,
        "Score voice allocation supports at most " + std::to_string(kMaximumScoreVoiceAllocationNotes) + " region notes");
  const auto valid = region.validate();
  if (!valid) return core::Result<ScoreVoicePlan>{valid.error()};
  std::vector<const domain::Note*> notes;
  for (const auto& note : region.notes) {
    if (note.endTick() > region.durationTick) return core::failure<ScoreVoicePlan>(core::ErrorCode::Conflict,
        "Voice allocation note exceeds its region", note.id.toString());
    notes.push_back(&note);
  }
  std::sort(notes.begin(), notes.end(), [](const auto* a, const auto* b) {
    return a->startTick == b->startTick ? a->id < b->id : a->startTick < b->startTick;
  });
  ScoreVoicePlan result;
  std::vector<const domain::Note*> tails;
  for (const auto* note : notes) {
    std::optional<std::size_t> chosen;
    const auto* lyric = region.findLyric(note->lyricTokenId);
    const bool literalContinuation = lyric && (lyric->surface == U"-" || lyric->surface == U"ー" || lyric->surface == U"〜");
    for (std::size_t i = 0; i < tails.size(); ++i) {
      const bool adjacentContinuation = literalContinuation && tails[i]->endTick() == note->startTick &&
          tails[i]->articulation != domain::NoteArticulation::Staccato && note->articulation != domain::NoteArticulation::Staccato;
      if (domain::continuesSharedLyric(*tails[i], *note) || adjacentContinuation) {
        if (chosen) return core::failure<ScoreVoicePlan>(core::ErrorCode::Conflict,
            "Vowel continuation has multiple possible source voices", note->id.toString());
        chosen = i;
      }
    }
    if (!chosen) {
      for (std::size_t i = 0; i < tails.size(); ++i) {
        if (tails[i]->endTick() <= note->startTick) { chosen = i; break; }
      }
    }
    if (!chosen) {
      if (tails.size() == maximumVoices) return core::failure<ScoreVoicePlan>(core::ErrorCode::Unsupported,
          "Simultaneous score notes exceed the voice limit", note->id.toString());
      chosen = tails.size();
      tails.push_back(note);
      result.voices.emplace_back();
    }
    tails[*chosen] = note;
    result.voices[*chosen].push_back(note->id);
  }
  return result;
}

core::Result<void> applyCompiledPerformanceGain(std::span<float> samples,
    const CompiledScorePerformance& performance, time::SampleFrame origin, std::stop_token stopToken) {
  if (samples.size() > 32ULL * 1024ULL * 1024ULL ||
      origin > std::numeric_limits<time::SampleFrame>::max() - static_cast<time::SampleFrame>(samples.size())) {
    return core::failure(core::ErrorCode::InvalidArgument, "Performance gain output exceeds bounds");
  }
  for (std::size_t i = 0; i < samples.size(); ++i) {
    if ((i & 4095U) == 0U && stopToken.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Performance gain was cancelled");
    const auto value = performance.at(origin + static_cast<time::SampleFrame>(i));
    const auto gain = value.dynamicsGain * value.articulationGain;
    const auto scaled = static_cast<double>(samples[i]) * static_cast<double>(gain);
    if (!std::isfinite(scaled) || std::abs(scaled) > std::numeric_limits<float>::max()) {
      return core::failure(core::ErrorCode::InvalidArgument, "Performance gain exceeds output range");
    }
    if (gain != 1.0F) samples[i] = static_cast<float>(scaled);
  }
  return {};
}
namespace {
std::optional<double> laneValue(const domain::PerformanceLane& lane, time::Tick tick,
    std::size_t upperIndex) {
  return domain::samplePerformanceLane(lane, tick, upperIndex);
}
}
core::Result<CompiledScorePerformance> compileScorePerformance(
    const domain::Project& project, const domain::VocalRegion& region,
    std::uint32_t sampleRate, std::span<const domain::PhonemeToken> phonemes, PhonemeTimingPolicy policy) {
  if ((policy != PhonemeTimingPolicy::SourceDependent && policy != PhonemeTimingPolicy::ProceduralInNote) ||
      sampleRate < 8000U || sampleRate > 384000U || region.notes.size() > 4096U || phonemes.size() > 16384U ||
      region.pitchAutomation.points().size() > 16384U ||
      region.dynamicsAutomation.points().size() > 16384U || project.tempoMap().events().size() > 4096U ||
      region.formantAutomation.points().size() > 16384U ||
      region.breathinessAutomation.points().size() > 16384U ||
      region.tensionAutomation.points().size() > 16384U ||
      region.airinessAutomation.points().size() > 16384U ||
      region.genderAutomation.points().size() > 16384U ||
      region.growlAutomation.points().size() > 16384U ||
      region.startTick.value() < 0 || region.durationTick.value() <= 0 ||
      region.startTick.value() > std::numeric_limits<std::int64_t>::max() - region.durationTick.value()) {
    return core::failure<CompiledScorePerformance>(core::ErrorCode::InvalidArgument, "Score performance input exceeds bounds");
  }
  const auto valid = region.validate();
  if (!valid) return core::Result<CompiledScorePerformance>{valid.error()};
  for (const auto& selection : region.performance.accepted) {
    if (selection.channel != domain::PerformanceChannel::Pitch &&
        // Timing is admitted here because it is consumed by the ordered timing plan
        // below, not by per-frame score evaluation. The remaining channels still have
        // no consumer, so claiming them would invent audio the backend cannot make.
        selection.channel != domain::PerformanceChannel::Timing &&
        selection.channel != domain::PerformanceChannel::Dynamics &&
        selection.channel != domain::PerformanceChannel::Attack &&
        selection.channel != domain::PerformanceChannel::Release) {
      return core::failure<CompiledScorePerformance>(core::ErrorCode::Unsupported,
          "Accepted performance channel is not yet supported by score evaluation");
    }
  }
  CompiledScorePerformance result;
  result.tempo_ = project.tempoMap();
  result.regionStart_ = region.startTick;
  result.sampleRate_ = sampleRate;
  result.pitch_ = region.pitchAutomation;
  result.dynamics_ = region.dynamicsAutomation;
  result.formant_ = region.formantAutomation;
  result.breathiness_ = region.breathinessAutomation;
  result.tension_ = region.tensionAutomation;
  result.airiness_ = region.airinessAutomation;
  result.gender_ = region.genderAutomation;
  result.growl_ = region.growlAutomation;
  result.performance_ = region.performance;
  result.notes_.reserve(region.notes.size());
  for (const auto& note : region.notes) {
    if (note.endTick() > region.durationTick) return core::failure<CompiledScorePerformance>(
        core::ErrorCode::Conflict, "Score note exceeds its region", note.id.toString());
    const auto start = result.tempo_.sampleFrameAt(region.startTick + note.startTick, sampleRate);
    const auto end = result.tempo_.sampleFrameAt(region.startTick + note.endTick(), sampleRate);
    if (start < 0 || end <= start) return core::failure<CompiledScorePerformance>(
        core::ErrorCode::Conflict, "Score note has no output frames", note.id.toString());
    const bool staccato = note.articulation == domain::NoteArticulation::Staccato;
    const auto gateEnd = staccato ? start + std::max<time::SampleFrame>(1, (end - start) / 2) : end;
    const auto release = staccato ? std::min<time::SampleFrame>(sampleRate / 100U, gateEnd - start) : 0;
    result.notes_.push_back({note.id, start, end, note.midiKey, note.vibrato, note.articulation,
        gateEnd, gateEnd - release});
  }
  std::sort(result.notes_.begin(), result.notes_.end(), [](const auto& a, const auto& b) { return a.startFrame < b.startFrame; });
  for (std::size_t i = 1; i < result.notes_.size(); ++i) {
    if (result.notes_[i].startFrame < result.notes_[i - 1U].endFrame) return core::failure<CompiledScorePerformance>(
        core::ErrorCode::Unsupported, "Overlapping score notes require explicit voice allocation");
  }
  if (!phonemes.empty()) {
    const auto timing = compilePhonemeTimingPlan(project, region, phonemes, sampleRate, policy);
    if (!timing) return core::Result<CompiledScorePerformance>{timing.error()};
    result.phonemeTiming_ = timing.value();
    for (std::size_t i = 1; i < result.notes_.size(); ++i) {
      auto& current = result.notes_[i];
      const auto& previous = result.notes_[i - 1U];
      const auto* note = region.findNote(current.id);
      const auto* lyric = region.findLyric(note->lyricTokenId);
      const bool continuation = (lyric && (lyric->surface == U"-" || lyric->surface == U"ー" || lyric->surface == U"〜")) ||
          domain::continuesSharedLyric(*region.findNote(previous.id), *note);
      if (!continuation || current.startFrame != previous.endFrame ||
          previous.articulation == domain::NoteArticulation::Staccato ||
          current.articulation == domain::NoteArticulation::Staccato) continue;
      const domain::PhonemeToken* previousVowel = nullptr;
      const domain::PhonemeToken* currentPhone = nullptr;
      std::size_t count = 0U;
      for (const auto& phone : phonemes) {
        if (phone.key.noteId == previous.id && phone.role == domain::PhonemeRole::Nucleus && phone.voiced) previousVowel = &phone;
        if (phone.key.noteId == current.id) { currentPhone = &phone; ++count; }
      }
      if (!previousVowel || !currentPhone || count != 1U ||
          currentPhone->role != domain::PhonemeRole::Nucleus || !currentPhone->voiced ||
          currentPhone->symbol != previousVowel->symbol) continue;
      current.reattack = false;
      current.transitionFromMidi = previous.midiKey;
      current.transitionEndFrame = current.startFrame + std::min<time::SampleFrame>(
          sampleRate / 50U, std::max<time::SampleFrame>(1, (current.endFrame - current.startFrame) / 2));
    }
  }
  const auto compileScope = [&](const domain::PerformanceScope& scope) {
    CompiledScorePerformance::FrameScope compiled;
    if (const auto* id = std::get_if<domain::NoteId>(&scope)) {
      const auto* note = region.findNote(*id);
      compiled.noteId = *id;
      compiled.startTick = note->startTick;
      compiled.endTick = note->endTick();
      compiled.start = result.tempo_.sampleFrameAt(region.startTick + note->startTick, sampleRate);
      compiled.end = result.tempo_.sampleFrameAt(region.startTick + note->endTick(), sampleRate);
    } else {
      const auto& range = std::get<domain::PerformanceTimeRange>(scope);
      compiled.startTick = range.startTick;
      compiled.endTick = range.endTick;
      compiled.start = result.tempo_.sampleFrameAt(region.startTick + range.startTick, sampleRate);
      compiled.end = result.tempo_.sampleFrameAt(region.startTick + range.endTick, sampleRate);
    }
    return compiled;
  };
  for (const auto& owner : result.performance_.ownership) result.ownershipScopes_.push_back(compileScope(owner.scope));
  for (const auto& selection : result.performance_.accepted) result.acceptedScopes_.push_back(compileScope(selection.scope));
  for (std::size_t i = 0; i < result.performance_.ownership.size(); ++i) {
    const auto& owner = result.performance_.ownership[i];
    if (result.ownershipScopes_[i].start < result.ownershipScopes_[i].end) {
      result.ownershipIndex_[static_cast<std::size_t>(owner.channel) * 2U +
          static_cast<std::size_t>(owner.mode)].push_back(i);
    }
  }
  for (std::size_t i = 0; i < result.performance_.accepted.size(); ++i) {
    const auto& selection = result.performance_.accepted[i];
    if (result.acceptedScopes_[i].start < result.acceptedScopes_[i].end) {
      result.acceptedIndex_[static_cast<std::size_t>(selection.channel)].push_back(i);
    }
    const auto take = std::find_if(result.performance_.takes.begin(), result.performance_.takes.end(),
        [&](const auto& value) { return value.id == selection.takeId; });
    const auto lane = std::find_if(take->lanes.begin(), take->lanes.end(),
        [&](const auto& value) { return value.channel == selection.channel; });
    result.acceptedLanes_.emplace_back(static_cast<std::size_t>(take - result.performance_.takes.begin()),
        static_cast<std::size_t>(lane - take->lanes.begin()));
  }
  const auto sortIndex = [](auto& buckets, const auto& scopes) {
    for (auto& bucket : buckets) std::sort(bucket.begin(), bucket.end(),
        [&](auto a, auto b) { return scopes[a].start < scopes[b].start; });
  };
  sortIndex(result.ownershipIndex_, result.ownershipScopes_);
  sortIndex(result.acceptedIndex_, result.acceptedScopes_);
  return result;
}

ScorePerformanceSample CompiledScorePerformance::at(time::SampleFrame frame) const noexcept {
  return evaluate(frame, false);
}
ScorePerformanceSample CompiledScorePerformance::inspectAt(time::SampleFrame frame) const noexcept {
  return evaluate(frame, true);
}
ScorePerformanceSample CompiledScorePerformance::evaluate(time::SampleFrame frame, bool inspect) const noexcept {
  ScorePerformanceSample result;
  const auto next = std::upper_bound(notes_.begin(), notes_.end(), frame,
      [](auto value, const auto& note) { return value < note.startFrame; });
  if (next == notes_.begin()) return result;
  const auto& note = *std::prev(next);
  if (frame >= note.endFrame) {
    // A staccato release stays closed through a gap/extended source tail; it
    // must not reopen just because there is no longer an active score note.
    if (note.articulation == domain::NoteArticulation::Staccato) result.articulationGain = 0.0F;
    else if (!acceptedIndex_[static_cast<std::size_t>(domain::PerformanceChannel::Release)].empty()) {
      const auto last = at(note.endFrame - 1);
      if (last.releaseMilliseconds && *last.releaseMilliseconds > 0.0) result.articulationGain = 0.0F;
    }
    return result;
  }
  const auto tick = tempo_.tickAtSampleFrame(frame, sampleRate_) - regionStart_;
  result.noteId = note.id;
  result.articulation = note.articulation;
  result.reattack = note.reattack;
  if (note.articulation == domain::NoteArticulation::Staccato) {
    if (frame >= note.gateEndFrame) result.articulationGain = 0.0F;
    else if (frame >= note.releaseStartFrame) {
      result.articulationGain = static_cast<float>(static_cast<double>(note.gateEndFrame - frame) /
          static_cast<double>(note.gateEndFrame - note.releaseStartFrame));
    }
  }
  result.dynamicsGain = dynamics_.valueAt(tick);
  result.formantSemitones = formant_.valueAt(tick);
  result.breathiness = breathiness_.valueAt(tick);
  result.tension = tension_.valueAt(tick);
  result.airiness = airiness_.valueAt(tick);
  result.gender = gender_.valueAt(tick);
  result.growl = growl_.valueAt(tick);
  const auto& vibrato = note.vibrato;
  if (vibrato.enabled) {
    const auto duration = static_cast<double>(note.endFrame - note.startFrame);
    const auto elapsed = static_cast<double>(frame - note.startFrame);
    const auto onset = duration * static_cast<double>(vibrato.startFraction);
    const auto active = duration - onset;
    if (elapsed >= onset && active > 0.0) {
      const auto position = elapsed - onset;
      const auto fadeIn = active * static_cast<double>(vibrato.fadeInFraction);
      const auto fadeOut = active * static_cast<double>(vibrato.fadeOutFraction);
      const auto envelope = std::min(fadeIn > 0 ? std::min(1.0, position / fadeIn) : 1.0,
          fadeOut > 0 ? std::min(1.0, (active - position) / fadeOut) : 1.0);
      const auto cycles = position / (static_cast<double>(sampleRate_) * vibrato.periodMilliseconds / 1000.0) + vibrato.phaseTurns;
      result.vibratoCents = vibrato.depthCents * envelope * std::sin(2.0 * std::numbers::pi * cycles);
    }
  }
  auto scoreMidi = static_cast<double>(note.midiKey);
  if (note.transitionFromMidi && frame < note.transitionEndFrame) {
    auto t = static_cast<double>(frame - note.startFrame) / static_cast<double>(note.transitionEndFrame - note.startFrame);
    t = t * t * (3.0 - 2.0 * t);
    scoreMidi = static_cast<double>(*note.transitionFromMidi) + t * (scoreMidi - *note.transitionFromMidi);
  }
  std::optional<double> baseCents = scoreMidi * 100.0;
  bool generatedPitch = false;
  const auto activeIndex = [&](const auto& index, const auto& scopes) -> std::optional<std::size_t> {
    const auto upper = std::upper_bound(index.begin(), index.end(), frame,
        [&](auto position, auto i) { return position < scopes[i].start; });
    if (upper == index.begin()) return std::nullopt;
    const auto i = *std::prev(upper);
    return scopes[i].contains(note.id, frame) ? std::optional<std::size_t>{i} : std::nullopt;
  };
  const auto owns = [&](domain::PerformanceChannel channel, domain::ManualPerformanceMode mode) {
    return activeIndex(ownershipIndex_[static_cast<std::size_t>(channel) * 2U +
        static_cast<std::size_t>(mode)], ownershipScopes_).has_value();
  };
  for (const auto& index : acceptedIndex_) {
    const auto active = activeIndex(index, acceptedScopes_);
    if (!active) continue;
    const auto i = *active;
    const auto& selection = performance_.accepted[i];
    const bool replaced = owns(selection.channel, domain::ManualPerformanceMode::Replace);
    if (!acceptedScopes_[i].contains(note.id, frame) ||
        (selection.channel == domain::PerformanceChannel::Pitch && vibrato.enabled) ||
        (replaced && !(inspect && selection.channel == domain::PerformanceChannel::Dynamics))) continue;
    const auto [takeIndex, laneIndex] = acceptedLanes_[i];
    const auto* lane = &performance_.takes[takeIndex].lanes[laneIndex];
    const auto& scope = acceptedScopes_[i];
    const auto sourceStart = scope.startTick + selection.sourceTickOffset;
    const auto sourceEnd = scope.endTick + selection.sourceTickOffset;
    const auto upper = std::upper_bound(lane->points.begin(), lane->points.end(), frame,
        [&](time::SampleFrame outputFrame, const auto& point) {
          // Points outside the selected source window need no absolute-time
          // arithmetic. In-window offsets are bounded by validated ownership.
          if (point.tick < sourceStart) return false;
          if (point.tick > sourceEnd) return true;
          const auto destinationTick = point.tick - selection.sourceTickOffset;
          return outputFrame < tempo_.sampleFrameAt(regionStart_ + destinationTick, sampleRate_);
        });
    const auto value = laneValue(*lane, tick + selection.sourceTickOffset,
        static_cast<std::size_t>(upper - lane->points.begin()));
    if (inspect && selection.channel == domain::PerformanceChannel::Dynamics && value)
      result.selectedGeneratedDynamicsGain = static_cast<float>(*value);
    if (replaced) continue;
    if (selection.channel == domain::PerformanceChannel::Pitch) {
      baseCents = value;
      generatedPitch = true;
    } else if (selection.channel == domain::PerformanceChannel::Attack) {
      result.attackMilliseconds = value;
    } else if (selection.channel == domain::PerformanceChannel::Release) {
      result.releaseMilliseconds = value;
    } else if (selection.channel == domain::PerformanceChannel::Dynamics && value) {
      result.dynamicsGain = static_cast<float>(*value);
    } else if (selection.channel == domain::PerformanceChannel::Formant && value) {
      // A manual formant edit is authoritative over the generated curve, exactly as a manual dynamics
      // edit is: the channel's own unit is semitones, so the value is applied as it was written.
      result.formantSemitones = static_cast<float>(*value);
    }
    // Every other channel stays out of the per-frame audio path on purpose. A
    // generated timing proposal is consumed by the ordered timing plan, and the
    // remaining channels are reported unsupported by the renderer capability table.
    // None of them is an amplitude, which is what a fallback assignment here would
    // have claimed by writing a microsecond offset into a gain.
  }
  if (result.attackMilliseconds && *result.attackMilliseconds > 0.0 && note.reattack) {
    const auto attackFrames = *result.attackMilliseconds * static_cast<double>(sampleRate_) / 1000.0;
    result.articulationGain *= static_cast<float>(std::clamp(
        static_cast<double>(frame - note.startFrame) / attackFrames, 0.0, 1.0));
  }
  const bool continues = next != notes_.end() && next->startFrame == note.endFrame && !next->reattack;
  if (result.releaseMilliseconds && *result.releaseMilliseconds > 0.0 && !continues) {
    const auto releaseFrames = *result.releaseMilliseconds * static_cast<double>(sampleRate_) / 1000.0;
    result.articulationGain *= static_cast<float>(std::clamp(
        static_cast<double>(note.endFrame - frame) / releaseFrames, 0.0, 1.0));
  }
  if (!baseCents) return result;
  const bool additive = owns(domain::PerformanceChannel::Pitch, domain::ManualPerformanceMode::PitchOffset);
  const auto manualCents = !generatedPitch || additive ? static_cast<double>(pitch_.valueAt(tick)) : 0.0;
  const auto midi = (*baseCents + manualCents + result.vibratoCents) / 100.0;
  result.scoreFrequencyHz = 440.0 * std::exp2((midi - 69.0) / 12.0);
  return result;
}
}
