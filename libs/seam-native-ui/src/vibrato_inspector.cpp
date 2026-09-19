#include "seam/native_ui/vibrato_inspector.hpp"
#include "seam/core/finite_decimal.hpp"
#include <charconv>
#include <cmath>

namespace seam::native_ui {
namespace {
constexpr std::array<decltype(&ui::VibratoFields::startFraction), 6U> fields{
    &ui::VibratoFields::startFraction, &ui::VibratoFields::fadeInFraction, &ui::VibratoFields::fadeOutFraction,
    &ui::VibratoFields::depthCents, &ui::VibratoFields::periodMilliseconds, &ui::VibratoFields::phaseTurns};
std::string format(std::optional<float> value) {
  if (!value) return "Mixed";
  std::array<char, 48U> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), *value, std::chars_format::general);
  return result.ec == std::errc{} ? std::string(buffer.data(), result.ptr) : std::string{};
}
}

VibratoInspectorDraft::VibratoInspectorDraft(ui::VibratoModel source, domain::RegionId region)
    : source_(std::move(source)), region_(region), preview_(source_) {}

core::Result<VibratoInspectorDraft> VibratoInspectorDraft::prepare(const application::EditorSession& session, domain::RegionId region) {
  auto source = ui::VibratoModel::prepare(session, region); if (!source) return core::Result<VibratoInspectorDraft>{source.error()};
  return VibratoInspectorDraft{std::move(source.value()), region};
}

std::string_view VibratoInspectorDraft::label(VibratoField field) noexcept {
  constexpr std::array<std::string_view, 7U> labels{"Enabled (On/Off)", "Start fraction", "Fade-in fraction", "Fade-out fraction",
      "Depth (cents)", "Period (ms)", "Phase (turns)"};
  const auto index = static_cast<std::size_t>(field); return index < labels.size() ? labels[index] : "Invalid vibrato field";
}

std::string VibratoInspectorDraft::text(VibratoField field) const {
  const auto index = static_cast<std::size_t>(field); if (index >= input_.size()) return {};
  if (input_[index]) return *input_[index];
  if (index == 0U) { const auto value = source_.values().enabled; return !value ? "Mixed" : *value ? "On" : "Off"; }
  return format(source_.values().*fields[index - 1U]);
}

bool VibratoInspectorDraft::mixed(VibratoField field) const noexcept {
  const auto index = static_cast<std::size_t>(field);
  if (index >= input_.size() || input_[index]) return false;
  return index == 0U ? !source_.values().enabled : !(source_.values().*fields[index - 1U]);
}

bool VibratoInspectorDraft::canApply(const application::EditorSession& session, domain::RegionId activeRegion) const {
  return !closed_ && error_.empty() && preview_ && !preview_->edits().empty() && source_.matches(session, activeRegion) && preview_->matches(session, activeRegion);
}

core::Result<void> VibratoInspectorDraft::set(const application::EditorSession& session, domain::RegionId activeRegion, VibratoField field, std::string textValue) {
  if (closed_ || !source_.matches(session, activeRegion)) return core::failure(core::ErrorCode::Conflict, "Vibrato selection changed; reopen the inspector");
  const auto index = static_cast<std::size_t>(field);
  if (index >= input_.size() || textValue.size() > 64U) return core::failure(core::ErrorCode::InvalidArgument, "Vibrato field input exceeds bounds");
  input_[index] = std::move(textValue); return rebuild(session);
}

core::Result<void> VibratoInspectorDraft::reset(const application::EditorSession& session, domain::RegionId activeRegion, VibratoField field) {
  if (closed_ || !source_.matches(session, activeRegion)) return core::failure(core::ErrorCode::Conflict, "Vibrato selection changed; reopen the inspector");
  const auto index = static_cast<std::size_t>(field);
  if (index >= input_.size()) return core::failure(core::ErrorCode::InvalidArgument, "Vibrato field is unavailable");
  input_[index].reset(); return rebuild(session);
}

core::Result<void> VibratoInspectorDraft::rebuild(const application::EditorSession& session) {
  preview_.reset(); error_.clear(); ui::VibratoFields patch;
  for (std::size_t i = 0U; i < input_.size(); ++i) {
    if (!input_[i]) continue;
    std::string_view value = *input_[i];
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1U);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1U);
    bool valid = true;
    if (i == 0U) {
      if (value == "On" || value == "on" || value == "1") patch.enabled = true;
      else if (value == "Off" || value == "off" || value == "0") patch.enabled = false;
      else valid = false;
    } else {
      float number{};
      valid = core::parseFiniteDecimal(value, number);
      if (valid) patch.*fields[i - 1U] = number;
    }
    if (!valid) {
      error_ = "Enter a valid value for " + std::string{label(static_cast<VibratoField>(i))};
      return core::failure(core::ErrorCode::InvalidArgument, error_);
    }
  }
  auto preview = ui::VibratoModel::prepare(session, region_, patch);
  if (!preview) { error_ = preview.error().message; return core::Result<void>{preview.error()}; }
  preview_.emplace(std::move(preview.value())); return core::success();
}

core::Result<void> VibratoInspectorDraft::apply(application::EditorSession& session, domain::RegionId activeRegion) {
  if (!canApply(session, activeRegion)) return core::failure(core::ErrorCode::Conflict, "No current valid vibrato changes to apply");
  const auto result = preview_->apply(session, activeRegion); if (result) closed_ = true; return result;
}
void VibratoInspectorDraft::cancel() noexcept { closed_ = true; source_.cancel(); if (preview_) preview_->cancel(); }
}
