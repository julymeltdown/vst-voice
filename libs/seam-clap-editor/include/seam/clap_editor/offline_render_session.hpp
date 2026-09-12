#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/rendering/render_snapshot.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace seam::clap_editor {

// FixedAudio renders against the score's persisted tempo map. FollowHost is
// reserved for a host timeline that has supplied a complete authoritative map;
// it must never be inferred from one instantaneous BPM value.
enum class OfflineTimingAuthority { FixedAudio, FollowHost };

enum class OfflineRenderState { Idle, Pending, Ready, Stale, Failed };

struct OfflineRenderIdentity final {
  std::uint64_t projectRevision{0U};
  std::uint32_t sampleRate{48000U};
  rendering::RenderQuality quality{rendering::RenderQuality::Final};
  OfflineTimingAuthority authority{OfflineTimingAuthority::FixedAudio};
  std::string projectContentHash;
  std::string timingMapHash;
  std::string rendererIdentity;

  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const OfflineRenderIdentity&,
                         const OfflineRenderIdentity&) = default;
};

struct OfflineRenderView final {
  OfflineRenderState state{OfflineRenderState::Idle};
  OfflineRenderIdentity identity;
  bool hasAudio{false};
  std::string diagnostic;
};

// Owner-thread publication gate for CLAP offline rendering. The class owns no
// PCM; the audio publication remains in AuthoringRenderCoordinator. It binds
// the host-facing offline claim to the exact render identity and rejects stale
// completion from an earlier project/rate/quality/resource request.
class OfflineRenderSession final {
public:
  [[nodiscard]] core::Result<void> begin(OfflineRenderIdentity identity);
  [[nodiscard]] core::Result<void> publish(
      const OfflineRenderIdentity& identity, bool hasAudio,
      std::string diagnostic = {});
  [[nodiscard]] core::Result<void> fail(
      const OfflineRenderIdentity& identity, std::string diagnostic);
  void invalidate(std::string diagnostic = {}) noexcept;

  [[nodiscard]] OfflineRenderView view() const;
  [[nodiscard]] bool readyFor(
      const OfflineRenderIdentity& identity) const noexcept;

  [[nodiscard]] static std::string_view stateName(
      OfflineRenderState state) noexcept;
  [[nodiscard]] static std::string_view authorityName(
      OfflineTimingAuthority authority) noexcept;

private:
  mutable std::mutex mutex_;
  OfflineRenderView view_;
};

}  // namespace seam::clap_editor
