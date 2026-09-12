#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/project_lifecycle.hpp"
#include "seam/authoring/autosave_service.hpp"
#include "seam/authoring/authoring_runtime.hpp"
#include "seam/authoring/voicebank_installer_service.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"

namespace {

seam::voicebank::VoicebankCandidate installStyleFixture(
    const std::filesystem::path& root, seam::authoring::VoicebankSession& session,
    bool singleStyle = false) {
  const auto source = root / "source";
  std::filesystem::create_directories(source / "audio");
  const auto samples = seam::test::support::sineWave(48000U, 220.0, 0.12);
  CHECK(seam::voicebank::writePcm16Wav(source / "audio/a.wav", 48000U, 1U, samples));
  auto manifest = seam::test::support::makeManifest({
      seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
                                   seam::voicebank::UnitKind::Sustain, samples.size())});
  manifest.id = "style.migration.fixture";
  manifest.styles = singleStyle ? std::vector<std::string>{"original"}
                               : std::vector<std::string>{"soft", "original"};
  CHECK(seam::voicebank::ManifestJsonCodec{}.save(manifest, source / "manifest.json"));
  CHECK(seam::core::durableAtomicWriteText(source / "license.txt", "Synthetic integration fixture\n"));
  const auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const auto package = root / "style.seambank";
  CHECK(seam::distribution::packSeambank(source, package, key.value()));
  seam::authoring::VoicebankInstallerService installer(session, root / "installed");
  const auto installed = installer.install(seam::authoring::VoicebankInstallRequest{
      .packagePath = package,
      .trustedPublicKeys = {key.value().publicKey},
  });
  CHECK(installed);
  CHECK(installed.value().candidate.trust == seam::voicebank::VoicebankTrust::TrustedInstalled);
  return installed.value().candidate;
}

seam::domain::Project legacyProject(const seam::voicebank::VoicebankCandidate& candidate) {
  seam::application::ProjectFactory factory{800U};
  auto project = factory.createProject("Legacy style migration");
  const auto id = factory.addVocalTrack(project, "Singer");
  auto* track = project.findVocalTrack(id);
  track->voicebank = {candidate.manifest.id, candidate.manifest.version, candidate.contentHash};
  track->styleSelection = {seam::domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution, {}};
  return project;
}

std::string legacyFileBytes(const seam::voicebank::VoicebankCandidate& candidate) {
  const auto source = seam::core::readTextFileLimited(
      std::filesystem::path{SEAM_SCHEMA8_SOURCE_ROOT} /
          "tests/singing_quality/corpus/unequal-rests.seam", 1U << 20U);
  CHECK(source);
  auto tree = seam::formats::parseJson(source.value());
  CHECK(tree);
  CHECK(tree.value().find("schemaVersion")->asInt64() == 7);
  auto& bank = tree.value().find("vocalTracks")->asArray().front().asObject().at("voicebank");
  bank.asObject().at("id") = seam::formats::JsonValue{candidate.manifest.id};
  bank.asObject().at("version") = seam::formats::JsonValue{candidate.manifest.version};
  bank.asObject().at("contentHash") = seam::formats::JsonValue{candidate.contentHash};
  return seam::formats::stringifyJson(tree.value());
}

}

TEST_CASE("legacy style migration uses a real installed exact bank and is idempotent") {
  const auto root = seam::test::support::temporaryDirectory("style-migration");
  seam::authoring::VoicebankSession banks({{root / "installed",
                                          seam::voicebank::VoicebankRootKind::Installed}}, false);
  const auto installed = installStyleFixture(root, banks);
  auto project = legacyProject(installed);
  const auto migrated = banks.migrateLegacyStyles(project);
  CHECK(migrated);
  CHECK(migrated.value());
  CHECK(project.vocalTracks().front().styleSelection.styleId == "soft");
  CHECK(project.vocalTracks().front().styleSelection.origin ==
        seam::domain::VoiceStyleOrigin::LegacyManifestFirst);
  const auto resolved = project;
  const auto repeated = banks.migrateLegacyStyles(project);
  CHECK(repeated);
  CHECK(!repeated.value());
  CHECK(project == resolved);
}

TEST_CASE("legacy style migration preserves missing exact banks and deliberate choices") {
  const auto root = seam::test::support::temporaryDirectory("style-migration-unresolved");
  seam::authoring::VoicebankSession banks({{root / "installed",
                                          seam::voicebank::VoicebankRootKind::Installed}}, false);
  const auto installed = installStyleFixture(root, banks);
  auto missing = legacyProject(installed);
  missing.vocalTracks().front().voicebank.contentHash = std::string(64U, '0');
  const auto before = missing;
  const auto unresolved = banks.migrateLegacyStyles(missing);
  CHECK(unresolved);
  CHECK(!unresolved.value());
  CHECK(missing == before);
  auto chosen = legacyProject(installed);
  chosen.vocalTracks().front().styleSelection = {seam::domain::VoiceStyleOrigin::Explicit, "original"};
  const auto noMigration = banks.migrateLegacyStyles(chosen);
  CHECK(noMigration);
  CHECK(!noMigration.value());
  CHECK(chosen.vocalTracks().front().styleSelection.styleId == "original");
}

TEST_CASE("project open migrates exact legacy style without changing durable base bytes") {
  const auto root = seam::test::support::temporaryDirectory("style-migration-open");
  seam::authoring::VoicebankSession banks({{root / "installed",
                                          seam::voicebank::VoicebankRootKind::Installed}}, false);
  const auto installed = installStyleFixture(root, banks);
  const auto original = legacyFileBytes(installed);
  const auto path = root / "old-song.seam";
  CHECK(seam::core::durableAtomicWriteText(path, original));
  seam::authoring::ProjectDocument document{seam::domain::Project{},
                                            seam::application::ProjectFactory{900U}};
  seam::authoring::ProjectLifecycleService lifecycle{&banks};
  CHECK(lifecycle.open(document, path));
  const auto& selected = document.session().project().vocalTracks().front().styleSelection;
  CHECK(selected.origin == seam::domain::VoiceStyleOrigin::LegacyManifestFirst);
  CHECK(selected.styleId == "soft");
  CHECK(!document.dirty());
  CHECK(document.identity().baseProjectHash == seam::core::sha256Hex(original));
  CHECK(seam::core::readTextFileLimited(path, 1U << 20U).value() == original);
  CHECK(lifecycle.save(document));
  const auto saved = seam::formats::ProjectJsonCodec{}.load(path);
  CHECK(saved);
  CHECK(saved.value().vocalTracks().front().styleSelection == selected);
  CHECK(document.identity().baseProjectHash != seam::core::sha256Hex(original));
}

TEST_CASE("autosave recovery migrates legacy style without rewriting recovery input") {
  const auto root = seam::test::support::temporaryDirectory("style-migration-recover");
  seam::authoring::VoicebankSession banks({{root / "installed",
                                          seam::voicebank::VoicebankRootKind::Installed}}, false);
  const auto installed = installStyleFixture(root, banks);
  seam::authoring::ProjectDocument source{legacyProject(installed),
                                          seam::application::ProjectFactory{900U}};
  CHECK(source.replaceProject(legacyProject(installed)));
  seam::authoring::AutosaveService autosave({.root = root / "recovery"});
  CHECK(autosave.request(source));
  CHECK(autosave.flush());
  const auto candidates = autosave.discover();
  CHECK(candidates);
  CHECK(candidates.value().size() == 1U);
  const auto& candidate = candidates.value().front();
  CHECK(candidate.recoverable);
  const auto original = seam::core::readTextFileLimited(candidate.autosavePath, 1U << 20U);
  CHECK(original);
  seam::authoring::ProjectDocument recovered{seam::domain::Project{},
                                             seam::application::ProjectFactory{900U}};
  CHECK(autosave.recover(recovered, candidate));
  CHECK(recovered.session().project().vocalTracks().front().styleSelection.origin ==
        seam::domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution);
  CHECK(autosave.recover(recovered, candidate, &banks));
  CHECK(recovered.session().project().vocalTracks().front().styleSelection.origin ==
        seam::domain::VoiceStyleOrigin::LegacyManifestFirst);
  CHECK(recovered.session().project().vocalTracks().front().styleSelection.styleId == "soft");
  CHECK(recovered.dirty());
  CHECK(recovered.identity().baseProjectHash == candidate.baseProjectHash);
  CHECK(seam::core::readTextFileLimited(candidate.autosavePath, 1U << 20U).value() == original.value());
}

TEST_CASE("runtime initialization resolves legacy style before the initial preview") {
  const auto root = seam::test::support::temporaryDirectory("style-migration-runtime");
  seam::authoring::VoicebankSession banks({}, false);
  const auto installed = installStyleFixture(root, banks);
  auto document = std::unique_ptr<seam::authoring::ProjectDocument>{
      new seam::authoring::ProjectDocument(
          legacyProject(installed), seam::application::ProjectFactory{900U})};
  document->markSaved(root / "song.seam", "durable-input-hash");
  const auto revision = document->session().revision();
  seam::authoring::AuthoringRuntime runtime{std::move(document), {
      .cacheRoot = root / "cache",
      .voicebankRoots = {{root / "installed", seam::voicebank::VoicebankRootKind::Installed}},
      .enableTransport = false,
  }};
  CHECK(runtime.initialize());
  CHECK(runtime.document().session().project().vocalTracks().front().styleSelection.origin ==
        seam::domain::VoiceStyleOrigin::LegacyManifestFirst);
  CHECK(runtime.document().session().project().vocalTracks().front().styleSelection.styleId == "soft");
  CHECK(runtime.document().session().revision() == revision);
  CHECK(!runtime.document().dirty());
  CHECK(runtime.document().identity().baseProjectHash == "durable-input-hash");
  runtime.shutdown();
}

TEST_CASE("relinking an exact legacy bank makes style migration undoable") {
  const auto root = seam::test::support::temporaryDirectory("style-migration-relink");
  seam::authoring::VoicebankSession installerSession({}, false);
  const auto installed = installStyleFixture(root, installerSession);
  seam::authoring::ProjectDocument document{legacyProject(installed),
                                            seam::application::ProjectFactory{900U}};
  const auto before = document.session().project();
  const auto trackId = before.vocalTracks().front().id;
  document.markSaved(root / "song.seam", "durable-input-hash");
  seam::authoring::VoicebankSession relink({}, false);
  const auto resolved = relink.relinkTrack(document, trackId,
      {root / "installed", seam::voicebank::VoicebankRootKind::Installed});
  CHECK(resolved);
  CHECK(resolved.value().resolved());
  CHECK(document.session().project().vocalTracks().front().styleSelection.styleId == "soft");
  CHECK(document.dirty());
  CHECK(document.identity().baseProjectHash == "durable-input-hash");
  CHECK(document.undo());
  CHECK(document.session().project() == before);
  CHECK(document.redo());
  CHECK(document.session().project().vocalTracks().front().styleSelection.styleId == "soft");
  const auto revision = document.session().revision();
  CHECK(relink.relinkTrack(document, trackId,
      {root / "installed", seam::voicebank::VoicebankRootKind::Installed}));
  CHECK(document.session().revision() == revision);
}

TEST_CASE("assigning a different bank cannot inherit legacy first-style migration") {
  const auto root = seam::test::support::temporaryDirectory("style-assignment-multiple");
  seam::authoring::VoicebankSession banks({}, false);
  const auto installed = installStyleFixture(root, banks);
  auto project = legacyProject(installed);
  project.vocalTracks().front().voicebank.id = "prior-singer";
  const auto before = project;
  const auto trackId = project.vocalTracks().front().id;
  seam::authoring::ProjectDocument document{project, seam::application::ProjectFactory{900U}};
  CHECK(banks.bindTrack(document, trackId, installed));
  CHECK(document.session().project().vocalTracks().front().styleSelection ==
        seam::domain::VoiceStyleSelection{});
  CHECK(banks.migrateLegacyStyles(document.session().project()));
  CHECK(document.session().project().vocalTracks().front().styleSelection.styleId.empty());
  CHECK(document.undo());
  CHECK(document.session().project() == before);
}

TEST_CASE("new single-style bank assignment and its style are one undo transaction") {
  const auto root = seam::test::support::temporaryDirectory("style-assignment-sole");
  seam::authoring::VoicebankSession banks({}, false);
  const auto installed = installStyleFixture(root, banks, true);
  auto project = legacyProject(installed);
  project.vocalTracks().front().voicebank.id = "prior-singer";
  const auto trackId = project.vocalTracks().front().id;
  const auto before = project;
  seam::authoring::ProjectDocument document{project, seam::application::ProjectFactory{900U}};
  CHECK(banks.bindTrack(document, trackId, installed));
  CHECK(document.session().revision() == 1U);
  const auto selected = document.session().project().vocalTracks().front().styleSelection;
  CHECK(selected.origin == seam::domain::VoiceStyleOrigin::SoleDeclaredStyle);
  CHECK(selected.styleId == "original");
  CHECK(document.undo());
  CHECK(document.session().project() == before);
  CHECK(document.redo());
  CHECK(document.session().project().vocalTracks().front().styleSelection == selected);
  seam::application::EditorSession adapter{project};
  CHECK(banks.bindTrack(adapter, trackId, installed));
  CHECK(adapter.project().vocalTracks().front().styleSelection == selected);
}

TEST_CASE("new project saves sole style only when its exact installed bank resolves") {
  for (const bool singleStyle : {true, false}) {
    const auto root = seam::test::support::temporaryDirectory("style-new-project");
    seam::authoring::VoicebankSession banks({}, false);
    const auto installed = installStyleFixture(root, banks, singleStyle);
    seam::authoring::ProjectDocument document{seam::domain::Project{},
                                              seam::application::ProjectFactory{900U}};
    const auto path = root / "new-song.seam";
    seam::authoring::ProjectLifecycleService lifecycle{&banks};
    CHECK(lifecycle.createNew(document, {
        .name = "New singer song", .initialVoicebank = installed, .projectPath = path}));
    const auto expected = singleStyle
        ? seam::domain::VoiceStyleSelection{seam::domain::VoiceStyleOrigin::SoleDeclaredStyle, "original"}
        : seam::domain::VoiceStyleSelection{};
    CHECK(document.session().project().vocalTracks().front().styleSelection == expected);
    const auto loaded = seam::formats::ProjectJsonCodec{}.load(path);
    CHECK(loaded);
    CHECK(loaded.value().vocalTracks().front().styleSelection == expected);
    CHECK(!document.dirty());
    seam::authoring::ProjectLifecycleService noCatalog;
    CHECK(noCatalog.createNew(document, {.name = "Unresolved new song", .initialVoicebank = installed}));
    CHECK(document.session().project().vocalTracks().front().styleSelection ==
          seam::domain::VoiceStyleSelection{});
  }
}
