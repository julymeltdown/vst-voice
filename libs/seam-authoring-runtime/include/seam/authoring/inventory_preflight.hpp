#pragma once
#include "seam/authoring/generation_campaign.hpp"
#include <functional>
#include <optional>

namespace seam::authoring {
// A held-out phrase set rendered before a campaign multiplies a phone class across
// the whole bank. The selection is bounded and coverage-complete: every distinct
// phone symbol and every distinct coverage kind the campaign declares has to be
// exercised by at least one phrase, and a bound that cannot cover them is refused
// instead of truncated.
struct InventoryPreflightLimits final {
  std::size_t maximumPhrases{64U};
  std::uint64_t maximumFrames{8ULL * 1024ULL * 1024ULL};
};
struct InventoryPreflightPhrase final {
  std::string takeId, coverageKey, style, verdict, detail;
  std::int64_t pitchLayer{0};
  std::vector<std::string> requiredPhones, producedPhones;
  double peak{0.0};
  std::uint64_t frameCount{0U}, nonzeroFrames{0U};
};
struct InventoryPreflightReport final {
  std::size_t requiredPhones{0U}, requiredKinds{0U}, produced{0U}, defective{0U};
  std::vector<InventoryPreflightPhrase> phrases;
  std::vector<std::string> defectiveClasses;
  bool passed{false};
  // The exact canonical bytes written to the campaign's preflight.json.
  std::string json;
};
[[nodiscard]] core::Result<std::vector<std::string>> selectInventoryPreflightTakeIds(
    std::string_view campaignDefinition, std::string_view campaignSha256,
    InventoryPreflightLimits limits = {}, std::stop_token stop = {});
// Ordinary immutable generation path, one phrase at a time, into a new directory.
// Never collects, so producer and assignment state are unchanged. refusePhrase is a
// documented injection seam for fault tests; production callers omit it.
[[nodiscard]] core::Result<InventoryPreflightReport> runInventoryPreflight(
    std::string_view campaignDefinition, std::string_view campaignSha256,
    const std::filesystem::path& directory, InventoryPreflightLimits limits = {},
    std::stop_token stop = {},
    std::function<std::optional<std::string>(std::size_t)> refusePhrase = {});
// Admission: the report has to name this campaign, its frozen producer and recipe,
// and its status, counts and defective classes have to follow from its own phrases.
[[nodiscard]] core::Result<void> verifyInventoryPreflight(
    std::string_view report, std::string_view campaignDefinition,
    std::string_view campaignSha256, std::stop_token stop = {});
// The report a campaign directory must carry before it may be advanced.
[[nodiscard]] core::Result<void> verifyCampaignPreflight(
    const std::filesystem::path& campaignRoot, std::string_view campaignDefinition,
    std::string_view campaignSha256, std::stop_token stop = {});
}
