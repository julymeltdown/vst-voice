#pragma once

// The user-visible text of the EMO/SCENE shell, its workspaces and its re-homed overlays, behind one
// table so it can be translated. English is the base: every entry has its English text compiled in
// (shell_strings.def, maintained by scripts/l10n/externalize_shell_strings.py), and a translation
// table maps the same stable keys to other text. A lookup never fails: an entry a table does not
// translate reads as English.
//
// Painters and semantics call tr() at the moment they draw or publish, so installing a table takes
// effect on the next frame. Text that comes from the project, the voicebank or the host (track
// names, lyrics, device names) is data and is never looked up here.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace seam::native_ui::design {

enum class Str : std::uint16_t {
#define SEAM_SHELL_STRING(key, english) key,
#include "seam/native_ui/design/shell_strings.def"
#undef SEAM_SHELL_STRING
};

[[nodiscard]] std::size_t shellStringCount() noexcept;
// The stable translation key ("ChangeVoice") and the compiled-in English text of an entry.
[[nodiscard]] std::string_view shellStringKey(Str id) noexcept;
[[nodiscard]] std::string_view englishShellString(Str id) noexcept;

// One complete set of shell text. The default table is English.
class ShellStringTable final {
public:
  ShellStringTable();
  // Replaces the text for a key. Returns false, and changes nothing, for an unknown key.
  bool set(std::string_view key, std::string text);
  [[nodiscard]] const char* text(Str id) const noexcept;
  // Pseudo-localization for layout testing: every entry accented, bracketed and padded to at least
  // (1 + expansion) times its English length in characters, so truncation and overflow show up
  // before a real translation exists.
  [[nodiscard]] static ShellStringTable pseudoLocalized(double expansion = 0.4);

private:
  std::vector<std::string> text_;
};

// The text of an entry in the installed table (English when none is installed). The pointer stays
// valid while that table is installed and unchanged.
[[nodiscard]] const char* tr(Str id) noexcept;

// Installs a table for every later lookup on the UI thread; null returns to English. The table
// must outlive its installation.
void installShellStrings(const ShellStringTable* table) noexcept;

// Installs a table for the lifetime of the scope, then restores the previous one.
class ScopedShellStrings final {
public:
  explicit ScopedShellStrings(const ShellStringTable& table) noexcept;
  ~ScopedShellStrings();
  ScopedShellStrings(const ScopedShellStrings&) = delete;
  ScopedShellStrings& operator=(const ScopedShellStrings&) = delete;

private:
  const ShellStringTable* previous_;
};

}  // namespace seam::native_ui::design

