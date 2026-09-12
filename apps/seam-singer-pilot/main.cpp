#include "seam/application/project_factory.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank/wav.hpp"

#include <array>
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
    if (argc != 2) throw std::runtime_error("Usage: seam_singer_pilot NEW_OUTPUT_DIRECTORY");
    const auto root = std::filesystem::absolute(argv[1]);
    if (!std::filesystem::create_directory(root)) throw std::runtime_error("Output directory must be new");
    application::ProjectFactory factory{91000U};
    auto project = factory.createProject("SEAM pilot: vowel and fricative ladder (unqualified)");
    const auto trackId = factory.addVocalTrack(project, "Original procedural pilot");
    const auto regionId = factory.addRegion(project, trackId, "a i u e o sa", time::Tick{0}, time::Tick{2880});
    auto* region = project.findRegion(regionId);
    const std::array<std::u32string, 6> lyrics{U"あ", U"い", U"う", U"え", U"お", U"さ"};
    const std::array<std::uint8_t, 6> pitches{60U, 62U, 64U, 65U, 67U, 72U};
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
        runs.emplace_back(formats::JsonValue::Object{{"variant", name}, {"wav", file.path.string()}, {"sha256", hash.value()},
            {"recipeHash", resource.value().identity.contentHash}, {"peak", peak},
            {"rms", std::sqrt(energy / static_cast<double>(wav.value().interleaved.size()))}});
        std::cout << file.path << '\n';
      }
    }
    require(core::durableAtomicWriteTextNew(root / "pilot.json", formats::stringifyJson(formats::JsonValue::Object{
        {"status", "UNQUALIFIED_LISTENING_PILOT"}, {"releaseEligible", false}, {"phrase", "a i u e o sa"},
        {"runs", std::move(runs)}}, true)));
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
