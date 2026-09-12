#include "seam/authoring/generation_campaign.hpp"
namespace seam::authoring {
core::Result<CampaignStorageUsage> inspectCampaignStorage(const std::filesystem::path& root,
    std::uint64_t maximumBytes, std::size_t maximumEntries, std::stop_token stop) {
  const auto fail = [](std::string message) { return core::failure<CampaignStorageUsage>(core::ErrorCode::Conflict, std::move(message)); };
  if (stop.stop_requested()) return fail("Campaign storage inspection cancelled");
  if (maximumBytes == 0U || maximumBytes > (1ULL << 40U) || maximumEntries == 0U || maximumEntries > 1048576U)
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
}
