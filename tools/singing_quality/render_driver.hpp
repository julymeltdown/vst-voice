#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/rendering/render_snapshot.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace seam::singing_quality {

struct Invocation final {
  std::filesystem::path project;
  std::filesystem::path manifest;
  std::filesystem::path audioLock;
  std::filesystem::path output;
  synthesis::RenderPolicy policy{synthesis::RenderPolicy::RespectVoicebank};
};

struct PreparedRender final {
  domain::Project project;
  std::vector<rendering::RenderSnapshot> snapshots;
  time::SampleFrame expectedFrames;
  std::string projectSha256;
  std::string manifestSha256;
};

inline constexpr std::uint32_t kSampleRate = 48000U;
inline constexpr time::SampleFrame kMaximumFrames = 65 * kSampleRate;

[[nodiscard]] core::Result<PreparedRender> prepare(const Invocation& invocation);
[[nodiscard]] core::Result<void> renderPacket(const PreparedRender& prepared,
                                             const Invocation& invocation);

}
