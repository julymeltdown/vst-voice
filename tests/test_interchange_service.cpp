#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/interchange_service.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

const char kUstx[] =
    "ustx_version: \"0.9\"\n"
    "name: Service fixture\n"
    "time_signatures:\n"
    "  - {bar_position: 0, beat_per_bar: 4, beat_unit: 4}\n"
    "tempos:\n"
    "  - {position: 0, bpm: 120}\n"
    "tracks:\n"
    "  - singer: fixture\n"
    "    track_name: Lead\n"
    "    mute: false\n"
    "    solo: false\n"
    "    volume: 0\n"
    "    pan: 0\n"
    "voice_parts:\n"
    "  - name: Verse\n"
    "    track_no: 0\n"
    "    position: 0\n"
    "    duration: 480\n"
    "    notes:\n"
    "      - position: 0\n"
    "        duration: 480\n"
    "        tone: 60\n"
    "        lyric: \"la\"\n"
    "        pitch: {data: [{x: 0, y: 0, shape: l}], snap_first: false}\n";

void writeText(const std::filesystem::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("unable to create fixture");
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

}  // namespace

TEST_CASE("interchange service imports an unsaved USTX draft with source identity") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-import");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  seam::application::ProjectFactory factory{930000U};
  seam::authoring::InterchangeService service;
  auto imported = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx,
                                                .projectName = "Draft",
                                                .language = seam::domain::Language::English});
  CHECK(imported);
  CHECK(imported.value().format == seam::authoring::InterchangeFormat::Ustx);
  CHECK(imported.value().project.name() == "Draft");
  CHECK(imported.value().project.vocalTracks().size() == 1U);
  CHECK(imported.value().project.vocalTracks().front().regions.front().notes.size() == 1U);
  CHECK(imported.value().sourceHash.size() == 64U);
  CHECK(imported.value().sourcePath == std::filesystem::absolute(source).lexically_normal());
}

TEST_CASE("interchange service exports create-new and preserves the project on collision") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-export");
  seam::application::ProjectFactory factory{940000U};
  auto project = factory.createProject("Service export");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto region = factory.addRegion(project, track, "Verse", seam::time::Tick{0}, seam::time::Tick{960});
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 64U, U"la", seam::domain::Language::English);
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));
  seam::authoring::InterchangeService service;
  const auto destination = root / "export.ustx";
  auto exported = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Ustx,
                                                .destination = destination});
  CHECK(exported);
  CHECK(exported.value().contentHash.size() == 64U);
  std::ifstream input(destination, std::ios::binary);
  const std::string before{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  CHECK(!before.empty());
  auto collision = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Ustx,
                                                .destination = destination});
  CHECK(!collision);
  std::ifstream afterInput(destination, std::ios::binary);
  const std::string after{std::istreambuf_iterator<char>{afterInput}, std::istreambuf_iterator<char>{}};
  CHECK(after == before);
}

TEST_CASE("interchange service uses bounded SMF import and export paths") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-smf");
  seam::application::ProjectFactory factory{950000U};
  auto project = factory.createProject("SMF service");
  const auto track = factory.addVocalTrack(project, "MIDI");
  const auto region = factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{960});
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{480}, 60U, U"la");
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));
  seam::authoring::InterchangeService service;
  const auto destination = root / "phrase.mid";
  auto exported = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Smf,
                                                .destination = destination,
                                                .trackId = track,
                                                .regionId = region});
  CHECK(exported);
  auto imported = service.importFile(destination, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Smf,
                                                .projectName = "Imported MIDI"});
  CHECK(imported);
  CHECK(imported.value().format == seam::authoring::InterchangeFormat::Smf);
  CHECK(imported.value().project.vocalTracks().front().regions.front().notes.size() == 1U);
}

TEST_CASE("interchange service rejects oversized input before codec work") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-oversize");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  seam::application::ProjectFactory factory{960000U};
  seam::authoring::InterchangeService service;
  seam::interchange::UstxLimits limits;
  limits.maximumInputBytes = 16U;  // Far below the fixture size.
  auto imported = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx},
      limits);
  CHECK(!imported);
  CHECK(imported.error().code == seam::core::ErrorCode::Unsupported);
}

TEST_CASE("interchange service rejects symlinked import paths") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-symlink");
  const auto real = root / "real.ustx";
  writeText(real, kUstx);
  const auto link = root / "link.ustx";
  std::error_code error;
  std::filesystem::create_symlink(real, link, error);
  if (error) return;  // Platform without symlink support cannot run this case.
  seam::application::ProjectFactory factory{970000U};
  seam::authoring::InterchangeService service;
  auto imported = service.importFile(link, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(!imported);
  CHECK(imported.error().code == seam::core::ErrorCode::Conflict);
}

TEST_CASE("interchange draft identity tracks the bytes actually read") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-identity");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  seam::application::ProjectFactory factory{980000U};
  seam::authoring::InterchangeService service;
  auto first = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(first);
  // Replace the file contents at the same path; the draft hash must change,
  // so a swapped or edited file cannot silently reuse the earlier identity.
  writeText(source, std::string{kUstx} + "# changed\n");
  auto second = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(second);
  CHECK(first.value().sourceHash != second.value().sourceHash);
}

TEST_CASE("interchange parent replacement cannot silently redirect an import") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-parent");
  const auto directoryA = root / "phrase-a";
  const auto directoryB = root / "phrase-b";
  std::filesystem::create_directories(directoryA);
  std::filesystem::create_directories(directoryB);
  writeText(directoryA / "song.ustx", kUstx);
  writeText(directoryB / "song.ustx", std::string{kUstx} + "# different parent\n");
  seam::application::ProjectFactory factory{990000U};
  seam::authoring::InterchangeService service;
  const auto heldPath = directoryA / "song.ustx";
  auto first = service.importFile(heldPath, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(first);
  // Swap the parent directory out from under the same path string.
  const auto parked = root / "phrase-a-parked";
  std::filesystem::rename(directoryA, parked);
  std::filesystem::rename(directoryB, directoryA);
  auto second = service.importFile(heldPath, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(second);
  CHECK(first.value().sourceHash != second.value().sourceHash);
}

TEST_CASE("interchange service rejects symlinked export destinations") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-exportlink");
  seam::application::ProjectFactory factory{991000U};
  auto project = factory.createProject("Link export");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto region = factory.addRegion(project, track, "Verse", seam::time::Tick{0}, seam::time::Tick{960});
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 64U, U"la", seam::domain::Language::English);
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));
  const auto real = root / "real.ustx";
  writeText(real, "pre-existing\n");
  const auto link = root / "link.ustx";
  std::error_code error;
  std::filesystem::create_symlink(real, link, error);
  if (error) return;
  seam::authoring::InterchangeService service;
  auto exported = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Ustx,
                                                .destination = link});
  CHECK(!exported);
  std::ifstream input(real, std::ios::binary);
  const std::string after{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  CHECK(after == "pre-existing\n");
}

TEST_CASE("interchange export failure leaves no destination and preserves the project") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-exportfail");
  seam::application::ProjectFactory factory{992000U};
  auto project = factory.createProject("Empty export");
  seam::authoring::InterchangeService service;
  const auto destination = root / "empty.mid";
  // SMF export requires a vocal track and region; this project has none.
  auto exported = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Smf,
                                                .destination = destination});
  CHECK(!exported);
  CHECK(!std::filesystem::exists(destination));
  CHECK(project.vocalTracks().empty());
}
