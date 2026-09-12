#include "test_framework.hpp"

#include "seam/native_ui/diagnostic_panel.hpp"
#include "seam/native_ui/diagnostic_search.hpp"
#include "seam/native_ui/diagnostic_search_job.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/application/tempo_commands.hpp"
#include <chrono>
#include <limits>

TEST_CASE("long render failure details remain visible searchable and independently coalesced") {
  using namespace seam;
  authoring::Diagnostic first{.code = "RENDER_FAILED", .messageKey = "render.failed",
      .actions = authoring::DiagnosticRegistry::actions("RENDER_FAILED")};
  first.setDetail(std::string(300U, 'x') + " missing-sample 歌");
  auto second = first; second.setDetail(std::string(300U, 'x') + " different-sample");
  native_ui::DiagnosticPanelModel panel; panel.add(first); panel.add(second);
  CHECK(panel.entries().size() == 2U); CHECK(authoring::DiagnosticRegistry::validate(first));
  const auto search = native_ui::DiagnosticSearchSnapshot::prepare(panel, "missing-sample"); CHECK(search);
  CHECK(search.value().hits().size() == 1U); CHECK(search.value().hits()[0].field == native_ui::DiagnosticSearchField::Detail);
  CHECK(native_ui::presentDiagnostic(first).technicalDetail.find(first.detail) != std::string::npos);
  CHECK(search.value().matches(panel));
  auto changed = first; changed.setDetail("new failure"); panel.clear(); panel.add(changed); panel.add(second);
  CHECK(!search.value().matches(panel));
  panel.clear(); first.setDetail(std::string(4096U, 'x') + "first-tail"); second.setDetail(std::string(4096U, 'x') + "second-tail");
  CHECK(first.detail == second.detail); CHECK(first.detailTruncated); CHECK(second.detailTruncated);
  CHECK(!first.sameIssueAs(second)); panel.add(first); panel.add(second); CHECK(panel.entries().size() == 2U);
}

TEST_CASE("diagnostic search jobs guard document and diagnostic changes without invoking recovery") {
  using namespace seam;
  application::ProjectFactory factory{251000U}; application::EditorSession session{factory.createProject("Diagnostic jobs")};
  native_ui::DiagnosticPanelModel panel; unsigned actions = 0U;
  panel.setActionHandler([&](const authoring::Diagnostic&, authoring::DiagnosticAction) { ++actions; return core::success(); });
  authoring::Diagnostic diagnostic{.code = "MEDIA_MISSING", .messageKey = "media.missing",
      .actions = {authoring::DiagnosticAction::RelinkMedia}};
  panel.add(diagnostic); const auto source = session.project();
  native_ui::DiagnosticSearchJob job;
  const auto retire = [&] {
    auto result = core::success(false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (job.preparing() && std::chrono::steady_clock::now() < deadline) {
      result = job.poll(session, panel); std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    CHECK(!job.preparing()); return result;
  };
  CHECK(job.start(session, panel, "MEDIA")); CHECK(!job.takeReady());
  CHECK(!job.start(session, panel, "missing")); CHECK(retire());
  auto ready = job.takeReady(); CHECK(ready); CHECK(!job.takeReady());
  CHECK(ready->snapshot().hits().size() == 1U); CHECK(ready->snapshot().source().front().affectedIds.empty());
  CHECK(ready->resolve(session, panel, 0U).value() == 0U); CHECK(!ready->resolve(session, panel, 1U));
  CHECK(session.project() == source); CHECK(session.selection().empty()); CHECK(actions == 0U);
  CHECK(job.start(session, panel, "MEDIA")); panel.add(diagnostic);
  CHECK(!retire()); CHECK(job.state() == native_ui::DiagnosticSearchJob::State::Failed);
  CHECK(!ready->resolve(session, panel, 0U)); // Counts changed without any document command.
  CHECK(job.start(session, panel, "MEDIA")); job.cancel(); CHECK(retire());
  CHECK(job.state() == native_ui::DiagnosticSearchJob::State::Cancelled); CHECK(!job.takeReady());
  CHECK(job.start(session, panel, "MEDIA")); CHECK(session.replaceProject(source));
  CHECK(!retire()); CHECK(!job.takeReady());
  CHECK(job.start(session, panel, "MEDIA")); CHECK(retire()); auto current = job.takeReady(); CHECK(current);
  CHECK(session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 90.0)));
  CHECK(!current->resolve(session, panel, 0U));
  CHECK(job.start(session, panel, "MEDIA")); CHECK(retire()); auto closable = job.takeReady(); CHECK(closable);
  closable->close(); CHECK(!closable->resolve(session, panel, 0U));
  CHECK(job.start(session, panel, "MEDIA")); CHECK(retire());
  CHECK(!job.start(session, panel, "")); CHECK(!job.takeReady());
  CHECK(actions == 0U); CHECK(session.selection().empty());
}

TEST_CASE("diagnostic search capture bounds bytes before copying and reports worker decoding failures") {
  using namespace seam;
  native_ui::DiagnosticPanelModel panel;
  authoring::Diagnostic diagnostic{.code = "MEDIA_MISSING", .messageKey = "media.missing",
      .affectedIds = {std::string(65537U, 'a')}, .actions = {authoring::DiagnosticAction::RelinkMedia}};
  panel.add(diagnostic); CHECK(!native_ui::DiagnosticSearchSnapshot::capture(panel));
  panel.clear(); diagnostic.affectedIds = {std::string(1U, static_cast<char>(0xff))}; panel.add(diagnostic);
  application::ProjectFactory factory{261000U}; application::EditorSession session{factory.createProject("Decode failure")};
  native_ui::DiagnosticSearchJob job; CHECK(job.start(session, panel, "MEDIA"));
  auto result = core::success(false);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (job.preparing() && std::chrono::steady_clock::now() < deadline) {
    result = job.poll(session, panel); std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(!job.preparing()); CHECK(!result); CHECK(!job.takeReady()); CHECK(job.state() == native_ui::DiagnosticSearchJob::State::Failed);
}

TEST_CASE("active diagnostic search preserves global issues field provenance and source changes") {
  using namespace seam;
  native_ui::DiagnosticPanelModel panel; unsigned actions = 0U;
  panel.setActionHandler([&](const authoring::Diagnostic&, authoring::DiagnosticAction) { ++actions; return core::success(); });
  authoring::Diagnostic global{.code = "MEDIA_MISSING", .messageKey = "media.missing",
      .actions = {authoring::DiagnosticAction::RelinkMedia}};
  auto scoped = global; scoped.affectedIds = {"🌸媒体-a"};
  panel.add(global); panel.add(scoped);
  const auto code = native_ui::DiagnosticSearchSnapshot::prepare(panel, "MEDIA_MISSING"); CHECK(code);
  CHECK(code.value().hits().size() == 2U); CHECK(code.value().source().front().affectedIds.empty());
  CHECK(code.value().hits()[0].sourceIndex == 0U); CHECK(code.value().hits()[1].sourceIndex == 1U);
  CHECK(code.value().hits()[0].field == native_ui::DiagnosticSearchField::Code);
  const auto id = native_ui::DiagnosticSearchSnapshot::prepare(panel, "媒体"); CHECK(id);
  CHECK(id.value().hits().size() == 1U); CHECK(id.value().hits()[0].sourceIndex == 1U);
  CHECK(id.value().hits()[0].field == native_ui::DiagnosticSearchField::AffectedId);
  CHECK(id.value().hits()[0].fieldIndex == 0U); CHECK(id.value().hits()[0].firstMatch == 1U);
  CHECK(id.value().hits()[0].matchedText == U"🌸媒体-a");
  const auto title = native_ui::DiagnosticSearchSnapshot::prepare(panel, "Backing media"); CHECK(title);
  CHECK(title.value().hits().size() == 2U); CHECK(title.value().hits()[0].field == native_ui::DiagnosticSearchField::Title);
  CHECK(code.value().matches(panel)); CHECK(actions == 0U);
  panel.add(global); CHECK(!code.value().matches(panel)); // Counts can change without a score revision.
  auto refreshed = native_ui::DiagnosticSearchSnapshot::prepare(panel, "media"); CHECK(refreshed); CHECK(refreshed.value().matches(panel));
  panel.dismiss(0U); CHECK(!refreshed.value().matches(panel)); CHECK(actions == 0U);
  CHECK(code.value().source().front().occurrenceCount == 1U); // Captured data remains immutable.
}

TEST_CASE("diagnostic search rejects invalid and excessive data even after an early match") {
  using namespace seam;
  native_ui::DiagnosticPanelModel panel;
  authoring::Diagnostic diagnostic{.code = "MEDIA_MISSING", .messageKey = "media.missing",
      .affectedIds = {std::string(1U, static_cast<char>(0xff))}, .actions = {authoring::DiagnosticAction::CopyDiagnostic}};
  panel.add(diagnostic);
  CHECK(!native_ui::DiagnosticSearchSnapshot::prepare(panel, "MEDIA"));
  panel.clear(); diagnostic.affectedIds = {std::string(65537U, 'a')}; panel.add(diagnostic);
  CHECK(!native_ui::DiagnosticSearchSnapshot::prepare(panel, "MEDIA"));
  panel.clear(); diagnostic.affectedIds.clear(); panel.add(diagnostic);
  const auto action = native_ui::DiagnosticSearchSnapshot::prepare(panel, "Copy details"); CHECK(action);
  CHECK(action.value().hits().size() == 1U); CHECK(action.value().hits()[0].field == native_ui::DiagnosticSearchField::Action);
  CHECK(!native_ui::DiagnosticSearchSnapshot::prepare(panel, ""));
  CHECK(!native_ui::DiagnosticSearchSnapshot::prepare(panel, std::string(257U, 'a')));
  std::stop_source stop; stop.request_stop(); CHECK(!native_ui::DiagnosticSearchSnapshot::prepare(panel, "MEDIA", stop.get_token()));
  panel.clear(); diagnostic.actions.assign(33U, authoring::DiagnosticAction::CopyDiagnostic); panel.add(diagnostic);
  CHECK(!native_ui::DiagnosticSearchSnapshot::prepare(panel, "MEDIA"));
  panel.clear(); diagnostic.actions = {authoring::DiagnosticAction::CopyDiagnostic};
  for (unsigned i = 0U; i < 65U; ++i) {
    diagnostic.messageKey = "message." + std::to_string(i); diagnostic.affectedIds = {std::string(65536U, 'a')}; panel.add(diagnostic);
  }
  CHECK(!native_ui::DiagnosticSearchSnapshot::prepare(panel, "MEDIA"));
}

TEST_CASE("diagnostic panel coalesces typed errors and validates actions") {
  seam::native_ui::DiagnosticPanelModel panel;
  panel.add(seam::authoring::Diagnostic{
      .code = "MEDIA_MISSING",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "media.missing",
      .actions = {seam::authoring::DiagnosticAction::RelinkMedia},
  });
  panel.add(seam::authoring::Diagnostic{
      .code = "MEDIA_MISSING",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "media.missing",
      .actions = {seam::authoring::DiagnosticAction::RelinkMedia},
      .occurrenceCount = 2U,
  });
  CHECK(panel.entries().size() == 1U);
  CHECK(panel.entries().front().diagnostic.occurrenceCount == 3U);
  CHECK(panel.errorCount() == 1U);
  CHECK(panel.activate(0U, seam::authoring::DiagnosticAction::RelinkMedia));
  CHECK(!panel.activate(0U, seam::authoring::DiagnosticAction::Retry));
}

TEST_CASE("diagnostic coalescing preserves affected scope severity and recovery actions") {
  using namespace seam;
  native_ui::DiagnosticPanelModel panel;
  authoring::Diagnostic first{.code = "MEDIA_MISSING", .severity = authoring::DiagnosticSeverity::Warning,
      .messageKey = "media.missing", .affectedIds = {"media-a"}, .actions = {authoring::DiagnosticAction::RelinkMedia}};
  auto second = first; second.affectedIds = {"media-b"};
  auto critical = first; critical.severity = authoring::DiagnosticSeverity::Critical;
  auto copy = first; copy.actions = {authoring::DiagnosticAction::CopyDiagnostic};
  panel.add(first); panel.add(second); panel.add(critical); panel.add(copy);
  CHECK(panel.entries().size() == 4U); CHECK(panel.hasBlockingIssue()); CHECK(panel.errorCount() == 1U);
  CHECK(panel.entries()[0].diagnostic.affectedIds == first.affectedIds);
  CHECK(panel.entries()[1].diagnostic.affectedIds == second.affectedIds);
  CHECK(panel.entries()[2].diagnostic.severity == authoring::DiagnosticSeverity::Critical);
  CHECK(panel.entries()[3].diagnostic.actions == copy.actions);
  std::vector<std::string> received;
  panel.setActionHandler([&](const authoring::Diagnostic& diagnostic, authoring::DiagnosticAction) {
    received = diagnostic.affectedIds; return core::success();
  });
  CHECK(panel.activate(1U, authoring::DiagnosticAction::RelinkMedia)); CHECK(received == second.affectedIds);
  CHECK(!panel.activate(3U, authoring::DiagnosticAction::RelinkMedia)); CHECK(received == second.affectedIds);
  second.occurrenceCount = 3U; panel.add(second);
  CHECK(panel.entries().size() == 4U); CHECK(panel.entries()[1].diagnostic.occurrenceCount == 4U);
  panel.dismiss(2U); CHECK(!panel.hasBlockingIssue());
  CHECK(panel.entries()[1].diagnostic.affectedIds == second.affectedIds);
}

TEST_CASE("diagnostic occurrence counts saturate without wrapping to an invalid count") {
  using namespace seam;
  native_ui::DiagnosticPanelModel panel;
  authoring::Diagnostic diagnostic{.code = "MEDIA_MISSING", .messageKey = "media.missing",
      .actions = {authoring::DiagnosticAction::RelinkMedia},
      .occurrenceCount = std::numeric_limits<std::size_t>::max() - 1U};
  panel.add(diagnostic); diagnostic.occurrenceCount = 2U; panel.add(diagnostic);
  CHECK(panel.entries().size() == 1U);
  CHECK(panel.entries().front().diagnostic.occurrenceCount == std::numeric_limits<std::size_t>::max());
  diagnostic.occurrenceCount = 1U; panel.add(diagnostic);
  CHECK(panel.entries().front().diagnostic.occurrenceCount == std::numeric_limits<std::size_t>::max());
  CHECK(authoring::DiagnosticRegistry::validate(panel.entries().front().diagnostic));
}
