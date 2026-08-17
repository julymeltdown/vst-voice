#include "seam/clap_editor/editor_runtime.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

int main() {
  seam::clap_editor::EditorRuntime runtime;
  const auto initial = runtime.projectCopy();
  if (initial.noteCount() < 4U || !initial.validate()) return 1;

  const auto before = runtime.primarySeamAmount();
  const auto seamResult = runtime.setPrimarySeamAmount(0.82F);
  if (!seamResult || runtime.primarySeamAmount() < 0.81F || before == 0.82F) {
    return 2;
  }

  runtime.requestRender(48000U);
  std::shared_ptr<const seam::clap_editor::RenderedPreview> preview;
  for (int attempt = 0; attempt < 200; ++attempt) {
    preview = runtime.renderedPreview();
    if (preview != nullptr && !preview->stereo.empty() &&
        preview->revision == runtime.revision()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  if (preview == nullptr || preview->stereo.empty()) return 3;
  double energy = 0.0;
  for (const auto sample : preview->stereo) {
    if (!std::isfinite(sample)) return 4;
    energy += std::abs(static_cast<double>(sample));
  }
  if (energy <= 1.0) return 5;

  const auto encoded = seam::clap_editor::encodeEditorState(runtime.projectCopy());
  if (!encoded || encoded.value().empty()) return 6;
  const auto decoded = seam::clap_editor::decodeEditorState(encoded.value());
  if (!decoded || decoded.value().noteCount() != initial.noteCount()) return 7;
  auto replacement = decoded.value();
  replacement.setName("Phase 11 restored while GUI survives");
  const auto replacementResult = runtime.replaceProject(std::move(replacement));
  if (!replacementResult ||
      runtime.projectCopy().name() != "Phase 11 restored while GUI survives") {
    return 8;
  }
  auto corrupted = encoded.value();
  corrupted.back() ^= std::byte{0x01};
  if (seam::clap_editor::decodeEditorState(corrupted)) return 9;

  seam::clap_editor::LiveSampleInstrument semantics;
  semantics.setOutputSampleRate(48000.0);
  semantics.noteOn(101, 67, 0.0F);
  if (semantics.activeVoiceCount() != 1U) return 10;
  semantics.noteOn(102, 69, 0.8F);
  if (semantics.activeVoiceCount() != 2U) return 11;
  semantics.choke(102, 69);
  if (semantics.activeVoiceCount() != 1U) return 12;
  semantics.choke(101, 67);
  if (semantics.activeVoiceCount() != 0U) return 13;

  runtime.noteOn(1, 67, 0.9F);
  double liveEnergy = 0.0;
  for (int frame = 0; frame < 4000; ++frame) {
    const auto sample = runtime.renderLiveSample();
    if (!std::isfinite(sample)) return 14;
    liveEnergy += std::abs(static_cast<double>(sample));
  }
  runtime.noteOff(1, 67);
  for (int frame = 0; frame < 4000; ++frame) {
    liveEnergy += std::abs(static_cast<double>(runtime.renderLiveSample()));
  }
  if (liveEnergy <= 1.0) return 15;

  const auto stats = runtime.renderStats();
  if (stats.submitted == 0U || stats.completed == 0U) return 16;

  std::atomic<std::uint32_t> completionCallbacks{0U};
  runtime.setRenderReadyCallback([&] {
    completionCallbacks.fetch_add(1U, std::memory_order_relaxed);
  });
  runtime.requestRender(48000U);
  for (int attempt = 0; attempt < 200 &&
       completionCallbacks.load(std::memory_order_relaxed) == 0U; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  runtime.setRenderReadyCallback({});
  if (completionCallbacks.load(std::memory_order_relaxed) == 0U) return 17;

  seam::clap_editor::RealtimePreviewPublication publication;
  std::atomic<bool> publicationOk{true};
  std::jthread reader([&](std::stop_token token) {
    while (!token.stop_requested()) {
      auto handle = publication.acquire();
      if (!handle || handle->sampleRate < 8000U ||
          handle->sampleRate > 192000U) {
        publicationOk.store(false, std::memory_order_relaxed);
        return;
      }
      for (const auto value : handle->stereo) {
        if (!std::isfinite(value)) {
          publicationOk.store(false, std::memory_order_relaxed);
          return;
        }
      }
    }
  });
  for (std::uint64_t revision = 1U; revision <= 500U; ++revision) {
    seam::clap_editor::RenderedPreview value;
    value.sampleRate = 48000U;
    value.revision = revision;
    value.stereo.assign(256U, static_cast<float>(revision % 17U) / 17.0F);
    while (!publication.publish(std::move(value))) {
      std::this_thread::yield();
      value.sampleRate = 48000U;
      value.revision = revision;
      value.stereo.assign(256U, static_cast<float>(revision % 17U) / 17.0F);
    }
  }
  reader.request_stop();
  reader.join();
  if (!publicationOk.load(std::memory_order_relaxed)) return 18;

  std::cout << "Phase 11 tests PASS: notes=" << initial.noteCount()
            << " previewEnergy=" << energy
            << " liveEnergy=" << liveEnergy << '\n';
  return 0;
}
