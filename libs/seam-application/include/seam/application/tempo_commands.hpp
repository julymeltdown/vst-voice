#pragma once
#include "seam/application/command.hpp"
#include <optional>

namespace seam::application {
// A missing value removes the event. Tick-zero removal is rejected by the map.
// History retains both complete maps and rejects out-of-band map replacement.
class EditTempoCommand final : public ICommand {
public:
  EditTempoCommand(time::Tick tick, std::optional<double> bpm) : tick_(tick), bpm_(bpm) {}
  [[nodiscard]] std::string_view name() const noexcept override { return "Edit tempo"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override { return CommandAudioImpact::ProjectAudio; }
  [[nodiscard]] CommandImpact impact() const override { CommandImpact result; result.scope = audioImpact(); result.projectWide = true; return result; }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  time::Tick tick_;
  std::optional<double> bpm_;
  std::optional<time::TempoMap> before_, after_;
};

class EditMeterCommand final : public ICommand {
public:
  struct Signature { std::uint8_t numerator, denominator; };
  EditMeterCommand(time::Tick tick, std::optional<Signature> signature) : tick_(tick), signature_(signature) {}
  [[nodiscard]] std::string_view name() const noexcept override { return "Edit meter"; }
  // Meter participates in performance-job context; conservatively invalidate
  // the project until beat-dependent rendering can use narrower dependencies.
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override { return CommandAudioImpact::ProjectAudio; }
  [[nodiscard]] CommandImpact impact() const override { CommandImpact result; result.scope = audioImpact(); result.projectWide = true; return result; }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  time::Tick tick_;
  std::optional<Signature> signature_;
  std::optional<time::MeterMap> before_, after_;
};
}
