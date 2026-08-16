#include "seam/voicebank/asset_path.hpp"

namespace seam::voicebank {
namespace {

bool containedBy(const std::filesystem::path& root,
                 const std::filesystem::path& candidate) {
  auto rootIterator = root.begin();
  auto candidateIterator = candidate.begin();
  for (; rootIterator != root.end(); ++rootIterator, ++candidateIterator) {
    if (candidateIterator == candidate.end() || *candidateIterator != *rootIterator) {
      return false;
    }
  }
  return true;
}

}  // namespace

core::Result<std::filesystem::path> resolveBankAsset(
    const std::filesystem::path& bankRoot,
    const std::filesystem::path& relativePath) {
  if (bankRoot.empty() || relativePath.empty() || relativePath.is_absolute()) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::InvalidArgument,
        "Voicebank asset path must be a non-empty relative path",
        relativePath.generic_string());
  }
  for (const auto& component : relativePath) {
    if (component == ".." || component == ".") {
      return core::failure<std::filesystem::path>(
          core::ErrorCode::InvalidArgument,
          "Voicebank asset path contains a forbidden component",
          relativePath.generic_string());
    }
  }

  std::error_code error;
  const auto canonicalRoot = std::filesystem::canonical(bankRoot, error);
  if (error || !std::filesystem::is_directory(canonicalRoot, error)) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::NotFound, "Voicebank root is unavailable",
        error ? error.message() : bankRoot.string());
  }

  auto current = canonicalRoot;
  for (const auto& component : relativePath) {
    current /= component;
    const auto status = std::filesystem::symlink_status(current, error);
    if (error) {
      return core::failure<std::filesystem::path>(
          core::ErrorCode::NotFound, "Voicebank asset component is unavailable",
          current.string());
    }
    if (std::filesystem::is_symlink(status)) {
      return core::failure<std::filesystem::path>(
          core::ErrorCode::Conflict,
          "Voicebank packages may not contain symbolic links",
          current.string());
    }
  }

  const auto canonicalCandidate = std::filesystem::canonical(current, error);
  if (error || !containedBy(canonicalRoot, canonicalCandidate)) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::Conflict,
        "Voicebank asset resolves outside the bank root",
        relativePath.generic_string());
  }
  const auto status = std::filesystem::status(canonicalCandidate, error);
  if (error || !std::filesystem::is_regular_file(status)) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::NotFound,
        "Voicebank asset is not a regular file",
        canonicalCandidate.string());
  }
  return canonicalCandidate;
}

}  // namespace seam::voicebank
