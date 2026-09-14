#include "seam/application/project_factory.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/text/unicode.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <charconv>

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
    const bool custom = argc >= 3 && std::string_view(argv[2]) == "phrase";
    const bool stops = argc == 3 && std::string_view(argv[2]) == "stops";
    const bool affricates = argc == 3 && std::string_view(argv[2]) == "affricates";
    const bool glides = argc == 3 && std::string_view(argv[2]) == "glides";
    if (argc < 2 || (custom ? argc < 4 || argc > 67 : argc > 3 || (argc == 3 && !stops && !affricates && !glides && std::string_view(argv[2]) != "articulation" && std::string_view(argv[2]) != "boundaries" && std::string_view(argv[2]) != "nasals")))
      throw std::runtime_error("Usage: seam_singer_pilot NEW_OUTPUT_DIRECTORY [articulation|boundaries|nasals|stops|affricates|glides] OR NEW_OUTPUT_DIRECTORY phrase LYRIC:MIDI[:TICKS] ... (1-64 notes)");
    const bool articulation = argc == 3 && std::string_view(argv[2]) == "articulation";
    const bool boundaries = argc == 3 && std::string_view(argv[2]) == "boundaries";
    const bool nasals = argc == 3 && std::string_view(argv[2]) == "nasals";
    const auto root = std::filesystem::absolute(argv[1]);
    application::ProjectFactory factory{91000U};
    auto project = factory.createProject("SEAM pilot: vowel and fricative ladder (unqualified)");
    const auto trackId = factory.addVocalTrack(project, "Original procedural pilot");
    const std::string phrase = stops ? "pa ba ta da ka ga" : affricates ? "tsu chi ta sa" : glides ? "ra wa ya a" : custom ? "User-authored diagnostic phrase" : nasals ? "N a N i N u" : boundaries ? "a a a a then a melisma (same melody)" : articulation ? "ma mi mu me mo na ni nu ne no pa ta ka sa" : "a i u e o sa";
    std::vector<std::u32string> lyrics = nasals
        ? std::vector<std::u32string>{U"ん", U"あ", U"ん", U"い", U"ん", U"う"} : boundaries
        ? std::vector<std::u32string>{U"あ", U"あ", U"あ", U"あ", U"あ", U"ー", U"ー", U"ー"} : articulation
        ? std::vector<std::u32string>{U"ま", U"み", U"む", U"め", U"も", U"な", U"に", U"ぬ", U"ね", U"の", U"ぱ", U"た", U"か", U"さ"}
        : std::vector<std::u32string>{U"あ", U"い", U"う", U"え", U"お", U"さ"};
    std::vector<std::uint8_t> pitches = boundaries
        ? std::vector<std::uint8_t>{60, 64, 67, 64, 60, 64, 67, 64} : articulation
        ? std::vector<std::uint8_t>{60, 62, 64, 65, 67, 67, 65, 64, 62, 60, 60, 64, 67, 72}
        : std::vector<std::uint8_t>{60, 62, 64, 65, 67, 72};
    std::vector<std::int64_t> durations;
    if (custom) {
      lyrics.clear(); pitches.clear();
      for (int index = 3; index < argc; ++index) {
        const std::string_view token{argv[index]};
        const auto colon = token.find(':');
        if (token.size() > 256U || colon == std::string_view::npos || colon == 0U)
          throw std::runtime_error("Each phrase note must be bounded UTF-8 LYRIC:MIDI[:TICKS]");
        unsigned pitch = 0U;
        const auto durationColon=token.find(':',colon+1U);
        const auto number = token.substr(colon + 1U,durationColon==std::string_view::npos ? durationColon : durationColon-colon-1U);
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), pitch);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || pitch < 24U || pitch > 96U)
          throw std::runtime_error("Phrase MIDI pitch must be an integer from 24 through 96");
        auto lyric = text::decodeUtf8Strict(token.substr(0U, colon)); require(lyric);
        lyrics.push_back(std::move(lyric.value())); pitches.push_back(static_cast<std::uint8_t>(pitch));
        std::int64_t duration=480;
        if (durationColon!=std::string_view::npos) {
          const auto ticks=token.substr(durationColon+1U);
          const auto parsedTicks=std::from_chars(ticks.data(),ticks.data()+ticks.size(),duration);
          if (parsedTicks.ec!=std::errc{} || parsedTicks.ptr!=ticks.data()+ticks.size() || duration<1 || duration>3840)
            throw std::runtime_error("Phrase duration must be an integer from 1 through 3840 ticks");
        }
        durations.push_back(duration);
      }
    }
    if (stops) {
      lyrics={U"ぱ",U"ば",U"た",U"だ",U"か",U"が"};
      pitches={60,60,64,64,67,67};
    }
    if (affricates) {
      // A released closure that continues into frication, beside the stop and the fricative it
      // is made from: つ (ts), ち (ch), then た (t) and さ (s) as the comparison points.
      lyrics={U"つ",U"ち",U"た",U"さ"};
      pitches={60,62,64,65};
    }
    if (glides) {
      // A voiced liquid or glide whose defining gesture is the formant transition into its
      // vowel: ら (r), わ (w), や (y), then あ alone as the comparison point.
      lyrics={U"ら",U"わ",U"や",U"あ"};
      pitches={60,62,64,64};
    }
    if (!custom) durations.assign(lyrics.size(),480);
    std::int64_t totalTicks=0;
    for (const auto duration:durations) totalTicks+=duration;
    if (totalTicks>61440) throw std::runtime_error("Phrase total duration exceeds 61440 ticks (32 seconds at 120 BPM)");
    if (!std::filesystem::create_directory(root)) throw std::runtime_error("Output directory must be new");
    const auto regionId = factory.addRegion(project, trackId, phrase, time::Tick{0},
        time::Tick{totalTicks});
    auto* region = project.findRegion(regionId);
    std::int64_t startTick=0;
    for (std::size_t index = 0; index < lyrics.size(); ++index) {
      auto [lyric, note] = factory.makeNote(time::Tick{startTick}, time::Tick{durations[index]},
          pitches[index], lyrics[index], domain::Language::Japanese);
      region->lyrics.push_back(std::move(lyric)); region->notes.push_back(std::move(note));
      startTick+=durations[index];
    }
    voice_design::VoiceRecipe base;
    base.id = "seam-pilot-01-diagnostic"; base.seed = 91000U;
    base.poses = {
        {"a", "neutral", 0.0, {{800, 90, 0}, {1250, 110, -3}, {2800, 160, -6}}},
        {"i", "neutral", 0.0, {{300, 70, 0}, {2300, 120, -3}, {3200, 170, -6}}},
        {"u", "neutral", 0.0, {{350, 80, 0}, {1100, 100, -3}, {2500, 160, -6}}},
        {"e", "neutral", 0.0, {{500, 80, 0}, {1900, 110, -3}, {2900, 160, -6}}},
        {"o", "neutral", 0.0, {{500, 90, 0}, {900, 110, -3}, {2600, 160, -6}}}};
    // A tuned alveolar sibilant, then the classes the scale needs beside it: a wider lower
    // sibilant (sh), and the two weak broad-band non-sibilants (h, f). These are declared
    // source parameters and spectra, not phonetic qualification.
    base.frications = {
        {"s", "neutral", {.seed = 91000U, .centerHz = 5500, .bandwidthHz = 3000, .gain = 0.12}},
        {"sh", "neutral", {.seed = 91010U, .centerHz = 3500, .bandwidthHz = 3000, .gain = 0.12}},
        {"h", "neutral", {.seed = 91011U, .centerHz = 1200, .bandwidthHz = 2400, .gain = 0.06}},
        {"f", "neutral", {.seed = 91012U, .centerHz = 2000, .bandwidthHz = 3000, .gain = 0.08}}};
    // A voiced fricative is noise plus voicing, and the voicing it adds is shaped by the same
    // tract, so it needs its own same-phone resonance pose before the binding can be declared.
    base.poses.push_back({"z", "neutral", 0.0, {{300, 80, 0}, {1700, 110, -3}, {2800, 160, -6}}});
    base.poses.push_back({"v", "neutral", 0.0, {{350, 80, 0}, {1100, 100, -3}, {2500, 160, -6}}});
    base.frications.push_back({"z", "neutral", {.seed = 91013U, .centerHz = 5000, .bandwidthHz = 2500, .gain = 0.10}, 0.35});
    base.frications.push_back({"v", "neutral", {.seed = 91014U, .centerHz = 2000, .bandwidthHz = 3000, .gain = 0.08}, 0.35});
    if (nasals || custom) {
      base.id = "seam-pilot-01-syllabic-nasal-diagnostic";
      base.poses.push_back({"N", "neutral", 1.0, {{300, 80, 0}, {1400, 110, -6}, {2600, 160, -9}}, voice_design::NasalResonance{280, 80, 1200, 120}});
    }
    if (articulation || custom || stops || affricates) {
      base.id = "seam-pilot-01-articulation-diagnostic";
      base.poses.push_back({"m", "neutral", 0.85, {{300, 80, 0}, {1100, 110, -6}, {2500, 160, -9}}, voice_design::NasalResonance{}});
      base.poses.push_back({"n", "neutral", 0.75, {{300, 80, 0}, {1700, 110, -6}, {2800, 160, -9}}, voice_design::NasalResonance{300, 90, 1500, 120}});
      base.plosives = {
          {"p", "neutral", {.seed = 91001U, .centerHz = 1200, .bandwidthHz = 1800, .gain = 0.12}, 10},
          {"t", "neutral", {.seed = 91002U, .centerHz = 4500, .bandwidthHz = 3000, .gain = 0.12}, 10},
          {"k", "neutral", {.seed = 91003U, .centerHz = 2500, .bandwidthHz = 2200, .gain = 0.12}, 10}};
    }
    if (stops || custom) {
      base.id="seam-pilot-01-voiced-stop-diagnostic";
      for (std::size_t index=0;index<3U;++index) {
        auto voiced=base.plosives[index];
        voiced.phone=std::array{"b","d","g"}[index];
        voiced.voicedClosure=voice_design::VoiceRecipe::VoicedClosure{0.2,400.0};
        base.plosives.push_back(std::move(voiced));
      }
    }
    if (affricates || custom) {
      // Experimental burst and tail spectra, not phonetic qualification: the release reuses the
      // paired stop's spectral centre and the tail reuses the paired fricative's. Voiced
      // affricates (じ/ぢ) remain unsupported and are refused rather than approximated.
      base.affricates = {
          {"ts", "neutral", {.seed=91004U,.centerHz=4500,.bandwidthHz=3000,.gain=0.12},
              {.seed=91005U,.centerHz=5500,.bandwidthHz=3000,.gain=0.12}, 10.0},
          {"ch", "neutral", {.seed=91006U,.centerHz=3000,.bandwidthHz=2500,.gain=0.12},
              {.seed=91007U,.centerHz=4500,.bandwidthHz=3500,.gain=0.12}, 12.0}};
    }
    if (affricates) base.id="seam-pilot-01-affricate-diagnostic";
    if (glides || custom) {
      // Experimental resonance banks and transition lengths, not phonetic qualification. Each
      // approximant needs its own same-phone resonance pose, which is what the transition moves
      // into; a static vowel remains the comparison point in the fixture.
      base.poses.push_back({"r", "neutral", 0.0, {{400, 80, 0}, {1400, 110, -3}, {2200, 160, -6}}});
      base.poses.push_back({"w", "neutral", 0.0, {{300, 80, 0}, {610, 100, -3}, {2200, 160, -6}}});
      base.poses.push_back({"y", "neutral", 0.0, {{250, 70, 0}, {2200, 120, -3}, {3000, 170, -6}}});
      base.approximants = {{"r", "neutral", 45.0}, {"w", "neutral", 60.0}, {"y", "neutral", 40.0}};
    }
    if (glides) base.id="seam-pilot-01-approximant-diagnostic";
    if (custom) {
      // The maximal diagnostic recipe also declares the palatalized consonants: each one takes
      // its release from the consonant already bound above and carries its own palatal resonance,
      // which is the shape its release moves through into the vowel. Experimental resonance and
      // transition data, not phonetic qualification.
      base.poses.push_back({"ky", "neutral", 0.0, {{250, 70, 0}, {2200, 120, -3}, {3000, 170, -6}}});
      base.poses.push_back({"gy", "neutral", 0.0, {{250, 70, 0}, {2100, 120, -3}, {2900, 170, -6}}});
      base.poses.push_back({"py", "neutral", 0.0, {{250, 70, 0}, {2200, 120, -3}, {3000, 170, -6}}});
      base.poses.push_back({"by", "neutral", 0.0, {{250, 70, 0}, {2100, 120, -3}, {2900, 170, -6}}});
      base.poses.push_back({"hy", "neutral", 0.0, {{300, 80, 0}, {2300, 120, -3}, {3100, 170, -6}}});
      base.poses.push_back({"fy", "neutral", 0.0, {{300, 80, 0}, {1900, 120, -3}, {2700, 170, -6}}});
      base.poses.push_back({"vy", "neutral", 0.0, {{300, 80, 0}, {1700, 120, -3}, {2600, 170, -6}}});
      base.poses.push_back({"ry", "neutral", 0.0, {{350, 80, 0}, {1900, 110, -3}, {2400, 160, -6}}});
      base.poses.push_back({"my", "neutral", 0.85, {{300, 80, 0}, {1900, 110, -6}, {2800, 160, -9}}, voice_design::NasalResonance{280, 80, 1200, 120}});
      base.poses.push_back({"ny", "neutral", 0.75, {{300, 80, 0}, {2200, 110, -6}, {3000, 160, -9}}, voice_design::NasalResonance{300, 90, 1700, 120}});
      base.palatalized = {{"ky", "neutral", "k"}, {"gy", "neutral", "g"},
          {"py", "neutral", "p"}, {"by", "neutral", "b"}, {"hy", "neutral", "h"},
          {"fy", "neutral", "f"}, {"vy", "neutral", "v"}, {"ry", "neutral", "r"},
          {"my", "neutral", "m"}, {"ny", "neutral", "n"}};
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
