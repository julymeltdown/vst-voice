#pragma once

// The ids that publish one issue of the diagnostics panel and the actions on it:
//
//   diagnostic.<index>.<code>@<identity>           the issue's row
//   diagnostic-action.<index>.<ACTION>@<identity>  one button of the issue
//
// Each names the issue by where it stands (the index, which is the order the rows are shown in) and by
// what it is (authoring::Diagnostic::issueIdentity, sixteen hexadecimal digits). The panel is rebuilt
// whenever the owner's list or the editor's notices change, and an old entry can go (an eviction, a
// dismissal, a notice that clears) while the ones after it move up. An id that a client kept from an
// earlier frame, or a press that was aimed at an earlier frame, must never act on the issue that has
// taken its place, and the code alone cannot tell them apart (a run of refused keys is a run of notices
// with one code), so the identity is part of the id and the controller checks it before it acts.

#include "seam/authoring/diagnostic.hpp"
#include "seam/core/result.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace seam::native_ui {

inline constexpr std::string_view kDiagnosticRowIdPrefix = "diagnostic.";
inline constexpr std::string_view kDiagnosticActionIdPrefix = "diagnostic-action.";

struct DiagnosticElementId final {
  // A button for one action of the issue, and not the issue's row.
  bool action{false};
  // Where the issue stood in the list when the id was made.
  std::size_t index{0U};
  // The issue's code for a row, the action's name for a button.
  std::string_view name;
  // authoring::Diagnostic::issueIdentity of the issue the id was made for.
  std::uint64_t identity{0U};
};

[[nodiscard]] inline std::string diagnosticIdentitySuffix(std::uint64_t identity) {
  static constexpr std::string_view kDigits = "0123456789abcdef";
  std::string out{"@"};
  for (int shift = 60; shift >= 0; shift -= 4)
    out.push_back(kDigits[static_cast<std::size_t>((identity >> shift) & 0xfU)]);
  return out;
}

[[nodiscard]] inline std::string diagnosticRowId(std::size_t index,
                                                 const authoring::Diagnostic& diagnostic) {
  return std::string{kDiagnosticRowIdPrefix} + std::to_string(index) + "." + diagnostic.code +
         diagnosticIdentitySuffix(diagnostic.issueIdentity());
}

[[nodiscard]] inline std::string diagnosticActionId(std::size_t index,
                                                    const authoring::Diagnostic& diagnostic,
                                                    authoring::DiagnosticAction action) {
  return std::string{kDiagnosticActionIdPrefix} + std::to_string(index) + "." +
         std::string{authoring::toString(action)} + diagnosticIdentitySuffix(diagnostic.issueIdentity());
}

// Whether the id belongs to a diagnostics row or button at all.
[[nodiscard]] inline bool isDiagnosticElementId(std::string_view id) noexcept {
  return id.starts_with(kDiagnosticRowIdPrefix) || id.starts_with(kDiagnosticActionIdPrefix);
}

// Takes such an id apart. A malformed one (no index, no name, no identity, an identity that is not
// sixteen hexadecimal digits) is InvalidArgument; whether it still names an issue is for the caller,
// against the list as it stands.
[[nodiscard]] inline core::Result<DiagnosticElementId> parseDiagnosticElementId(std::string_view id) {
  const auto action = id.starts_with(kDiagnosticActionIdPrefix);
  const auto prefix = action ? kDiagnosticActionIdPrefix : kDiagnosticRowIdPrefix;
  const auto malformed = [] {
    return core::failure<DiagnosticElementId>(core::ErrorCode::InvalidArgument,
                                              "Diagnostic accessibility id is malformed");
  };
  if (!id.starts_with(prefix)) return malformed();
  const auto rest = id.substr(prefix.size());
  const auto dot = rest.find('.');
  const auto at = rest.rfind('@');
  if (dot == std::string_view::npos || dot == 0U || at == std::string_view::npos || at <= dot + 1U)
    return malformed();
  DiagnosticElementId parsed;
  parsed.action = action;
  const auto indexEnd = rest.data() + dot;
  const auto index = std::from_chars(rest.data(), indexEnd, parsed.index);
  if (index.ec != std::errc{} || index.ptr != indexEnd) return malformed();
  parsed.name = rest.substr(dot + 1U, at - dot - 1U);
  const auto digits = rest.substr(at + 1U);
  if (digits.size() != 16U) return malformed();
  const auto identity = std::from_chars(digits.data(), digits.data() + digits.size(), parsed.identity, 16);
  if (identity.ec != std::errc{} || identity.ptr != digits.data() + digits.size()) return malformed();
  return core::success(parsed);
}

}  // namespace seam::native_ui
