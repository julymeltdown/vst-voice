#include "seam/clap_editor/offline_render_session.hpp"

#include <algorithm>
#include <cctype>

namespace seam::clap_editor {
namespace {

bool validHash(std::string_view value) noexcept {
  if (value.size() != 64U) return false;
  return std::all_of(value.begin(), value.end(), [](char byte) {
    return std::isdigit(static_cast<unsigned char>(byte)) != 0 ||
           (byte >= 'a' && byte <= 'f');
  });
}

bool validIdentityText(std::string_view value) noexcept {
  if (value.empty() || value.size() > 128U) return false;
  return std::all_of(value.begin(), value.end(), [](char byte) {
    const auto unsignedByte = static_cast<unsigned char>(byte);
    return unsignedByte >= 0x21U && unsignedByte <= 0x7eU;
  });
}

}  // namespace

core::Result<void> OfflineRenderIdentity::validate() const {
  if (sampleRate < 8000U || sampleRate > 384000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Offline render sample rate is outside supported bounds");
  }
  if (quality != rendering::RenderQuality::Final) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Offline render identity must use Final quality");
  }
  if (!validHash(projectContentHash) || !validHash(timingMapHash) ||
      !validIdentityText(rendererIdentity)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Offline render identity contains an invalid digest or renderer identity");
  }
  return core::success();
}

core::Result<void> OfflineRenderSession::begin(OfflineRenderIdentity identity) {
  const auto valid = identity.validate();
  if (!valid) return valid;
  std::lock_guard lock(mutex_);
  view_ = OfflineRenderView{
      .state = OfflineRenderState::Pending,
      .identity = std::move(identity),
      .hasAudio = false,
      .diagnostic = "Offline render preparation is pending",
  };
  return core::success();
}

core::Result<void> OfflineRenderSession::publish(
    const OfflineRenderIdentity& identity, bool hasAudio,
    std::string diagnostic) {
  const auto valid = identity.validate();
  if (!valid) return valid;
  if (!hasAudio) {
    const auto failureMessage = diagnostic.empty()
                                    ? "Offline render produced no audible audio"
                                    : diagnostic;
    const auto failed = fail(identity, failureMessage);
    if (!failed) return failed;
    return core::failure(core::ErrorCode::Conflict, failureMessage);
  }
  std::lock_guard lock(mutex_);
  if (view_.state != OfflineRenderState::Pending ||
      view_.identity != identity) {
    return core::failure(core::ErrorCode::Conflict,
                         "Offline render completion is stale");
  }
  view_.state = OfflineRenderState::Ready;
  view_.hasAudio = true;
  view_.diagnostic = diagnostic.empty() ? "Offline render is ready"
                                       : std::move(diagnostic);
  return core::success();
}

core::Result<void> OfflineRenderSession::fail(
    const OfflineRenderIdentity& identity, std::string diagnostic) {
  const auto valid = identity.validate();
  if (!valid) return valid;
  if (diagnostic.empty()) diagnostic = "Offline render failed";
  std::lock_guard lock(mutex_);
  if (view_.state != OfflineRenderState::Pending ||
      view_.identity != identity) {
    return core::failure(core::ErrorCode::Conflict,
                         "Offline render failure is stale");
  }
  view_.state = OfflineRenderState::Failed;
  view_.hasAudio = false;
  view_.diagnostic = std::move(diagnostic);
  return core::success();
}

void OfflineRenderSession::invalidate(std::string diagnostic) noexcept {
  try {
    std::lock_guard lock(mutex_);
    if (view_.state == OfflineRenderState::Pending ||
        view_.state == OfflineRenderState::Ready) {
      view_.state = OfflineRenderState::Stale;
      view_.hasAudio = false;
      view_.diagnostic = diagnostic.empty()
                             ? "Offline render is stale and must be prepared again"
                             : std::move(diagnostic);
    }
  } catch (...) {
  }
}

OfflineRenderView OfflineRenderSession::view() const {
  std::lock_guard lock(mutex_);
  return view_;
}

bool OfflineRenderSession::readyFor(
    const OfflineRenderIdentity& identity) const noexcept {
  std::lock_guard lock(mutex_);
  return view_.state == OfflineRenderState::Ready && view_.hasAudio &&
         view_.identity == identity;
}

std::string_view OfflineRenderSession::stateName(
    OfflineRenderState state) noexcept {
  switch (state) {
    case OfflineRenderState::Idle: return "idle";
    case OfflineRenderState::Pending: return "pending";
    case OfflineRenderState::Ready: return "ready";
    case OfflineRenderState::Stale: return "stale";
    case OfflineRenderState::Failed: return "failed";
  }
  return "unknown";
}

std::string_view OfflineRenderSession::authorityName(
    OfflineTimingAuthority authority) noexcept {
  switch (authority) {
    case OfflineTimingAuthority::FixedAudio: return "fixed-audio";
    case OfflineTimingAuthority::FollowHost: return "follow-host";
  }
  return "unknown";
}

}  // namespace seam::clap_editor
