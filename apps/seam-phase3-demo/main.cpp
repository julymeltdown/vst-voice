#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/logger.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/phrase_segmenter.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/render_scheduler.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/rendering/stale_audio_store.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/spectrogram.hpp"
#include "seam/voicebank/validator.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank/waveform.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t kSampleRate = 48000;
constexpr double kBpm = 154.0;

void printError(const seam::core::Error& error) {
  std::cerr << "ERROR: " << error.message;
  if (!error.context.empty()) std::cerr << " (" << error.context << ')';
  std::cerr << '\n';
}

double midiToHz(std::int32_t midi) {
  return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
}

std::vector<float> makeSyntheticVoiceUnit(std::int32_t rootMidi,
                                          double consonantNoise,
                                          double phaseOffset) {
  constexpr std::size_t frames = 30000;
  std::vector<float> samples(frames, 0.0F);
  const auto fundamental = midiToHz(rootMidi);
  std::uint32_t noiseState = static_cast<std::uint32_t>(
      0x73b49d1fU + static_cast<std::uint32_t>(rootMidi * 131));
  for (std::size_t index = 0; index < samples.size(); ++index) {
    noiseState = noiseState * 1664525U + 1013904223U;
    const auto noise = static_cast<double>((noiseState >> 8U) & 0xffffU) /
                           32767.5 -
                       1.0;
    const auto seconds = static_cast<double>(index) /
                         static_cast<double>(kSampleRate);
    const auto attack = std::min(1.0, static_cast<double>(index) / 256.0);
    const auto remaining = samples.size() - index - 1U;
    const auto release = std::min(1.0, static_cast<double>(remaining) / 900.0);
    const auto envelope = std::min(attack, release);
    const auto voiceOnset = std::clamp(
        (static_cast<double>(index) - 2600.0) / 2200.0, 0.0, 1.0);
    const auto consonantEnvelope = std::clamp(
        1.0 - static_cast<double>(index) / 4600.0, 0.0, 1.0);
    const auto phase = 2.0 * std::numbers::pi * fundamental * seconds + phaseOffset;
    const auto harmonic = std::sin(phase) + 0.31 * std::sin(phase * 2.0 + 0.19) +
                          0.12 * std::sin(phase * 3.0 + 0.43);
    const auto breath = noise * (0.018 + 0.012 * std::sin(phase * 0.5));
    const auto value = envelope *
        (voiceOnset * (0.25 * harmonic + breath) +
         consonantEnvelope * consonantNoise * noise);
    samples[index] = static_cast<float>(std::clamp(value, -0.8, 0.8));
  }
  return samples;
}

std::vector<seam::voicebank::PitchMark> makePitchMarks(
    std::int32_t rootMidi,
    seam::time::SampleFrame start,
    seam::time::SampleFrame end) {
  const auto period = static_cast<seam::time::SampleFrame>(std::llround(
      static_cast<double>(kSampleRate) / midiToHz(rootMidi)));
  std::vector<seam::voicebank::PitchMark> marks;
  for (auto frame = start; frame < end; frame += std::max<seam::time::SampleFrame>(2, period)) {
    marks.push_back(seam::voicebank::PitchMark{
        .frame = frame,
        .confidence = 0.98F,
        .locked = false,
    });
  }
  return marks;
}

seam::voicebank::Unit makeUnit(std::string id,
                               std::string alias,
                               std::vector<std::string> phones,
                               seam::voicebank::UnitKind kind,
                               std::filesystem::path path,
                               std::int32_t rootMidi,
                               seam::voicebank::RendererHint renderer,
                               std::int32_t take = 1,
                               std::int32_t priority = 0) {
  constexpr seam::time::SampleFrame stableStart = 5600;
  constexpr seam::time::SampleFrame releaseStart = 24500;
  return seam::voicebank::Unit{
      .id = std::move(id),
      .alias = std::move(alias),
      .phones = std::move(phones),
      .kind = kind,
      .audioPath = std::move(path),
      .rootMidi = rootMidi,
      .style = "original",
      .take = take,
      .priority = priority,
      .gainDb = -1.5F,
      .renderer = renderer,
      .markers = seam::voicebank::UnitMarkers{
          .audioOffset = 0,
          .consonantEnd = 2800,
          .vowelOnset = 3800,
          .stableStart = stableStart,
          .loopStart = 7600,
          .loopEnd = 21800,
          .releaseStart = releaseStart,
          .audioEnd = 30000,
      },
      .pitchMarks = makePitchMarks(rootMidi, stableStart, releaseStart),
      .enabled = true,
  };
}

seam::core::Result<seam::voicebank::Manifest> createDemoBank(
    const std::filesystem::path& root) {
  std::error_code error;
  std::filesystem::create_directories(root / "audio", error);
  if (error) {
    return seam::core::failure<seam::voicebank::Manifest>(
        seam::core::ErrorCode::IoError,
        "Unable to create Phase 3 synthetic voicebank", error.message());
  }
  struct Source final {
    std::string file;
    std::int32_t midi;
    double noise;
    double phase;
  };
  const std::vector<Source> sources{
      {"k-i-01.wav", 64, 0.22, 0.00},
      {"k-i-02.wav", 64, 0.15, 0.37},
      {"m-i-01.wav", 66, 0.05, 0.19},
      {"h-a-01.wav", 67, 0.12, 0.31},
      {"a-sustain-01.wav", 69, 0.01, 0.53},
  };
  for (const auto& source : sources) {
    const auto samples = makeSyntheticVoiceUnit(
        source.midi, source.noise, source.phase);
    const auto written = seam::voicebank::writeMonoPcm16Wav(
        root / "audio" / source.file, kSampleRate, samples);
    if (!written) return seam::core::Result<seam::voicebank::Manifest>{written.error()};
  }

  seam::voicebank::Manifest manifest{
      .id = "official.voice.01.phase3.synthetic",
      .version = "0.3.0-dev",
      .displayName = "SEAM Phase 3 Synthetic Test Bank",
      .characterId = "",
      .characterVersion = "",
      .language = seam::domain::Language::Japanese,
      .expectedSampleRate = kSampleRate,
      .styles = {"original"},
      .units = {
          makeUnit("ja.original.e4.k-i.01", "k i", {"k", "i"},
                   seam::voicebank::UnitKind::Cv, "audio/k-i-01.wav", 64,
                   seam::voicebank::RendererHint::Raw, 1, 2),
          makeUnit("ja.original.e4.k-i.02", "k i", {"k", "i"},
                   seam::voicebank::UnitKind::Cv, "audio/k-i-02.wav", 64,
                   seam::voicebank::RendererHint::ClassicPsola, 2, 0),
          makeUnit("ja.original.fs4.m-i.01", "m i", {"m", "i"},
                   seam::voicebank::UnitKind::Cv, "audio/m-i-01.wav", 66,
                   seam::voicebank::RendererHint::ClassicPsola),
          makeUnit("ja.original.g4.h-a.01", "h a", {"h", "a"},
                   seam::voicebank::UnitKind::Cv, "audio/h-a-01.wav", 67,
                   seam::voicebank::RendererHint::Raw),
          makeUnit("ja.original.a4.a-sustain.01", "a sustain", {"a"},
                   seam::voicebank::UnitKind::Sustain,
                   "audio/a-sustain-01.wav", 69,
                   seam::voicebank::RendererHint::ClassicPsola),
      },
  };
  const auto validation = manifest.validate();
  if (!validation) return seam::core::Result<seam::voicebank::Manifest>{validation.error()};
  seam::voicebank::ManifestJsonCodec codec;
  const auto saved = codec.save(manifest, root / "manifest.json");
  if (!saved) return seam::core::Result<seam::voicebank::Manifest>{saved.error()};
  return manifest;
}

std::string rendererName(seam::voicebank::RendererHint value) {
  return std::string{seam::voicebank::rendererHintName(value)};
}

seam::core::Result<void> writePreviewSvg(
    const std::filesystem::path& path,
    const seam::domain::VocalRegion& region,
    std::span<const seam::domain::PhonemeToken> phonemes,
    const seam::synthesis::UnitPlan& plan,
    const seam::synthesis::PhraseRenderResult& result,
    const seam::rendering::RenderSchedulerStats& schedulerStats,
    std::string_view contentHash) {
  std::ostringstream svg;
  svg << std::fixed << std::setprecision(2);
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1600\" height=\"940\" viewBox=\"0 0 1600 940\">\n";
  svg << "<rect width=\"1600\" height=\"940\" fill=\"#100f13\"/>\n";
  svg << "<rect width=\"1600\" height=\"72\" fill=\"#201d24\"/>\n";
  svg << "<text x=\"28\" y=\"33\" fill=\"#f2ecf2\" font-family=\"sans-serif\" font-size=\"22\" font-weight=\"700\">Project SEAM · Phase 3</text>\n";
  svg << "<text x=\"28\" y=\"55\" fill=\"#a89eaa\" font-family=\"monospace\" font-size=\"11\">PERSISTENT UNIT OVERRIDES · EDITABLE SEAMS · CLASSIC PSOLA · BACKGROUND CACHE</text>\n";
  svg << "<text x=\"1260\" y=\"42\" fill=\"#d0c5ce\" font-family=\"monospace\" font-size=\"11\">MASTER · " << contentHash << "</text>\n";

  svg << "<rect x=\"24\" y=\"96\" width=\"1120\" height=\"340\" rx=\"8\" fill=\"#18161c\" stroke=\"#3b3540\"/>\n";
  svg << "<text x=\"46\" y=\"126\" fill=\"#aaa0aa\" font-family=\"monospace\" font-size=\"11\">PIANO ROLL / UNIT PLAN</text>\n";
  constexpr double startX = 70.0;
  constexpr double width = 1030.0;
  constexpr double tickScale = width / 6720.0;
  for (int bar = 0; bar <= 7; ++bar) {
    const auto x = startX + static_cast<double>(bar * 960) * tickScale;
    svg << "<line x1=\"" << x << "\" y1=\"145\" x2=\"" << x
        << "\" y2=\"410\" stroke=\"#302b33\"/>\n";
  }
  for (const auto& note : region.notes) {
    const auto x = startX + static_cast<double>(note.startTick.value()) * tickScale;
    const auto noteWidth = static_cast<double>(note.durationTick.value()) * tickScale;
    const auto y = 360.0 - static_cast<double>(note.midiKey - 60U) * 13.0;
    svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\""
        << noteWidth << "\" height=\"25\" rx=\"4\" fill=\"#513542\" stroke=\"#bd7695\"/>\n";
  }
  for (std::size_t index = 0; index < plan.entries.size(); ++index) {
    const auto x = 70.0 + static_cast<double>(index) * 245.0;
    const auto& entry = plan.entries[index];
    const auto& placement = result.placements[index];
    svg << "<rect x=\"" << x << "\" y=\"386\" width=\"220\" height=\"34\" rx=\"4\" fill=\"#29242d\" stroke=\""
        << (entry.forced ? "#cb668d" : "#685d6b") << "\"/>\n";
    svg << "<text x=\"" << x + 9.0 << "\" y=\"407\" fill=\"#eee6ec\" font-family=\"monospace\" font-size=\"10\">"
        << entry.unitId << " · " << rendererName(placement.actualRenderer) << "</text>\n";
  }

  svg << "<rect x=\"24\" y=\"462\" width=\"1120\" height=\"192\" rx=\"8\" fill=\"#18161c\" stroke=\"#3b3540\"/>\n";
  svg << "<text x=\"46\" y=\"493\" fill=\"#aaa0aa\" font-family=\"monospace\" font-size=\"11\">PHONEME / SEAM / PITCH AUTOMATION</text>\n";
  for (std::size_t index = 0; index < phonemes.size(); ++index) {
    const auto x = 54.0 + static_cast<double>(index) * 142.0;
    svg << "<rect x=\"" << x << "\" y=\"516\" width=\"118\" height=\"35\" rx=\"3\" fill=\"#2a252e\" stroke=\"#635766\"/>\n";
    svg << "<text x=\"" << x + 50.0 << "\" y=\"539\" fill=\"#f0e7ed\" font-family=\"monospace\" font-size=\"14\">"
        << phonemes[index].symbol << "</text>\n";
  }
  svg << "<path d=\"M54 615 C190 580 310 633 440 595 S720 575 1085 608\" fill=\"none\" stroke=\"#c87398\" stroke-width=\"3\"/>\n";
  svg << "<text x=\"54\" y=\"638\" fill=\"#8f8490\" font-family=\"monospace\" font-size=\"10\">PITCH CENTS · MANUAL CURVE</text>\n";

  svg << "<rect x=\"1172\" y=\"96\" width=\"404\" height=\"558\" rx=\"8\" fill=\"#19171c\" stroke=\"#3b3540\"/>\n";
  svg << "<text x=\"1196\" y=\"128\" fill=\"#aaa0aa\" font-family=\"monospace\" font-size=\"11\">RENDER INSPECTOR</text>\n";
  double y = 166.0;
  for (const auto& placement : result.placements) {
    svg << "<text x=\"1196\" y=\"" << y << "\" fill=\"#e6dce4\" font-family=\"monospace\" font-size=\"10\">"
        << placement.unitId << "</text>\n";
    y += 18.0;
    svg << "<text x=\"1210\" y=\"" << y << "\" fill=\"#a99ca8\" font-family=\"monospace\" font-size=\"9\">"
        << rendererName(placement.requestedRenderer) << " → "
        << rendererName(placement.actualRenderer) << " · seam "
        << placement.seamAmount << (placement.forcedSelection ? " · LOCKED" : "")
        << "</text>\n";
    y += 28.0;
  }
  svg << "<line x1=\"1196\" y1=\"420\" x2=\"1550\" y2=\"420\" stroke=\"#3d3740\"/>\n";
  svg << "<text x=\"1196\" y=\"454\" fill=\"#aaa0aa\" font-family=\"monospace\" font-size=\"11\">BACKGROUND RENDER</text>\n";
  svg << "<text x=\"1196\" y=\"486\" fill=\"#ded4dc\" font-family=\"monospace\" font-size=\"10\">submitted  " << schedulerStats.submitted << "</text>\n";
  svg << "<text x=\"1196\" y=\"510\" fill=\"#ded4dc\" font-family=\"monospace\" font-size=\"10\">completed  " << schedulerStats.completed << "</text>\n";
  svg << "<text x=\"1196\" y=\"534\" fill=\"#ded4dc\" font-family=\"monospace\" font-size=\"10\">cache hits " << schedulerStats.cacheHits << "</text>\n";
  svg << "<text x=\"1196\" y=\"558\" fill=\"#ded4dc\" font-family=\"monospace\" font-size=\"10\">cancelled  " << schedulerStats.cancelled << "</text>\n";
  svg << "<text x=\"1196\" y=\"596\" fill=\"#bb7895\" font-family=\"monospace\" font-size=\"10\">STALE-WHILE-RENDER ACTIVE</text>\n";

  svg << "<rect x=\"24\" y=\"680\" width=\"1552\" height=\"230\" rx=\"8\" fill=\"#18161c\" stroke=\"#3b3540\"/>\n";
  svg << "<text x=\"46\" y=\"711\" fill=\"#aaa0aa\" font-family=\"monospace\" font-size=\"11\">COMPOSED PCM / SAMPLE BOUNDARIES</text>\n";
  const auto& samples = result.audio.samples;
  const auto stride = std::max<std::size_t>(1, samples.size() / 1450U);
  std::ostringstream points;
  for (std::size_t index = 0; index < samples.size(); index += stride) {
    const auto x = 55.0 + 1490.0 * static_cast<double>(index) /
                              static_cast<double>(samples.size());
    const auto waveY = 805.0 - static_cast<double>(samples[index]) * 85.0;
    points << x << ',' << waveY << ' ';
  }
  svg << "<polyline points=\"" << points.str() << "\" fill=\"none\" stroke=\"#d7c5cf\" stroke-width=\"1\"/>\n";
  svg << "</svg>\n";

  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return seam::core::failure(seam::core::ErrorCode::IoError,
                               "Unable to create Phase 3 SVG", path.string());
  }
  stream << svg.str();
  return stream ? seam::core::success()
                : seam::core::failure(seam::core::ErrorCode::IoError,
                                      "Unable to write Phase 3 SVG", path.string());
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path outputDirectory{"out/phase3"};
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--output" && index + 1 < argc) {
      outputDirectory = argv[++index];
    } else if (argument == "--help") {
      std::cout << "Usage: seam_phase3_demo [--output DIRECTORY]\n";
      return 0;
    } else {
      outputDirectory = argument;
    }
  }
  std::error_code error;
  std::filesystem::remove_all(outputDirectory, error);
  error.clear();
  std::filesystem::create_directories(outputDirectory, error);
  if (error) {
    std::cerr << "Unable to create output directory: " << error.message() << '\n';
    return 2;
  }

  const auto bankRoot = outputDirectory / "synthetic-voicebank";
  auto bankResult = createDemoBank(bankRoot);
  if (!bankResult) {
    printError(bankResult.error());
    return 3;
  }
  auto manifest = std::move(bankResult).value();
  seam::voicebank::BankValidator validator;
  const auto validation = validator.validate(manifest, bankRoot);
  if (validation.errorCount() != 0) {
    std::cerr << "Synthetic voicebank has validation errors\n";
    return 4;
  }

  seam::application::ProjectFactory factory{3000};
  auto project = factory.createProject(
      "Project SEAM — Phase 3 Persistent Editing and Render Loop");
  project.settings().sampleRate = kSampleRate;
  project.settings().snapGrid = seam::time::Tick{240};
  static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{0}, kBpm));
  const auto trackId = factory.addVocalTrack(project, "Voicebank 01 / Original");
  auto* track = project.findVocalTrack(trackId);
  track->voicebank = {manifest.id, manifest.version, "phase3-synthetic-content"};
  track->character = {"official.character.01", "0.3.0-dev"};
  const auto regionId = factory.addRegion(
      project, trackId, "Controlled discontinuity", seam::time::Tick{0},
      seam::time::Tick{15360});
  auto* region = project.findRegion(regionId);

  struct DemoNote final { seam::time::Tick start; std::uint8_t midi; std::u32string lyric; };
  const std::vector<DemoNote> notes{
      {seam::time::Tick{1920}, 64, U"き"},
      {seam::time::Tick{2880}, 66, U"み"},
      {seam::time::Tick{3840}, 67, U"は"},
      {seam::time::Tick{4800}, 69, U"ー"},
  };
  std::vector<seam::domain::NoteId> noteIds;
  for (const auto& source : notes) {
    auto [lyric, note] = factory.makeNote(
        source.start, seam::time::Tick{960}, source.midi, source.lyric,
        seam::domain::Language::Japanese);
    noteIds.push_back(note.id);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  region->unitSelectionOverrides.push_back(seam::domain::UnitSelectionOverride{
      .startKey = seam::domain::PhonemeKey{noteIds[0], 0},
      .tokenCount = 2,
      .unitId = "ja.original.e4.k-i.02",
      .renderer = seam::domain::UnitRendererKind::ClassicPsola,
      .locked = true,
  });
  region->seamOverrides.push_back(seam::domain::SeamOverride{
      .incomingStartKey = seam::domain::PhonemeKey{noteIds[1], 0},
      .seamAmount = 0.91F,
      .overlap = seam::time::Microseconds{8500},
      .phaseReset = 0.75F,
      .envelopeBlend = 0.12F,
      .curve = seam::domain::SeamCurve::HardCharacter,
      .locked = true,
  });
  for (const auto point : {
           seam::domain::PitchAutomationPoint{
               .tick = seam::time::Tick{1920}, .cents = -16.0F,
               .interpolation = seam::domain::CurveInterpolation::Smooth},
           seam::domain::PitchAutomationPoint{
               .tick = seam::time::Tick{2400}, .cents = 28.0F,
               .interpolation = seam::domain::CurveInterpolation::Smooth},
           seam::domain::PitchAutomationPoint{
               .tick = seam::time::Tick{2879}, .cents = 0.0F,
               .interpolation = seam::domain::CurveInterpolation::Linear},
       }) {
    const auto inserted = region->pitchAutomation.upsert(point);
    if (!inserted) {
      printError(inserted.error());
      return 5;
    }
  }
  const auto projectValidation = project.validate();
  if (!projectValidation) {
    printError(projectValidation.error());
    return 6;
  }

  seam::core::StreamLogger logger{std::cerr};
  seam::application::EditorSession session{std::move(project), &logger};
  region = session.project().findRegion(regionId);
  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto phonemes = phonemizer.phonemize(*region);
  if (!phonemes.warnings.empty()) {
    std::cerr << "Unexpected phonemizer warning count: "
              << phonemes.warnings.size() << '\n';
  }
  seam::synthesis::DeterministicUnitSelector selector;
  auto unitPlan = selector.select(manifest, *region, phonemes.tokens, "original");
  if (!unitPlan) {
    printError(unitPlan.error());
    return 7;
  }
  seam::synthesis::TimingSolver timingSolver;
  auto timing = timingSolver.solve(
      session.project(), *region, phonemes.tokens, unitPlan.value(), manifest,
      kSampleRate);
  if (!timing) {
    printError(timing.error());
    return 8;
  }

  seam::synthesis::RawPhraseRenderer rawRenderer;
  auto raw = rawRenderer.render(
      manifest, bankRoot, timing.value(), kSampleRate,
      seam::synthesis::RawRenderParameters{.loopPrint = 0.9F,
                                           .additionalGainDb = -2.0F},
      seam::synthesis::SeamSettings{
          .seamAmount = 0.7F,
          .curve = seam::domain::SeamCurve::HardCharacter,
          .sampleRate = kSampleRate,
      });
  if (!raw) {
    printError(raw.error());
    return 9;
  }

  seam::synthesis::ConcatenativePhraseRenderer productionRenderer;
  seam::synthesis::PhraseRenderOptions options{
      .renderer = seam::synthesis::RendererDispatchParameters{
          .policy = seam::synthesis::RenderPolicy::RespectVoicebank,
          .allowRawFallback = true,
          .rendererOverride = std::nullopt,
          .raw = seam::synthesis::RawRenderParameters{
              .loopPrint = 0.82F,
              .additionalGainDb = -2.0F,
          },
          .psola = seam::synthesis::PsolaRenderParameters{
              .sourcePitchResidual = 0.35F,
              .additionalGainDb = -2.0F,
              .pitchCurve = {},
          },
      },
      .defaultSeam = seam::synthesis::SeamSettings{
          .seamAmount = 0.68F,
          .curve = seam::domain::SeamCurve::HardCharacter,
          .sampleRate = kSampleRate,
      },
  };
  auto production = productionRenderer.render(
      manifest, bankRoot, session.project(), *region, unitPlan.value(),
      timing.value(), kSampleRate, options);
  if (!production) {
    printError(production.error());
    return 10;
  }

  const auto rawPath = outputDirectory / "phase3-raw-reference.wav";
  const auto productionPath = outputDirectory / "phase3-psola-phrase.wav";
  if (!seam::voicebank::writeMonoPcm16Wav(
          rawPath, kSampleRate, raw.value().audio.samples) ||
      !seam::voicebank::writeMonoPcm16Wav(
          productionPath, kSampleRate, production.value().audio.samples)) {
    std::cerr << "Unable to write Phase 3 audio\n";
    return 11;
  }
  const auto waveform = seam::voicebank::WaveformPyramid::build(
      production.value().audio.samples, 64, 12);
  if (!waveform || !seam::voicebank::writeWaveformSvg(
          outputDirectory / "phase3-waveform.svg",
          waveform.value().levelFor(64.0), 1400.0, 320.0,
          "Project SEAM Phase 3 PSOLA and Editable Seam Output")) {
    std::cerr << "Unable to write Phase 3 waveform\n";
    return 12;
  }
  const auto spectrogram = seam::voicebank::buildSpectrogram(
      production.value().audio.samples,
      seam::voicebank::SpectrogramConfig{
          .fftSize = 1024,
          .hopSize = 256,
          .minimumDb = -90.0F,
          .maximumDb = -6.0F,
      });
  if (!spectrogram || !seam::voicebank::writeSpectrogramPgm(
          outputDirectory / "phase3-spectrogram.pgm", spectrogram.value())) {
    std::cerr << "Unable to write Phase 3 spectrogram\n";
    return 13;
  }

  seam::rendering::PhraseSegmenter segmenter;
  auto segments = segmenter.segment(*region);
  if (!segments || segments.value().empty()) {
    std::cerr << "Unable to segment Phase 3 phrase\n";
    return 14;
  }
  seam::rendering::RenderSnapshotFactory snapshotFactory;
  auto snapshot = snapshotFactory.create(
      session.project(), manifest, trackId, segments.value().front(),
      session.revision() + 1U, seam::rendering::RenderQuality::Preview,
      bankRoot, kSampleRate, "original", options);
  if (!snapshot) {
    printError(snapshot.error());
    return 15;
  }

  seam::rendering::PcmCache cache{outputDirectory / "pcm-cache"};
  seam::rendering::StaleWhileRenderStore staleStore;
  auto stalePcm = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{
          .sampleRate = kSampleRate,
          .startFrame = raw.value().audio.startFrame,
          .samples = raw.value().audio.samples,
      });
  static_cast<void>(staleStore.publish(segments.value().front().id, 0, stalePcm));
  static_cast<void>(staleStore.markDirty(segments.value().front().id));

  seam::rendering::BackgroundRenderScheduler scheduler{cache, 2};
  const auto obsoleteKey = snapshot.value().contentHash + "old";
  if (!scheduler.submit(seam::rendering::ScheduledRenderRequest{
          .phraseId = segments.value().front().id,
          .cacheKey = obsoleteKey,
          .revision = 1,
          .sampleRate = kSampleRate,
          .priority = seam::rendering::RenderPriority::Background,
          .task = [](std::stop_token token)
              -> seam::core::Result<seam::synthesis::PhraseAudio> {
            for (int count = 0; count < 100; ++count) {
              if (token.stop_requested()) {
                return seam::core::failure<seam::synthesis::PhraseAudio>(
                    seam::core::ErrorCode::Conflict, "obsolete preview cancelled");
              }
              std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            return seam::synthesis::PhraseAudio{
                .startFrame = 0, .samples = std::vector<float>(64, 0.0F)};
          },
      })) {
    std::cerr << "Unable to submit obsolete render\n";
    return 16;
  }
  const auto renderSnapshot = snapshot.value();
  if (!scheduler.submit(seam::rendering::ScheduledRenderRequest{
          .phraseId = segments.value().front().id,
          .cacheKey = snapshot.value().contentHash,
          .revision = snapshot.value().revision,
          .sampleRate = kSampleRate,
          .priority = seam::rendering::RenderPriority::Playhead,
          .task = [renderSnapshot](std::stop_token token)
              -> seam::core::Result<seam::synthesis::PhraseAudio> {
            seam::rendering::PhraseRenderPipeline pipeline;
            auto pipelineResult = pipeline.render(renderSnapshot, token);
            if (!pipelineResult) {
              return seam::core::Result<seam::synthesis::PhraseAudio>{
                  pipelineResult.error()};
            }
            return std::move(pipelineResult).value().rendered.audio;
          },
      })) {
    std::cerr << "Unable to submit Phase 3 production render\n";
    return 17;
  }
  if (!scheduler.waitIdle(std::chrono::seconds{5})) {
    std::cerr << "Phase 3 scheduler did not become idle\n";
    return 18;
  }
  auto completions = scheduler.drainCompleted();
  for (const auto& completion : completions) {
    if ((completion.status == seam::rendering::RenderCompletionStatus::Completed ||
         completion.status == seam::rendering::RenderCompletionStatus::CacheHit) &&
        completion.pcm != nullptr) {
      static_cast<void>(staleStore.publish(completion.phraseId, completion.revision, completion.pcm));
    }
  }
  const auto renderedSnapshot = staleStore.current(segments.value().front().id);
  if (!renderedSnapshot.has_value() ||
      renderedSnapshot->revision != snapshot.value().revision) {
    std::cerr << "Stale-while-render publication failed\n";
    return 19;
  }
  if (!scheduler.submit(seam::rendering::ScheduledRenderRequest{
          .phraseId = segments.value().front().id,
          .cacheKey = snapshot.value().contentHash,
          .revision = snapshot.value().revision,
          .sampleRate = kSampleRate,
          .priority = seam::rendering::RenderPriority::Playhead,
          .task = [](std::stop_token)
              -> seam::core::Result<seam::synthesis::PhraseAudio> {
            return seam::core::failure<seam::synthesis::PhraseAudio>(
                seam::core::ErrorCode::Internal,
                "cache hit should bypass this callback");
          },
      })) {
    std::cerr << "Unable to request cache verification\n";
    return 20;
  }
  const auto cacheCompletions = scheduler.drainCompleted();
  if (cacheCompletions.size() != 1U ||
      cacheCompletions.front().status !=
          seam::rendering::RenderCompletionStatus::CacheHit) {
    std::cerr << "Phase 3 cache hit verification failed\n";
    return 21;
  }

  seam::rendering::SpscAudioRingBuffer ring{8192};
  const auto current = staleStore.current(segments.value().front().id);
  if (!current.has_value() || current->pcm == nullptr) {
    std::cerr << "Phase 3 stale audio snapshot is unavailable\n";
    return 22;
  }
  const auto currentPcm = current->pcm;
  const auto written = ring.write(std::span<const float>{currentPcm->samples}.first(
      std::min<std::size_t>(4096U, currentPcm->samples.size())));
  std::vector<float> callbackBuffer(512, 0.0F);
  const auto read = ring.read(callbackBuffer);
  if (written < callbackBuffer.size() || read != callbackBuffer.size()) {
    std::cerr << "Phase 3 audio ring buffer verification failed\n";
    return 22;
  }

  seam::formats::ProjectJsonCodec projectCodec;
  const auto projectPath = outputDirectory / "phase3-demo.seam.json";
  if (!projectCodec.save(session.project(), projectPath)) {
    std::cerr << "Unable to save Phase 3 project\n";
    return 23;
  }
  const auto loaded = projectCodec.load(projectPath);
  if (!loaded || !(loaded.value() == session.project())) {
    std::cerr << "Phase 3 schema round trip failed\n";
    return 24;
  }

  const auto schedulerStats = scheduler.stats();
  const auto preview = writePreviewSvg(
      outputDirectory / "phase3-editor.svg", *region, phonemes.tokens,
      unitPlan.value(), production.value(), schedulerStats,
      snapshot.value().contentHash);
  if (!preview) {
    printError(preview.error());
    return 25;
  }

  seam::formats::JsonValue::Array unitSummary;
  for (const auto& placement : production.value().placements) {
    unitSummary.emplace_back(seam::formats::JsonValue::Object{
        {"unitId", placement.unitId},
        {"requestedRenderer", rendererName(placement.requestedRenderer)},
        {"actualRenderer", rendererName(placement.actualRenderer)},
        {"fallback", placement.usedFallback},
        {"forced", placement.forcedSelection},
        {"seamAmount", static_cast<double>(placement.seamAmount)},
    });
  }
  const auto cacheStats = cache.stats();
  const auto summaryObject = seam::formats::JsonValue::Object{
      {"phase", std::int64_t{3}},
      {"branchPolicy", "master-only"},
      {"projectSchema", std::int64_t{seam::formats::ProjectJsonCodec::kSchemaVersion}},
      {"voicebankSchema", std::int64_t{seam::voicebank::Manifest::kSchemaVersion}},
      {"notes", static_cast<std::int64_t>(region->notes.size())},
      {"phonemes", static_cast<std::int64_t>(phonemes.tokens.size())},
      {"phraseId", segments.value().front().id},
      {"snapshotHash", snapshot.value().contentHash},
      {"unitSelectionOverrides", static_cast<std::int64_t>(region->unitSelectionOverrides.size())},
      {"seamOverrides", static_cast<std::int64_t>(region->seamOverrides.size())},
      {"pitchAutomationPoints", static_cast<std::int64_t>(region->pitchAutomation.points().size())},
      {"renderedUnits", std::move(unitSummary)},
      {"rawFrames", static_cast<std::int64_t>(raw.value().audio.samples.size())},
      {"productionFrames", static_cast<std::int64_t>(production.value().audio.samples.size())},
      {"schedulerSubmitted", static_cast<std::int64_t>(schedulerStats.submitted)},
      {"schedulerCompleted", static_cast<std::int64_t>(schedulerStats.completed)},
      {"schedulerCacheHits", static_cast<std::int64_t>(schedulerStats.cacheHits)},
      {"schedulerCancelled", static_cast<std::int64_t>(schedulerStats.cancelled)},
      {"schedulerStale", static_cast<std::int64_t>(schedulerStats.stale)},
      {"cacheMemoryHits", static_cast<std::int64_t>(cacheStats.memoryHits)},
      {"cacheDiskHits", static_cast<std::int64_t>(cacheStats.diskHits)},
      {"cacheWrites", static_cast<std::int64_t>(cacheStats.writes)},
      {"staleWhileRenderRevision", static_cast<std::int64_t>(current->revision)},
      {"audioRingFramesWritten", static_cast<std::int64_t>(written)},
      {"audioRingFramesRead", static_cast<std::int64_t>(read)},
      {"projectRoundTripEqual", true},
      {"bankErrors", static_cast<std::int64_t>(validation.errorCount())},
      {"bankWarnings", static_cast<std::int64_t>(validation.warningCount())},
  };
  const auto summaryPath = outputDirectory / "phase3-summary.json";
  std::ofstream summary(summaryPath, std::ios::binary | std::ios::trunc);
  summary << seam::formats::stringifyJson(summaryObject, true) << '\n';
  if (!summary) {
    std::cerr << "Unable to write Phase 3 summary\n";
    return 26;
  }

  std::cout << "Project SEAM Phase 3 demo completed\n"
            << "  project schema: " << seam::formats::ProjectJsonCodec::kSchemaVersion << '\n'
            << "  voicebank schema: " << seam::voicebank::Manifest::kSchemaVersion << '\n'
            << "  persistent unit overrides: " << region->unitSelectionOverrides.size() << '\n'
            << "  editable seam overrides: " << region->seamOverrides.size() << '\n'
            << "  PSOLA placements: " << std::count_if(
                   production.value().placements.begin(),
                   production.value().placements.end(), [](const auto& value) {
                     return value.actualRenderer ==
                            seam::voicebank::RendererHint::ClassicPsola;
                   }) << '\n'
            << "  scheduler completed: " << schedulerStats.completed << '\n'
            << "  scheduler cache hits: " << schedulerStats.cacheHits << '\n'
            << "  output: " << outputDirectory << '\n';
  return 0;
}
