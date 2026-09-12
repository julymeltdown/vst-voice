#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/platform/file_dialog.hpp"

#include <algorithm>
#include <exception>

namespace seam::native_ui {
namespace {
namespace production = voicebank_production;

core::Result<production::SampleManifestDraftIdentity> explicitDraftIdentity(
    const platform::SampleManifestDraftIdentityInput& input) {
  const auto text = [](std::string_view value, std::size_t maximum) {
    return !value.empty() && value.size() <= maximum && std::none_of(value.begin(), value.end(),
        [](unsigned char c) { return c < 32U || c == 127U; });
  };
  const auto identifier = [&](std::string_view value) {
    return text(value, 128U) && std::all_of(value.begin(), value.end(), [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    });
  };
  const auto language = input.language == "ja" ? domain::Language::Japanese : input.language == "en" ? domain::Language::English :
      input.language == "ko" ? domain::Language::Korean : domain::Language::Unspecified;
  if (!identifier(input.id) || !identifier(input.version) || !text(input.displayName, 256U) || !text(input.style, 128U) ||
      language == domain::Language::Unspecified)
    return core::failure<production::SampleManifestDraftIdentity>(core::ErrorCode::InvalidArgument,
        "Enter bank ID/version, display name, explicit ja/en/ko language and style; no identity field is inferred");
  return production::SampleManifestDraftIdentity{input.id, input.version, input.displayName, language, input.style};
}
} // namespace

core::Result<void> createStudioSampleManifestDraft(VoicebankStudioController& controller, platform::IFileDialog& dialog) {
  if (controller.proceduralImportBusy() || controller.dirty())
    return core::failure(core::ErrorCode::Conflict, "Finish production work and save existing manifest edits before creating another draft");
  const auto context = controller.captureSampleReviewContext(); if (!context) return core::Result<void>{context.error()};
  const auto entered = dialog.chooseSampleManifestDraftIdentity();
  if (!entered) return core::Result<void>{entered.error()};
  if (!entered.value()) return core::success();
  auto current = controller.validateSampleReviewContext(context.value()); if (!current) return current;
  if (controller.dirty() || controller.proceduralImportBusy())
    return core::failure(core::ErrorCode::Conflict, "Studio changed while entering draft identity");
  const auto identity = explicitDraftIdentity(*entered.value()); if (!identity) return core::Result<void>{identity.error()};
  const auto destination = dialog.choose({.purpose = platform::FileDialogPurpose::CreateSampleManifestDraft,
      .title = "Create New Editable Draft Directory — Estimates Only, Not Approved",
      .initialDirectory = controller.manifestPath().parent_path(), .suggestedName = identity.value().id + "-draft", .extensions = {}});
  if (!destination) return core::Result<void>{destination.error()};
  if (!destination.value()) return core::success();
  return controller.beginSampleManifestDraftCreation(context.value(), identity.value(), *destination.value());
}

core::Result<void> VoicebankStudioController::beginSampleManifestDraftCreation(const SampleReviewContext& context,
    production::SampleManifestDraftIdentity identity, std::filesystem::path destination, production::SampleManifestDraftOptions options) {
  if (proceduralImportBusy() || dirty_)
    return core::failure(core::ErrorCode::Conflict, "Finish work and save current manifest edits before draft creation");
  const auto current = validateSampleReviewContext(context); if (!current) return current;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [root = productionWorkspaceRoot_, project = *productionProject_, context, identity = std::move(identity),
         destination = std::move(destination), options = std::move(options), width = logicalWidth_, height = logicalHeight_, stop]()
            -> core::Result<SampleReviewWorkResult> {
      auto created = production::createSampleManifestDraft(root, project, identity, destination, options, stop);
      if (!created) return core::Result<SampleReviewWorkResult>{created.error()};
      // From this point onward the directory exists. Never turn a load failure,
      // late cancellation or stale owner-thread context into "nothing created".
      SampleReviewWorkResult result{.context = context, .createdDraft = std::move(created.value())};
      if (stop.stop_requested()) {
        result.draftLoadDiagnostic = "Draft committed; automatic opening cancelled. Open the retained manifest when ready.";
        return result;
      }
      try {
        VoicebankStudioController loader;
        auto loaded = loader.prepareSampleManifestLoad(result.createdDraft->root / "manifest.json",
            width, height, stop, &*result.createdDraft);
        if (!loaded) {
          result.draftLoadDiagnostic = "Draft committed but automatic opening failed: " + loaded.error().message;
          return result;
        }
        result.loadedUnit = std::move(loaded.value());
      } catch (const std::exception& error) {
        result.draftLoadDiagnostic = "Draft committed but automatic opening raised an error: " + std::string{error.what()};
      }
      return result;
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal, "Cannot start editable draft creation", error.what()); }
  sampleReviewStatus_ = status_ = "CREATING EDITABLE DRAFT / ALL MARKERS AND PITCH ESTIMATED / ESC CANCEL";
  return core::success();
}
} // namespace seam::native_ui
