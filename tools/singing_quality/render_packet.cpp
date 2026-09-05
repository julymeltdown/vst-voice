#include "render_driver.hpp"

#include "seam/build/version.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace seam::singing_quality {
namespace {

using Json = formats::JsonValue;
using Object = Json::Object;
using Array = Json::Array;

Json phraseDiagnostics(const rendering::RenderSnapshot& snapshot,
                       const rendering::PhrasePipelineResult& result) {
  Array resources;
  for (const auto& unit : snapshot.selectedUnits) {
    resources.emplace_back(Object{{"unit_id", unit.unitId}, {"audio_sha256", unit.audioSha256}});
  }
  Array timing;
  for (const auto& placement : result.timing.placements) {
    timing.emplace_back(Object{
        {"unit_id", placement.unitId}, {"note_id", placement.startKey.noteId.toString()},
        {"target_midi", static_cast<std::int64_t>(placement.targetMidi)},
        {"source_start_tick", placement.sourceStartTick.value()},
        {"source_end_tick", placement.sourceEndTick.value()},
        {"note_on_frame", placement.noteOn},
        {"destination_start_frame", placement.destinationStart},
        {"destination_end_frame", placement.destinationEnd},
        {"desired_vowel_onset_frame", placement.desiredVowelOnset}});
  }
  Array placements;
  for (const auto& placement : result.rendered.placements) {
    placements.emplace_back(Object{
        {"unit_id", placement.unitId},
        {"requested_start_frame", placement.requestedStart},
        {"aligned_start_frame", placement.alignedStart},
        {"frame_count", placement.frameCount}, {"vowel_onset_frame", placement.vowelOnset},
        {"requested_renderer", std::string(voicebank::rendererHintName(placement.requestedRenderer))},
        {"actual_renderer", std::string(voicebank::rendererHintName(placement.actualRenderer))},
        {"used_fallback", placement.usedFallback},
        {"forced_selection", placement.forcedSelection},
        {"diagnostic", placement.diagnostic}});
  }
  Array issues;
  for (const auto& issue : result.timing.issues) {
    issues.emplace_back(Object{{"code", static_cast<std::int64_t>(issue.code)},
                               {"unit_id", issue.unitId}, {"message", issue.message}});
  }
  return Object{{"phrase_id", snapshot.segment.id}, {"snapshot_sha256", snapshot.contentHash},
                {"render_abi", snapshot.renderAbiId},
                {"start_tick", snapshot.segment.startTick.value()},
                {"end_tick", snapshot.segment.endTick.value()},
                {"audio_start_frame", result.rendered.audio.startFrame},
                {"resources", std::move(resources)}, {"target_timing", std::move(timing)},
                {"rendered_placements", std::move(placements)}, {"timing_issues", std::move(issues)}};
}

core::Result<void> mix(std::vector<float>& output, const synthesis::PhraseAudio& phrase) {
  const auto count = static_cast<time::SampleFrame>(phrase.samples.size());
  if (count == 0 || count > kMaximumFrames || phrase.startFrame < -kMaximumFrames ||
      phrase.startFrame > kMaximumFrames || count > kMaximumFrames - phrase.startFrame) {
    return core::failure(core::ErrorCode::Unsupported, "Phrase audio exceeds diagnostic bounds");
  }
  const auto end = std::max(time::SampleFrame{0}, phrase.startFrame + count);
  if (static_cast<std::size_t>(end) > output.size()) output.resize(static_cast<std::size_t>(end), 0.0F);
  for (time::SampleFrame source = std::max(time::SampleFrame{0}, -phrase.startFrame);
       source < count; ++source) {
    output[static_cast<std::size_t>(phrase.startFrame + source)] +=
        phrase.samples[static_cast<std::size_t>(source)];
  }
  return core::success();
}

Json buildIdentity() {
  return Object{
      {"application_version", std::string(build::kApplicationVersion)},
      {"build_id", std::string(build::kBuildId)},
      {"compiled_source_commit", std::string(build::kSourceCommit)},
      {"build_epoch", static_cast<std::int64_t>(build::kBuildEpoch)},
      {"render_abi", std::string(build::kRenderAbiId)},
      {"compiler_id", SEAM_SINGING_COMPILER_ID},
      {"compiler_version", SEAM_SINGING_COMPILER_VERSION},
      {"configuration", SEAM_SINGING_CONFIGURATION}};
}

}

core::Result<void> renderPacket(const PreparedRender& prepared, const Invocation& invocation) {
  std::error_code error;
  if (!std::filesystem::create_directory(invocation.output, error) || error) {
    return core::failure(core::ErrorCode::Conflict,
        "Output directory must be new and its parent must exist", invocation.output.string());
  }
  std::vector<float> audio(static_cast<std::size_t>(prepared.expectedFrames), 0.0F);
  Array phrases;
  const auto start = std::chrono::steady_clock::now();
  for (const auto& snapshot : prepared.snapshots) {
    const auto result = rendering::PhraseRenderPipeline{}.render(snapshot);
    if (!result) return core::Result<void>{result.error()};
    const auto mixed = mix(audio, result.value().rendered.audio);
    if (!mixed) return mixed;
    phrases.push_back(phraseDiagnostics(snapshot, result.value()));
  }
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (std::any_of(audio.begin(), audio.end(), [](float value) { return !std::isfinite(value); })) {
    return core::failure(core::ErrorCode::InvariantViolation, "Renderer produced nonfinite audio");
  }
  const auto statistics = voicebank::analyzeAudio(audio);
  const auto written = voicebank::writeWav(invocation.output / "dry.wav",
      voicebank::WavOutputFormat{kSampleRate, 1U, voicebank::WavSampleFormat::Float32}, audio);
  if (!written) return written;
  const auto saved = formats::ProjectJsonCodec{}.save(prepared.project,
                                                      invocation.output / "saved-project.seam");
  if (!saved) return saved;
  const Json report{Object{
      {"schema_version", std::int64_t{1}}, {"evidence_class", "auditory-diagnostic"},
      {"project_sha256", prepared.projectSha256}, {"manifest_sha256", prepared.manifestSha256},
      {"build", buildIdentity()}, {"sample_rate", static_cast<std::int64_t>(kSampleRate)},
      {"renderer_policy", invocation.policy == synthesis::RenderPolicy::ForceRaw ? "raw" : "bank"},
      {"elapsed_render_seconds", elapsed}, {"sample_format", "float32-mono"},
      {"frames", static_cast<std::int64_t>(audio.size())},
      {"duration_seconds", static_cast<double>(audio.size()) / kSampleRate},
      {"measured_audio", Object{{"peak", statistics.peak}, {"rms", statistics.rms},
          {"dc_offset", statistics.dcOffset},
          {"clipped_samples", static_cast<std::int64_t>(statistics.clippedSamples)}}},
      {"phrases", std::move(phrases)}}};
  return core::durableAtomicWriteTextNew(invocation.output / "diagnostics.json",
                                         formats::stringifyJson(report));
}

}
