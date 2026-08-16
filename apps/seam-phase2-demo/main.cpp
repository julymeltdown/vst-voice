#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/logger.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/ui/phoneme_lane_model.hpp"
#include "seam/ui/piano_roll_model.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/spectrogram.hpp"
#include "seam/voicebank/validator.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank/waveform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numbers>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t kSampleRate = 48000;
constexpr double kBpm = 154.0;

void printError(const seam::core::Error& error) {
  std::cerr << "ERROR: " << error.message;
  if (!error.context.empty()) std::cerr << " (" << error.context << ')';
  std::cerr << '\n';
}

std::string escapeXml(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const auto character : value) {
    switch (character) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&apos;"; break;
      default: escaped.push_back(character); break;
    }
  }
  return escaped;
}

double midiToHz(std::int32_t midi) {
  return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
}

std::vector<float> makeSyntheticVoiceUnit(std::int32_t rootMidi,
                                          double consonantNoise,
                                          double phaseOffset) {
  constexpr std::size_t frames = 24000;
  std::vector<float> samples(frames, 0.0F);
  const auto fundamental = midiToHz(rootMidi);
  std::uint32_t noiseState = static_cast<std::uint32_t>(
      0x8f4d3a21U + static_cast<std::uint32_t>(rootMidi * 97));
  for (std::size_t index = 0; index < samples.size(); ++index) {
    noiseState = noiseState * 1664525U + 1013904223U;
    const auto noise = static_cast<double>((noiseState >> 8U) & 0xffffU) / 32767.5 - 1.0;
    const auto seconds = static_cast<double>(index) / kSampleRate;
    const auto attack = std::min(1.0, static_cast<double>(index) / 256.0);
    const auto releaseFrames = samples.size() - index - 1U;
    const auto release = std::min(1.0, static_cast<double>(releaseFrames) / 768.0);
    const auto envelope = std::min(attack, release);

    const auto voiceOnset = std::clamp(
        (static_cast<double>(index) - 2400.0) / 2200.0, 0.0, 1.0);
    const auto consonantEnvelope = std::clamp(
        1.0 - static_cast<double>(index) / 4300.0, 0.0, 1.0);
    const auto phase = 2.0 * std::numbers::pi * fundamental * seconds + phaseOffset;
    const auto harmonic = std::sin(phase) + 0.34 * std::sin(phase * 2.0 + 0.21) +
                          0.16 * std::sin(phase * 3.0 + 0.47);
    const auto breath = noise * (0.025 + 0.018 * std::sin(phase * 0.5));
    const auto value = envelope *
        (voiceOnset * (0.24 * harmonic + breath) +
         consonantEnvelope * consonantNoise * noise);
    samples[index] = static_cast<float>(std::clamp(value, -0.82, 0.82));
  }
  return samples;
}

seam::voicebank::Unit makeUnit(std::string id,
                               std::string alias,
                               std::vector<std::string> phones,
                               seam::voicebank::UnitKind kind,
                               std::filesystem::path path,
                               std::int32_t rootMidi,
                               std::int32_t take = 1,
                               std::int32_t priority = 0) {
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
      .gainDb = 0.0F,
      .renderer = seam::voicebank::RendererHint::Raw,
      .markers = seam::voicebank::UnitMarkers{
          .audioOffset = 0,
          .consonantEnd = 2600,
          .vowelOnset = 3600,
          .stableStart = 5200,
          .loopStart = 7200,
          .loopEnd = 16800,
          .releaseStart = 19800,
          .audioEnd = 24000,
      },
      .enabled = true,
  };
}

seam::core::Result<seam::voicebank::Manifest> createDemoBank(
    const std::filesystem::path& bankRoot) {
  std::error_code error;
  std::filesystem::create_directories(bankRoot / "audio", error);
  if (error) {
    return seam::core::failure<seam::voicebank::Manifest>(
        seam::core::ErrorCode::IoError, "Unable to create demo voicebank directory",
        error.message());
  }

  struct Source final {
    std::string file;
    std::int32_t midi;
    double noise;
    double phase;
  };
  const std::vector<Source> sources{
      {"k-i-01.wav", 64, 0.23, 0.00},
      {"k-i-02.wav", 64, 0.17, 0.41},
      {"m-i-01.wav", 66, 0.05, 0.19},
      {"h-a-01.wav", 67, 0.13, 0.31},
      {"a-sustain-01.wav", 69, 0.01, 0.53},
  };
  for (const auto& source : sources) {
    const auto samples = makeSyntheticVoiceUnit(source.midi, source.noise, source.phase);
    const auto written = seam::voicebank::writeMonoPcm16Wav(
        bankRoot / "audio" / source.file, kSampleRate, samples);
    if (!written) return seam::core::Result<seam::voicebank::Manifest>{written.error()};
  }

  seam::voicebank::Manifest manifest{
      .id = "official.voice.01.phase2.synthetic",
      .version = "0.2.0-dev",
      .displayName = "SEAM Phase 2 Synthetic Test Bank",
      .language = seam::domain::Language::Japanese,
      .expectedSampleRate = kSampleRate,
      .styles = {"original"},
      .units = {
          makeUnit("ja.original.e4.k-i.01", "k i", {"k", "i"},
                   seam::voicebank::UnitKind::Cv, "audio/k-i-01.wav", 64, 1, 2),
          makeUnit("ja.original.e4.k-i.02", "k i", {"k", "i"},
                   seam::voicebank::UnitKind::Cv, "audio/k-i-02.wav", 64, 2, 0),
          makeUnit("ja.original.fs4.m-i.01", "m i", {"m", "i"},
                   seam::voicebank::UnitKind::Cv, "audio/m-i-01.wav", 66),
          makeUnit("ja.original.g4.h-a.01", "h a", {"h", "a"},
                   seam::voicebank::UnitKind::Cv, "audio/h-a-01.wav", 67),
          makeUnit("ja.original.a4.a-sustain.01", "a sustain", {"a"},
                   seam::voicebank::UnitKind::Sustain,
                   "audio/a-sustain-01.wav", 69),
      },
  };
  const auto validation = manifest.validate();
  if (!validation) return seam::core::Result<seam::voicebank::Manifest>{validation.error()};
  seam::voicebank::ManifestJsonCodec codec;
  const auto saved = codec.save(manifest, bankRoot / "manifest.json");
  if (!saved) return seam::core::Result<seam::voicebank::Manifest>{saved.error()};
  return manifest;
}

seam::core::Result<void> writeEditorSvg(
    const std::filesystem::path& path,
    const seam::ui::PianoRollModel& pianoRoll,
    const seam::ui::PhonemeLaneModel& phonemeLane,
    const seam::phonemizer::Result& phonemes,
    const seam::synthesis::UnitPlan& unitPlan,
    const seam::synthesis::TimingPlan& timingPlan,
    const seam::voicebank::ValidationReport& validation,
    std::span<const float> audio,
    std::uint64_t revision) {
  constexpr double editorX = 180.0;
  constexpr double editorY = 126.0;
  constexpr double editorWidth = 1040.0;
  constexpr double editorHeight = 430.0;
  constexpr double phonemeY = 586.0;
  constexpr double unitY = 646.0;
  constexpr double waveformY = 726.0;
  constexpr double inspectorX = 1244.0;

  const auto notes = pianoRoll.visibleNotes();
  std::map<seam::domain::NoteId, seam::ui::NoteVisual> notesById;
  for (const auto& note : notes) notesById.emplace(note.noteId, note);

  std::ostringstream svg;
  svg << std::fixed << std::setprecision(2);
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1600\" height=\"920\" viewBox=\"0 0 1600 920\">\n";
  svg << "<defs><linearGradient id=\"bar\" x1=\"0\" x2=\"0\" y1=\"0\" y2=\"1\"><stop stop-color=\"#29262d\"/><stop offset=\"1\" stop-color=\"#18171b\"/></linearGradient><filter id=\"soft\"><feDropShadow dx=\"0\" dy=\"3\" stdDeviation=\"5\" flood-opacity=\"0.22\"/></filter></defs>\n";
  svg << "<rect width=\"1600\" height=\"920\" fill=\"#111014\"/>\n";
  svg << "<rect width=\"1600\" height=\"70\" fill=\"url(#bar)\"/>\n";
  svg << "<text x=\"26\" y=\"31\" fill=\"#f2edf3\" font-family=\"sans-serif\" font-size=\"21\" font-weight=\"650\">Project SEAM</text>\n";
  svg << "<text x=\"26\" y=\"53\" fill=\"#9d959f\" font-family=\"monospace\" font-size=\"11\">PHONEME · VOICEBANK · RAW CONCATENATIVE VERTICAL SLICE</text>\n";
  svg << "<rect x=\"1292\" y=\"18\" width=\"280\" height=\"34\" rx=\"6\" fill=\"#151418\" stroke=\"#49414a\"/><text x=\"1312\" y=\"40\" fill=\"#c7bdc7\" font-family=\"monospace\" font-size=\"11\">MASTER · PHASE 2 · REV " << revision << "</text>\n";

  svg << "<rect x=\"20\" y=\"92\" width=\"138\" height=\"796\" rx=\"8\" fill=\"#19181d\" stroke=\"#37323a\"/>\n";
  svg << "<text x=\"38\" y=\"122\" fill=\"#a79fa8\" font-family=\"monospace\" font-size=\"11\">TRACKS</text>\n";
  svg << "<rect x=\"31\" y=\"142\" width=\"116\" height=\"74\" rx=\"5\" fill=\"#31252e\" stroke=\"#8c526c\"/>\n";
  svg << "<text x=\"43\" y=\"167\" fill=\"#f0e6ed\" font-family=\"sans-serif\" font-size=\"13\">Voice 01</text><text x=\"43\" y=\"188\" fill=\"#b48a9f\" font-family=\"monospace\" font-size=\"9\">RAW / ORIGINAL</text>\n";
  svg << "<circle cx=\"45\" cy=\"204\" r=\"4\" fill=\"#b25778\"/><circle cx=\"61\" cy=\"204\" r=\"4\" fill=\"#625b66\"/>\n";
  svg << "<text x=\"38\" y=\"258\" fill=\"#7f7882\" font-family=\"monospace\" font-size=\"9\">SAMPLE BANK</text><text x=\"38\" y=\"277\" fill=\"#c3bac4\" font-family=\"monospace\" font-size=\"10\">5 units</text>\n";

  svg << "<rect x=\"" << editorX << "\" y=\"" << editorY << "\" width=\"" << editorWidth << "\" height=\"" << editorHeight << "\" rx=\"7\" fill=\"#17161b\" stroke=\"#39343c\" filter=\"url(#soft)\"/>\n";
  const auto keyboardWidth = pianoRoll.viewport().keyboardWidth;
  svg << "<rect x=\"" << editorX << "\" y=\"" << editorY << "\" width=\"" << keyboardWidth << "\" height=\"" << editorHeight << "\" fill=\"#211f25\"/>\n";

  const auto& timeline = pianoRoll.timeline();
  const auto& pitch = pianoRoll.pitch();
  const auto quarter = seam::time::Tick{timeline.ppq()};
  for (std::int64_t quarterIndex = 0; quarterIndex < 18; ++quarterIndex) {
    const auto tick = seam::time::Tick{quarterIndex * quarter.value()};
    const auto x = editorX + keyboardWidth + timeline.tickToPixel(tick);
    if (x > editorX + editorWidth) break;
    const bool bar = quarterIndex % 4 == 0;
    svg << "<line x1=\"" << x << "\" y1=\"" << editorY << "\" x2=\"" << x << "\" y2=\"" << editorY + editorHeight << "\" stroke=\"" << (bar ? "#4a414b" : "#29262d") << "\" stroke-width=\"" << (bar ? 1.25 : 0.65) << "\"/>\n";
    if (bar) svg << "<text x=\"" << x + 5.0 << "\" y=\"" << editorY + 17.0 << "\" fill=\"#8e858f\" font-family=\"monospace\" font-size=\"9\">" << quarterIndex / 4 + 1 << "</text>\n";
  }
  for (int midi = pitch.topMidiKey(); midi >= 48; --midi) {
    const auto y = editorY + pitch.midiToPixel(midi);
    if (y > editorY + editorHeight) break;
    const bool black = midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6 || midi % 12 == 8 || midi % 12 == 10;
    svg << "<rect x=\"" << editorX << "\" y=\"" << y << "\" width=\"" << keyboardWidth << "\" height=\"" << pitch.rowHeight() << "\" fill=\"" << (black ? "#242129" : "#302d34") << "\"/><line x1=\"" << editorX << "\" y1=\"" << y << "\" x2=\"" << editorX + editorWidth << "\" y2=\"" << y << "\" stroke=\"#252229\" stroke-width=\"0.5\"/>\n";
    if (midi % 12 == 0) svg << "<text x=\"" << editorX + 12.0 << "\" y=\"" << y + 13.0 << "\" fill=\"#aaa2ab\" font-family=\"monospace\" font-size=\"9\">C" << midi / 12 - 1 << "</text>\n";
  }

  for (const auto& note : notes) {
    const auto x = editorX + note.bounds.x;
    const auto y = editorY + note.bounds.y;
    svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << note.bounds.width << "\" height=\"" << note.bounds.height << "\" rx=\"3\" fill=\"#533947\" stroke=\"#c07b9a\"/>\n";
    svg << "<text x=\"" << x + 6.0 << "\" y=\"" << y + 14.0 << "\" fill=\"#f5edf2\" font-family=\"sans-serif\" font-size=\"11\">" << escapeXml(note.lyric) << "</text>\n";
  }

  svg << "<text x=\"" << editorX << "\" y=\"" << phonemeY - 11.0 << "\" fill=\"#968d97\" font-family=\"monospace\" font-size=\"10\">PHONEME LANE</text><rect x=\"" << editorX << "\" y=\"" << phonemeY << "\" width=\"" << editorWidth << "\" height=\"42\" rx=\"4\" fill=\"#19171c\" stroke=\"#3a333d\"/>\n";
  for (const auto& phoneme : phonemeLane.visuals()) {
    const auto x = editorX + phoneme.bounds.x;
    const auto fill = phoneme.locked ? "#684052" : "#302b34";
    svg << "<rect x=\"" << x << "\" y=\"" << phonemeY + 5.0 << "\" width=\"" << phoneme.bounds.width << "\" height=\"32\" rx=\"3\" fill=\"" << fill << "\" stroke=\"#775d71\"/><text x=\"" << x + 7.0 << "\" y=\"" << phonemeY + 26.0 << "\" fill=\"#eadfe8\" font-family=\"monospace\" font-size=\"11\">" << escapeXml(phoneme.symbol) << (phoneme.locked ? " · LOCK" : "") << "</text>\n";
  }

  svg << "<text x=\"" << editorX << "\" y=\"" << unitY - 11.0 << "\" fill=\"#968d97\" font-family=\"monospace\" font-size=\"10\">UNIT LANE · DETERMINISTIC PLAN</text><rect x=\"" << editorX << "\" y=\"" << unitY << "\" width=\"" << editorWidth << "\" height=\"54\" rx=\"4\" fill=\"#18171b\" stroke=\"#3a333d\"/>\n";
  for (const auto& entry : unitPlan.entries) {
    const auto& firstToken = phonemes.tokens.at(entry.tokenStart);
    const auto& lastToken = phonemes.tokens.at(entry.tokenStart + entry.tokenCount - 1U);
    const auto first = notesById.find(firstToken.key.noteId);
    const auto last = notesById.find(lastToken.key.noteId);
    if (first == notesById.end() || last == notesById.end()) continue;
    const auto x = editorX + first->second.bounds.x;
    const auto right = editorX + last->second.bounds.right();
    const auto unitWidth = std::max(58.0, right - x);
    svg << "<rect x=\"" << x << "\" y=\"" << unitY + 6.0 << "\" width=\"" << unitWidth << "\" height=\"42\" rx=\"4\" fill=\"#25222a\" stroke=\"#6d5968\"/><text x=\"" << x + 7.0 << "\" y=\"" << unitY + 25.0 << "\" fill=\"#efe6ec\" font-family=\"monospace\" font-size=\"9\">" << escapeXml(entry.unitId) << "</text><text x=\"" << x + 7.0 << "\" y=\"" << unitY + 40.0 << "\" fill=\"#a994a0\" font-family=\"monospace\" font-size=\"8\">TOKENS " << entry.tokenCount << " · ALT " << entry.alternatives.size() << "</text>\n";
  }

  svg << "<text x=\"" << editorX << "\" y=\"" << waveformY - 11.0 << "\" fill=\"#968d97\" font-family=\"monospace\" font-size=\"10\">RENDERED PCM · RAW SEAM 74%</text><rect x=\"" << editorX << "\" y=\"" << waveformY << "\" width=\"" << editorWidth << "\" height=\"150\" rx=\"4\" fill=\"#161519\" stroke=\"#39333b\"/>\n";
  if (!audio.empty()) {
    const auto columns = static_cast<std::size_t>(editorWidth - 20.0);
    svg << "<path d=\"";
    for (std::size_t column = 0; column < columns; ++column) {
      const auto begin = column * audio.size() / columns;
      const auto end = std::max(begin + 1U, (column + 1U) * audio.size() / columns);
      float peak = 0.0F;
      for (std::size_t index = begin; index < std::min(end, audio.size()); ++index) {
        peak = std::max(peak, std::abs(audio[index]));
      }
      const auto x = editorX + 10.0 + static_cast<double>(column);
      const auto y = waveformY + 75.0 - static_cast<double>(peak) * 65.0;
      svg << (column == 0 ? "M" : " L") << x << ' ' << y;
    }
    for (std::size_t column = columns; column-- > 0;) {
      const auto begin = column * audio.size() / columns;
      const auto end = std::max(begin + 1U, (column + 1U) * audio.size() / columns);
      float peak = 0.0F;
      for (std::size_t index = begin; index < std::min(end, audio.size()); ++index) {
        peak = std::max(peak, std::abs(audio[index]));
      }
      const auto x = editorX + 10.0 + static_cast<double>(column);
      const auto y = waveformY + 75.0 + static_cast<double>(peak) * 65.0;
      svg << " L" << x << ' ' << y;
    }
    svg << " Z\" fill=\"#81506a\" fill-opacity=\"0.62\" stroke=\"#c27d9c\" stroke-width=\"0.7\"/>\n";
  }

  svg << "<rect x=\"" << inspectorX << "\" y=\"92\" width=\"332\" height=\"796\" rx=\"8\" fill=\"#19181d\" stroke=\"#37323a\"/>\n";
  svg << "<text x=\"" << inspectorX + 22.0 << "\" y=\"124\" fill=\"#ddd4dc\" font-family=\"sans-serif\" font-size=\"15\" font-weight=\"600\">Raw concatenative render</text>\n";
  svg << "<text x=\"" << inspectorX + 22.0 << "\" y=\"153\" fill=\"#9c929d\" font-family=\"monospace\" font-size=\"10\">PHONEMES</text><text x=\"" << inspectorX + 220.0 << "\" y=\"153\" fill=\"#eee6ec\" font-family=\"monospace\" font-size=\"11\">" << phonemes.tokens.size() << "</text>\n";
  svg << "<text x=\"" << inspectorX + 22.0 << "\" y=\"177\" fill=\"#9c929d\" font-family=\"monospace\" font-size=\"10\">SELECTED UNITS</text><text x=\"" << inspectorX + 220.0 << "\" y=\"177\" fill=\"#eee6ec\" font-family=\"monospace\" font-size=\"11\">" << unitPlan.entries.size() << "</text>\n";
  svg << "<text x=\"" << inspectorX + 22.0 << "\" y=\"201\" fill=\"#9c929d\" font-family=\"monospace\" font-size=\"10\">TIMING ISSUES</text><text x=\"" << inspectorX + 220.0 << "\" y=\"201\" fill=\"#eee6ec\" font-family=\"monospace\" font-size=\"11\">" << timingPlan.issues.size() << "</text>\n";
  svg << "<text x=\"" << inspectorX + 22.0 << "\" y=\"225\" fill=\"#9c929d\" font-family=\"monospace\" font-size=\"10\">BANK ERRORS</text><text x=\"" << inspectorX + 220.0 << "\" y=\"225\" fill=\"" << (validation.errorCount() == 0 ? "#9cc5a5" : "#d97d83") << "\" font-family=\"monospace\" font-size=\"11\">" << validation.errorCount() << "</text>\n";
  svg << "<line x1=\"" << inspectorX + 20.0 << "\" y1=\"248\" x2=\"" << inspectorX + 312.0 << "\" y2=\"248\" stroke=\"#37323a\"/>\n";
  svg << "<text x=\"" << inspectorX + 22.0 << "\" y=\"279\" fill=\"#a79ca6\" font-family=\"monospace\" font-size=\"10\">SEAM AMOUNT</text><rect x=\"" << inspectorX + 22.0 << "\" y=\"293\" width=\"276\" height=\"8\" rx=\"4\" fill=\"#302d34\"/><rect x=\"" << inspectorX + 22.0 << "\" y=\"293\" width=\"204\" height=\"8\" rx=\"4\" fill=\"#9f5d7a\"/><circle cx=\"" << inspectorX + 226.0 << "\" cy=\"297\" r=\"7\" fill=\"#e0b2c5\"/>\n";
  svg << "<text x=\"" << inspectorX + 22.0 << "\" y=\"335\" fill=\"#a79ca6\" font-family=\"monospace\" font-size=\"10\">LOCKED PHONEME</text><text x=\"" << inspectorX + 22.0 << "\" y=\"359\" fill=\"#eadfe7\" font-family=\"monospace\" font-size=\"12\">k · START -70 ms</text>\n";
  svg << "<line x1=\"" << inspectorX + 20.0 << "\" y1=\"384\" x2=\"" << inspectorX + 312.0 << "\" y2=\"384\" stroke=\"#37323a\"/>\n";
  svg << "<text x=\"" << inspectorX + 22.0 << "\" y=\"416\" fill=\"#a79ca6\" font-family=\"monospace\" font-size=\"10\">VOICEBANK VALIDATION</text>\n";
  double issueY = 442.0;
  if (validation.issues.empty()) {
    svg << "<text x=\"" << inspectorX + 22.0 << "\" y=\"" << issueY << "\" fill=\"#9cc5a5\" font-family=\"monospace\" font-size=\"10\">NO ISSUES</text>\n";
  } else {
    for (const auto& issue : validation.issues) {
      if (issueY > 600.0) break;
      svg << "<circle cx=\"" << inspectorX + 27.0 << "\" cy=\"" << issueY - 4.0 << "\" r=\"3\" fill=\"" << (issue.severity == seam::voicebank::IssueSeverity::Error ? "#d97d83" : issue.severity == seam::voicebank::IssueSeverity::Warning ? "#d5ad6a" : "#8fa5b2") << "\"/><text x=\"" << inspectorX + 38.0 << "\" y=\"" << issueY << "\" fill=\"#c9bec7\" font-family=\"monospace\" font-size=\"9\">" << escapeXml(seam::voicebank::issueCodeName(issue.code)) << "</text>\n";
      issueY += 22.0;
    }
  }
  svg << "<line x1=\"" << inspectorX + 20.0 << "\" y1=\"630\" x2=\"" << inspectorX + 312.0 << "\" y2=\"630\" stroke=\"#37323a\"/>\n";
  svg << "<text x=\"" << inspectorX + 22.0 << "\" y=\"662\" fill=\"#a79ca6\" font-family=\"monospace\" font-size=\"10\">CURRENT BOUNDARY</text><text x=\"" << inspectorX + 22.0 << "\" y=\"688\" fill=\"#eadfe7\" font-family=\"monospace\" font-size=\"11\">k-i.01  →  m-i.01</text><text x=\"" << inspectorX + 22.0 << "\" y=\"710\" fill=\"#a994a0\" font-family=\"monospace\" font-size=\"9\">AUDIBLE SPLICE PRESERVED</text>\n";
  svg << "<rect x=\"" << inspectorX + 22.0 << "\" y=\"754\" width=\"276\" height=\"82\" rx=\"5\" fill=\"#151418\" stroke=\"#3e3841\"/><text x=\"" << inspectorX + 38.0 << "\" y=\"780\" fill=\"#d8cdd5\" font-family=\"sans-serif\" font-size=\"12\">Character display: Minimal</text><text x=\"" << inspectorX + 38.0 << "\" y=\"804\" fill=\"#918791\" font-family=\"monospace\" font-size=\"9\">EDITOR REMAINS FULLY USABLE OFF</text>\n";
  svg << "</svg>\n";

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return seam::core::failure(seam::core::ErrorCode::IoError,
                               "Unable to create Phase 2 editor SVG", path.string());
  }
  const auto data = svg.str();
  output.write(data.data(), static_cast<std::streamsize>(data.size()));
  output.flush();
  if (!output) {
    return seam::core::failure(seam::core::ErrorCode::IoError,
                               "Unable to write Phase 2 editor SVG", path.string());
  }
  return seam::core::success();
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path outputDirectory{"out/phase2"};
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--help" || argument == "-h") {
      std::cout << "Usage: seam_phase2_demo [--output DIRECTORY] [DIRECTORY]\n";
      return 0;
    }
    if (argument == "--output" || argument == "-o") {
      if (index + 1 >= argc) {
        std::cerr << "--output requires a directory\n";
        return 1;
      }
      outputDirectory = std::filesystem::path{argv[++index]};
      continue;
    }
    if (!argument.empty() && argument.front() == '-') {
      std::cerr << "Unknown option: " << argument << '\n';
      return 1;
    }
    outputDirectory = std::filesystem::path{argument};
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
  auto manifestResult = createDemoBank(bankRoot);
  if (!manifestResult) {
    printError(manifestResult.error());
    return 3;
  }
  const auto manifest = std::move(manifestResult).value();
  seam::voicebank::BankValidator validator;
  const auto bankValidation = validator.validate(manifest, bankRoot);
  if (!bankValidation.ok()) {
    std::cerr << "Synthetic voicebank did not pass validation\n";
    return 4;
  }

  seam::application::ProjectFactory factory{1000};
  auto project = factory.createProject("Project SEAM — Phase 2 Raw Voice Vertical Slice");
  project.settings().sampleRate = kSampleRate;
  project.settings().snapGrid = seam::time::Tick{240};
  project.settings().characterDisplay = seam::domain::CharacterDisplayMode::Minimal;
  static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{0}, kBpm));
  const auto trackId = factory.addVocalTrack(project, "Voicebank 01 / Original");
  auto* track = project.findVocalTrack(trackId);
  track->voicebank = {manifest.id, manifest.version, "phase2-synthetic-content"};
  track->character = {"official.character.01", "0.2.0-dev"};
  const auto regionId = factory.addRegion(
      project, trackId, "Demo phrase", seam::time::Tick{0}, seam::time::Tick{15360});
  auto* region = project.findRegion(regionId);

  struct DemoNote final {
    seam::time::Tick start;
    std::uint8_t midi;
    std::u32string lyric;
  };
  const std::vector<DemoNote> notes{
      {seam::time::Tick{1920}, 64, U"き"},
      {seam::time::Tick{2880}, 66, U"み"},
      {seam::time::Tick{3840}, 67, U"は"},
      {seam::time::Tick{4800}, 69, U"ー"},
  };
  seam::domain::NoteId firstNoteId;
  for (const auto& source : notes) {
    auto [lyric, note] = factory.makeNote(
        source.start, seam::time::Tick{960}, source.midi, source.lyric,
        seam::domain::Language::Japanese);
    if (!firstNoteId.valid()) firstNoteId = note.id;
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  region->phonemeOverrides.push_back(seam::domain::PhonemeOverride{
      .key = seam::domain::PhonemeKey{firstNoteId, 0},
      .symbol = std::nullopt,
      .timing = seam::domain::PhonemeTiming{
          .startOffset = seam::time::Microseconds{-70000},
          .endOffset = seam::time::Microseconds{0},
      },
      .locked = true,
  });
  const auto projectValidation = project.validate();
  if (!projectValidation) {
    printError(projectValidation.error());
    return 5;
  }

  seam::core::StreamLogger logger{std::cerr};
  seam::application::EditorSession session{std::move(project), &logger};
  region = session.project().findRegion(regionId);
  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto phonemes = phonemizer.phonemize(*region);
  if (!phonemes.warnings.empty()) {
    for (const auto& warning : phonemes.warnings) {
      std::cerr << "Phonemizer warning: " << warning.message << '\n';
    }
  }

  seam::synthesis::DeterministicUnitSelector selector;
  auto unitPlan = selector.select(manifest, *region, phonemes.tokens, "original");
  if (!unitPlan) {
    printError(unitPlan.error());
    return 6;
  }
  seam::synthesis::TimingSolver timingSolver;
  auto timingPlan = timingSolver.solve(
      session.project(), *region, phonemes.tokens, unitPlan.value(), manifest,
      kSampleRate);
  if (!timingPlan) {
    printError(timingPlan.error());
    return 7;
  }
  seam::synthesis::RawPhraseRenderer renderer;
  auto rendered = renderer.render(
      manifest, bankRoot, timingPlan.value(), kSampleRate,
      seam::synthesis::RawRenderParameters{
          .loopPrint = 0.86F,
          .additionalGainDb = -2.5F,
      },
      seam::synthesis::SeamSettings{.seamAmount = 0.74F, .sampleRate = kSampleRate});
  if (!rendered) {
    printError(rendered.error());
    return 8;
  }

  const auto phraseWav = outputDirectory / "phase2-raw-phrase.wav";
  const auto wavWritten = seam::voicebank::writeMonoPcm16Wav(
      phraseWav, kSampleRate, rendered.value().audio.samples);
  if (!wavWritten) {
    printError(wavWritten.error());
    return 9;
  }
  const auto waveform = seam::voicebank::WaveformPyramid::build(
      rendered.value().audio.samples, 64, 12);
  if (!waveform) {
    printError(waveform.error());
    return 10;
  }
  const auto waveformWritten = seam::voicebank::writeWaveformSvg(
      outputDirectory / "phase2-raw-waveform.svg",
      waveform.value().levelFor(64.0), 1400.0, 320.0,
      "Project SEAM Phase 2 Raw Concatenative Output");
  if (!waveformWritten) {
    printError(waveformWritten.error());
    return 11;
  }
  const auto spectrogram = seam::voicebank::buildSpectrogram(
      rendered.value().audio.samples,
      seam::voicebank::SpectrogramConfig{
          .fftSize = 1024,
          .hopSize = 256,
          .minimumDb = -90.0F,
          .maximumDb = -6.0F,
      });
  if (!spectrogram) {
    printError(spectrogram.error());
    return 12;
  }
  const auto spectrogramWritten = seam::voicebank::writeSpectrogramPgm(
      outputDirectory / "phase2-raw-spectrogram.pgm", spectrogram.value());
  if (!spectrogramWritten) {
    printError(spectrogramWritten.error());
    return 13;
  }

  seam::formats::ProjectJsonCodec projectCodec;
  const auto projectPath = outputDirectory / "phase2-demo.seam.json";
  const auto projectSaved = projectCodec.save(session.project(), projectPath);
  if (!projectSaved) {
    printError(projectSaved.error());
    return 14;
  }
  const auto loadedProject = projectCodec.load(projectPath);
  if (!loadedProject || !(loadedProject.value() == session.project())) {
    std::cerr << "Project schema 2 round trip failed\n";
    return 15;
  }

  seam::ui::PianoRollModel pianoRoll{session, factory, regionId};
  pianoRoll.setViewport({{0.0, 0.0, 1040.0, 430.0}, 72.0});
  pianoRoll.timeline().setPixelsPerQuarter(136.0);
  pianoRoll.pitch().setTopMidiKey(78);
  pianoRoll.pitch().setRowHeight(22.0);
  pianoRoll.rebuildIndex();
  seam::ui::PhonemeLaneModel phonemeLane;
  phonemeLane.rebuild(pianoRoll, phonemes, 0.0, 32.0);
  const auto svgWritten = writeEditorSvg(
      outputDirectory / "phase2-editor.svg", pianoRoll, phonemeLane, phonemes,
      unitPlan.value(), timingPlan.value(), bankValidation,
      rendered.value().audio.samples, session.revision());
  if (!svgWritten) {
    printError(svgWritten.error());
    return 16;
  }

  const auto audioStats = seam::voicebank::analyzeAudio(rendered.value().audio.samples);
  seam::formats::JsonValue::Array selectedUnits;
  for (const auto& entry : unitPlan.value().entries) {
    selectedUnits.emplace_back(entry.unitId);
  }
  seam::formats::JsonValue::Array validationIssues;
  for (const auto& issue : bankValidation.issues) {
    validationIssues.emplace_back(seam::formats::JsonValue::Object{
        {"severity", std::string{seam::voicebank::issueSeverityName(issue.severity)}},
        {"code", std::string{seam::voicebank::issueCodeName(issue.code)}},
        {"unitId", issue.unitId},
        {"message", issue.message},
    });
  }
  const auto summaryValue = seam::formats::JsonValue::Object{
      {"phase", std::int64_t{2}},
      {"branchPolicy", "master-only"},
      {"projectSchema", std::int64_t{seam::formats::ProjectJsonCodec::kSchemaVersion}},
      {"voicebankSchema", std::int64_t{seam::voicebank::Manifest::kSchemaVersion}},
      {"notes", static_cast<std::int64_t>(region->notes.size())},
      {"phonemes", static_cast<std::int64_t>(phonemes.tokens.size())},
      {"selectedUnits", std::move(selectedUnits)},
      {"timingIssues", static_cast<std::int64_t>(timingPlan.value().issues.size())},
      {"bankErrors", static_cast<std::int64_t>(bankValidation.errorCount())},
      {"bankWarnings", static_cast<std::int64_t>(bankValidation.warningCount())},
      {"bankIssues", std::move(validationIssues)},
      {"audioStartFrame", rendered.value().audio.startFrame},
      {"audioFrames", static_cast<std::int64_t>(rendered.value().audio.samples.size())},
      {"audioPeak", static_cast<double>(audioStats.peak)},
      {"audioRms", audioStats.rms},
      {"audioDcOffset", audioStats.dcOffset},
      {"projectRoundTripEqual", true},
      {"lockedPhonemeOverride", true},
      {"characterDisplay", "minimal"},
  };
  const auto summaryPath = outputDirectory / "phase2-summary.json";
  std::ofstream summary(summaryPath, std::ios::binary | std::ios::trunc);
  if (!summary) {
    std::cerr << "Unable to create summary file\n";
    return 17;
  }
  summary << seam::formats::stringifyJson(summaryValue, true) << '\n';
  summary.flush();
  if (!summary) {
    std::cerr << "Unable to write summary file\n";
    return 18;
  }

  std::cout << "Project SEAM Phase 2 demo completed\n"
            << "  phonemes: " << phonemes.tokens.size() << '\n'
            << "  selected units: " << unitPlan.value().entries.size() << '\n'
            << "  raw output frames: " << rendered.value().audio.samples.size() << '\n'
            << "  voicebank errors: " << bankValidation.errorCount() << '\n'
            << "  project: " << projectPath << '\n'
            << "  voicebank: " << bankRoot / "manifest.json" << '\n'
            << "  audio: " << phraseWav << '\n'
            << "  editor evidence: " << outputDirectory / "phase2-editor.svg" << '\n'
            << "  summary: " << summaryPath << '\n';
  return 0;
}
