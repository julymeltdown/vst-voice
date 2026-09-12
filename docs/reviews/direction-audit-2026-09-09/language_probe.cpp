#include "seam/application/project_factory.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include <algorithm>
#include <iostream>
#include <stop_token>

int main() {
  using namespace seam;
  for (auto language : {domain::Language::English, domain::Language::Korean}) {
    application::ProjectFactory factory{912000U};
    auto project = factory.createProject("read-only audit");
    auto track = factory.addVocalTrack(project, "probe");
    auto id = factory.addRegion(project, track, "probe", time::Tick{0}, time::Tick{3840});
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 60U,
        language == domain::Language::English ? U"hello" : U"아", language);
    auto* region = project.findRegion(id);
    region->lyrics.push_back(lyric);
    region->notes.push_back(note);
    const auto base = phonemizer::resolvePronunciation(*region);
    if (!base || base.value().pronunciation.tokens.empty()) return 2;
    domain::PhonemeOverride edit;
    edit.key = {note.id, 0};
    edit.symbol = language == domain::Language::English ? "s" : "i";
    edit.sourceContextId = std::string(64U, 'a');
    region->phonemeOverrides.push_back(edit);
    const auto resolved = phonemizer::resolvePronunciation(*region);
    if (!resolved || resolved.value().pronunciation.tokens.empty()) return 2;
    const bool orphan = std::any_of(resolved.value().pronunciation.warnings.begin(),
        resolved.value().pronunciation.warnings.end(), [](const auto& warning) {
          return warning.code == phonemizer::WarningCode::OrphanOverride;
        });
    std::cout << (language == domain::Language::English ? "en" : "ko")
        << " invalid_context_applied=" << (resolved.value().pronunciation.tokens.front().symbol == edit.symbol)
        << " orphan_warning=" << orphan << '\n';
  }
  application::ProjectFactory factory{913000U};
  auto project = factory.createProject("cross-language audit");
  auto track = factory.addVocalTrack(project, "probe");
  auto id = factory.addRegion(project, track, "probe", time::Tick{0}, time::Tick{3840});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 60U, U"あ", domain::Language::Japanese);
  auto* region = project.findRegion(id);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  const auto base = phonemizer::resolvePronunciation(*region);
  if (!base) return 2;
  domain::PhonemeOverride edit;
  edit.key = {note.id, 0};
  edit.symbol = "i";
  edit.sourceContextId = phonemizer::phonemeEditContextId(base.value(), edit.key);
  region->phonemeOverrides.push_back(edit);
  application::SetLyricCommand change{lyric.id, U"아", domain::Language::Korean};
  const auto applied = change.apply(project);
  if (!applied) { std::cout << "cross_language_error=" << applied.error().message << '\n'; return 2; }
  region = project.findRegion(id);
  std::cout << "ja_to_ko unresolved=" << region->phonemeOverrides.front().unresolved
      << " context_replaced=" << (region->phonemeOverrides.front().sourceContextId != edit.sourceContextId) << '\n';
  auto [secondLyric, secondNote] = factory.makeNote(time::Tick{960}, time::Tick{960}, 62U, U"hello", domain::Language::English);
  region->lyrics.push_back(secondLyric);
  region->notes.push_back(secondNote);
  std::stop_source cancelled;
  cancelled.request_stop();
  const auto result = phonemizer::resolvePronunciation(*region, cancelled.get_token());
  std::cout << "pre_cancelled_mixed_error=" << (result ? "unexpected success" : result.error().message) << '\n';
}
