#include "render_driver.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voicebank/asset_path.hpp"
#include "seam/voicebank/manifest_json.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace seam::singing_quality {
namespace {

using AudioLock = std::map<std::string, std::string, std::less<>>;

core::Result<AudioLock> loadAudioLock(const std::filesystem::path& path) {
  const auto text = core::readTextFileLimited(path, 1024U * 1024U);
  if (!text) return core::Result<AudioLock>{text.error()};
  const auto parsed = formats::parseJson(text.value());
  if (!parsed) return core::Result<AudioLock>{parsed.error()};
  if (!parsed.value().isObject() || parsed.value().asObject().empty()) {
    return core::failure<AudioLock>(core::ErrorCode::ParseError,
                                   "Audio lock must be a nonempty path-to-SHA256 object");
  }
  AudioLock result;
  for (const auto& [name, value] : parsed.value().asObject()) {
    if (!value.isString() || value.asString().size() != 64U ||
        !std::all_of(value.asString().begin(), value.asString().end(), [](char character) {
          return (character >= '0' && character <= '9') ||
                 (character >= 'a' && character <= 'f');
        })) {
      return core::failure<AudioLock>(core::ErrorCode::ParseError,
                                     "Audio lock contains an invalid SHA256", name);
    }
    result.emplace(name, value.asString());
  }
  return result;
}

core::Result<void> verifyBankAudio(const voicebank::Manifest& manifest,
                                  const std::filesystem::path& root,
                                  const AudioLock& lock) {
  std::set<std::string> visited;
  for (const auto& unit : manifest.units) {
    const auto name = unit.audioPath.generic_string();
    if (!visited.insert(name).second) continue;
    const auto expected = lock.find(name);
    if (expected == lock.end()) {
      return core::failure(core::ErrorCode::Conflict, "Manifest audio is not locked", name);
    }
    const auto asset = voicebank::resolveBankAsset(root, unit.audioPath);
    if (!asset) return core::Result<void>{asset.error()};
    const auto actual = core::sha256File(asset.value(), 32ULL * 1024ULL * 1024ULL);
    if (!actual) return core::Result<void>{actual.error()};
    if (actual.value() != expected->second) {
      return core::failure(core::ErrorCode::Conflict, "Audio SHA256 differs from lock", name);
    }
  }
  if (visited.size() != lock.size()) {
    return core::failure(core::ErrorCode::Conflict, "Audio lock has unreferenced entries");
  }
  return core::success();
}

}

core::Result<PreparedRender> prepare(const Invocation& invocation) {
  const auto project = formats::ProjectJsonCodec{}.load(invocation.project);
  if (!project) return core::Result<PreparedRender>{project.error()};
  const auto manifest = voicebank::ManifestJsonCodec{}.load(invocation.manifest);
  if (!manifest) return core::Result<PreparedRender>{manifest.error()};
  const auto& song = project.value();
  if (song.vocalTracks().size() != 1U || !song.audioTracks().empty() ||
      song.vocalTracks().front().regions.size() != 1U) {
    return core::failure<PreparedRender>(core::ErrorCode::Unsupported,
        "Diagnostic driver requires one vocal track, one region and no backing tracks");
  }
  const auto& track = song.vocalTracks().front();
  const auto& region = track.regions.front();
  const auto& bank = manifest.value();
  if (track.voicebank.id != bank.id || track.voicebank.version != bank.version ||
      region.startTick != time::Tick{0} || track.muted || track.solo ||
      track.gainDb != 0.0F || track.pan != 0.0F || region.notes.size() > 256U) {
    return core::failure<PreparedRender>(core::ErrorCode::InvalidArgument,
        "Diagnostic track must reference the supplied bank, use neutral mix and start at zero");
  }
  const auto frames = song.tempoMap().sampleFrameAt(region.durationTick, kSampleRate);
  if (frames <= 0 || frames > kMaximumFrames) {
    return core::failure<PreparedRender>(core::ErrorCode::Unsupported,
                                         "Diagnostic project must be at most 65 seconds");
  }
  const auto lock = loadAudioLock(invocation.audioLock);
  if (!lock) return core::Result<PreparedRender>{lock.error()};
  const auto bankRoot = invocation.manifest.parent_path();
  const auto verified = verifyBankAudio(bank, bankRoot, lock.value());
  if (!verified) return core::Result<PreparedRender>{verified.error()};
  const auto segments = rendering::PhraseSegmenter{}.segment(region);
  if (!segments) return core::Result<PreparedRender>{segments.error()};
  if (segments.value().empty()) {
    return core::failure<PreparedRender>(core::ErrorCode::InvalidArgument,
                                         "Diagnostic project has no renderable notes");
  }
  std::vector<rendering::RenderSnapshot> snapshots;
  synthesis::PhraseRenderOptions options;
  options.renderer.policy = invocation.policy;
  for (const auto& segment : segments.value()) {
    auto snapshot = rendering::RenderSnapshotFactory{}.create(
        song, bank, track.id, segment, 1U, rendering::RenderQuality::Final,
        bankRoot, kSampleRate, "original", options);
    if (!snapshot) return core::Result<PreparedRender>{snapshot.error()};
    for (const auto& identity : snapshot.value().selectedUnits) {
      const auto* unit = bank.findUnit(identity.unitId);
      if (unit == nullptr || lock.value().at(unit->audioPath.generic_string()) != identity.audioSha256) {
        return core::failure<PreparedRender>(core::ErrorCode::Conflict,
            "Frozen snapshot audio differs from the corpus lock", identity.unitId);
      }
    }
    snapshots.push_back(std::move(snapshot).value());
  }
  const auto projectHash = core::sha256File(invocation.project);
  if (!projectHash) return core::Result<PreparedRender>{projectHash.error()};
  const auto manifestHash = core::sha256File(invocation.manifest);
  if (!manifestHash) return core::Result<PreparedRender>{manifestHash.error()};
  return PreparedRender{song, std::move(snapshots), frames,
                         projectHash.value(), manifestHash.value()};
}

}
