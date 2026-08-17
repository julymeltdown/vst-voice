#pragma once

#include "seam/clap/session.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace seam::clap {

[[nodiscard]] core::Result<std::vector<std::byte>> encodeState(
    const PluginSession& session);
[[nodiscard]] core::Result<PluginSession> decodeState(
    std::span<const std::byte> bytes);
[[nodiscard]] core::Result<void> writeStateFile(
    const std::filesystem::path& path, const PluginSession& session);
[[nodiscard]] core::Result<PluginSession> readStateFile(
    const std::filesystem::path& path);

}  // namespace seam::clap
