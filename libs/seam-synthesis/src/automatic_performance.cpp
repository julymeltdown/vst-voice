#include "seam/synthesis/automatic_performance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace seam::synthesis {
namespace {

constexpr std::size_t kMaximumGeneratedNotes = 4096U;

// Proposal defaults, not measured quality targets. They are named here so a later
// study can replace them with measured values instead of hunting magic numbers.
constexpr double kPhrasePeakCents = 18.0;
constexpr double kPhraseEdgeCents = 6.0;
constexpr double kOnsetScoopCents = 35.0;
constexpr double kHumanizeCents = 2.0;
constexpr double kDynamicsSwing = 0.12;
constexpr double kDynamicsPhraseStartAccent = 1.03;
constexpr double kDynamicsLeapAccent = 1.05;
constexpr double kDynamicsFinalDamping = 0.92;
constexpr double kLeapSemitones = 5.0;
constexpr double kAttackPhraseStartMs = 25.0;
constexpr double kAttackLegatoMs = 55.0;
constexpr double kAttackStaccatoMs = 15.0;
constexpr double kAttackDefaultMs = 35.0;
constexpr double kReleasePhraseFinalMs = 120.0;
constexpr double kReleaseStaccatoMs = 25.0;
constexpr double kReleaseLegatoMs = 45.0;
constexpr double kReleaseStopCodaMs = 60.0;
constexpr double kReleaseDefaultMs = 40.0;

std::int32_t seededCents(std::uint64_t seed, domain::NoteId id) noexcept {
  auto value = seed ^ (id.value() + 0x9e3779b97f4a7c15ULL +
                       (seed << 6U) + (seed >> 2U));
  value ^= value >> 30U;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27U;
  value *= 0x94d049bb133111ebULL;
  value ^= value >> 31U;
  return static_cast<std::int32_t>(value % 9U) - 4;
}

template <typename T>
void appendPoint(std::vector<domain::PerformancePoint>& points,
                 time::Tick tick, T value) {
  if (!points.empty() && points.back().tick == tick) {
    points.back().value = static_cast<double>(value);
    return;
  }
  points.push_back({tick, static_cast<double>(value)});
}

void appendCents(std::vector<domain::PerformancePoint>& points, time::Tick tick,
                 double cents) {
  appendPoint(points, tick, std::clamp(cents, 0.0, 12700.0));
}

// One phrase is a run of notes with no rest between them. Phrase position is the
// context this backend shapes: rests are what a listener hears as phrase breaks.
struct Phrase final {
  std::size_t first{0U};
  std::size_t count{0U};
};

std::vector<Phrase> phrasesOf(const domain::VocalRegion& region) {
  std::vector<Phrase> phrases;
  std::size_t index = 0U;
  while (index < region.notes.size()) {
    const std::size_t first = index;
    while (index + 1U < region.notes.size() &&
           region.notes[index + 1U].startTick <= region.notes[index].endTick()) {
      ++index;
    }
    phrases.push_back(Phrase{first, index - first + 1U});
    ++index;
  }
  return phrases;
}

// Phrase arch: 1.0 at the phrase centre and 0.0 at either edge.
double archWeight(double position) noexcept {
  const double offset = position - 0.5;
  return 1.0 - 4.0 * offset * offset;
}

// The symbol a note starts with decides whether an onset can scoop at all: a
// phrase-initial vowel simply starts on pitch.
class NotePhones final {
public:
  explicit NotePhones(const phonemizer::Result& pronunciation) {
    for (const auto& token : pronunciation.tokens) {
      auto& entry = byNote_[token.key.noteId];
      if (entry.first.empty() && !token.symbol.empty()) entry.first = token.symbol;
      entry.last = token.symbol;
    }
  }
  [[nodiscard]] std::string_view first(domain::NoteId id) const {
    const auto found = byNote_.find(id);
    return found == byNote_.end() ? std::string_view{} : std::string_view{found->second.first};
  }
  [[nodiscard]] std::string_view last(domain::NoteId id) const {
    const auto found = byNote_.find(id);
    return found == byNote_.end() ? std::string_view{} : std::string_view{found->second.last};
  }

private:
  struct Entry final {
    std::string first;
    std::string last;
  };
  std::unordered_map<domain::NoteId, Entry> byNote_;
};

bool isStopSymbol(std::string_view symbol) noexcept {
  return symbol == "p" || symbol == "t" || symbol == "k" || symbol == "q" ||
         symbol == "cl" || symbol == "py" || symbol == "ky" || symbol == "pp" ||
         symbol == "tt" || symbol == "kk";
}

// Every backend validates the same request contract before it shapes anything.
core::Result<void> validateRequest(const domain::Project& project,
    const domain::VocalRegion& region,
    const phonemizer::ResolvedPronunciation& pronunciation,
    const AutomaticPerformanceRequest& request, std::stop_token stopToken) {
  if (stopToken.stop_requested()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Automatic performance generation was cancelled");
  }
  const auto projectValid = project.validate();
  if (!projectValid) return projectValid;
  const auto regionValid = region.validate();
  if (!regionValid) return regionValid;
  if (!request.regionId.valid() || request.regionId != region.id ||
      request.capturedRevision != region.performance.revision) {
    return core::failure(core::ErrorCode::Conflict,
        "Automatic performance request is stale or targets another region");
  }
  const auto pronunciationValid = pronunciation.identity.validate();
  if (!pronunciationValid) return pronunciationValid;
  if (request.pronunciation != pronunciation.identity) {
    return core::failure(core::ErrorCode::Conflict,
        "Automatic performance request pronunciation no longer matches the region");
  }
  if (request.channels.empty() || request.channels.size() > 12U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Automatic performance channel set is invalid");
  }
  for (std::size_t index = 0U; index < request.channels.size(); ++index) {
    const auto channel = request.channels[index];
    if (std::find(request.channels.begin(), request.channels.begin() +
                      static_cast<std::ptrdiff_t>(index), channel) !=
        request.channels.begin() + static_cast<std::ptrdiff_t>(index)) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Automatic performance channels repeat");
    }
    if (channel != domain::PerformanceChannel::Pitch &&
        channel != domain::PerformanceChannel::Dynamics &&
        channel != domain::PerformanceChannel::Attack &&
        channel != domain::PerformanceChannel::Release) {
      return core::failure(core::ErrorCode::Unsupported,
          "Automatic performance channel requires a qualified generator",
          std::string{domain::performanceChannelUnit(channel)});
    }
  }
  const auto rangeValid = request.range.validate();
  if (!rangeValid || request.range.endTick > region.durationTick) {
    return core::failure(core::ErrorCode::InvalidArgument,
        "Automatic performance range is outside the region");
  }
  if (region.notes.size() > kMaximumGeneratedNotes) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Automatic performance note count exceeds bounds");
  }
  return core::success();
}

}  // namespace

core::Result<domain::PerformanceTake> generateAutomaticPerformance(
    const domain::Project& project, const domain::VocalRegion& region,
    const phonemizer::ResolvedPronunciation& pronunciation,
    AutomaticPerformanceRequest request, std::stop_token stopToken) {
  using Output = domain::PerformanceTake;
  const auto requestValid = validateRequest(project, region, pronunciation, request, stopToken);
  if (!requestValid) return core::Result<Output>{requestValid.error()};

  Output result{
      .id = std::move(request.takeId),
      .sourceRegionId = region.id,
      .capturedRevision = request.capturedRevision,
      .resource = std::move(request.resource),
      .pronunciation = std::move(request.pronunciation),
      .generatorId = std::move(request.generatorId),
      .generatorVersion = std::move(request.generatorVersion),
      .seed = request.seed,
      .range = request.range,
      .state = domain::PerformanceProposalState::Proposed,
      .lanes = {},
  };

  const auto makeLane = [&](domain::PerformanceChannel channel) {
    domain::PerformanceLane lane{.channel = channel, .points = {}};
    const auto begin = request.range.startTick;
    const auto end = request.range.endTick;
    if (channel == domain::PerformanceChannel::Pitch) {
      for (const auto& note : region.notes) {
        if (stopToken.stop_requested()) return lane;
        if (note.endTick() <= begin || note.startTick >= end) continue;
        const auto tick = std::max(begin, note.startTick);
        const auto cents = std::clamp(
            static_cast<std::int32_t>(note.midiKey) * 100 +
                seededCents(request.seed, note.id),
            0, 12700);
        appendPoint(lane.points, tick, cents);
        const auto noteEnd = std::min(end, note.endTick());
        appendPoint(lane.points, noteEnd, cents);
      }
      if (lane.points.empty()) appendPoint(lane.points, begin, 0);
    } else if (channel == domain::PerformanceChannel::Dynamics) {
      appendPoint(lane.points, begin, region.dynamicsAutomation.valueAt(begin));
      for (const auto& note : region.notes) {
        if (stopToken.stop_requested()) return lane;
        if (note.endTick() <= begin || note.startTick >= end) continue;
        appendPoint(lane.points, std::clamp(note.startTick, begin, end),
                    region.dynamicsAutomation.valueAt(
                        std::clamp(note.startTick, begin, end)));
        appendPoint(lane.points, std::clamp(note.endTick(), begin, end),
                    region.dynamicsAutomation.valueAt(
                        std::clamp(note.endTick(), begin, end)));
      }
      appendPoint(lane.points, end, region.dynamicsAutomation.valueAt(end));
    } else if (channel == domain::PerformanceChannel::Attack) {
      for (const auto& note : region.notes) {
        if (stopToken.stop_requested()) return lane;
        if (note.endTick() <= begin || note.startTick >= end) continue;
        appendPoint(lane.points, std::max(begin, note.startTick),
                    note.articulation == domain::NoteArticulation::Staccato
                        ? 20.0
                        : 35.0);
      }
      if (lane.points.empty()) appendPoint(lane.points, begin, 0.0);
    } else {
      for (const auto& note : region.notes) {
        if (stopToken.stop_requested()) return lane;
        if (note.endTick() <= begin || note.startTick >= end) continue;
        appendPoint(lane.points, std::min(end, note.endTick()), 40.0);
      }
      if (lane.points.empty()) appendPoint(lane.points, begin, 0.0);
    }
    return lane;
  };

  result.lanes.reserve(request.channels.size());
  for (const auto channel : request.channels) {
    if (stopToken.stop_requested()) {
      return core::failure<Output>(core::ErrorCode::Conflict,
                                   "Automatic performance generation was cancelled");
    }
    result.lanes.push_back(makeLane(channel));
  }
  if (stopToken.stop_requested()) {
    return core::failure<Output>(core::ErrorCode::Conflict,
                                 "Automatic performance generation was cancelled");
  }
  const auto valid = result.validate();
  if (!valid) return core::Result<Output>{valid.error()};
  return result;
}

core::Result<domain::PerformanceTake> generatePhraseAwarePerformance(
    const domain::Project& project, const domain::VocalRegion& region,
    const phonemizer::ResolvedPronunciation& pronunciation,
    AutomaticPerformanceRequest request, std::stop_token stopToken) {
  using Output = domain::PerformanceTake;
  const auto valid = validateRequest(project, region, pronunciation, request, stopToken);
  if (!valid) return core::Result<Output>{valid.error()};
  // This backend stamps its own identity. A request naming a different generator
  // or an unknown version of this one is refused rather than silently mislabelled,
  // because the take's identity is what later invalidation and cache keys use.
  if ((!request.generatorId.empty() && request.generatorId != kPhraseAwareGeneratorId) ||
      (!request.generatorVersion.empty() &&
       request.generatorVersion != kPhraseAwareGeneratorVersion)) {
    return core::failure<Output>(core::ErrorCode::Unsupported,
        "Requested generator is not this proposal backend",
        request.generatorId + " " + request.generatorVersion);
  }

  const auto phrases = phrasesOf(region);
  const NotePhones phones{pronunciation.pronunciation};
  const auto begin = request.range.startTick;
  const auto end = request.range.endTick;

  Output result{
      .id = std::move(request.takeId),
      .sourceRegionId = region.id,
      .capturedRevision = request.capturedRevision,
      .resource = std::move(request.resource),
      .pronunciation = std::move(request.pronunciation),
      .generatorId = std::string{kPhraseAwareGeneratorId},
      .generatorVersion = std::string{kPhraseAwareGeneratorVersion},
      .seed = request.seed,
      .range = request.range,
      .state = domain::PerformanceProposalState::Proposed,
      .lanes = {},
  };

  // Position of a note inside its phrase, plus the interval it arrives by. Both are
  // context a singer hears; neither depends on the seed.
  const auto positionOf = [&](std::size_t phraseIndex, std::size_t offset) {
    const auto& phrase = phrases[phraseIndex];
    if (phrase.count <= 1U) return 0.5;
    return static_cast<double>(offset) / static_cast<double>(phrase.count - 1U);
  };
  const auto leapInto = [&](std::size_t index) {
    if (index == 0U) return 0.0;
    const auto previous = static_cast<double>(region.notes[index - 1U].midiKey);
    const auto current = static_cast<double>(region.notes[index].midiKey);
    return current - previous;
  };
  const auto shapedCents = [&](std::size_t index, std::size_t phraseIndex,
                               std::size_t offset) {
    const auto position = positionOf(phraseIndex, offset);
    const auto arch = archWeight(position);
    const double base = static_cast<double>(region.notes[index].midiKey) * 100.0;
    const double humanize =
        static_cast<double>(seededCents(request.seed, region.notes[index].id)) /
        (4.0 / kHumanizeCents);
    return base + kPhrasePeakCents * arch - kPhraseEdgeCents * (1.0 - arch) + humanize;
  };
  const auto pitchLane = [&] {
    domain::PerformanceLane lane{.channel = domain::PerformanceChannel::Pitch, .points = {}};
    for (std::size_t phraseIndex = 0U; phraseIndex < phrases.size(); ++phraseIndex) {
      const auto& phrase = phrases[phraseIndex];
      for (std::size_t offset = 0U; offset < phrase.count; ++offset) {
        const auto index = phrase.first + offset;
        const auto& note = region.notes[index];
        if (note.endTick() <= begin || note.startTick >= end) continue;
        const auto start = std::max(begin, note.startTick);
        const auto stopAt = std::min(end, note.endTick());
        const auto shaped = shapedCents(index, phraseIndex, offset);
        const auto firstSymbol = phones.first(note.id);
        const bool onsetScoops = offset == 0U && !firstSymbol.empty() &&
            !phonemizer::isVowelSymbol(firstSymbol) &&
            phonemizer::inferRole(firstSymbol) == domain::PhonemeRole::Onset;
        if (onsetScoops) {
          appendCents(lane.points, start, shaped - kOnsetScoopCents);
          const auto duration = (note.endTick() - note.startTick).value();
          const auto settleTick = std::min(stopAt.value(), start.value() + duration / 6);
          if (settleTick > start.value()) appendCents(lane.points, time::Tick{settleTick}, shaped);
        } else {
          appendCents(lane.points, start, shaped);
        }
        if (stopAt > start) {
          // The phrase's last note settles on its written pitch; every other note
          // holds the shaped value through its own span.
          const bool phraseFinal = offset + 1U == phrase.count;
          appendCents(lane.points, stopAt,
              phraseFinal ? static_cast<double>(note.midiKey) * 100.0 : shaped);
        }
      }
    }
    if (lane.points.empty()) appendCents(lane.points, begin, 0.0);
    return lane;
  };
  const auto dynamicsLane = [&] {
    domain::PerformanceLane lane{.channel = domain::PerformanceChannel::Dynamics, .points = {}};
    const auto gainAt = [&](time::Tick tick) {
      return region.dynamicsAutomation.valueAt(tick);
    };
    appendPoint(lane.points, begin, gainAt(begin));
    for (std::size_t index = 0U; index < region.notes.size(); ++index) {
      const auto& note = region.notes[index];
      if (note.endTick() <= begin || note.startTick >= end) continue;
      std::size_t phraseIndex = 0U;
      std::size_t offset = 0U;
      for (std::size_t candidate = 0U; candidate < phrases.size(); ++candidate) {
        if (index >= phrases[candidate].first &&
            index < phrases[candidate].first + phrases[candidate].count) {
          phraseIndex = candidate;
          offset = index - phrases[candidate].first;
          break;
        }
      }
      const bool phraseFinal = offset + 1U == phrases[phraseIndex].count;
      const double arch = archWeight(positionOf(phraseIndex, offset));
      double factor = 1.0 + kDynamicsSwing * arch;
      if (offset == 0U) factor *= kDynamicsPhraseStartAccent;
      if (std::abs(leapInto(index)) >= kLeapSemitones) factor *= kDynamicsLeapAccent;
      if (phraseFinal) factor *= kDynamicsFinalDamping;
      const auto clamped = [&](time::Tick tick) {
        return std::clamp<double>(static_cast<double>(gainAt(tick)) * factor, 0.0,
            static_cast<double>(domain::kMaximumDynamicsGain));
      };
      const auto start = std::max(begin, note.startTick);
      const auto stopAt = std::min(end, note.endTick());
      appendPoint(lane.points, start, clamped(start));
      if (stopAt > start) appendPoint(lane.points, stopAt, clamped(stopAt));
    }
    appendPoint(lane.points, end, gainAt(end));
    return lane;
  };
  const auto attackLane = [&] {
    domain::PerformanceLane lane{.channel = domain::PerformanceChannel::Attack, .points = {}};
    for (std::size_t phraseIndex = 0U; phraseIndex < phrases.size(); ++phraseIndex) {
      const auto& phrase = phrases[phraseIndex];
      for (std::size_t offset = 0U; offset < phrase.count; ++offset) {
        const auto& note = region.notes[phrase.first + offset];
        if (note.endTick() <= begin || note.startTick >= end) continue;
        double attack = kAttackDefaultMs;
        const bool slurred = note.slurGroup.has_value();
        if (note.articulation == domain::NoteArticulation::Staccato) {
          attack = kAttackStaccatoMs;
        } else if (note.articulation == domain::NoteArticulation::Legato || slurred) {
          attack = kAttackLegatoMs;
        } else if (offset == 0U) {
          attack = kAttackPhraseStartMs;
        }
        appendPoint(lane.points, std::max(begin, note.startTick), attack);
      }
    }
    if (lane.points.empty()) appendPoint(lane.points, begin, 0.0);
    return lane;
  };
  const auto releaseLane = [&] {
    domain::PerformanceLane lane{.channel = domain::PerformanceChannel::Release, .points = {}};
    for (std::size_t phraseIndex = 0U; phraseIndex < phrases.size(); ++phraseIndex) {
      const auto& phrase = phrases[phraseIndex];
      for (std::size_t offset = 0U; offset < phrase.count; ++offset) {
        const auto& note = region.notes[phrase.first + offset];
        if (note.endTick() <= begin || note.startTick >= end) continue;
        const bool phraseFinal = offset + 1U == phrase.count;
        const bool slurred = note.slurGroup.has_value();
        double release = kReleaseDefaultMs;
        if (note.articulation == domain::NoteArticulation::Staccato) {
          release = kReleaseStaccatoMs;
        } else if (phraseFinal) {
          release = kReleasePhraseFinalMs;
        } else if (note.articulation == domain::NoteArticulation::Legato || slurred) {
          release = kReleaseLegatoMs;
        } else if (isStopSymbol(phones.last(note.id))) {
          release = kReleaseStopCodaMs;
        }
        appendPoint(lane.points, std::max(begin, std::min(end, note.endTick())), release);
      }
    }
    if (lane.points.empty()) appendPoint(lane.points, end, 0.0);
    return lane;
  };

  for (const auto channel : request.channels) {
    if (stopToken.stop_requested()) {
      return core::failure<Output>(core::ErrorCode::Conflict,
                                   "Automatic performance generation was cancelled");
    }
    switch (channel) {
      case domain::PerformanceChannel::Pitch: result.lanes.push_back(pitchLane()); break;
      case domain::PerformanceChannel::Dynamics: result.lanes.push_back(dynamicsLane()); break;
      case domain::PerformanceChannel::Attack: result.lanes.push_back(attackLane()); break;
      default: result.lanes.push_back(releaseLane()); break;
    }
  }
  if (stopToken.stop_requested()) {
    return core::failure<Output>(core::ErrorCode::Conflict,
                                 "Automatic performance generation was cancelled");
  }
  const auto takeValid = result.validate();
  if (!takeValid) return core::Result<Output>{takeValid.error()};
  return result;
}

}  // namespace seam::synthesis
