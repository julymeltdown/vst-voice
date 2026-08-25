#include "seam/native_ui/render_status_panel.hpp"

#include <algorithm>
#include <cmath>

namespace seam::native_ui {

namespace {

std::string_view renderQualityName(
    seam::rendering::RenderQuality quality) noexcept {
  switch (quality) {
    case seam::rendering::RenderQuality::Preview: return "PREVIEW";
    case seam::rendering::RenderQuality::Final: return "FINAL";
  }
  return "UNKNOWN";
}

}

std::string_view renderStatusStateName(RenderStatusState state) noexcept {
  switch (state) {
    case RenderStatusState::Idle: return "IDLE";
    case RenderStatusState::Queued: return "QUEUED";
    case RenderStatusState::Rendering: return "RENDERING";
    case RenderStatusState::Ready: return "READY";
    case RenderStatusState::Stale: return "STALE";
    case RenderStatusState::Cancelled: return "CANCELLED";
    case RenderStatusState::Failed: return "FAILED";
  }
  return "UNKNOWN";
}

void RenderStatusPanelModel::update(RenderStatusView view) noexcept {
  if (!std::isfinite(view.fraction)) view.fraction = 0.0;
  view.fraction = std::clamp(view.fraction, 0.0, 1.0);
  if (view.totalPhrases == 0U) {
    view.completedPhrases = 0U;
  } else {
    view.completedPhrases =
        std::min(view.completedPhrases, view.totalPhrases);
  }
  view_ = std::move(view);
}

bool RenderStatusPanelModel::isStale() const noexcept {
  return view_.hasAudibleAudio &&
         (view_.audibleAudioStale ||
         (view_.audibleRevision != view_.requestedRevision ||
          view_.audibleQuality != view_.requestedQuality));
}

bool RenderStatusPanelModel::canCancel() const noexcept {
  return view_.state == RenderStatusState::Queued ||
         view_.state == RenderStatusState::Rendering;
}

bool RenderStatusPanelModel::canRetry() const noexcept {
  return view_.state == RenderStatusState::Cancelled ||
         view_.state == RenderStatusState::Failed;
}

std::string RenderStatusPanelModel::label() const {
  std::string result{renderStatusStateName(view_.state)};
  result += " REQ r" + std::to_string(view_.requestedRevision) + " " +
            std::string{renderQualityName(view_.requestedQuality)};
  result += " AUD ";
  result += view_.hasAudibleAudio
                 ? "r" + std::to_string(view_.audibleRevision) + " " +
                       std::string{renderQualityName(view_.audibleQuality)}
                 : "NONE";
  if (isStale()) {
    result += " STALE AUDIO";
  }
  if (!view_.activeVoicebankId.empty()) {
    result += " VB " + view_.activeVoicebankId;
    if (!view_.activeVoicebankVersion.empty()) {
      result += "@" + view_.activeVoicebankVersion;
    }
  }
  if (!view_.activeRenderer.empty()) {
    result += " R " + view_.activeRenderer;
  }
  if (!view_.diagnostic.empty() &&
      view_.diagnostic != "Production multi-track routing render completed" &&
      view_.diagnostic != "Production render request queued" &&
      view_.diagnostic != "Production render in progress") {
    result += " / " + view_.diagnostic;
  }
  return result;
}

}
