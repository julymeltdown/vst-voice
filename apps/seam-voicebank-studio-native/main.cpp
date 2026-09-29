#include "options.hpp"
#include "studio_app.hpp"

#include "seam/native_ui/native_window.hpp"

#include <clocale>
#include <iostream>
#include <string_view>

using seam::voicebank_studio_native::parseOptions;
using seam::voicebank_studio_native::printUsage;

int main(int argc, char** argv) {
  static_cast<void>(std::setlocale(LC_ALL, ""));
  const auto options = parseOptions(argc, argv);
  if (!options.has_value()) {
    if (argc > 1 && std::string_view{argv[1]} == "--help") return 0;
    printUsage();
    return 2;
  }

  const auto application =
      seam::voicebank_studio_native::createVoicebankStudioApp(options->forceSyntheticInput);
  auto& app = *application;
  const auto openedBank = app.open(*options);
  if (!openedBank) {
    std::cerr << "Voicebank Studio load failed: " << openedBank.error().message;
    if (!openedBank.error().context.empty()) std::cerr << " (" << openedBank.error().context << ')';
    std::cerr << '\n';
    return 3;
  }
  if (options->productionUnitIndex.has_value()) {
    const auto selected = app.selectProductionUnit(*options->productionUnitIndex);
    if (!selected) {
      std::cerr << "Voicebank production selection failed: "
                << selected.error().message << '\n';
      return 7;
    }
  }
  if (options->importTake.has_value()) {
    const auto imported = app.importProductionTake(*options->importTake);
    if (!imported) {
      std::cerr << "Voicebank production import failed: "
                << imported.error().message << '\n';
      return 7;
    }
    std::cout << "production_imported_take="
              << options->importTake->string() << '\n';
  }
  if (options->operationKind.has_value()) {
    const auto processed = app.applyProductionOperation(
        {.kind = *options->operationKind,
         .channelIndex = options->channelIndex,
         .targetSampleRate = options->targetSampleRate,
         .targetPeak = options->targetPeak,
         .startFrame = options->startFrame,
         .endFrame = options->endFrame});
    if (!processed) {
      std::cerr << "Voicebank production operation failed: "
                << processed.error().message << '\n';
      return 7;
    }
    std::cout << "production_revision=" << processed.value().revisionId << '\n'
              << "production_output_sha256="
              << processed.value().outputSha256 << '\n';
  }
  if (options->exportU57Inputs.has_value()) {
    const auto exported = app.exportProductionInputs(*options->exportU57Inputs);
    if (!exported) {
      std::cerr << "Voicebank production export failed: "
                << exported.error().message << '\n';
      return 6;
    }
    std::cout << "production_brief=" << exported.value().briefPath.string() << '\n'
              << "candidate_template="
              << exported.value().candidateTemplatePath.string() << '\n';
  }
  auto window = seam::native_ui::createNativeWindow();
  app.setWindow(*window);
  const auto opened = window->open(seam::native_ui::NativeWindowConfig{
      .title = "Project SEAM / Voicebank Studio",
      .width = options->windowWidth,
      .height = options->windowHeight,
      .scale = 1.0,
      .minimumWidth = 720U,
      .minimumHeight = 520U,
      .autoCloseAfter = options->recordDuration.count() > 0
                            ? options->recordDuration
                            : options->autoClose,
      .screenshotPath = options->screenshot,
      .restoreSavedFrame = !options->windowSizeSpecified,
  }, app);
  if (!opened) {
    std::cerr << "Native Voicebank Studio unavailable: " << opened.error().message << '\n';
    return 4;
  }
  if (options->recordDuration.count() > 0) {
    const auto started = app.startRecording();
    if (!started) {
      std::cerr << "Voicebank Studio recording failed: "
                << started.error().message << '\n';
      return 5;
    }
  }
  const auto result = window->run();
  const auto stopped = app.stopRecording();
  app.finishPendingImport();
  const auto info = app.inputInfo();
  const auto stats = app.inputStats();
  std::cout << "input_backend=" << info.backend << '\n'
            << "input_physical=" << (info.physical ? "true" : "false") << '\n'
            << "input_callbacks=" << stats.callbacks << '\n'
            << "input_frames=" << stats.frames << '\n'
            << "input_read_failures=" << stats.readFailures << '\n'
            << "recorded_frames=" << app.lastRecordedFrames() << '\n'
            << "recorded_wav=" << app.lastRecording().string() << '\n';
  if (const auto* production = app.productionProject(); production != nullptr) {
    const auto queues = app.productionQueues();
    std::cout << "production_project=" << production->projectId << '\n'
              << "production_generation=" << production->lastDurableGeneration << '\n'
              << "production_inventory_sha256=" << production->inventorySha256 << '\n'
              << "production_missing=" << queues.missing << '\n'
              << "production_rejected=" << queues.rejected << '\n'
              << "production_retake=" << queues.retake << '\n'
              << "production_marker_review=" << queues.markerReview << '\n'
              << "production_pitch_review=" << queues.pitchReview << '\n'
              << "production_approved=" << queues.approved << '\n'
              << "production_staged_recovery="
              << app.stagedRecoveryCandidateCount() << '\n';
  }
  if (!stopped) {
    std::cerr << "Voicebank Studio recording failed: "
              << stopped.error().message << '\n';
    return 5;
  }
  if (!app.lastError().empty()) {
    std::cerr << "last_studio_error=" << app.lastError() << '\n';
  }
  return result;
}
