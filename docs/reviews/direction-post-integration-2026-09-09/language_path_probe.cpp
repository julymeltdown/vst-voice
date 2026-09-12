#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/english_phonemizer.hpp"
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
  if (argc > 1) {
    const std::filesystem::path requested{argv[1]};
    const auto canonical = std::filesystem::canonical(requested.parent_path()) / requested.filename();
    std::cout << "requested=" << requested.string() << '\n'
        << "canonical_result=" << canonical.string() << '\n'
        << "lexically_equal=" << (requested == canonical) << '\n'
        << "filesystem_equivalent=" << std::filesystem::equivalent(requested, canonical) << '\n';
  }
  seam::application::ProjectFactory factory{987000U};
  auto project = factory.createProject("Read-only English roles diagnostic");
  const auto track = factory.addVocalTrack(project, "English");
  const auto regionId = factory.addRegion(project, track, "Probe", seam::time::Tick{0}, seam::time::Tick{960});
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"sing", seam::domain::Language::English);
  auto* region = project.findRegion(regionId);
  region->lyrics.push_back(std::move(lyric)); region->notes.push_back(std::move(note));
  const auto result = seam::phonemizer::EnglishPhonemizer{}.phonemize(*region);
  for (const auto& token : result.tokens) {
    const auto role = token.role == seam::domain::PhonemeRole::Onset ? "Onset" :
        token.role == seam::domain::PhonemeRole::Nucleus ? "Nucleus" :
        token.role == seam::domain::PhonemeRole::Coda ? "Coda" : "Other";
    std::cout << "sing " << token.symbol << " role=" << role << " voiced=" << token.voiced << '\n';
  }
  std::cout << "warnings=" << result.warnings.size() << '\n';
}
