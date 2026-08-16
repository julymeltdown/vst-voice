#pragma once

#include "seam/domain/project.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/ui/geometry.hpp"
#include "seam/ui/timeline_transform.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <optional>
#include <string>
#include <vector>

namespace seam::ui {

struct UnitLaneVisual final {
  domain::PhonemeKey startKey;
  std::string unitId;
  std::vector<std::string> alternatives;
  Rect bounds;
  std::int32_t targetMidi{60};
  bool forced{false};
  voicebank::RendererHint requestedRenderer{voicebank::RendererHint::Raw};
  voicebank::RendererHint actualRenderer{voicebank::RendererHint::Raw};
  bool usedFallback{false};
  float seamAmount{0.7F};
  domain::SeamCurve seamCurve{domain::SeamCurve::HardCharacter};
  std::string diagnostic;
};

class UnitLaneModel final {
public:
  void rebuild(const domain::Project& project,
               const domain::VocalRegion& region,
               const phonemizer::Result& phonemes,
               const synthesis::UnitPlan& plan,
               const synthesis::TimingPlan& timing,
               const synthesis::PhraseRenderResult* rendered,
               const TimelineTransform& timeline,
               double contentLeft,
               double laneTop,
               double laneHeight,
               std::uint32_t sampleRate);

  [[nodiscard]] const std::vector<UnitLaneVisual>& visuals() const noexcept {
    return visuals_;
  }
  [[nodiscard]] std::optional<domain::PhonemeKey> hitTest(Point point) const;

private:
  std::vector<UnitLaneVisual> visuals_;
};

}  // namespace seam::ui
