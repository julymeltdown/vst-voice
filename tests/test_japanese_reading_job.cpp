#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/authoring/japanese_reading_job.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/application/project_factory.hpp"
#include <chrono>
#include <thread>

namespace {
template <typename Poll>
seam::core::Result<bool> waitForReadingJob(Poll poll) {
  // The helper has a 10-second wall deadline; resource verification happens
  // outside that interval. This tests lifecycle completion, not cold-start speed.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
  for (;;) {
    auto result = poll();
    if (!result || result.value()) return result;
    if (std::chrono::steady_clock::now() >= deadline)
      throw seam::test::Failure{"Japanese reading job did not retire within 15 seconds"};
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
}

struct Fixture {
  seam::application::ProjectFactory factory{421000U};
  seam::domain::Project project{factory.createProject("Reading job")};
  seam::domain::TrackId track{factory.addVocalTrack(project, "Singer")};
  seam::domain::RegionId region{factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{3840})};
  std::vector<seam::domain::NoteId> notes;
  Fixture() {
    for (const auto* text : {U"学", U"校", U"へ"}) {
      auto [lyric, note] = factory.makeNote(seam::time::Tick{static_cast<std::int64_t>(notes.size()) * 960}, seam::time::Tick{960}, 60U, text, seam::domain::Language::Japanese);
      notes.push_back(note.id); project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
    }
  }
};
seam::core::Result<seam::authoring::StagedJapaneseReadingResource> resource(const std::filesystem::path& root) {
  using namespace seam; using namespace authoring;
  std::filesystem::create_directory(root / "dictionary");
  JapaneseReadingResourceSpec spec{SEAM_READING_CAPTURE_PROCESS_PROBE, {}, std::string(40U, 'a'), root / "dictionary", {}};
  auto digest = core::sha256File(spec.executable, 64U * 1024U * 1024U); if (!digest) return core::Result<StagedJapaneseReadingResource>{digest.error()}; spec.executableSha256 = digest.value();
  for (std::size_t i = 0U; i < 4U; ++i) { CHECK(core::durableAtomicWriteText(spec.dictionaryDirectory / kJapaneseDictionaryFiles[i], "dictionary")); spec.dictionarySha256[i] = core::sha256Hex("dictionary"); }
  auto verified = VerifiedJapaneseReadingResource::verify(spec); if (!verified) return core::Result<StagedJapaneseReadingResource>{verified.error()};
  return StagedJapaneseReadingResource::prepare(verified.value(), root);
}
}

TEST_CASE("Japanese reading job runs staged helper off-thread and adopts only current result") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam; Fixture f; application::EditorSession session{f.project}; const auto root = test::support::temporaryDirectory("reading-job");
  auto staged = resource(root); CHECK(staged); const auto identity = staged.value().resource().identity();
  authoring::JapaneseReadingJob job; CHECK(job.start(session, f.region, f.notes, std::move(staged.value()))); CHECK(job.state() == authoring::JapaneseReadingJob::State::Preparing); CHECK(job.requestId() == 1U);
  const auto ready = waitForReadingJob([&] { return job.poll(session, f.region, identity); });
  if (!ready) throw test::Failure{"Japanese reading failed: " + ready.error().message};
  CHECK(ready.value()); CHECK(job.state() == authoring::JapaneseReadingJob::State::Ready);
  const auto* result = job.current(session, f.region, identity); CHECK(result); CHECK(result->reading.tokens.size() == 2U); CHECK(result->bindings[0].crossesLyrics);
  CHECK(result->bindings[0].notes.size() == 2U); CHECK(result->bindings[1].notes.size() == 1U); CHECK(session.project() == f.project); CHECK(!session.canUndo());
  auto restarted = resource(root); CHECK(restarted); CHECK(job.start(session, f.region, f.notes, std::move(restarted.value()))); // Retired workers can be restarted.
  CHECK(job.requestId() == 2U); job.cancel(); CHECK(job.state() == authoring::JapaneseReadingJob::State::Cancelled); CHECK(job.current(session, f.region, identity) == nullptr);
  CHECK(waitForReadingJob([&] { return job.pollCancelled(); }));
  CHECK(job.pollCancelled()); CHECK(job.state() == authoring::JapaneseReadingJob::State::Cancelled); CHECK(!job.preparing());
  auto stale = resource(root); CHECK(stale); CHECK(job.start(session, f.region, f.notes, std::move(stale.value())));
  const auto replaced = session.replaceProject(f.project); CHECK(replaced); auto stalePoll = job.poll(session, f.region, identity);
  if (stalePoll) {
    CHECK(!stalePoll.value());
    CHECK(!waitForReadingJob([&] { return job.poll(session, f.region, identity); }));
  }
  CHECK(job.state() == authoring::JapaneseReadingJob::State::Failed); CHECK(job.current(session, f.region, identity) == nullptr);
#endif
}

TEST_CASE("Japanese reading job rejects concurrent starts and preserves terminal failure details") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam; Fixture f; application::EditorSession session{f.project}; const auto root = test::support::temporaryDirectory("reading-job-failure");
  auto staged = resource(root); CHECK(staged); const auto identity = staged.value().resource().identity();
  authoring::JapaneseReadingJob job; CHECK(job.start(session, f.region, f.notes, std::move(staged.value())));
  auto second = resource(root); CHECK(second); const auto concurrent = job.start(session, f.region, f.notes, std::move(second.value()));
  CHECK(!concurrent); CHECK(concurrent.error().message.find("Retire") != std::string::npos); CHECK(job.requestId() == 1U);
  job.cancel();
  CHECK(waitForReadingJob([&] { return job.poll(session, f.region, identity); }));
  CHECK(job.poll(session, f.region, identity)); CHECK(job.state() == authoring::JapaneseReadingJob::State::Cancelled);
  auto missing = resource(root); CHECK(missing);
  const auto missingPath = missing.value().resource().spec().executable;
  std::filesystem::permissions(missingPath.parent_path(), std::filesystem::perms::owner_write,
      std::filesystem::perm_options::add);
  CHECK(std::filesystem::remove(missingPath));
  CHECK(job.start(session, f.region, f.notes, std::move(missing.value())));
  CHECK(!waitForReadingJob([&] { return job.poll(session, f.region, identity); }));
  CHECK(job.state() == authoring::JapaneseReadingJob::State::Failed); CHECK(!job.error().empty());
  CHECK(job.poll(session, f.region, identity)); CHECK(job.state() == authoring::JapaneseReadingJob::State::Failed);
  CHECK(!job.current(session, f.region, identity));
#endif
}
