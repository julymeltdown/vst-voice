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
