#include "seam/synthesis/phoneme_timing_plan.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace seam::synthesis {
namespace {

// Where one accepted selection applies, in region-relative ticks. A note scope
// resolves through the notes that exist now, so a generated proposal is read against
// current material rather than a stale identity.
std::optional<domain::PerformanceTimeRange> acceptedSpan(const domain::VocalRegion& region,
    const domain::AcceptedPerformanceSelection& selection) {
  if (const auto* noteId = std::get_if<domain::NoteId>(&selection.scope)) {
    const auto* note = region.findNote(*noteId);
    if (note == nullptr) return std::nullopt;
    return domain::PerformanceTimeRange{note->startTick, note->endTick()};
  }
  return std::get<domain::PerformanceTimeRange>(selection.scope);
}

// The generated timing displacement for one syllable, in microseconds. It comes from
// an accepted Timing proposal, is read where the syllable would otherwise land, and is
// suppressed wherever the creator has taken the channel over: manual replacement
// wins, exactly as it does for pitch.
std::optional<time::Microseconds> generatedTimingOffset(const domain::VocalRegion& region,
    domain::NoteId noteId, time::Tick tick) {
  const auto& performance = region.performance;
  if (!performance.permitsGenerated(domain::PerformanceChannel::Timing, noteId, tick, false)) {
    return std::nullopt;
  }
  for (const auto& selection : performance.accepted) {
    if (selection.channel != domain::PerformanceChannel::Timing) continue;
    const auto span = acceptedSpan(region, selection);
    if (!span) continue;
    if (!span->contains(tick)) continue;
    const auto take = std::find_if(performance.takes.begin(), performance.takes.end(),
        [&](const auto& value) { return value.id == selection.takeId; });
    if (take == performance.takes.end()) continue;
    const auto lane = std::find_if(take->lanes.begin(), take->lanes.end(),
        [](const auto& value) { return value.channel == domain::PerformanceChannel::Timing; });
    if (lane == take->lanes.end()) continue;
    const auto value =
        domain::samplePerformanceLane(*lane, tick + selection.sourceTickOffset);
    if (!value.has_value()) return std::nullopt;
    return time::Microseconds{std::llround(*value)};
  }
  return std::nullopt;
}

}  // namespace

core::Result<std::vector<PhonemeTimingAnchor>> compilePhonemeTimingPlan(
    const domain::Project& project, const domain::VocalRegion& region,
    std::span<const domain::PhonemeToken> tokens, std::uint32_t sampleRate, PhonemeTimingPolicy policy) {
  using Output = std::vector<PhonemeTimingAnchor>;
  if ((policy != PhonemeTimingPolicy::SourceDependent && policy != PhonemeTimingPolicy::ProceduralInNote) ||
      sampleRate < 8000U || sampleRate > 384000U || tokens.empty() || tokens.size() > 16384U) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Phoneme timing input exceeds bounds");
  }
  Output result(tokens.size());
  if (region.startTick.value() < 0 || region.durationTick.value() <= 0 ||
      region.startTick.value() > std::numeric_limits<std::int64_t>::max() - region.durationTick.value()) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Phoneme timing region bounds are invalid");
  }
  const auto regionEnd = project.tempoMap().sampleFrameAt(region.startTick + region.durationTick, static_cast<double>(sampleRate));
  std::unordered_set<domain::NoteId> seen;
  for (std::size_t begin = 0U; begin < tokens.size();) {
    const auto id = tokens[begin].key.noteId;
    const auto* note = region.findNote(id);
    if (!note || !seen.insert(id).second) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Phoneme timing requires contiguous note groups");
    const auto valid = note->validate();
    if (!valid) return core::Result<Output>{valid.error()};
    if (note->endTick() > region.durationTick) return core::failure<Output>(core::ErrorCode::Conflict, "Phoneme timing note exceeds its region", id.toString());
    if (region.startTick.value() < 0 || region.startTick.value() > std::numeric_limits<std::int64_t>::max() - note->endTick().value()) {
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Absolute phoneme timing tick overflows");
    }
    auto end = begin;
    std::vector<std::size_t> nuclei;
    while (end < tokens.size() && tokens[end].key.noteId == id) {
      const auto tokenValid = tokens[end].validate();
      if (!tokenValid) return core::Result<Output>{tokenValid.error()};
      if (tokens[end].key.ordinal != end - begin) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Phoneme ordinals must be contiguous");
      if (tokens[end].role == domain::PhonemeRole::Nucleus) nuclei.push_back(end);
      ++end;
    }
    if (nuclei.empty()) for (auto i = begin; i < end; ++i) nuclei.push_back(i);
    const auto startFrame = project.tempoMap().sampleFrameAt(region.startTick + note->startTick, static_cast<double>(sampleRate));
    const auto endFrame = project.tempoMap().sampleFrameAt(region.startTick + note->endTick(), static_cast<double>(sampleRate));
    if (startFrame < 0 || endFrame <= startFrame || endFrame - startFrame < static_cast<time::SampleFrame>(nuclei.size())) {
      return core::failure<Output>(core::ErrorCode::Conflict, "Note is too short for its phoneme nuclei", id.toString());
    }
    const auto boundary = [&](std::size_t index) {
      const auto length = endFrame - startFrame;
      const auto count = static_cast<time::SampleFrame>(nuclei.size());
      const auto n = static_cast<time::SampleFrame>(index);
      return startFrame + (length / count) * n + ((length % count) * n) / count;
    };
    std::vector<std::size_t> groups(end - begin);
    for (auto i = begin; i < end; ++i) {
      auto next = std::lower_bound(nuclei.begin(), nuclei.end(), i);
      if (tokens[i].role == domain::PhonemeRole::Coda) {
        next = std::upper_bound(nuclei.begin(), nuclei.end(), i);
        if (next != nuclei.begin()) --next;
      } else if (next == nuclei.end()) next = std::prev(nuclei.end());
      const auto group = static_cast<std::size_t>(next - nuclei.begin());
      groups[i - begin] = group;
      auto anchor = boundary(group);
      auto finish = boundary(group + 1U);
      // A generated timing proposal displaces the syllable from its score position;
      // a neutral zero leaves it where the score put it. It composes here, before any
      // audio exists, so the ordered timing solver sees one timeline rather than a
      // per-frame offset applied after placement. Authored offsets stay absolute from
      // the note start and always win: they are the creator's own timing intent.
      if (i == nuclei[group] && !tokens[i].timing.startOffset) {
        const auto anchorTick = project.tempoMap().tickAtSampleFrame(anchor, sampleRate) -
                                region.startTick;
        if (const auto offset = generatedTimingOffset(region, id, anchorTick)) {
          const auto delta = static_cast<time::SampleFrame>(std::llround(
              static_cast<double>(*offset) * static_cast<double>(sampleRate) / 1000000.0));
          if (delta > 0 && anchor > std::numeric_limits<time::SampleFrame>::max() - delta) {
            return core::failure<Output>(core::ErrorCode::Conflict,
                "Generated timing offset overflows the output timeline", tokens[i].key.toString());
          }
          anchor += delta;
          // The score still owns the note's sounding window: its attack, release and
          // vibrato phase are evaluated against the note span, which a generated
          // proposal does not move. A displacement that would place the syllable
          // outside that window is therefore refused rather than accepted as a
          // gesture no envelope could cover.
          if (anchor < startFrame || anchor > endFrame) {
            return core::failure<Output>(core::ErrorCode::Conflict,
                "Generated timing leaves the note span the score defines",
                tokens[i].key.toString());
          }
        }
      }
      const auto applyOffset = [&](time::Microseconds offset, time::SampleFrame& value) {
        const auto delta = static_cast<time::SampleFrame>(std::llround(static_cast<double>(offset) * static_cast<double>(sampleRate) / 1000000.0));
        if (delta > 0 && startFrame > std::numeric_limits<time::SampleFrame>::max() - delta) return false;
        value = startFrame + delta;
        return true;
      };
      if ((tokens[i].timing.startOffset && !applyOffset(*tokens[i].timing.startOffset, anchor)) ||
          (tokens[i].timing.endOffset && !applyOffset(*tokens[i].timing.endOffset, finish)) ||
          ((tokens[i].timing.endOffset || group + 1U == nuclei.size()) && finish <= anchor)) {
        return core::failure<Output>(core::ErrorCode::Conflict, "Phoneme timing offsets produce an invalid span", tokens[i].key.toString());
      }
      result[i] = {tokens[i].key, anchor, finish,
          tokens[i].timing.startOffset ? std::optional<time::SampleFrame>{anchor} : std::nullopt,
          static_cast<std::uint16_t>(group),
          tokens[nuclei[group]].role == domain::PhonemeRole::Nucleus
              ? std::optional<domain::PhonemeKey>{tokens[nuclei[group]].key} : std::nullopt,
          tokens[i].timing.endOffset.has_value(), tokens[i].voiced};
      if (finish > regionEnd) {
        return core::failure<Output>(core::ErrorCode::Conflict,
            "Phoneme release exceeds the region; shorten the end offset or extend the region", tokens[i].key.toString());
      }
    }
    std::vector<std::optional<time::SampleFrame>> generatedStarts(nuclei.size());
    std::vector<std::optional<std::size_t>> generatedCodas(nuclei.size());
    if (policy == PhonemeTimingPolicy::ProceduralInNote) {
      std::vector<std::optional<std::size_t>> onsets(nuclei.size());
      std::vector<std::optional<std::size_t>> codas(nuclei.size());
      std::vector<bool> eligibleGroups(nuclei.size(), true);
      for (auto i = begin; i < end; ++i) {
        const auto group = groups[i - begin];
        if (tokens[i].timing.startOffset || tokens[i].timing.endOffset) eligibleGroups[group] = false;
        if (i == nuclei[group]) continue;
        // A declared event phone occupies an edge exactly the way an onset or a coda does: the
        // moraic obstruent before its vowel is an onset-position closure and after its vowel a
        // coda-position one. Its role says which phone it is, not which edge it sits on, so an
        // event that is neither is what makes a group ineligible rather than the role itself.
        const bool eventPhone = tokens[i].role==domain::PhonemeRole::Geminate ||
            tokens[i].role==domain::PhonemeRole::Breath || tokens[i].role==domain::PhonemeRole::Silence;
        const bool onsetPosition = i<nuclei[group] && (tokens[i].role==domain::PhonemeRole::Onset || eventPhone);
        const bool codaPosition = i>nuclei[group] && (tokens[i].role==domain::PhonemeRole::Coda || eventPhone);
        if (onsetPosition) {
          if (onsets[group]) eligibleGroups[group]=false;
          onsets[group]=i;
        } else if (codaPosition) {
          if (codas[group]) eligibleGroups[group]=false;
          codas[group]=i;
        } else eligibleGroups[group]=false;
      }
      for (std::size_t group = 0U; group < nuclei.size(); ++group) {
        const auto onset = onsets[group];
        if (!eligibleGroups[group] || (!onset && !codas[group]) || tokens[nuclei[group]].role != domain::PhonemeRole::Nucleus || !tokens[nuclei[group]].voiced) continue;
        generatedCodas[group]=codas[group];
        const auto start = boundary(group), finish = boundary(group + 1U);
        if (finish-start < 1+static_cast<int>(onset.has_value())+static_cast<int>(codas[group].has_value()))
          return core::failure<Output>(core::ErrorCode::Conflict,"Procedural syllable is too short for its gestures",tokens[nuclei[group]].key.toString());
        if (!onset) continue;
        // Engineering default, not a measured phonetic duration. Each edge
        // reserves at most one quarter of a normal syllable, capped at 60 ms.
        const auto duration = std::max<time::SampleFrame>(1, std::min<time::SampleFrame>(
            static_cast<time::SampleFrame>(sampleRate) * 60 / 1000, (finish - start) / 4));
        // The gesture starts where this syllable's nucleus actually resolved, so a
        // generated timing proposal moves the onset with its vowel instead of
        // stretching the consonant against a fixed score boundary.
        const auto resolvedStart = result[nuclei[group]].nucleusFrame;
        result[*onset].inferredStartFrame = resolvedStart;
        result[nuclei[group]].nucleusFrame = resolvedStart + duration;
        generatedStarts[group] = start;
      }
    }
    for (std::size_t group = 1U; group < nuclei.size(); ++group) {
      if (result[nuclei[group]].nucleusFrame <= result[nuclei[group - 1U]].nucleusFrame) {
        return core::failure<Output>(core::ErrorCode::Conflict,
            "Phoneme nucleus timing must remain strictly ordered", tokens[nuclei[group]].key.toString());
      }
    }
    for (auto i = begin; i < end; ++i) {
      const auto group = groups[i - begin];
      if (group + 1U == nuclei.size()) continue;
      // Automatic ends follow the fully edited next nucleus, not its obsolete
      // equal-time boundary. Validate only after that dependency is resolved.
      const auto nextAnchor = generatedStarts[group + 1U].value_or(result[nuclei[group + 1U]].nucleusFrame);
      if (!tokens[i].timing.endOffset) result[i].endFrame = nextAnchor;
      else if (result[i].endFrame > nextAnchor) {
        return core::failure<Output>(core::ErrorCode::Conflict,
            "Explicit phoneme end crosses the next nucleus", tokens[i].key.toString());
      }
      if (result[i].endFrame <= result[i].nucleusFrame) {
        return core::failure<Output>(core::ErrorCode::Conflict,
            "Phoneme timing leaves no space before the next nucleus", tokens[i].key.toString());
      }
    }
    // Resolve coda ends after next-syllable dependencies, then reserve their
    // own tail without relabeling generated boundaries as authored edits.
    for (std::size_t group=0U;group<nuclei.size();++group) if (generatedCodas[group]) {
      const auto coda=*generatedCodas[group];
      const auto finish=result[coda].endFrame;
      const auto length=finish-boundary(group);
      const auto duration=std::max<time::SampleFrame>(1,std::min<time::SampleFrame>(sampleRate*60/1000,length/4));
      const auto start=finish-duration;
      if (start<=result[nuclei[group]].nucleusFrame)
        return core::failure<Output>(core::ErrorCode::Conflict,"Procedural coda leaves no voiced nucleus span",tokens[coda].key.toString());
      result[coda].inferredStartFrame=start;
      result[nuclei[group]].endFrame=start;
    }
    // Publish the syllable's actual (possibly edited) nucleus separately from
    // a token's explicit start. Onsets must never masquerade as vowel anchors.
    for (auto i = begin; i < end; ++i) {
      result[i].nucleusFrame = result[nuclei[groups[i - begin]]].nucleusFrame;
    }
    begin = end;
  }
  return result;
}
}
