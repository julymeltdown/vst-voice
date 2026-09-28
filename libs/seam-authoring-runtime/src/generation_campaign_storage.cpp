#include "seam/authoring/generation_campaign.hpp"
#include <limits>
namespace seam::authoring {
namespace {
core::Result<CampaignStorageUsage> scanCampaignStorage(const std::filesystem::path& root,
    std::uint64_t maximumBytes, std::size_t maximumEntries, std::stop_token stop) {
  const auto fail = [](std::string message) { return core::failure<CampaignStorageUsage>(core::ErrorCode::Conflict, std::move(message)); };
  if (stop.stop_requested()) return fail("Campaign storage inspection cancelled");
  if (maximumBytes == 0U || maximumEntries == 0U || maximumEntries > 1048576U)
    return fail("Campaign storage inspection limits are invalid");
  std::error_code error;
  const auto status = std::filesystem::symlink_status(root, error);
  if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) return fail("Campaign storage root is unsafe");
  CampaignStorageUsage usage;
  std::filesystem::recursive_directory_iterator iterator{root, std::filesystem::directory_options::none, error}, end;
  if (error) return fail("Cannot enumerate campaign storage");
  while (iterator != end) {
    if (stop.stop_requested()) return fail("Campaign storage inspection cancelled");
    if (usage.entries == maximumEntries || iterator.depth() > 32) return fail("Campaign storage exceeds entry or depth limit");
    ++usage.entries;
    const auto item = iterator->symlink_status(error);
    if (error || std::filesystem::is_symlink(item)) return fail("Campaign storage contains an unreadable entry or symbolic link");
    if (std::filesystem::is_regular_file(item)) {
      const auto size = iterator->file_size(error);
      if (error || size > maximumBytes - usage.logicalBytes) return fail("Campaign retained bytes exceed the admitted limit");
      usage.logicalBytes += size;
    } else if (!std::filesystem::is_directory(item)) return fail("Campaign storage contains a special file");
    iterator.increment(error);
    if (error) return fail("Cannot enumerate campaign storage");
  }
  return usage;
}
}  // namespace

core::Result<CampaignStorageUsage> inspectCampaignStorage(const std::filesystem::path& root,
    std::uint64_t maximumBytes, std::size_t maximumEntries, std::stop_token stop) {
  if (maximumBytes > (1ULL << 40U))
    return core::failure<CampaignStorageUsage>(core::ErrorCode::Conflict, "Campaign storage inspection limits are invalid");
  return scanCampaignStorage(root, maximumBytes, maximumEntries, stop);
}

core::Result<CampaignStorageUsage> measureCampaignRequestStorage(const std::filesystem::path& root,
    std::size_t batchCount, std::size_t maximumEntries, std::stop_token stop) {
  const auto fail = [](std::string message) { return core::failure<CampaignStorageUsage>(core::ErrorCode::Conflict, std::move(message)); };
  if (stop.stop_requested()) return fail("Campaign storage inspection cancelled");
  if (maximumEntries == 0U || maximumEntries > 1048576U || batchCount > 16384U)
    return fail("Campaign storage inspection limits are invalid");
  std::error_code error;
  const auto rootStatus = std::filesystem::symlink_status(root, error);
  if (error || !std::filesystem::is_directory(rootStatus) || std::filesystem::is_symlink(rootStatus))
    return fail("Campaign storage root is unsafe");
  const auto definition = root / "campaign.json";
  const auto definitionStatus = std::filesystem::symlink_status(definition, error);
  if (error || !std::filesystem::is_regular_file(definitionStatus)) return fail("Campaign definition is missing or unsafe");
  CampaignStorageUsage usage{std::filesystem::file_size(definition, error), 1U};
  if (error) return fail("Cannot measure the campaign definition");
  for (std::size_t index = 0U; index < batchCount; ++index) {
    const auto directory = root / ("batch-" + std::to_string(index));
    const auto status = std::filesystem::symlink_status(directory, error);
    if (error == std::errc::no_such_file_or_directory || (!error && status.type() == std::filesystem::file_type::not_found)) {
      error.clear();
      continue;
    }
    if (usage.entries >= maximumEntries) return fail("Campaign storage exceeds entry or depth limit");
    // The scan refuses a batch directory that is itself a link or not a directory.
    const auto batch = scanCampaignStorage(directory, std::numeric_limits<std::uint64_t>::max() - usage.logicalBytes,
                                           maximumEntries - usage.entries, stop);
    if (!batch) return batch;
    usage.logicalBytes += batch.value().logicalBytes;
    usage.entries += batch.value().entries + 1U;
  }
  return usage;
}
}
