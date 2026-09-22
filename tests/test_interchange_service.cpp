#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/interchange_service.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#ifndef _WIN32
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#endif

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
  CHECK(imported.value().sourcePath ==
        std::filesystem::weakly_canonical(source.parent_path()) / source.filename());
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

#ifndef _WIN32
// POSIX-only: the held-input admission boundary is POSIX-verified; Windows
// reparse semantics are pending platform evidence, so these cases are not
// registered there rather than counted as trivial passes.
TEST_CASE("interchange service rejects symlinked import paths") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-symlink");
  const auto real = root / "real.ustx";
  writeText(real, kUstx);
  const auto link = root / "link.ustx";
  std::error_code error;
  std::filesystem::create_symlink(real, link, error);
  CHECK(!error);  // Fixture creation is required on POSIX, not optional.
  seam::application::ProjectFactory factory{970000U};
  seam::authoring::InterchangeService service;
  auto imported = service.importFile(link, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(!imported);
  CHECK(imported.error().code == seam::core::ErrorCode::Conflict);
}

TEST_CASE("interchange service canonicalizes an intermediate symlink component") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-midlink");
  const auto realDir = root / "real-dir";
  std::filesystem::create_directories(realDir);
  writeText(realDir / "song.ustx", kUstx);
  const auto linkDir = root / "linked-dir";
  std::error_code error;
  std::filesystem::create_directory_symlink(realDir, linkDir, error);
  CHECK(!error);
  seam::application::ProjectFactory factory{975000U};
  seam::authoring::InterchangeService service;
  // The admitted path resolves deterministically to the real directory;
  // the draft identity and recorded sourcePath are the canonical location,
  // identical to importing the real path directly.
  auto viaLink = service.importFile(linkDir / "song.ustx", factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  auto direct = service.importFile(realDir / "song.ustx", factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(viaLink);
  CHECK(direct);
  CHECK(viaLink.value().sourceHash == direct.value().sourceHash);
  CHECK(viaLink.value().sourcePath == direct.value().sourcePath);
}

TEST_CASE("a held import reads the originally opened inode across parent replacement") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-held-parent");
  const auto directoryA = root / "phrase-a";
  const auto directoryB = root / "phrase-b";
  std::filesystem::create_directories(directoryA);
  std::filesystem::create_directories(directoryB);
  writeText(directoryA / "song.ustx", kUstx);
  writeText(directoryB / "song.ustx", std::string{kUstx} + "# different parent\n");
  seam::application::ProjectFactory factory{976000U};
  seam::authoring::InterchangeService service;
  const auto heldPath = directoryA / "song.ustx";
  // Baseline hash of the bytes the held import must retain.
  auto baseline = service.importFile(heldPath, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(baseline);
  // Swap the parent mid-admission: the descriptor pins the originally opened
  // inode, so the read still returns the original bytes rather than the
  // replacement directory's contents.
  auto injected = service.importFile(heldPath, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx},
      {}, {},
      [&](seam::core::HeldReadStage stage) -> seam::core::Result<void> {
        if (stage == seam::core::HeldReadStage::Opened) {
          const auto parked = root / "phrase-a-parked";
          std::filesystem::rename(directoryA, parked);
          std::filesystem::rename(directoryB, directoryA);
        }
        return seam::core::success();
      });
  CHECK(injected);
  CHECK(injected.value().sourceHash == baseline.value().sourceHash);
}

TEST_CASE("a held import rejects in-place mutation during the read") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-held-mutate");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  seam::application::ProjectFactory factory{977000U};
  seam::authoring::InterchangeService service;
  auto injected = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx},
      {}, {},
      [&](seam::core::HeldReadStage stage) -> seam::core::Result<void> {
        if (stage == seam::core::HeldReadStage::ContentRead) {
          // Rewrite the same inode with different-length content; the
          // post-read identity check must reject rather than admit.
          writeText(source, std::string{kUstx} + "# mutated in place\n");
        }
        return seam::core::success();
      });
  CHECK(!injected);
  CHECK(injected.error().code == seam::core::ErrorCode::Conflict);
}

TEST_CASE("a held import rejects a same-size mutation with a restored mtime") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-held-ctime");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  seam::application::ProjectFactory factory{978000U};
  seam::authoring::InterchangeService service;
  auto injected = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx},
      {}, {},
      [&](seam::core::HeldReadStage stage) -> seam::core::Result<void> {
        if (stage == seam::core::HeldReadStage::ContentRead) {
          // Rewrite the SAME inode with SAME-length content, then restore
          // the original mtime. ctime is kernel-maintained and cannot be
          // restored, so the post-read identity check must still reject.
          struct stat info {};
          CHECK(::stat(source.c_str(), &info) == 0);
          std::string mutated{kUstx};
          mutated[0] = (mutated[0] == 'u') ? 'v' : 'u';
          writeText(source, mutated);
#ifdef __APPLE__
          const timespec times[2] = {info.st_atimespec, info.st_mtimespec};
#else
          const timespec times[2] = {info.st_atim, info.st_mtim};
#endif
          CHECK(::utimensat(AT_FDCWD, source.c_str(), times, 0) == 0);
        }
        return seam::core::success();
      });
  CHECK(!injected);
  CHECK(injected.error().code == seam::core::ErrorCode::Conflict);
}

TEST_CASE("a held import rejects a FIFO without blocking the admission") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-fifo");
  const auto fifo = root / "song.ustx";
  CHECK(::mkfifo(fifo.c_str(), 0600) == 0);
  seam::application::ProjectFactory factory{979000U};
  seam::authoring::InterchangeService service;
  // A FIFO with no writer must not block inside open: run the import on a
  // detached worker and bound the wait. A hang fails the case without
  // stalling the suite; the detached thread is abandoned only on failure.
  std::atomic<int> outcome{0};  // 0 pending, 1 rejected, 2 imported
  std::thread worker([&] {
    auto imported = service.importFile(fifo, factory,
        seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
    outcome.store(imported ? 2 : 1, std::memory_order_release);
  });
  worker.detach();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (outcome.load(std::memory_order_acquire) == 0
         && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(outcome.load(std::memory_order_acquire) == 1);
}

TEST_CASE("a held import closes its descriptor when the admission throws") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-held-throw");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  const auto openDescriptors = [] {
    std::error_code error;
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/fd", error)) {
      static_cast<void>(entry);
      ++count;
    }
    return count;
  };
  const auto before = openDescriptors();
  seam::application::ProjectFactory factory{980100U};
  seam::authoring::InterchangeService service;
  try {
    static_cast<void>(service.importFile(source, factory,
        seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx},
        {}, {},
        [](seam::core::HeldReadStage) -> seam::core::Result<void> {
          throw std::runtime_error{"injected admission failure"};
        }));
    CHECK(false);  // The injector must propagate.
  } catch (const std::runtime_error&) {
  }
  CHECK(openDescriptors() == before);
}
#endif  // _WIN32 (POSIX-only held-admission cases)

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

#ifndef _WIN32
// POSIX-only: reparse-point semantics are pending Windows platform evidence.
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
  CHECK(!error);
  seam::authoring::InterchangeService service;
  auto exported = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Ustx,
                                                .destination = link});
  CHECK(!exported);
  std::ifstream input(real, std::ios::binary);
  const std::string after{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  CHECK(after == "pre-existing\n");
}
#endif  // _WIN32

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
