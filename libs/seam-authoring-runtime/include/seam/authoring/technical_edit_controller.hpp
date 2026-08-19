#pragma once

#include "seam/authoring/project_document.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/synthesis/unit_selection.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace seam::authoring {

struct TechnicalUnitView final {
  synthesis::UnitPlanEntry entry;
  bool usedFallback{false};
  std::string diagnostic;
};

struct TechnicalRenderView final {
  std::vector<TechnicalUnitView> units;
};

class TechnicalEditController final {
public:
  using RenderViewProvider = std::function<TechnicalRenderView()>;
  using EditCommittedCallback = std::function<void()>;

  TechnicalEditController(ProjectDocument& document,
                          domain::RegionId regionId,
                          RenderViewProvider renderViewProvider,
                          EditCommittedCallback editCommitted = {});

  void setRegion(domain::RegionId regionId) noexcept { regionId_ = regionId; }
  [[nodiscard]] domain::RegionId regionId() const noexcept { return regionId_; }

  [[nodiscard]] core::Result<void> movePhonemeBoundary(
      domain::PhonemeKey key, bool startBoundary,
      time::Microseconds offset);
  [[nodiscard]] core::Result<void> selectUnitVariant(
      domain::PhonemeKey key, std::string unitId,
      domain::UnitRendererKind renderer);
  [[nodiscard]] core::Result<void> cycleUnitVariant(domain::PhonemeKey key);
  [[nodiscard]] core::Result<void> cycleUnitRenderer(domain::PhonemeKey key);

  [[nodiscard]] core::Result<void> upsertPitchPoint(
      domain::PitchAutomationPoint point);
  [[nodiscard]] core::Result<void> movePitchPoint(
      time::Tick from, domain::PitchAutomationPoint point);
  [[nodiscard]] core::Result<void> removePitchPoint(time::Tick tick);
  [[nodiscard]] core::Result<void> cyclePitchInterpolation(time::Tick tick);

  [[nodiscard]] core::Result<void> upsertSeam(domain::SeamOverride seam);
  [[nodiscard]] core::Result<void> removeSeam(domain::PhonemeKey key);

  [[nodiscard]] core::Result<void> undo();
  [[nodiscard]] core::Result<void> redo();

  [[nodiscard]] phonemizer::Result phonemes() const;
  [[nodiscard]] std::optional<TechnicalUnitView> unitDiagnostic(
      domain::PhonemeKey key) const;

private:
  [[nodiscard]] const domain::VocalRegion* region() const noexcept;
  [[nodiscard]] domain::VocalRegion* region() noexcept;
  [[nodiscard]] std::optional<TechnicalUnitView> unitView(
      domain::PhonemeKey key) const;
  [[nodiscard]] core::Result<void> commit(
      std::unique_ptr<application::ICommand> command);
  void notifyEdit() const;

  ProjectDocument* document_{nullptr};
  domain::RegionId regionId_{};
  RenderViewProvider renderViewProvider_;
  EditCommittedCallback editCommitted_;
};

}  // namespace seam::authoring
