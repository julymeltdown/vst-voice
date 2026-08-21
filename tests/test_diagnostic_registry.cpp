#include "test_framework.hpp"

#include "seam/authoring/diagnostic.hpp"

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
