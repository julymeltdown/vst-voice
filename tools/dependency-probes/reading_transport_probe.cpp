// Explicit development integration probe, not a production service/resource loader.
#include "seam/authoring/helper_process.hpp"
#include "seam/authoring/japanese_reading_response.hpp"
#include "seam/authoring/japanese_reading_resource.hpp"
#include "seam/authoring/japanese_reading_stage.hpp"
#include "seam/authoring/japanese_reading_capture.hpp"
#include "seam/application/project_factory.hpp"
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 4) { std::cerr << "Usage: probe HELPER DICTIONARY EXPECTED_HELPER_SHA256\n"; return 2; }
  const auto resource = seam::authoring::VerifiedJapaneseReadingResource::verify({argv[1], argv[3],
      "462fc38e7520aa89e4d32b2611749208528c901e", argv[2], {
      "888ee94c5a8a7a26d24ab3f1b7155441351954fd51ea06b4a2f78bd742492b2f",
      "62fd16b4f64c851d5dc352ef0d5740c5fc83ddc7c203b2b0b1fc5271969a14ce",
      "49bb7ae70f373e99801ed1c8a9e68a2678947c8a49f7cd176bbd2d37b93f10e3",
      "ce97851ecda075914fa3ffe7294a1ab34ee4f6d56ba6bf9197d74143b5dffbfe"}});
  if (!resource) { std::cerr << resource.error().message << '\n'; return 6; }
  const auto staged = seam::authoring::StagedJapaneseReadingResource::prepare(resource.value(), std::filesystem::temp_directory_path());
  if (!staged) { std::cerr << staged.error().message << '\n'; return 8; }
  const auto& snapshot = staged.value().resource();
  seam::application::ProjectFactory factory{410000U}; auto project = factory.createProject("Reading transport");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{7680});
  std::vector<seam::domain::NoteId> ids;
  for (const auto* surface : {U"私", U"は", U"今日", U"学", U"校", U"へ", U"行く", U"。"}) {
    auto [lyric, note] = factory.makeNote(seam::time::Tick{static_cast<std::int64_t>(ids.size()) * 960}, seam::time::Tick{960},
        60U, surface, seam::domain::Language::Japanese);
    ids.push_back(note.id); project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
  }
  project.findRegion(region)->notes[3].phoneticHint = "g a";
  seam::application::EditorSession session{project};
  const auto capture = seam::authoring::JapaneseReadingCapture::prepare(session, region, ids, staged.value());
  if (!capture) { std::cerr << capture.error().message << '\n'; return 9; }
  const auto decoded = capture.value().read();
  if (!decoded) { std::cerr << decoded.error().message << '\n'; return 4; }
  const auto& tokens = decoded.value().reading.tokens;
  if (tokens.size() != 7U || tokens[1].surface != "は" || tokens[1].pronunciation != "ワ" ||
      tokens[4].surface != "へ" || tokens[4].pronunciation != "エ") return 5;
  if (!capture.value().matches(session, region, snapshot.identity()) || session.project() != project || session.canUndo() ||
      decoded.value().bindings[1].notes != std::vector<seam::domain::NoteId>{ids[1]} ||
      decoded.value().bindings[3].notes != std::vector<seam::domain::NoteId>{ids[3], ids[4]} ||
      !decoded.value().bindings[3].crossesLyrics || !decoded.value().bindings[3].touchesExplicitHint) return 10;
  if (!session.replaceProject(project) || capture.value().matches(session, region, snapshot.identity())) return 11;
  std::cout << "PASS: captured phrase -> staged reader -> contextual reading -> exact note bindings and stale-document rejection; release qualification pending\n";
}
