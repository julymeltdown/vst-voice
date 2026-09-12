#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/authoring/japanese_reading_capture.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"

TEST_CASE("reading capture binds contextual words to notes and guards source generation and explicit hints") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam; using namespace seam::authoring;
  const auto root = test::support::temporaryDirectory("reading-capture");
  std::filesystem::create_directory(root / "dictionary");
  JapaneseReadingResourceSpec spec{root / "reader", core::sha256Hex("helper"), std::string(40U, 'a'), root / "dictionary", {}};
  CHECK(core::durableAtomicWriteText(spec.executable, "helper"));
  for (std::size_t i = 0U; i < 4U; ++i) {
    CHECK(core::durableAtomicWriteText(spec.dictionaryDirectory / kJapaneseDictionaryFiles[i], "dictionary"));
    spec.dictionarySha256[i] = core::sha256Hex("dictionary");
  }
  const auto verified = VerifiedJapaneseReadingResource::verify(spec); CHECK(verified);
  const auto staged = StagedJapaneseReadingResource::prepare(verified.value(), root); CHECK(staged);
  const auto identity = staged.value().resource().identity();
  application::ProjectFactory factory{399000U}; auto project = factory.createProject("Reading capture");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{3840});
  std::vector<domain::NoteId> ids;
  for (const auto* surface : {U"学", U"校", U"へ"}) {
    auto [lyric, note] = factory.makeNote(time::Tick{static_cast<std::int64_t>(ids.size()) * 960}, time::Tick{960}, 60U, surface, domain::Language::Japanese);
    ids.push_back(note.id); project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
  }
  project.findRegion(region)->notes.front().phoneticHint = "g a";
  application::EditorSession session{project};
  const auto capture = JapaneseReadingCapture::prepare(session, region, ids, staged.value()); CHECK(capture);
  CHECK(capture.value().source() == "学校へ"); CHECK(capture.value().owners().size() == 3U);
  phonemizer::JapaneseReadingResult reading{identity, core::sha256Hex("学校へ"), {
      {0U, 6U, "学校", phonemizer::JapaneseReadingStatus::Known, "ガッコウ", "ガッコー"},
      {6U, 3U, "へ", phonemizer::JapaneseReadingStatus::Known, "ヘ", "エ"}}};
  const auto bound = capture.value().bind(reading); CHECK(bound); CHECK(bound.value().bindings.size() == 2U);
  CHECK(bound.value().phoneProjections.size() == 2U);
  CHECK(bound.value().phoneProjections[0].phones == (std::vector<std::string>{"g", "a", "cl", "k", "o", "o"}));
  CHECK(bound.value().phoneProjections[1].phones == (std::vector<std::string>{"e"}));
  CHECK(bound.value().bindings[0].notes == (std::vector<domain::NoteId>{ids[0], ids[1]}));
  CHECK(bound.value().bindings[0].crossesLyrics); CHECK(bound.value().bindings[0].touchesExplicitHint);
  CHECK(!bound.value().bindings[0].crossesUnownedText); CHECK(!bound.value().bindings[1].touchesExplicitHint);
  CHECK(session.project() == project); CHECK(!session.canUndo()); CHECK(capture.value().matches(session, region, identity));
  application::EditorSession identical{project}; CHECK(!capture.value().matches(identical, region, identity));
  auto changedIdentity = identity; changedIdentity.helperSha256[0] = 'f'; CHECK(!capture.value().matches(session, region, changedIdentity));
  session.project().findRegion(region)->notes.front().phoneticHint = "k a"; CHECK(!capture.value().matches(session, region, identity));
  session.project().findRegion(region)->notes.front().phoneticHint = "g a";
  CHECK(session.replaceProject(project)); CHECK(!capture.value().matches(session, region, identity));
  CHECK(capture.value().bind(reading)); // Immutable work remains readable, not publishable into changed state.
  const std::vector<domain::NoteId> skipped{ids[0], ids[2]}, repeated{ids[0], ids[0]};
  CHECK(!JapaneseReadingCapture::prepare(session, region, skipped, staged.value()));
  CHECK(!JapaneseReadingCapture::prepare(session, region, repeated, staged.value()));
  session.project().findRegion(region)->notes[1].startTick = time::Tick{480};
  CHECK(!JapaneseReadingCapture::prepare(session, region, ids, staged.value()));
  CHECK(session.replaceProject(project));
  auto* notes = &session.project().findRegion(region)->notes;
  (*notes)[1].lyricTokenId = (*notes)[0].lyricTokenId;
  (*notes)[0].articulation = domain::NoteArticulation::Legato; (*notes)[1].articulation = domain::NoteArticulation::Legato;
  const auto melisma = JapaneseReadingCapture::prepare(session, region, ids, staged.value()); CHECK(melisma);
  CHECK(melisma.value().source() == "学へ"); CHECK(melisma.value().owners()[0].byteOffset == melisma.value().owners()[1].byteOffset);
  (*notes)[1].startTick = time::Tick{1080}; (*notes)[2].startTick = time::Tick{2160};
  const auto rests = JapaneseReadingCapture::prepare(session, region, ids, staged.value()); CHECK(rests);
  CHECK(rests.value().source() == "学 学 へ");
#endif
}

TEST_CASE("validated single-note reading Apply persists phones and identity with exact undo and reload") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam; using namespace seam::authoring;
  const auto root = test::support::temporaryDirectory("reading-apply"); auto staged = [&]() -> core::Result<StagedJapaneseReadingResource> {
    std::filesystem::create_directory(root / "dictionary");
    JapaneseReadingResourceSpec spec{SEAM_READING_CAPTURE_PROCESS_PROBE, {}, std::string(40U, 'a'), root / "dictionary", {}};
    auto digest = core::sha256File(spec.executable, 64U * 1024U * 1024U); if (!digest) return core::Result<StagedJapaneseReadingResource>{digest.error()}; spec.executableSha256 = digest.value();
    for (std::size_t i = 0U; i < 4U; ++i) { CHECK(core::durableAtomicWriteText(spec.dictionaryDirectory / kJapaneseDictionaryFiles[i], "dictionary")); spec.dictionarySha256[i] = core::sha256Hex("dictionary"); }
    const auto verified = VerifiedJapaneseReadingResource::verify(spec); if (!verified) return core::Result<StagedJapaneseReadingResource>{verified.error()};
    return StagedJapaneseReadingResource::prepare(verified.value(), root);
  }(); CHECK(staged);
  application::ProjectFactory factory{431000U}; auto project = factory.createProject("Reading Apply"); const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 60U, U"漢", domain::Language::Japanese);
  const auto noteId = note.id; project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
  application::EditorSession session{project}; const std::vector<domain::NoteId> selected{noteId};
  const auto capture = JapaneseReadingCapture::prepare(session, region, selected, std::move(staged.value())); CHECK(capture);
  auto review = capture.value().read(); CHECK(review); CHECK(review.value().phoneProjections[0].phones == (std::vector<std::string>{"k", "a", "N"}));
  CHECK(capture.value().apply(std::move(review.value()), session)); CHECK(session.revision() == 1U); CHECK(session.canUndo());
  CHECK(session.project().findRegion(region)->findNote(noteId)->phoneticHint == "k a N");
  CHECK(session.project().findRegion(region)->performance.pronunciation); CHECK(session.project().findRegion(region)->performance.pronunciation->resolverId == "open-jtalk-mecab-naist");
  const auto phones = phonemizer::resolveJapanesePronunciation(*session.project().findRegion(region)); CHECK(phones); CHECK(phones.value().pronunciation.tokens.size() == 3U);
  CHECK(phones.value().pronunciation.tokens[0].symbol == "k"); CHECK(phones.value().pronunciation.tokens[2].symbol == "N"); CHECK(session.project().findRegion(region)->findLyric(lyric.id)->surface == U"漢");
  CHECK(formats::ProjectJsonCodec{}.save(session.project(), root / "applied.seam")); const auto reloaded = formats::ProjectJsonCodec{}.load(root / "applied.seam"); CHECK(reloaded); CHECK(reloaded.value() == session.project());
  CHECK(session.undo()); CHECK(session.project() == project); CHECK(session.redo()); CHECK(session.project().findRegion(region)->findNote(noteId)->phoneticHint == "k a N");
#endif
}
