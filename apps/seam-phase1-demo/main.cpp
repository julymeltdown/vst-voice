#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/logger.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/platform/audio_callback.hpp"
#include "seam/ui/piano_roll_model.hpp"
#include "seam/ui/svg_renderer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void fail(const seam::core::Error& error) {
  std::cerr << "ERROR: " << error.message;
  if (!error.context.empty()) {
    std::cerr << " (" << error.context << ')';
  }
  std::cerr << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path outputDirectory{"out/phase1"};
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--help" || argument == "-h") {
      std::cout << "Usage: seam_phase1_demo [--output DIRECTORY] [DIRECTORY]\n";
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
  std::error_code filesystemError;
  std::filesystem::create_directories(outputDirectory, filesystemError);
  if (filesystemError) {
    std::cerr << "Unable to create output directory: " << filesystemError.message() << '\n';
    return 1;
  }

  seam::core::StreamLogger logger{std::cerr};
  seam::application::ProjectFactory factory{1000};
  auto project = factory.createProject("Project SEAM — Phase 1 Editor Foundation");
  project.settings().sampleRate = 48000.0;
  project.settings().snapEnabled = true;
  project.settings().snapGrid = seam::time::Tick{240};
  static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{0}, 154.0));
  static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{15360}, 138.0));
  static_cast<void>(project.meterMap().addOrReplace(seam::time::Tick{0}, 4, 4));

  const auto trackId = factory.addVocalTrack(project, "Voicebank 01");
  auto* track = project.findVocalTrack(trackId);
  track->voicebank = {"official.voice.01", "0.1.0-dev", "phase1-test-bank"};
  track->character = {"official.character.01", "0.1.0-dev"};

  constexpr std::size_t kNoteCount = 10000;
  constexpr std::int64_t kStep = 240;
  const auto regionDuration = seam::time::Tick{static_cast<std::int64_t>(kNoteCount) * kStep + 1920};
  const auto regionId = factory.addRegion(project, trackId, "10k-note virtualization fixture",
                                          seam::time::Tick{0}, regionDuration);
  auto* region = project.findRegion(regionId);
  region->lyrics.reserve(kNoteCount);
  region->notes.reserve(kNoteCount);

  const std::u32string syllables[] = {U"a", U"i", U"u", U"e", U"o", U"ka", U"ra", U"se"};
  for (std::size_t index = 0; index < kNoteCount; ++index) {
    const auto start = seam::time::Tick{static_cast<std::int64_t>(index) * kStep};
    const auto midi = static_cast<std::uint8_t>(48 + (index * 7) % 25);
    auto [lyric, note] = factory.makeNote(start, seam::time::Tick{180}, midi,
                                          syllables[index % std::size(syllables)]);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();

  const auto selectedNoteA = region->notes[8].id;
  const auto selectedNoteB = region->notes[9].id;
  const auto selectedNoteC = region->notes[10].id;

  const auto validation = project.validate();
  if (!validation) {
    fail(validation.error());
    return 2;
  }

  seam::application::EditorSession session{std::move(project), &logger};
  session.selection().selectOnly(selectedNoteA);
  session.selection().add(selectedNoteB);
  session.selection().add(selectedNoteC);

  seam::ui::PianoRollModel pianoRoll{session, factory, regionId};
  pianoRoll.setViewport({{0.0, 0.0, 1440.0, 812.0}, 76.0});
  pianoRoll.timeline().setPixelsPerQuarter(72.0);
  pianoRoll.pitch().setTopMidiKey(84);
  pianoRoll.pitch().setRowHeight(18.0);

  const auto moveResult = pianoRoll.moveSelection(seam::time::Tick{240}, 1);
  if (!moveResult) {
    fail(moveResult.error());
    return 3;
  }
  if (const auto undo = session.undo(); !undo) {
    fail(undo.error());
    return 4;
  }
  if (const auto redo = session.redo(); !redo) {
    fail(redo.error());
    return 5;
  }
  pianoRoll.rebuildIndex();

  seam::formats::ProjectJsonCodec codec;
  const auto projectPath = outputDirectory / "phase1-demo.seam.json";
  if (const auto save = codec.save(session.project(), projectPath); !save) {
    fail(save.error());
    return 6;
  }
  auto loaded = codec.load(projectPath);
  if (!loaded) {
    fail(loaded.error());
    return 7;
  }
  if (!(loaded.value() == session.project())) {
    std::cerr << "Serialized project does not equal the in-memory project\n";
    return 8;
  }

  seam::ui::SvgEditorRenderer renderer;
  const auto svgPath = outputDirectory / "phase1-piano-roll.svg";
  auto render = renderer.render(pianoRoll, svgPath, session.project().name(), session.revision());
  if (!render) {
    fail(render.error());
    return 9;
  }

  seam::platform::SilenceProcessor silence;
  seam::platform::AudioCallbackSimulator callbackSimulator{48000.0, 64};
  callbackSimulator.run(silence, 256);

  const auto summaryPath = outputDirectory / "phase1-summary.json";
  std::ofstream summary(summaryPath, std::ios::binary | std::ios::trunc);
  if (!summary) {
    std::cerr << "Unable to create summary file: " << summaryPath << '\n';
    return 10;
  }
  summary << "{\n"
          << "  \"branchPolicy\": \"master-only\",\n"
          << "  \"projectNotes\": " << session.project().noteCount() << ",\n"
          << "  \"visibleNotes\": " << render.value().visibleNotes << ",\n"
          << "  \"editorRevision\": " << session.revision() << ",\n"
          << "  \"svgBuildMs\": " << render.value().buildMilliseconds << ",\n"
          << "  \"svgWriteMs\": " << render.value().renderMilliseconds << ",\n"
          << "  \"audioCallbacks\": " << silence.callbackCount() << ",\n"
          << "  \"audioFrames\": " << silence.processedFrames() << ",\n"
          << "  \"roundTripEqual\": true\n"
          << "}\n";
  summary.flush();
  if (!summary) {
    std::cerr << "Unable to write summary file: " << summaryPath << '\n';
    return 11;
  }

  std::cout << "Project SEAM Phase 1 demo completed\n"
            << "  notes: " << session.project().noteCount() << '\n'
            << "  visible notes: " << render.value().visibleNotes << '\n'
            << "  revision: " << session.revision() << '\n'
            << "  project: " << projectPath << '\n'
            << "  preview: " << svgPath << '\n'
            << "  summary: " << summaryPath << '\n';
  return 0;
}
