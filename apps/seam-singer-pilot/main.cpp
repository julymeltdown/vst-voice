#include "seam/application/project_factory.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank/pitch.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
template<class T> void require(const seam::core::Result<T>& value) {
  if (!value) throw std::runtime_error(value.error().message);
}
}

// Reproducible listening material through the production export path. No source
// registration, reviewer decision, trained model, or release approval is created.
int main(int argc, char** argv) {
  using namespace seam;
  try {
    if (argc < 2 || argc > 3 || (argc == 3 && std::string_view(argv[2]) != "articulation" && std::string_view(argv[2]) != "boundaries"))
      throw std::runtime_error("Usage: seam_singer_pilot NEW_OUTPUT_DIRECTORY [articulation|boundaries]");
    const bool articulation = argc == 3 && std::string_view(argv[2]) == "articulation";
    const bool boundaries = argc == 3 && std::string_view(argv[2]) == "boundaries";
    const auto root = std::filesystem::absolute(argv[1]);
    if (!std::filesystem::create_directory(root)) throw std::runtime_error("Output directory must be new");
    application::ProjectFactory factory{91000U};
    auto project = factory.createProject("SEAM pilot: vowel and fricative ladder (unqualified)");
    const auto trackId = factory.addVocalTrack(project, "Original procedural pilot");
    const std::string phrase = boundaries ? "a a a a then a melisma (same melody)" : articulation ? "ma mi mu me mo na ni nu ne no pa ta ka sa" : "a i u e o sa";
    const std::vector<std::u32string> lyrics = boundaries
        ? std::vector<std::u32string>{U"あ", U"あ", U"あ", U"あ", U"あ", U"ー", U"ー", U"ー"} : articulation
        ? std::vector<std::u32string>{U"ま", U"み", U"む", U"め", U"も", U"な", U"に", U"ぬ", U"ね", U"の", U"ぱ", U"た", U"か", U"さ"}
        : std::vector<std::u32string>{U"あ", U"い", U"う", U"え", U"お", U"さ"};
    const std::vector<std::uint8_t> pitches = boundaries
        ? std::vector<std::uint8_t>{60, 64, 67, 64, 60, 64, 67, 64} : articulation
        ? std::vector<std::uint8_t>{60, 62, 64, 65, 67, 67, 65, 64, 62, 60, 60, 64, 67, 72}
        : std::vector<std::uint8_t>{60, 62, 64, 65, 67, 72};
    const auto regionId = factory.addRegion(project, trackId, phrase, time::Tick{0},
        time::Tick{static_cast<std::int64_t>(lyrics.size()) * 480});
    auto* region = project.findRegion(regionId);
    for (std::size_t index = 0; index < lyrics.size(); ++index) {
      auto [lyric, note] = factory.makeNote(time::Tick{static_cast<std::int64_t>(index) * 480}, time::Tick{480},
          pitches[index], lyrics[index], domain::Language::Japanese);
      region->lyrics.push_back(std::move(lyric)); region->notes.push_back(std::move(note));
    }
    voice_design::VoiceRecipe base;
    base.id = "seam-pilot-01-diagnostic"; base.seed = 91000U;
    base.poses = {
        {"a", "neutral", 0.0, {{800, 90, 0}, {1250, 110, -3}, {2800, 160, -6}}},
        {"i", "neutral", 0.0, {{300, 70, 0}, {2300, 120, -3}, {3200, 170, -6}}},
        {"u", "neutral", 0.0, {{350, 80, 0}, {1100, 100, -3}, {2500, 160, -6}}},
        {"e", "neutral", 0.0, {{500, 80, 0}, {1900, 110, -3}, {2900, 160, -6}}},
        {"o", "neutral", 0.0, {{500, 90, 0}, {900, 110, -3}, {2600, 160, -6}}}};
    base.frications = {{"s", "neutral", {.seed = 91000U, .centerHz = 5500, .bandwidthHz = 3000, .gain = 0.12}}};
    if (articulation) {
      base.id = "seam-pilot-01-articulation-diagnostic";
      base.poses.push_back({"m", "neutral", 0.85, {{300, 80, 0}, {1100, 110, -6}, {2500, 160, -9}}, voice_design::NasalResonance{}});
      base.poses.push_back({"n", "neutral", 0.75, {{300, 80, 0}, {1700, 110, -6}, {2800, 160, -9}}, voice_design::NasalResonance{300, 90, 1500, 120}});
      base.plosives = {
          {"p", "neutral", {.seed = 91001U, .centerHz = 1200, .bandwidthHz = 1800, .gain = 0.12}, 10},
          {"t", "neutral", {.seed = 91002U, .centerHz = 4500, .bandwidthHz = 3000, .gain = 0.12}, 10},
          {"k", "neutral", {.seed = 91003U, .centerHz = 2500, .bandwidthHz = 2200, .gain = 0.12}, 10}};
    }
    formats::JsonValue::Array runs;
    for (const std::string name : {"baseline", "higher-formants", "breathier"}) {
      auto recipe = base;
      if (name == "higher-formants") for (auto& pose : recipe.poses) for (auto& band : pose.formants) band.frequencyHz *= 1.15;
      if (name == "breathier") { recipe.phonation.aspiration = 0.20; recipe.phonation.spectralTiltDbPerOctave = -15.0; }
      const auto resource = voice_design::freezeVoiceRecipeResource(recipe); require(resource);
      const auto recipeFile = root / (name + "-recipe.json");
      require(voice_design::saveVoiceRecipeFile(recipeFile, recipe));
      project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
          resource.value().identity, recipeFile.filename().string(), "neutral"};
      require(formats::ProjectJsonCodec{}.save(project, root / (name + ".seam")));
      const std::vector<rendering::TrackSingerSource> sources{rendering::TrackProceduralSource{trackId, resource.value(), "neutral"}};
      authoring::ExportSettings settings; settings.format = voicebank::WavSampleFormat::Float32;
      settings.includeProceduralCandidates = true;
      const auto exported = authoring::ExportService{}.exportSetWithSources(project, sources, trackId, regionId, 1U, root / name, settings);
      require(exported);
      if (exported.value().state != authoring::ExportState::Committed) throw std::runtime_error("Pilot export was not committed");
      for (const auto& file : exported.value().files) {
        if (file.path.extension() != ".wav") continue;
        const auto wav = voicebank::readWav(file.path); require(wav);
        double peak = 0.0, energy = 0.0;
        for (float sample : wav.value().interleaved) {
          if (!std::isfinite(sample)) throw std::runtime_error("Pilot contains nonfinite samples");
          peak = std::max(peak, std::abs(static_cast<double>(sample))); energy += static_cast<double>(sample) * sample;
        }
        if (energy == 0.0 || wav.value().interleaved.empty()) throw std::runtime_error("Pilot is silent");
        const auto hash = core::sha256File(file.path); require(hash);
        // Analyze the dry mono candidate, avoiding master routing and tails.
        // Broad-range estimation is deliberately not constrained to score F0:
        // octave errors and missing voicing must remain observable.
        if (file.path.parent_path().filename() == "candidates") {
          const auto mono = wav.value().monoMix();
          voicebank::PitchConfig config;
          config.correlationMethod = voicebank::PitchCorrelationMethod::Fft;
          const auto measured = voicebank::analyzePitch(mono, wav.value().sampleRate, config); require(measured);
          formats::JsonValue::Array notes;
          for (std::size_t index = 0; index < pitches.size(); ++index) {
            // Fixed central half of each score note; no data-dependent exclusions.
            const auto begin = static_cast<std::size_t>(std::llround(project.tempoMap().secondsAt(
                time::Tick{static_cast<std::int64_t>(index) * 480 + 120}) * wav.value().sampleRate));
            const auto end = static_cast<std::size_t>(std::llround(project.tempoMap().secondsAt(
                time::Tick{static_cast<std::int64_t>(index) * 480 + 360}) * wav.value().sampleRate));
            const auto expected = 440.0 * std::exp2((static_cast<double>(pitches[index]) - 69.0) / 12.0);
            std::size_t total = 0U, voiced = 0U, within = 0U, octaves = 0U;
            std::vector<double> errors;
            for (const auto& frame : measured.value()) {
              if (frame.sourceFrame < begin || frame.sourceFrame + config.frameSize > end) continue;
              ++total;
              if (!frame.voiced || frame.f0Hz <= 0.0) continue;
              ++voiced;
              const auto cents = std::abs(1200.0 * std::log2(frame.f0Hz / expected));
              errors.push_back(cents);
              if (cents <= 50.0) ++within;
              if (cents >= 1150.0) ++octaves;
            }
            std::sort(errors.begin(), errors.end());
            formats::JsonValue median;
            if (!errors.empty()) median = (errors[(errors.size() - 1U) / 2U] + errors[errors.size() / 2U]) / 2.0;
            notes.emplace_back(formats::JsonValue::Object{
                {"noteIndex", static_cast<std::int64_t>(index)}, {"expectedHz", expected},
                {"analysisFrames", static_cast<std::int64_t>(total)}, {"voicedFrames", static_cast<std::int64_t>(voiced)},
                {"within50CentsFrames", static_cast<std::int64_t>(within)}, {"largePitchErrorFrames", static_cast<std::int64_t>(octaves)},
                {"medianAbsoluteCents", median}});
          }
          require(core::durableAtomicWriteTextNew(root / (name + "-pitch.json"), formats::stringifyJson(formats::JsonValue::Object{
              {"status", "DIAGNOSTIC_NOT_QUALIFICATION"}, {"audioSha256", hash.value()},
              {"windowPolicy", "central-half-full-analysis-windows-no-data-dependent-exclusions"},
              {"notes", std::move(notes)}}, true)));
        }
        runs.emplace_back(formats::JsonValue::Object{{"variant", name}, {"wav", file.path.string()}, {"sha256", hash.value()},
            {"recipeHash", resource.value().identity.contentHash}, {"peak", peak},
            {"rms", std::sqrt(energy / static_cast<double>(wav.value().interleaved.size()))}});
        std::cout << file.path << '\n';
      }
    }
    require(core::durableAtomicWriteTextNew(root / "pilot.json", formats::stringifyJson(formats::JsonValue::Object{
        {"status", "UNQUALIFIED_LISTENING_PILOT"}, {"releaseEligible", false}, {"phrase", phrase},
        {"runs", std::move(runs)}}, true)));
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
