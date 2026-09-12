#pragma once

#include "seam/domain/project.hpp"
#include <map>
#include <stop_token>

namespace seam::ui {
inline constexpr std::size_t maximumDependencyReviewRecords = 30000U;
enum class DependentEditKind { Phoneme, Unit, Seam };
struct DependentEditOutcome final {
  DependentEditKind kind;
  domain::PhonemeKey key;
  // Missing means absent on that side, never implicitly resolved.
  std::optional<bool> beforeUnresolved;
  std::optional<bool> afterUnresolved;
  friend bool operator==(const DependentEditOutcome&, const DependentEditOutcome&) = default;
};

inline core::Result<std::size_t> dependencyReviewRecordCount(const domain::VocalRegion& region) {
  std::size_t count = 0U;
  for (const auto size : {region.phonemeOverrides.size(), region.unitSelectionOverrides.size(), region.seamOverrides.size()}) {
    if (size > maximumDependencyReviewRecords - count)
      return core::failure<std::size_t>(core::ErrorCode::InvalidArgument, "Review exceeds 30000 dependency records");
    count += size;
  }
  return core::success(count);
}

inline core::Result<std::vector<DependentEditOutcome>> compareDependentEdits(
    const domain::VocalRegion& before, const domain::VocalRegion& after, std::stop_token stop = {}) {
  const auto beforeCount = dependencyReviewRecordCount(before); if (!beforeCount) return core::Result<std::vector<DependentEditOutcome>>{beforeCount.error()};
  const auto afterCount = dependencyReviewRecordCount(after); if (!afterCount) return core::Result<std::vector<DependentEditOutcome>>{afterCount.error()};
  std::vector<DependentEditOutcome> outcomes;
  outcomes.reserve(beforeCount.value() + afterCount.value());
  const auto append = [&](DependentEditKind kind, const auto& oldRecords, const auto& newRecords, auto key) -> core::Result<void> {
    std::map<domain::PhonemeKey, std::pair<std::optional<bool>, std::optional<bool>>> records;
    for (const auto& record : oldRecords) {
      if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Dependency review cancelled");
      auto& state = records[key(record)];
      if (state.first.has_value()) return core::failure(core::ErrorCode::Conflict, "Duplicate source dependency key");
      state.first = record.unresolved;
    }
    for (const auto& record : newRecords) {
      if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Dependency review cancelled");
      auto& state = records[key(record)];
      if (state.second.has_value()) return core::failure(core::ErrorCode::Conflict, "Duplicate result dependency key");
      state.second = record.unresolved;
    }
    for (const auto& [id, state] : records) {
      if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Dependency review cancelled");
      outcomes.push_back({kind, id, state.first, state.second});
    }
    return core::success();
  };
  auto result = append(DependentEditKind::Phoneme, before.phonemeOverrides, after.phonemeOverrides, [](const auto& item) { return item.key; });
  if (result) result = append(DependentEditKind::Unit, before.unitSelectionOverrides, after.unitSelectionOverrides, [](const auto& item) { return item.startKey; });
  if (result) result = append(DependentEditKind::Seam, before.seamOverrides, after.seamOverrides, [](const auto& item) { return item.incomingStartKey; });
  if (!result) return core::Result<std::vector<DependentEditOutcome>>{result.error()};
  if (stop.stop_requested()) return core::failure<std::vector<DependentEditOutcome>>(core::ErrorCode::Conflict, "Dependency review cancelled");
  return core::success(std::move(outcomes));
}
}
