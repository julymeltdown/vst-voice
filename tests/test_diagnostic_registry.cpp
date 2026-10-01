#include "test_framework.hpp"

#include "seam/authoring/diagnostic.hpp"

#include <algorithm>

TEST_CASE("diagnostic detail remains bounded display safe and separate from its message key") {
  using namespace seam;
  auto diagnostic = authoring::DiagnosticRegistry::fromError(core::Error{core::ErrorCode::IoError, std::string(4097U, 'a')});
  CHECK(diagnostic.messageKey == "generic.failure"); CHECK(diagnostic.detail.size() == 4096U); CHECK(diagnostic.detailTruncated);
  CHECK(!diagnostic.detailEscaped); CHECK(authoring::DiagnosticRegistry::validate(diagnostic));
  diagnostic.setDetail(std::string(4096U, 'b')); CHECK(!diagnostic.detailTruncated); CHECK(diagnostic.detail.size() == 4096U);
  diagnostic.setDetail(std::string(4094U, 'x') + "歌"); CHECK(diagnostic.detail.size() == 4094U); CHECK(diagnostic.detailTruncated);
  CHECK(authoring::DiagnosticRegistry::validate(diagnostic));
  diagnostic.setDetail("歌🙂\nnext"); CHECK(diagnostic.detail == "歌🙂\nnext"); CHECK(!diagnostic.detailTruncated);
  diagnostic.setDetail(std::string{"a\0b", 3U} + std::string(1U, static_cast<char>(0xff)));
  CHECK(diagnostic.detail == "a\\x00b\\xFF"); CHECK(diagnostic.detailEscaped); CHECK(!diagnostic.detailTruncated);
  CHECK(authoring::DiagnosticRegistry::validate(diagnostic));
  auto other = diagnostic; other.detailEscaped = false; CHECK(!other.sameIssueAs(diagnostic));
  diagnostic.detail = std::string(1U, static_cast<char>(0xff)); CHECK(!authoring::DiagnosticRegistry::validate(diagnostic));
  diagnostic.detail = std::string(4097U, 'a'); CHECK(!authoring::DiagnosticRegistry::validate(diagnostic));
}

TEST_CASE("a diagnostic equals another only when every field it carries is the same") {
  using namespace seam;
  const authoring::Diagnostic base{
      .code = "BANK_MISSING",
      .severity = authoring::DiagnosticSeverity::Error,
      .messageKey = "bank.missing",
      .affectedIds = {"track-1"},
      .actions = {authoring::DiagnosticAction::RelinkVoicebank},
      .occurrenceCount = 1U,
  };
  CHECK(base == authoring::Diagnostic{base});
  {
    auto other = base;
    other.code = "MEDIA_MISSING";
    CHECK(!(other == base));
  }
  {
    auto other = base;
    other.severity = authoring::DiagnosticSeverity::Warning;
    CHECK(!(other == base));
  }
  {
    auto other = base;
    other.messageKey = "media.missing";
    CHECK(!(other == base));
  }
  {
    auto other = base;
    other.affectedIds = {"track-2"};
    CHECK(!(other == base));
  }
  {
    auto other = base;
    other.actions = {authoring::DiagnosticAction::ChooseVoicebank};
    CHECK(!(other == base));
  }
  {
    // How many times it came is not which issue it is, and it is part of what the creator is shown.
    auto other = base;
    other.occurrenceCount = 2U;
    CHECK(other.sameIssueAs(base));
    CHECK(!(other == base));
  }
  {
    auto other = base;
    other.setDetail("the voicebank folder was moved");
    CHECK(!(other == base));
  }
  {
    auto other = base;
    other.detailTruncated = true;
    CHECK(!(other == base));
  }
  {
    auto other = base;
    other.detailEscaped = true;
    CHECK(!(other == base));
  }
  {
    auto other = base;
    other.detailSourceHash = "0123abcd";
    CHECK(!(other == base));
  }
}

TEST_CASE("diagnostic registry validates registered codes and actions") {
  CHECK(seam::authoring::DiagnosticRegistry::isRegistered("BANK_UNTRUSTED"));
  const auto actions = seam::authoring::DiagnosticRegistry::actions("BANK_UNTRUSTED");
  CHECK(!actions.empty());
  const seam::authoring::Diagnostic diagnostic{
      .code = "BANK_UNTRUSTED",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "bank.untrusted",
      .affectedIds = {"track-1"},
      .actions = actions,
      .occurrenceCount = 1U,
  };
  CHECK(seam::authoring::DiagnosticRegistry::validate(diagnostic));
}

TEST_CASE("diagnostic registry rejects unknown or unregistered actions") {
  const seam::authoring::Diagnostic unknown{
      .code = "USER_INPUT",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "unknown",
      .actions = {seam::authoring::DiagnosticAction::Retry},
  };
  CHECK(!seam::authoring::DiagnosticRegistry::validate(unknown));

  const seam::authoring::Diagnostic wrongAction{
      .code = "AUDIO_UNAVAILABLE",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "audio.unavailable",
      .actions = {seam::authoring::DiagnosticAction::Retry},
  };
  CHECK(!seam::authoring::DiagnosticRegistry::validate(wrongAction));
}

TEST_CASE("diagnostic registry preserves mapped severity and actions from errors") {
  const auto diagnostic = seam::authoring::DiagnosticRegistry::fromError(
      seam::core::Error{seam::core::ErrorCode::NotFound, "missing project"});
  CHECK(diagnostic.code == "PROJECT_NOT_FOUND");
  CHECK(diagnostic.severity == seam::authoring::DiagnosticSeverity::Error);
  CHECK(!diagnostic.actions.empty());
  CHECK(diagnostic.actions.front() == seam::authoring::DiagnosticAction::OpenSupport);
}

TEST_CASE("diagnostic registry exposes bounded recovery and copy actions") {
  const auto actions = seam::authoring::DiagnosticRegistry::actions(
      "PERSISTENCE_FAILED");
  CHECK(std::find(actions.begin(), actions.end(),
                  seam::authoring::DiagnosticAction::SaveAs) != actions.end());
  CHECK(std::find(actions.begin(), actions.end(),
                  seam::authoring::DiagnosticAction::OpenRecoveryFolder) !=
        actions.end());
  CHECK(std::find(actions.begin(), actions.end(),
                  seam::authoring::DiagnosticAction::CopyDiagnostic) !=
        actions.end());
  CHECK(seam::authoring::toString(
            seam::authoring::DiagnosticAction::CopyDiagnostic) ==
        "COPY_DIAGNOSTIC");
}

TEST_CASE("diagnostic registry registers the editor's own notices with only the actions the editor answers itself") {
  using namespace seam::authoring;
  // The plug-in connects no diagnostic action callback, so a notice the editor raises can offer only
  // what the editor does without one: dismissing it and, for the host that would not follow, asking
  // again.
  CHECK(DiagnosticRegistry::isRegistered("EDIT_REFUSED"));
  CHECK(DiagnosticRegistry::isRegistered("SELECTION_SYNC_FAILED"));
  CHECK(DiagnosticRegistry::severity("EDIT_REFUSED") == DiagnosticSeverity::Warning);
  CHECK(DiagnosticRegistry::severity("SELECTION_SYNC_FAILED") == DiagnosticSeverity::Warning);
  CHECK((DiagnosticRegistry::actions("EDIT_REFUSED") == std::vector{DiagnosticAction::Dismiss}));
  CHECK((DiagnosticRegistry::actions("SELECTION_SYNC_FAILED") ==
         std::vector{DiagnosticAction::Retry, DiagnosticAction::Dismiss}));

  Diagnostic refused{.code = "EDIT_REFUSED",
                     .severity = DiagnosticSeverity::Warning,
                     .messageKey = "editor.edit-refused",
                     .actions = DiagnosticRegistry::actions("EDIT_REFUSED"),
                     .occurrenceCount = 1U};
  refused.setDetail("Selected notes must belong to the active region");
  CHECK(DiagnosticRegistry::validate(refused));
  // An action the editor cannot answer is not offered: a refused key has nothing to retry and no
  // callback to copy through.
  refused.actions = {DiagnosticAction::Retry};
  CHECK(!DiagnosticRegistry::validate(refused));
  refused.actions = {DiagnosticAction::CopyDiagnostic};
  CHECK(!DiagnosticRegistry::validate(refused));

  Diagnostic sync{.code = "SELECTION_SYNC_FAILED",
                  .severity = DiagnosticSeverity::Warning,
                  .messageKey = "editor.selection-sync-failed",
                  .actions = DiagnosticRegistry::actions("SELECTION_SYNC_FAILED"),
                  .occurrenceCount = 1U};
  CHECK(DiagnosticRegistry::validate(sync));
  sync.actions = {DiagnosticAction::OpenSettings};
  CHECK(!DiagnosticRegistry::validate(sync));
}
