#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/content_identity.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for Phase 12A tests
#endif

namespace {

std::shared_ptr<const seam::clap_editor::RenderedPreview> waitFor(
    seam::clap_editor::EditorRuntime& runtime,
    seam::clap_editor::PreviewStatus status) {
  for (int attempt = 0; attempt < 600; ++attempt) {
    auto preview = runtime.renderedPreview();
    if (preview != nullptr && preview->revision == runtime.revision() &&
        preview->status == status) {
      return preview;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return runtime.renderedPreview();
}

}  // namespace

int main() {
  using namespace seam;
  const auto fixture = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK};
  voicebank::VoicebankCatalog catalog;
  const std::vector roots{voicebank::VoicebankSearchRoot{
      .path = fixture,
      .kind = voicebank::VoicebankRootKind::Development,
  }};
  auto scanned = catalog.scan(roots);
  if (!scanned || scanned.value().size() != 1U) return 1;
  const auto& candidate = scanned.value().front();
  if (candidate.trust != voicebank::VoicebankTrust::DevelopmentFixture ||
      candidate.contentHash.empty() ||
      candidate.manifest.id != "demo.public-domain.human.production") {
    return 2;
  }

  const domain::VoicebankReference exact{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash,
  };
  if (!catalog.resolve(exact, scanned.value()).resolved()) return 3;
  auto wrongVersion = exact;
  wrongVersion.version = "999.0.0";
  if (catalog.resolve(wrongVersion, scanned.value()).status !=
      voicebank::VoicebankResolveStatus::VersionMismatch) return 4;
  auto noHash = exact;
  noHash.contentHash.clear();
  if (catalog.resolve(noHash, scanned.value()).status !=
      voicebank::VoicebankResolveStatus::ContentHashMissing) return 22;
  auto wrongHash = exact;
  wrongHash.contentHash.assign(64U, '0');
  if (catalog.resolve(wrongHash, scanned.value()).status !=
      voicebank::VoicebankResolveStatus::ContentMismatch) return 5;
  auto presentationOnly = candidate.manifest;
  presentationOnly.displayName = "Changed presentation name";
  presentationOnly.characterId = "official.character.changed";
  presentationOnly.characterVersion = "99.0.0";
  auto presentationIdentity =
      voicebank::computeVoicebankContentHash(presentationOnly, candidate.bankRoot);
  if (!presentationIdentity || presentationIdentity.value() != candidate.contentHash) {
    return 26;
  }

  const auto installedRoot = std::filesystem::temp_directory_path() /
                             "project-seam-phase12a-installed";
  std::error_code installError;
  std::filesystem::remove_all(installedRoot, installError);
  const auto installedBank = installedRoot / candidate.manifest.id /
                             candidate.manifest.version;
  std::filesystem::create_directories(installedBank.parent_path(), installError);
  if (installError) return 17;
  std::filesystem::copy(fixture, installedBank,
                        std::filesystem::copy_options::recursive,
                        installError);
  if (installError) return 18;
  std::ofstream receipt(installedBank / "install-receipt.json");
  receipt << "{\n"
          << "  \"schemaVersion\": 2,\n"
          << "  \"voicebankId\": \"" << candidate.manifest.id << "\",\n"
          << "  \"voicebankVersion\": \"" << candidate.manifest.version << "\",\n"
          << "  \"contentHash\": \"" << candidate.contentHash << "\",\n"
          << "  \"packageDigest\": \"test-package\",\n"
          << "  \"signerKeyId\": \"test-key\",\n"
          << "  \"signatureValid\": true,\n"
          << "  \"signerTrusted\": true\n"
          << "}\n";
  receipt.close();
  auto installed = catalog.scan({voicebank::VoicebankSearchRoot{
      .path = installedRoot,
      .kind = voicebank::VoicebankRootKind::Installed,
  }});
  if (!installed || installed.value().size() != 1U ||
      installed.value().front().trust !=
          voicebank::VoicebankTrust::TrustedInstalled ||
      !catalog.resolve(exact, installed.value()).resolved()) return 19;
  auto duplicateRoots = catalog.scan({
      voicebank::VoicebankSearchRoot{
          .path = fixture,
          .kind = voicebank::VoicebankRootKind::Development,
      },
      voicebank::VoicebankSearchRoot{
          .path = installedRoot,
          .kind = voicebank::VoicebankRootKind::Installed,
      },
  });
  const auto duplicateResolution = duplicateRoots
      ? catalog.resolve(exact, duplicateRoots.value())
      : voicebank::VoicebankResolution{};
  if (!duplicateRoots || duplicateRoots.value().size() != 2U ||
      !duplicateResolution.resolved() ||
      duplicateResolution.candidate->trust !=
          voicebank::VoicebankTrust::TrustedInstalled) return 27;

  {
    std::ofstream tamper(installedBank / "audio" / "human-vowel-demo.wav",
                         std::ios::binary | std::ios::app);
    tamper.put('x');
  }
  auto tampered = catalog.scan({voicebank::VoicebankSearchRoot{
      .path = installedRoot,
      .kind = voicebank::VoicebankRootKind::Installed,
  }});
  if (!tampered || tampered.value().size() != 1U ||
      catalog.resolve(exact, tampered.value()).status !=
          voicebank::VoicebankResolveStatus::ContentMismatch) return 20;

  clap_editor::EditorRuntime runtime(std::nullopt,
      std::filesystem::path{"assets/character-01"}, roots);
  auto preview = waitFor(runtime, clap_editor::PreviewStatus::Ready);
  if (preview == nullptr || preview->status != clap_editor::PreviewStatus::Ready ||
      preview->stereo.empty() || preview->unitCount != 8U ||
      preview->unitPlan.size() != 8U || preview->fallbackCount != 0U ||
      preview->phraseCount == 0U || preview->voicebankContentHash != candidate.contentHash) {
    return 6;
  }
  std::set<domain::UnitRendererKind> rendererKinds;
  for (const auto& entry : preview->unitPlan) rendererKinds.insert(entry.renderer);
  if (!rendererKinds.contains(domain::UnitRendererKind::Raw) ||
      !rendererKinds.contains(domain::UnitRendererKind::ClassicPsola) ||
      !rendererKinds.contains(domain::UnitRendererKind::SpectralClassic) ||
      !rendererKinds.contains(domain::UnitRendererKind::Stretch)) return 21;
  const auto project = runtime.projectCopy();
  if (project.vocalTracks().empty() ||
      project.vocalTracks().front().voicebank != exact) return 7;
  const auto encoded = clap_editor::encodeEditorState(project);
  if (!encoded) return 8;
  const auto decoded = clap_editor::decodeEditorState(encoded.value());
  if (!decoded || decoded.value().vocalTracks().front().voicebank != exact) return 9;

  const auto cacheRoot = std::filesystem::temp_directory_path() /
                         "project-seam-phase12a-parity-cache";
  std::error_code error;
  std::filesystem::remove_all(cacheRoot, error);
  rendering::PcmCache cache{cacheRoot};
  rendering::ProductionProjectRenderer renderer;
  const auto trackId = project.vocalTracks().front().id;
  const auto regionId = project.vocalTracks().front().regions.front().id;
  const std::array sources{rendering::TrackVoicebankSource{
      .trackId = trackId,
      .manifest = candidate.manifest,
      .bankRoot = candidate.bankRoot,
      .contentHash = candidate.contentHash,
  }};
  auto direct = renderer.render(project, sources, trackId, regionId,
                                runtime.revision(), 48000U,
                                rendering::RenderQuality::Preview,
                                synthesis::PhraseRenderOptions{}, &cache);
  if (!direct || direct.value().channelCount != preview->channelCount ||
      direct.value().interleaved != preview->interleaved ||
      direct.value().unitCount != preview->unitCount ||
      direct.value().activeUnitPlan != preview->unitPlan) return 10;
  auto cached = renderer.render(project, sources, trackId, regionId,
                                runtime.revision(), 48000U,
                                rendering::RenderQuality::Preview,
                                synthesis::PhraseRenderOptions{}, &cache);
  if (!cached || cached.value().cacheHits != cached.value().phraseCount ||
      cached.value().interleaved != direct.value().interleaved) return 12;

  auto missingProject = project;
  missingProject.vocalTracks().front().voicebank = domain::VoicebankReference{
      .id = "missing.voicebank",
      .version = "1.0.0",
      .contentHash = std::string(64U, 'a'),
  };
  clap_editor::EditorRuntime missing(std::move(missingProject),
      std::filesystem::path{"assets/character-01"}, roots);
  auto missingPreview = waitFor(missing,
      clap_editor::PreviewStatus::VoicebankMissing);
  if (missingPreview == nullptr || !missingPreview->stereo.empty() ||
      missing.voicebankResolution().resolved()) return 13;
  const auto relinked = missing.selectVoicebank(candidate.manifest.id,
                                                 candidate.manifest.version,
                                                 candidate.contentHash);
  if (!relinked) return 14;
  auto relinkedPreview = waitFor(missing, clap_editor::PreviewStatus::Ready);
  if (relinkedPreview == nullptr || relinkedPreview->stereo.empty() ||
      !missing.voicebankResolution().resolved()) return 15;

  auto rootRelinkProject = project;
  clap_editor::EditorRuntime rootRelink(std::move(rootRelinkProject),
      std::filesystem::path{"assets/character-01"}, {});
  const auto beforeRoot = rootRelink.voicebankResolution();
  if (beforeRoot.resolved()) return 23;
  const auto added = rootRelink.addVoicebankSearchRoot(
      voicebank::VoicebankSearchRoot{.path = fixture,
                                     .kind = voicebank::VoicebankRootKind::Development});
  if (!added) return 24;
  auto rootRelinkPreview = waitFor(rootRelink, clap_editor::PreviewStatus::Ready);
  if (rootRelinkPreview == nullptr || rootRelinkPreview->stereo.empty() ||
      !rootRelink.voicebankResolution().resolved()) return 25;

  auto mismatchedProject = project;
  mismatchedProject.vocalTracks().front().voicebank.contentHash.assign(64U, 'f');
  clap_editor::EditorRuntime mismatch(std::move(mismatchedProject),
      std::filesystem::path{"assets/character-01"}, roots);
  const auto mismatchPreview = waitFor(
      mismatch, clap_editor::PreviewStatus::VoicebankContentMismatch);
  if (mismatchPreview == nullptr || !mismatchPreview->stereo.empty()) return 16;

  std::cout << "Phase 12A tests PASS: bank=" << candidate.manifest.id
            << " phrases=" << preview->phraseCount
            << " units=" << preview->unitCount
            << " frames=" << preview->stereo.size() / 2U << '\n';
  return 0;
}
