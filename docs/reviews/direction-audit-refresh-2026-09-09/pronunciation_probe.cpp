#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include <iostream>
#include <initializer_list>

void inspect(const char* label, seam::domain::Language language,
             std::initializer_list<const char32_t*> lyrics) {
  using namespace seam;
  application::ProjectFactory factory{920000U};
  auto project = factory.createProject("Pronunciation audit");
  auto track = factory.addVocalTrack(project, "Audit");
  auto id = factory.addRegion(project, track, "Audit", time::Tick{0}, time::Tick{3840});
  auto* region = project.findRegion(id);
  std::int64_t tick = 0;
  for (auto text : lyrics) {
    auto [lyric, note] = factory.makeNote(time::Tick{tick}, time::Tick{960}, 60U, std::u32string{text}, language);
    region->lyrics.push_back(std::move(lyric)); region->notes.push_back(std::move(note)); tick += 960;
  }
  const auto result = phonemizer::resolvePronunciation(*region);
  std::cout << label << " =>";
  if (!result) { std::cout << " ERROR " << result.error().message << '\n'; return; }
  for (const auto& token : result.value().pronunciation.tokens) std::cout << ' ' << token.symbol;
  std::cout << " warnings=" << result.value().pronunciation.warnings.size() << '\n';
}
int main() {
  using seam::domain::Language;
  inspect("강", Language::Korean, {U"강"});
  inspect("밤", Language::Korean, {U"밤"});
  inspect("밥", Language::Korean, {U"밥"});
  inspect("어", Language::Korean, {U"어"});
  inspect("오", Language::Korean, {U"오"});
  inspect("으", Language::Korean, {U"으"});
  inspect("우", Language::Korean, {U"우"});
  inspect("먹어 single note", Language::Korean, {U"먹어"});
  inspect("먹/어 split notes", Language::Korean, {U"먹", U"어"});
  inspect("read", Language::English, {U"read"});
}
