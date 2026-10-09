#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/authoring/helper_process.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace {
using namespace seam;
struct BindingFixture {
  std::filesystem::path root = test::support::temporaryDirectory("project-binding");
  std::filesystem::path path = root / "song.seam";
  application::ProjectFactory factory{100};
  domain::Project project = factory.createProject("Binding fixture");
  domain::TrackId track = factory.addVocalTrack(project, "Voice");
  domain::RegionId region = factory.addRegion(project, track, "Verse", time::Tick{0}, time::Tick{3840});
  BindingFixture() {
    project.findVocalTrack(track)->voicebank = {"fixture.singer", "1", std::string(64, 'a')};
    add(domain::Language::Japanese);
  }
  void add(domain::Language language) {
    auto* value = project.findRegion(region);
    auto [lyric, note] = factory.makeNote(time::Tick{static_cast<std::int64_t>(value->notes.size()) * 960},
        time::Tick{960}, 60, U"a", language);
    value->lyrics.push_back(lyric); value->notes.push_back(note);
  }
  std::vector<std::string> save(std::string family = "sample", std::string languages = "ja") {
    CHECK(formats::ProjectJsonCodec{}.save(project, path));
    return {"verify-project-binding", path.string(), core::sha256File(path).value(), track.toString(), region.toString(),
        family, "fixture.singer", "1", std::string(64, 'a'), languages};
  }
  auto run(const std::vector<std::string>& args) {
    return authoring::runBoundedHelperProcess({.executable=SEAM_TEST_VOICEBANK_CLI, .arguments=args});
  }
};
}

TEST_CASE("Native project bindings decode each singer family and note-linked language without resource access") {
#if !defined(_WIN32)
#if defined(SEAM_TEST_PYTHON)
  const auto cliSha = core::sha256File(SEAM_TEST_VOICEBANK_CLI).value();
#endif
  for (const std::string family : {"sample", "recipe", "model"}) {
    BindingFixture f;
    auto* track = f.project.findVocalTrack(f.track);
    if (family == "recipe") track->proceduralRecipe = domain::ProceduralRecipeReference{
        {domain::SingerResourceKind::Procedural, "fixture.singer", "1", std::string(64,'a')},
        "missing-do-not-open/recipe.json", "neutral"};
    if (family == "model") track->neuralResource = domain::NeuralResourceReference{
        {domain::SingerResourceKind::Neural, "fixture.singer", "1", std::string(64,'a')}};
    if (family != "sample") track->voicebank = {"inactive.bank", "99", std::string(64,'f')};
    for (const auto language : {domain::Language::Japanese, domain::Language::English, domain::Language::Korean}) {
      f.project.findRegion(f.region)->lyrics.front().language = language;
      auto args = f.save(family, language == domain::Language::Japanese ? "ja" : language == domain::Language::English ? "en" : "ko");
      const auto run = f.run(args); CHECK(run);
      const auto record = formats::parseJson(run.value().standardOutput); CHECK(record);
      CHECK(record.value().find("payloadFamily")->asString() == family);
      CHECK(record.value().find("resourceAdmission")->asString() == "NOT_CHECKED");
      CHECK(!record.value().find("authorizesRelease")->asBool());
      CHECK(record.value().find("storedVoicebankRole")->asString() == (family == "sample" ? "SELECTED" : "INACTIVE"));
      if (family != "sample" && language == domain::Language::Japanese) {
        auto wrong=args;wrong[5]="sample";wrong[6]="inactive.bank";wrong[7]="99";wrong[8]=std::string(64,'f');
        CHECK(!f.run(wrong));
        wrong=args;wrong[5]=family=="recipe" ? "model" : "recipe";CHECK(!f.run(wrong));
      }
#if defined(SEAM_TEST_PYTHON)
      if (language == domain::Language::Japanese) {
      const auto recordPath=f.root/"record.json";
      CHECK(core::durableAtomicWriteText(recordPath,run.value().standardOutput));
      std::vector<std::string> replay{(std::filesystem::path{SEAM_TEST_SOURCE_DIR}/"scripts/verify_project_binding_record.py").string(),
          "--record",recordPath.string(),"--record-sha256",core::sha256File(recordPath).value(),
          "--project",f.path.string(),"--voicebank-cli",SEAM_TEST_VOICEBANK_CLI,
          "--cli-sha256",cliSha};
      CHECK(authoring::runBoundedHelperProcess({.executable=SEAM_TEST_PYTHON,.arguments=replay}));
      auto forged=record.value(); forged.asObject()["noteCount"]=std::int64_t{2};
      CHECK(core::durableAtomicWriteText(recordPath,formats::stringifyJson(forged)));
      replay[4]=core::sha256File(recordPath).value();
      CHECK(!authoring::runBoundedHelperProcess({.executable=SEAM_TEST_PYTHON,.arguments=replay}));
      }
#endif
      if (family == "sample" && language == domain::Language::Japanese)
      for (const std::size_t field : {2U,3U,4U,6U,7U,8U,9U}) {
        auto wrong=args;
        wrong[field]=field==2U || field==8U ? std::string(64,'b') :
            field==3U || field==4U ? "000000000000ffff" :
            field==9U ? (args[9]=="ja" ? "en" : "ja") : "different";
        CHECK(!f.run(wrong));
      }
    }
    CHECK(!std::filesystem::exists(f.root/"missing-do-not-open"));
  }
#endif
}

TEST_CASE("Native project binding distinguishes mixed linked lyrics unused lyrics and missing links") {
#if !defined(_WIN32)
  BindingFixture f; f.add(domain::Language::English); f.add(domain::Language::Korean);
  auto* region=f.project.findRegion(f.region);
  auto unused=f.factory.makeNote(time::Tick{0},time::Tick{960},60,U"unused",domain::Language::Unspecified).first;
  region->lyrics.push_back(unused);
  auto args=f.save("sample","en,ja,ko"); CHECK(f.run(args));
  const auto parsed=formats::parseJson(f.run(args).value().standardOutput); CHECK(parsed);
  CHECK(parsed.value().find("unusedLyricCount")->asInt64()==1);
  auto partial=args;partial[9]="ja";CHECK(!f.run(partial));
  region->lyrics.front().language=domain::Language::Unspecified;CHECK(!f.run(f.save("sample","en,ja,ko")));
  region->lyrics.front().language=domain::Language::Japanese;
  args=f.save("sample","en,ja,ko");
  auto raw=formats::parseJson(core::readTextFileLimited(f.path,1024*1024).value()).value();
  auto& rawRegion=raw.asObject()["vocalTracks"].asArray().front().asObject()["regions"].asArray().front();
  rawRegion.asObject()["lyrics"].asArray().front().asObject()["language"]="unknown-language";
  CHECK(core::durableAtomicWriteText(f.path,formats::stringifyJson(raw)));
  args[2]=core::sha256File(f.path).value(); CHECK(!f.run(args));
  args=f.save("sample","en,ja,ko");
  raw=formats::parseJson(core::readTextFileLimited(f.path,1024*1024).value()).value();
  raw.asObject()["vocalTracks"].asArray().front().asObject()["regions"].asArray().front().asObject()["notes"].asArray().front().asObject()["lyricId"]="0000000000000000";
  CHECK(core::durableAtomicWriteText(f.path,formats::stringifyJson(raw)));
  args[2]=core::sha256File(f.path).value(); CHECK(!f.run(args));
  // Unknown unused tokens and tokens outside the selected region also refuse.
  args=f.save("sample","en,ja,ko");
  raw=formats::parseJson(core::readTextFileLimited(f.path,1024*1024).value()).value();
  raw.asObject()["vocalTracks"].asArray().front().asObject()["regions"].asArray().front().asObject()["lyrics"].asArray().back().asObject()["language"]="xx";
  CHECK(core::durableAtomicWriteText(f.path,formats::stringifyJson(raw)));
  args[2]=core::sha256File(f.path).value(); CHECK(!f.run(args));
  const auto other=f.factory.addRegion(f.project,f.track,"Other",time::Tick{3840},time::Tick{960});
  f.project.findRegion(other)->lyrics.push_back(unused);
  args=f.save("sample","en,ja,ko");
  raw=formats::parseJson(core::readTextFileLimited(f.path,1024*1024).value()).value();
  raw.asObject()["vocalTracks"].asArray().front().asObject()["regions"].asArray().back().asObject()["lyrics"].asArray().front().asObject()["language"]="xx";
  CHECK(core::durableAtomicWriteText(f.path,formats::stringifyJson(raw)));
  args[2]=core::sha256File(f.path).value(); CHECK(!f.run(args));
  f.project.findRegion(f.region)->notes.clear(); CHECK(!f.run(f.save("sample","en,ko")));
#endif
}

TEST_CASE("Native project binding refuses nonregular oversized and codec-invalid pinned input") {
#if !defined(_WIN32)
  BindingFixture f; auto args=f.save();
  const auto link=f.root/"linked.seam";std::filesystem::create_symlink(f.path,link);
  auto changed=args;changed[1]=link.string();CHECK(!f.run(changed));
  const auto fifo=f.root/"fifo.seam";CHECK(::mkfifo(fifo.c_str(),0600)==0);
  changed[1]=fifo.string();CHECK(!f.run(changed));
  CHECK(core::durableAtomicWriteText(f.path,"{\"formatId\":\"com.project-seam.project\",\"schemaVersion\":20}"));
  args[2]=core::sha256File(f.path).value();CHECK(!f.run(args));
  std::filesystem::resize_file(f.path,64ULL*1024ULL*1024ULL+1ULL);CHECK(!f.run(args));
  // Remove our oversized negative input: save correctly refuses its oversized backup.
  CHECK(std::filesystem::remove(f.path));
  args=f.save();
  auto text=core::readTextFileLimited(f.path,1024*1024).value();
  text.insert(1,"\"schemaVersion\":20,");
  CHECK(core::durableAtomicWriteText(f.path,text));args[2]=core::sha256File(f.path).value();CHECK(!f.run(args));
  // Historical-writer fixture, with only a declared sample identity supplied.
  const auto old=core::readTextFileLimited(std::filesystem::path{SEAM_TEST_SOURCE_DIR}/
      "tests/fixtures/projects/schema-1-historical-writer.seam",1024*1024);CHECK(old);
  auto raw=formats::parseJson(old.value()).value();
  raw.asObject()["vocalTracks"].asArray().front().asObject()["voicebank"]=formats::JsonValue::Object{
      {"id","fixture.singer"},{"version","1"},{"contentHash",std::string(64,'a')}};
  CHECK(core::durableAtomicWriteText(f.path,formats::stringifyJson(raw)));
  args[2]=core::sha256File(f.path).value();args[3]="0000000000000065";args[4]="0000000000000066";
  const auto migrated=f.run(args);CHECK(migrated);
  const auto record=formats::parseJson(migrated.value().standardOutput);CHECK(record);
  CHECK(record.value().find("sourceSchemaVersion")->asInt64()==1);
  CHECK(record.value().find("codecSchemaVersion")->asInt64()==seam::formats::ProjectJsonCodec::kSchemaVersion);
#endif
}
