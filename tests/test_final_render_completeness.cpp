#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/core/sha256.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"
#ifdef SEAM_COMPLETENESS_CLAP
#include "seam/clap_editor/editor_runtime.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <thread>

namespace {
using namespace seam;
using time::Tick;

struct CompletenessFixture {
  std::filesystem::path root{test::support::temporaryDirectory("final-completeness")};
  application::ProjectFactory factory{981100U};
  domain::Project project{factory.createProject("Complete song")};
  domain::TrackId track{factory.addVocalTrack(project, "Singer")};
  domain::RegionId region{factory.addRegion(project, track, "Two phrases", Tick{0}, Tick{3840})};
  voicebank::Manifest bank{test::support::makeManifest({
      test::support::makeUnit("a", {"a"}, "audio/a.wav", 69, voicebank::UnitKind::Sustain),
      test::support::makeUnit("i", {"i"}, "audio/i.wav", 69, voicebank::UnitKind::Sustain)})};

  CompletenessFixture() {
    std::filesystem::create_directories(root / "audio");
    for (const auto& unit : bank.units) {
      CHECK(voicebank::writeMonoPcm16Wav(root / unit.audioPath, 48000U,
          test::support::sineWave(48000U, 440.0, 0.5, 0.05F)));
    }
    auto* singer = project.findVocalTrack(track);
    singer->voicebank = {bank.id, bank.version, std::string(64U, 'a')};
    singer->styleSelection = {domain::VoiceStyleOrigin::Explicit, "original"};
    addNote(region, Tick{0}, U"あ", "a");
    addNote(region, Tick{2400}, U"い", "i");
    CHECK(project.validate());
    const auto phrases = rendering::PhraseSegmenter{}.segment(*project.findRegion(region));
    CHECK(phrases); CHECK(phrases.value().size() == 2U);
  }

  void addNote(domain::RegionId id, Tick start, std::u32string text, std::string hint) {
    auto [lyric, note] = factory.makeNote(start, Tick{480}, 69U, std::move(text), domain::Language::Japanese);
    note.phoneticHint = std::move(hint);
    project.findRegion(id)->lyrics.push_back(std::move(lyric));
    project.findRegion(id)->notes.push_back(std::move(note));
  }

  std::vector<rendering::TrackVoicebankSource> sources() const {
    std::vector<rendering::TrackVoicebankSource> result;
    for (const auto& singer : project.vocalTracks()) {
      result.push_back({.trackId = singer.id, .manifest = bank, .bankRoot = root,
          .contentHash = singer.voicebank.contentHash});
    }
    return result;
  }

  core::Result<rendering::ProjectRenderResult> render(rendering::RenderQuality quality) const {
    return rendering::ProductionProjectRenderer{}.render(project, sources(), track, region, 1U, 48000U, quality);
  }

  domain::TrackId addBacking(std::string filename = "audio/a.wav") {
    const auto id = factory.nextTrackId();
    project.audioTracks().push_back(domain::AudioTrack{
        .id = id, .name = "Backing", .mediaPath = (root / filename).string(),
        .startTick = Tick{0}, .outputRoute = {.bus = domain::BusId{1U},
            .matrix = domain::RoutingMatrix::monoToStereo()}});
    CHECK(project.validate());
    return id;
  }
};

void checkAudible(const rendering::ProjectRenderResult& result) {
  CHECK(std::any_of(result.interleaved.begin(), result.interleaved.end(), [](float x) { return x != 0.0F; }));
}

// Include directories as well as file bytes: a failed publication must not leave
// a receipt, staging tree, backup, or partially created destination behind.
std::map<std::string, std::string> tree(const std::filesystem::path& root) {
  std::map<std::string, std::string> result;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
    const auto relative = entry.path().lexically_relative(root).generic_string();
    if (entry.is_directory()) result.emplace(relative + "/", "directory");
    else {
      std::ifstream input{entry.path(), std::ios::binary}; CHECK(input.good());
      const std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
      CHECK(!input.bad());
      result.emplace(relative, core::sha256Hex(bytes));
    }
  }
  return result;
}

authoring::ExportSettings settings(bool master, bool stems) {
  return {.sampleRate = 48000U, .channels = 2U, .format = voicebank::WavSampleFormat::Float32,
      .includeMaster = master, .includeStems = stems, .replaceExisting = true};
}
}

TEST_CASE("Final rejects a missing sample phrase while Preview retains the successful phrase") {
  CompletenessFixture f;
  const auto complete = f.render(rendering::RenderQuality::Final); CHECK(complete);
  CHECK(complete.value().diagnostics.empty()); CHECK(complete.value().phraseCount == 2U);
  f.bank.units.pop_back();
  const auto preview = f.render(rendering::RenderQuality::Preview); CHECK(preview); checkAudible(preview.value());
  CHECK(preview.value().phraseCount == 1U); CHECK(preview.value().diagnostics.size() == 1U);
  const auto& diagnostic = preview.value().diagnostics.front();
  CHECK(diagnostic.code == core::ErrorCode::NotFound);
  CHECK(diagnostic.trackId == f.track); CHECK(diagnostic.regionId == f.region); CHECK(!diagnostic.phraseId.empty());
  const auto final = f.render(rendering::RenderQuality::Final);
  CHECK(!final); CHECK(final.error().code == diagnostic.code);
  CHECK(final.error().message.find("incomplete") != std::string::npos);
  CHECK(final.error().context.find(f.track.toString()) != std::string::npos);
  CHECK(final.error().context.find(f.region.toString()) != std::string::npos);
  CHECK(final.error().context.find(diagnostic.phraseId) != std::string::npos);
}

TEST_CASE("Final rejects a conflicting sample phrase even when another singer succeeds") {
  CompletenessFixture f;
  const auto badTrack = f.factory.addVocalTrack(f.project, "Unresolved legacy singer");
  const auto badRegion = f.factory.addRegion(f.project, badTrack, "Conflict", Tick{0}, Tick{960});
  f.addNote(badRegion, Tick{0}, U"あ", "a");
  auto* singer = f.project.findVocalTrack(badTrack);
  singer->voicebank = {f.bank.id, f.bank.version, std::string(64U, 'a')};
  singer->styleSelection = {domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution, ""};
  CHECK(f.project.validate());
  const auto preview = f.render(rendering::RenderQuality::Preview); CHECK(preview); checkAudible(preview.value());
  CHECK(preview.value().diagnostics.size() == 1U);
  CHECK(preview.value().diagnostics.front().code == core::ErrorCode::Conflict);
  const auto final = f.render(rendering::RenderQuality::Final);
  CHECK(!final); CHECK(final.error().code == core::ErrorCode::Conflict);
  CHECK(final.error().context.find(badTrack.toString()) != std::string::npos);
  CHECK(final.error().context.find(badRegion.toString()) != std::string::npos);
}

TEST_CASE("Final rejects missing and changed backing media without rejecting muted or unsoloed tracks") {
  for (const auto fault : {0U, 1U, 2U, 3U}) {
    CompletenessFixture f;
    const auto badTrack = f.addBacking();
    auto& backing = f.project.audioTracks().back();
    if (fault == 0U) backing.mediaPath.clear();
    if (fault == 1U) backing.mediaPath = (f.root / "missing.wav").string();
    if (fault == 2U) backing.mediaHash = std::string(64U, '0');
    if (fault == 3U) backing.sourceFrameCount = 24001U;
    CHECK(f.project.validate());
    const auto preview = f.render(rendering::RenderQuality::Preview); CHECK(preview); checkAudible(preview.value());
    CHECK(preview.value().diagnostics.size() == 1U);
    CHECK(preview.value().diagnostics.front().trackId == badTrack);
    const auto final = f.render(rendering::RenderQuality::Final); CHECK(!final);
    CHECK(final.error().code == preview.value().diagnostics.front().code);
    CHECK(final.error().context.find(badTrack.toString()) != std::string::npos);
    backing.muted = true;
    const auto muted = f.render(rendering::RenderQuality::Final); CHECK(muted); CHECK(muted.value().diagnostics.empty());
    backing.muted = false; f.project.findVocalTrack(f.track)->solo = true;
    const auto unsoloed = f.render(rendering::RenderQuality::Final); CHECK(unsoloed); CHECK(unsoloed.value().diagnostics.empty());
    CHECK(muted.value().interleaved == unsoloed.value().interleaved);
  }
}

TEST_CASE("single-file export refuses partial phrases or backing media and preserves prior files") {
  for (const bool failedPhrase : {true, false}) {
    CompletenessFixture f;
    const authoring::ExportService exporter;
    const auto existing = f.root / "song.wav";
    CHECK(exporter.exportProject(f.project, f.sources(), f.track, f.region, 1U, existing));
    if (failedPhrase) f.bank.units.pop_back(); else f.addBacking("missing.wav");
    const auto before = tree(f.root);
    for (const auto& destination : {existing, f.root / "new.wav"}) {
      const auto result = exporter.exportProject(f.project, f.sources(), f.track, f.region, 2U, destination);
      CHECK(!result); CHECK(result.error().message.find("incomplete") != std::string::npos);
      CHECK(tree(f.root) == before);
    }
  }
}

TEST_CASE("Export Set refuses partial masters and cleans an already rendered stem on failure") {
  for (const bool failedPhrase : {true, false}) {
    for (const bool master : {true, false}) {
      CompletenessFixture f;
      const authoring::ExportService exporter;
      const auto options = settings(master, !master);
      const auto existing = f.root / "song-set";
      CHECK(exporter.exportSet(f.project, f.sources(), f.track, f.region, 1U, existing, options));
      if (failedPhrase) f.bank.units.pop_back(); else f.addBacking("missing.wav");
      const auto before = tree(f.root);
      for (const auto& destination : {existing, f.root / "new-set"}) {
        std::uint64_t completed = 0U;
        const auto result = exporter.exportSet(f.project, f.sources(), f.track, f.region, 2U, destination, options,
            [&](const authoring::ExportProgress& progress) { completed = std::max(completed, progress.completedFiles); });
        CHECK(!result);
        if (!master && !failedPhrase) CHECK(completed == 1U);
        CHECK(tree(f.root) == before);
      }
    }
  }
}

TEST_CASE("direct rendered export rejects diagnostic-bearing PCM before staging or destination changes") {
  CompletenessFixture f;
  const authoring::ExportService exporter;
  const auto complete = f.render(rendering::RenderQuality::Final); CHECK(complete);
  const auto existing = f.root / "song.wav";
  CHECK(exporter.commitRendered(complete.value(), 1U, existing));
  const auto before = tree(f.root);
  for (const auto code : {core::ErrorCode::NotFound, core::ErrorCode::Conflict, core::ErrorCode::Unsupported}) {
    auto incomplete = complete.value();
    incomplete.diagnostics.push_back({.trackId = f.track, .regionId = f.region, .phraseId = "failed-phrase",
        .code = code, .message = "Missing requested content", .context = "frozen source identity"});
    for (const auto& destination : {existing, f.root / "never-created/song.wav"}) {
      const auto result = exporter.commitRendered(incomplete, 2U, destination);
      CHECK(!result); CHECK(result.error().code == code);
      CHECK(result.error().message.find("incomplete") != std::string::npos);
      CHECK(result.error().context.find("failed-phrase") != std::string::npos);
      CHECK(result.error().context.find("frozen source identity") != std::string::npos);
      CHECK(tree(f.root) == before);
    }
  }
}

TEST_CASE("unsupported sample Formant remains a hard failure in Preview Final and export") {
  CompletenessFixture f;
  f.addBacking();
  CHECK(f.project.findRegion(f.region)->formantAutomation.upsert({Tick{0}, 7.0F}));
  for (const auto quality : {rendering::RenderQuality::Preview, rendering::RenderQuality::Final}) {
    const auto rendered = f.render(quality); CHECK(!rendered);
    CHECK(rendered.error().code == core::ErrorCode::Unsupported);
  }
  const auto before = tree(f.root);
  const auto result = authoring::ExportService{}.exportProject(f.project, f.sources(), f.track, f.region, 1U, f.root / "bad.wav");
  CHECK(!result); CHECK(result.error().code == core::ErrorCode::Unsupported); CHECK(tree(f.root) == before);
}

#ifdef SEAM_COMPLETENESS_CLAP
TEST_CASE("CLAP keeps a partial Preview available but refuses incomplete offline audio") {
  CompletenessFixture f;
  f.addBacking("missing.wav");
  CHECK(voicebank::ManifestJsonCodec{}.save(f.bank, f.root / "manifest.json"));
  const std::vector<voicebank::VoicebankSearchRoot> roots{{f.root, voicebank::VoicebankRootKind::Development}};
  const auto scanned = voicebank::VoicebankCatalog{}.scan(roots); CHECK(scanned); CHECK(scanned.value().size() == 1U);
  f.project.findVocalTrack(f.track)->voicebank.contentHash = scanned.value().front().contentHash;
  clap_editor::EditorRuntime runtime{f.project, {}, roots};
  std::shared_ptr<const clap_editor::RenderedPreview> preview;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
  while (std::chrono::steady_clock::now() < deadline) {
    preview = runtime.renderedPreview();
    if (preview && preview->revision == runtime.revision() && preview->status == clap_editor::PreviewStatus::Ready) break;
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(preview); CHECK(preview->revision == runtime.revision()); CHECK(preview->status == clap_editor::PreviewStatus::Ready);
  CHECK(std::any_of(preview->interleaved.begin(), preview->interleaved.end(), [](float x) { return x != 0.0F; }));
  const auto offline = runtime.prepareOfflineRender(std::chrono::seconds{15});
  CHECK(!offline); CHECK(!runtime.offlineRenderReady()); CHECK(!runtime.acquireOfflineRenderedPreview());
}
#endif
