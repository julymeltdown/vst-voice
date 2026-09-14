#pragma once
#include "seam/domain/phoneme.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"
#include <string>
#include <vector>

namespace seam::voice_design {
// How two neighbouring sources share the frames on either side of their shared boundary. The
// linguistic spans stay ordered and non-overlapping; an overlap here is acoustic only, which is
// what keeps the global span check intact while still letting one phone colour its neighbour.
enum class TransitionKind {
  // The tract's own poles move from the previous pose to the next over the window. One filter runs
  // the whole time, so the transition is a formant movement rather than a change of filter.
  // Declared only where the tract is sounding on both sides of the boundary and the two poses are
  // two points in one parameter space; a boundary the tract is silent across (a voiceless consonant)
  // is a crossfade, because there is no sounding movement there to hear.
  FormantInterpolation,
  // Two independently filtered banks are crossfaded. This is the fallback a topology change needs:
  // an oral tract and a nasal-only tract are not two points in one parameter space.
  // It is also what a boundary declares when the tract is not sounding across it, and what the
  // renderer keeps for a declared movement this recipe's two banks cannot make.
  BankCrossfade,
};
// What each layer is doing across one transition, so a reader never has to infer it from the
// window. The parts that cannot cross a boundary say so here rather than being silently skipped.
struct TransitionComposition final {
  // The voiced lane keeps sounding across the boundary and its gain ramps inside the window. When
  // this is false, the transition ends where the voicing does and the window belongs to the release.
  bool voicingContinues{false};
  // A closure, a burst or an affricate release owns its own frames. A transition that would begin
  // inside one is not created at all, so this is a report of that decision rather than a rule the
  // renderer re-derives.
  bool releaseOwnsFrames{false};
  // Whether the target's own attack is inside the window (a consonant colouring its vowel) or the
  // window sits before the target starts (a gesture whose motion is its own last frames).
  bool targetAttackInsideWindow{false};
  // Aspiration and noise are source properties, not tract properties: they cross the boundary with
  // their own sources and are not re-triggered by the tract movement.
  bool noisePassesThrough{false};
  friend bool operator==(const TransitionComposition&, const TransitionComposition&) = default;
};
// One bounded acoustic transition between two gestures the plan already placed. `span` is absolute
// and always inside the phrase context; `frames` is its length, which is the same number the
// renderer schedules, so a reader can compare the declaration against the render.
struct ArticulationTransition final {
  domain::PhonemeKey from, to;
  std::string fromPhone, toPhone;
  synthesis::PhraseFrameRange span;
  std::uint32_t frames{0U};
  TransitionKind kind{TransitionKind::FormantInterpolation};
  TransitionComposition composition;
  friend bool operator==(const ArticulationTransition&, const ArticulationTransition&) = default;
};
}
