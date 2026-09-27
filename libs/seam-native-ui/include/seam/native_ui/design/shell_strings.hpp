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

#include "seam/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <initializer_list>
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

// Whole sentences with values in them use numbered placeholders, so a translation keeps the full
// sentence and may reorder the values: "{0} notes above" or "{1}개 중 {0}". trf() looks the entry
// up and fills {0}, {1}, ... with args; "{{" and "}}" are literal braces. A placeholder with no
// argument is left as written, which a test would notice.
[[nodiscard]] std::string trf(Str id, std::initializer_list<std::string_view> args);
[[nodiscard]] std::string formatShellText(std::string_view text,
                                          std::initializer_list<std::string_view> args);
// The placeholder numbers a text uses, sorted and without repeats ("{1} of {0}" gives 0, 1), or
// nothing when a brace is malformed (an unclosed "{", a "{x}"): such text is never accepted from a
// translation file.
struct ShellPlaceholders final {
  bool valid{true};
  std::vector<std::size_t> indices;
  friend bool operator==(const ShellPlaceholders&, const ShellPlaceholders&) = default;
};
[[nodiscard]] ShellPlaceholders shellPlaceholders(std::string_view text);

// Installs a table for every later lookup on the UI thread; null returns to English. The table
// must outlive its installation.
void installShellStrings(const ShellStringTable* table) noexcept;
// Returns to English only when table is the one installed, so a shell that drops its table never
// uninstalls another shell's.
void uninstallShellStrings(const ShellStringTable* table) noexcept;

// Translation files. One JSON file per language, assets/l10n/<language>.json, keyed by the stable
// keys of shell_strings.def:
//
//   { "language": "ko", "name": "한국어", "strings": { "ChangeVoice": "보이스 변경", ... } }
//
// Loading validates every entry against the compiled-in English. An unknown key is ignored, a
// missing key reads as English, and an entry whose placeholders are not exactly English's (a
// {0} dropped or added, a malformed brace, a value that is not a string) is rejected and reads as
// English too, so a bad translation can never lose a value from a sentence. The report lists all
// three; a shipped file has none (scripts/l10n/externalize_shell_strings.py --check).
struct ShellStringLoadReport final {
  std::string language;
  std::string name;
  std::size_t translated{0U};
  std::vector<std::string> unknownKeys;
  std::vector<std::string> missingKeys;
  std::vector<std::string> rejectedKeys;
  [[nodiscard]] bool clean() const noexcept {
    return unknownKeys.empty() && missingKeys.empty() && rejectedKeys.empty();
  }
};
struct LoadedShellStrings final {
  ShellStringTable table;
  ShellStringLoadReport report;
};
// Fails only when the text is not a translation file at all (not JSON, no "strings" object, no
// "language"); every entry-level problem is in the report instead.
[[nodiscard]] core::Result<LoadedShellStrings> parseShellStrings(std::string_view json);
[[nodiscard]] core::Result<LoadedShellStrings> loadShellStrings(const std::filesystem::path& file);

// The languages the shell offers: English, compiled in, and each shipped translation. The name is
// the language's own name ("한국어"), which is never translated: a reader looking for their language
// finds it in their own script whatever the interface currently reads.
struct ShellLanguage final {
  std::string_view code;
  std::string_view name;
};
[[nodiscard]] const std::vector<ShellLanguage>& shellLanguages();
// The offered language for a platform language tag ("ko-KR", "ko", "en-KR"): its base language
// when offered, and English otherwise.
[[nodiscard]] std::string_view shellLanguageFor(std::string_view tag) noexcept;

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
