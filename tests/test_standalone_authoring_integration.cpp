#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/standalone/authoring_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {
using namespace std::chrono_literals;

std::unique_ptr<seam::standalone::AuthoringSession> makeSession(
    seam::native_ui::EditorHostCallbacks callbacks = {}) {
  auto created = seam::standalone::AuthoringSession::create(
      seam::standalone::AuthoringSessionConfig{
          .cacheRoot = seam::test::support::temporaryDirectory("standalone-authoring-cache"),
          .voicebankRoots = {seam::voicebank::VoicebankSearchRoot{
              .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
              .kind = seam::voicebank::VoicebankRootKind::Development,
          }},
          .sampleRate = 48000U,
          .outputChannels = 2U,
          .bindFirstAvailableVoicebank = true,
          .allowDevelopmentVoicebanks = true,
      },
      std::move(callbacks));
  CHECK(created);
  return std::move(created).value();
}

std::shared_ptr<const seam::authoring::PublishedProjectAudio> waitReady(
    seam::standalone::AuthoringSession& session, std::uint64_t revision) {
  for (int attempt = 0; attempt < 1200; ++attempt) {
    auto audio = session.runtime().renderer().latest();
    if (audio != nullptr && audio->projectRevision == revision &&
        audio->state == seam::authoring::RenderState::Ready &&
        !audio->result.interleaved.empty()) {
      return audio;
    }
    std::this_thread::sleep_for(5ms);
  }
  return session.runtime().renderer().latest();
}
}  // namespace

TEST_CASE("standalone authoring session renders visible project through production voicebank") {
  std::size_t microscopePlaybackCount = 0U;
  auto session = makeSession(seam::native_ui::EditorHostCallbacks{
      .playMicroscopeSample =
          [&microscopePlaybackCount](const seam::voicebank::Unit&,
                                      const seam::voicebank::AudioBuffer&)
          -> seam::core::Result<void> {
        ++microscopePlaybackCount;
        return seam::core::success();
      },
  });
  CHECK(session->runtime().document().session().project().name() == "Untitled");
  CHECK(session->runtime().voicebanks().resolveTrack(
      session->runtime().document().session().project(), session->trackId()).resolved());

  auto [lyric, note] = session->runtime().document().factory().makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 64U, U"こ",
      seam::domain::Language::Japanese);
  const auto noteId = note.id;
  CHECK(session->runtime().execute(
      std::make_unique<seam::application::AddNoteCommand>(
          session->regionId(), std::move(lyric), std::move(note))));
  const auto firstRevision = session->runtime().document().session().revision();
  const auto first = waitReady(*session, firstRevision);
  CHECK(first != nullptr);
  CHECK(first->state == seam::authoring::RenderState::Ready);
  CHECK(!first->result.interleaved.empty());
  CHECK(!first->result.phraseContentHashes.empty());
  const auto firstHash = first->result.phraseContentHashes.front();

  CHECK(session->controller().openSampleMicroscope(
      seam::domain::PhonemeKey{.noteId = noteId, .ordinal = 0U}));
  CHECK(session->controller().sampleMicroscopeOpen());
  CHECK(session->controller().sampleMicroscope() != nullptr);
  CHECK(!session->controller().sampleMicroscope()->waveform().empty());
  CHECK(session->controller().pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{0.0, 0.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 2,
  }));
  CHECK(microscopePlaybackCount == 1U);
  session->controller().closeSampleMicroscope();
  CHECK(!session->controller().sampleMicroscopeOpen());

  session->controller().resize(1280.0, 720.0);
  session->runtime().document().session().selection().selectOnly(noteId);
  CHECK(session->controller().keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::Up,
      .modifiers = {},
      .repeat = false,
  }));
  const auto secondRevision = session->runtime().document().session().revision();
  CHECK(secondRevision == firstRevision + 1U);
  const auto second = waitReady(*session, secondRevision);
  CHECK(second != nullptr);
  CHECK(second->state == seam::authoring::RenderState::Ready);
  CHECK(!second->result.phraseContentHashes.empty());
  CHECK(second->result.phraseContentHashes.front() != firstHash);
}

TEST_CASE("standalone transport emits production renderer audio") {
  auto session = makeSession();
  auto [lyric, note] = session->runtime().document().factory().makeNote(
      seam::time::Tick{0}, seam::time::Tick{1920}, 64U, U"こ",
      seam::domain::Language::Japanese);
  CHECK(session->runtime().execute(
      std::make_unique<seam::application::AddNoteCommand>(
          session->regionId(), std::move(lyric), std::move(note))));
  const auto revision = session->runtime().document().session().revision();
  CHECK(waitReady(*session, revision) != nullptr);
  CHECK(session->runtime().transport().play());
  auto& ring = session->runtime().transport().ringBuffer();
  for (int attempt = 0; attempt < 200 && ring.availableReadFrames() == 0U; ++attempt) {
    std::this_thread::sleep_for(2ms);
  }
  CHECK(ring.availableReadFrames() > 0U);
  std::vector<float> samples(512U * 2U, 0.0F);
  CHECK(ring.readFrames(samples) > 0U);
  double energy = 0.0;
  for (const auto value : samples) energy += std::abs(value);
  CHECK(energy > 0.01);
}

TEST_CASE("standalone forwards native voicebank browser selection to its host") {
  std::string selectedId;
  auto session = makeSession(seam::native_ui::EditorHostCallbacks{
      .selectVoicebank = [&selectedId](std::string_view id, std::string_view,
                                       std::string_view) {
        selectedId = std::string{id};
        return seam::core::success();
      },
  });
  seam::authoring::VoicebankBrowserModel browser{/*allowDevelopmentFixtures=*/true};
  browser.rebuild(session->runtime().voicebanks().candidates());
  CHECK(!browser.cards().empty());
  session->controller().setVoicebankCards(browser.cards());
  CHECK(session->controller().keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::V, .modifiers = {}, .repeat = false}));
  session->controller().rebuildAccessibilityTree();
  CHECK(session->controller().dispatchAccessibility(
      "voicebank.card.0", seam::native_ui::SemanticAction::Activate));
  CHECK(selectedId == browser.cards().front().id);
}

TEST_CASE("development fixture startup clears provisional missing-bank state") {
  auto session = makeSession();
  const auto diagnostics = session->runtime().diagnostics();
  CHECK(std::none_of(diagnostics.begin(), diagnostics.end(),
                     [](const auto& diagnostic) {
                       return diagnostic.code == "BANK_MISSING";
                     }));
}
