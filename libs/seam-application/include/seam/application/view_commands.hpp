#pragma once

#include "seam/application/command.hpp"

#include <optional>

namespace seam::application {

class SetTechnicalLanePresentationCommand final : public ICommand {
public:
  SetTechnicalLanePresentationCommand(
      domain::TechnicalLane lane,
      domain::TechnicalLanePresentation presentation)
      : lane_(lane), after_(presentation) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Set technical lane presentation";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ViewOnly;
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TechnicalLane lane_;
  domain::TechnicalLanePresentation after_;
  std::optional<domain::TechnicalLanePresentation> before_;
};

}
