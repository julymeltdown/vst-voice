// Synthetic PCM and declarations only. These tests establish byte handling,
// not a recording license, a singer, annotation truth or listening acceptance.
#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/prepared_phone_donor.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/take_inspection_receipt.hpp"
#include <algorithm>
#include <array>
#include <span>

namespace {
namespace p = seam::voicebank_production;
using J = seam::formats::JsonValue;
struct Fixture final {
  std::filesystem::path root{seam::test::support::temporaryDirectory("prepared-donor-snapshot")};
  std::string id{"fixture_l00000_n1"}, packetSha;
  std::vector<std::byte> audio = std::vector<std::byte>(204U, std::byte{0});
  explicit Fixture(bool twoDonors = false) {
    const auto tag = [&](std::size_t at, std::string_view text) {
      for (const auto c : text) audio[at++] = static_cast<std::byte>(c);
    };
    const auto le = [&](std::size_t at, std::uint32_t value, std::size_t width) {
      for (std::size_t n = 0; n < width; ++n) audio[at + n] = static_cast<std::byte>((value >> (8U * n)) & 255U);
    };
    tag(0, "RIFF"); tag(8, "WAVEfmt "); tag(36, "data");
    le(4, 196, 4); le(16, 16, 4); le(20, 1, 2); le(22, 1, 2);
    le(24, 8000, 4); le(28, 16000, 4); le(32, 2, 2); le(34, 16, 2); le(40, 160, 4);
    audio[44] = std::byte{23};
    const std::string license = "SYNTHETIC TEST FIXTURE ONLY\n";
    const std::string notice = "Synthetic fixture author; original PCM copied; unassigned and unapproved.\n";
    const auto audioSha = seam::core::sha256Hex(audio);
    const J sourceAudio = J::Object{{"path", "original.wav"}, {"sha256", audioSha}};
    const J sourceLabels = J::Object{{"path", "original.lab"}, {"sha256", seam::core::sha256Hex("0.01 0 aa\n")}};
    const J sourceLicense = J::Object{{"path", "LICENSE.txt"}, {"sha256", seam::core::sha256Hex(license)},
        {"sourceUrl", "https://example.invalid/synthetic-test"}, {"attribution", "Synthetic fixture author"}};
    const J contract = J::Object{{"format", "festvox-end-times-v1"}, {"annotationStatus", "AUTOMATIC_UNREVIEWED"},
        {"timeUnit", "seconds"}, {"boundaryRule", "ceil-seconds-times-sample-rate-v1"}};
    J::Array spans{J::Object{{"sourceId", "fixture"}, {"firstLabelIndex", std::int64_t{0}}, {"labelCount", std::int64_t{1}}}};
    if (twoDonors) spans.push_back(J::Object{{"sourceId", "fixture"}, {"firstLabelIndex", std::int64_t{1}}, {"labelCount", std::int64_t{1}}});
    const J plan = J::Object{{"schemaVersion", std::int64_t{1}}, {"documentKind", "SourceLabelDonorPlan"}, {"language", "en"},
        {"labelContract", contract}, {"license", sourceLicense},
        {"sources", J::Array{J::Object{{"id", "fixture"}, {"sourceType", "speech"}, {"audio", sourceAudio}, {"labels", sourceLabels}}}},
        {"spans", std::move(spans)}};
    const auto planText = seam::formats::stringifyJson(plan);
    const J proposal = J::Object{{"id", id}, {"derivedPath", id + ".wav"}, {"derivedSha256", audioSha},
        {"pcmSha256", seam::core::sha256Hex(std::span{audio}.subspan(44U))}, {"sourceId", "fixture"}, {"sourceType", "speech"},
        {"sourceAudio", sourceAudio}, {"sourceLabels", sourceLabels}, {"firstLabelIndex", std::int64_t{0}}, {"labelCount", std::int64_t{1}},
        {"sourceAnnotations", J::Array{J::Object{{"index", std::int64_t{0}}, {"startSeconds", "0"}, {"endSeconds", "0.01"},
          {"startFrame", std::int64_t{0}}, {"endFrameExclusive", std::int64_t{80}}, {"sourcePhone", "aa"}, {"opaqueColumn", "0"}}}},
        {"sourcePhones", J::Array{"aa"}}, {"startFrame", std::int64_t{0}}, {"endFrameExclusive", std::int64_t{80}}, {"sampleRate", std::int64_t{8000}},
        {"frames", std::int64_t{80}}, {"durationSeconds", 0.01}, {"nonzeroPcm", true}, {"assignment", "NOT_ASSIGNED"},
        {"pitchReview", "NOT_REVIEWED"}, {"phoneticReview", "NOT_REVIEWED"}, {"listeningReview", "NOT_REVIEWED"}};
    J::Array proposals{proposal};
    if (twoDonors) {
      const std::string secondId = "fixture_l00001_n1";
      auto secondAudio = audio; secondAudio[44] = std::byte{24};
      auto second = proposal;
      auto& fields = second.asObject();
      fields["id"] = secondId; fields["derivedPath"] = secondId + ".wav";
      fields["derivedSha256"] = seam::core::sha256Hex(secondAudio);
      fields["pcmSha256"] = seam::core::sha256Hex(std::span{secondAudio}.subspan(44U));
      fields["firstLabelIndex"] = std::int64_t{1}; fields["startFrame"] = std::int64_t{80}; fields["endFrameExclusive"] = std::int64_t{160};
      fields["sourceAnnotations"] = J::Array{J::Object{{"index", std::int64_t{1}}, {"startSeconds", "0.01"}, {"endSeconds", "0.02"},
          {"startFrame", std::int64_t{80}}, {"endFrameExclusive", std::int64_t{160}}, {"sourcePhone", "aa"}, {"opaqueColumn", "0"}}};
      proposals.push_back(std::move(second));
      CHECK(seam::core::durableAtomicWriteNew(root / (secondId + ".wav"), secondAudio));
    }
    const J packet = J::Object{{"schemaVersion", std::int64_t{1}}, {"documentKind", "UnassignedPhoneDonorProposals"},
        {"status", "UNAPPROVED"}, {"language", "en"}, {"planSha256", seam::core::sha256Hex(planText)},
        {"labelContract", contract}, {"license", sourceLicense}, {"retainedLicensePath", "license-source.txt"},
        {"retainedAttributionPath", "attribution.txt"}, {"retainedAttributionSha256", seam::core::sha256Hex(notice)},
        {"changeNotice", "Synthetic PCM copied; no pitch or phone assignment."}, {"producerRegistration", "NOT_REGISTERED"},
        {"performerRights", "NOT_ESTABLISHED"}, {"rightsClearance", "NOT_APPROVED"}, {"bankAdmission", "NOT_APPROVED"},
        {"proposals", std::move(proposals)}};
    const auto text = seam::formats::stringifyJson(packet); packetSha = seam::core::sha256Hex(text);
    CHECK(seam::core::durableAtomicWriteTextNew(root / "donors.json", text));
    CHECK(seam::core::durableAtomicWriteTextNew(root / "intake-plan.json", planText));
    CHECK(seam::core::durableAtomicWriteTextNew(root / "license-source.txt", license));
    CHECK(seam::core::durableAtomicWriteTextNew(root / "attribution.txt", notice));
    CHECK(seam::core::durableAtomicWriteNew(root / (id + ".wav"), audio));
  }
  ~Fixture() { std::error_code error; std::filesystem::remove_all(root, error); }
  p::PreparedPhoneDonorSnapshot load() const {
    auto result = p::loadPreparedPhoneDonor(root, packetSha, id); CHECK(result); return std::move(result.value());
  }
};
}

#if !defined(_WIN32)
TEST_CASE("Durable directory creation syncs ancestors before children and resyncs existing leaves") {
  const auto root = seam::test::support::temporaryDirectory("durable-directory-order");
  const auto target = root / "source-evidence/packets/packet";
  using Stage = seam::core::DirectoryCreateStage;
  std::vector<std::pair<std::filesystem::path, Stage>> events;
  const std::array chain{root, root / "source-evidence", root / "source-evidence/packets", target};
  const auto result = seam::core::durableCreateDirectories(target,
      [&](Stage stage, const std::filesystem::path& directory) -> seam::core::Result<void> {
        const auto found = std::find(chain.begin(), chain.end(), directory); CHECK(found != chain.end());
        const auto next = found + 1;
        if (next != chain.end()) CHECK(!std::filesystem::exists(*next));
        CHECK(std::filesystem::is_directory(directory));
        events.emplace_back(directory, stage); return seam::core::success();
      });
  CHECK(result);
  std::vector<std::pair<std::filesystem::path, Stage>> expected;
  for (const auto& directory : chain) {
    expected.emplace_back(directory, Stage::DirectoryReady);
    expected.emplace_back(directory, Stage::ParentSyncCompleted);
  }
  CHECK(events == expected);
  events.clear();
  CHECK(seam::core::durableCreateDirectories(target,
      [&](Stage stage, const std::filesystem::path& directory) {
        events.emplace_back(directory, stage); return seam::core::success();
      }));
  expected = {{target, Stage::DirectoryReady}, {target, Stage::ParentSyncCompleted}};
  CHECK(events == expected);
  std::filesystem::remove_all(root);
}

TEST_CASE("Durable directory failures stop before descendants and retry the unsynced component") {
  const auto root = seam::test::support::temporaryDirectory("durable-directory-failure");
  using Stage = seam::core::DirectoryCreateStage;
  std::size_t caseId = 0U;
  for (const auto stage : {Stage::DirectoryReady, Stage::ParentSyncCompleted}) {
    for (const auto depth : {1U, 2U, 3U}) {
      const auto base = root / std::to_string(caseId++);
      const auto target = base / "source-evidence/packets/packet/leaf";
      const std::array chain{base, base / "source-evidence", base / "source-evidence/packets", base / "source-evidence/packets/packet"};
      const auto stop = chain[depth]; bool injected = false;
      const auto failed = seam::core::durableCreateDirectories(target,
          [&](Stage at, const std::filesystem::path& directory) {
            if (directory == stop && at == stage) {
              injected = true;
              return seam::core::failure(seam::core::ErrorCode::IoError, "Injected directory durability failure");
            }
            return seam::core::success();
          });
      CHECK(!failed); CHECK(injected); CHECK(!std::filesystem::exists(target));
      bool retried = false;
      CHECK(seam::core::durableCreateDirectories(target,
          [&](Stage at, const std::filesystem::path& directory) {
            if (directory == stop && at == Stage::ParentSyncCompleted) retried = true;
            return seam::core::success();
          }));
      CHECK(retried); CHECK(std::filesystem::is_directory(target));
    }
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("Durable directory creation refuses links and non-directory ancestors without traversing them") {
  const auto root = seam::test::support::temporaryDirectory("durable-directory-unsafe");
  const auto outside = root / "outside"; std::filesystem::create_directory(outside);
  std::filesystem::create_directory_symlink(outside, root / "linked");
  std::filesystem::create_directory_symlink(root / "missing", root / "dangling");
  CHECK(seam::core::durableAtomicWriteTextNew(root / "file", "unrelated bytes"));
  for (const auto name : {"linked", "dangling", "file"}) {
    CHECK(!seam::core::durableCreateDirectories(root / name));
    CHECK(!seam::core::durableCreateDirectories(root / name / "child"));
  }
  CHECK(!seam::core::durableCreateDirectories({}));
  CHECK(std::filesystem::is_empty(outside)); CHECK(!std::filesystem::exists(root / "missing"));
  CHECK(seam::core::readTextFileLimited(root / "file", 100U).value() == "unrelated bytes");
  std::filesystem::remove_all(root);
}
#endif

TEST_CASE("Prepared donor evidence verifies packet and notices without claiming raw audio") {
  Fixture f;
  auto snapshot = f.load();
  CHECK(std::filesystem::remove(f.root / (f.id + ".wav")));
  const auto evidence = p::loadPreparedPhoneDonorEvidence(f.root, f.packetSha, f.id);
  CHECK(evidence);
  CHECK(evidence.value().derivedAudioSha256 == snapshot.audio.sha256);
  CHECK(evidence.value().packet.bytes == snapshot.evidence.packet.bytes);
  CHECK(!p::loadPreparedPhoneDonor(f.root, f.packetSha, f.id));
  CHECK(std::filesystem::remove(f.root / "attribution.txt"));
  CHECK(!p::loadPreparedPhoneDonorEvidence(f.root, f.packetSha, f.id));
}

TEST_CASE("Owned donor snapshot revalidation uses held bytes after preparation paths disappear") {
  Fixture f;
  auto snapshot = f.load();
  const auto originalPacket = snapshot.evidence.packet.bytes;
  const auto originalAudio = snapshot.audio.bytes;
  CHECK(std::filesystem::remove_all(f.root) > 0U);
  snapshot.evidence.proposalJson = "untrusted cached text";
  snapshot.evidence.derivedAudioSha256 = "untrusted cached hash";
  snapshot.sampleRate = 1U; snapshot.frames = 1U; snapshot.nonzeroPcm = false; snapshot.pcmSha256 = "stale";
  auto result = p::validatePreparedPhoneDonorSnapshot(std::move(snapshot), f.packetSha, f.id);
  CHECK(result);
  CHECK(result.value().sampleRate == 8000U); CHECK(result.value().frames == 80U); CHECK(result.value().nonzeroPcm);
  CHECK(result.value().evidence.packet.bytes == originalPacket); CHECK(result.value().audio.bytes == originalAudio);
  CHECK(result.value().evidence.derivedAudioSha256 == result.value().audio.sha256);
  CHECK(seam::formats::parseJson(result.value().evidence.proposalJson));
}

TEST_CASE("Owned donor snapshot refuses mutation of every held file and changed selection") {
  Fixture f;
  const auto original = f.load();
  for (std::size_t i = 0; i < 5U; ++i) {
    auto changed = original;
    const std::array files{&changed.evidence.packet, &changed.evidence.plan, &changed.evidence.license,
        &changed.evidence.attribution, &changed.audio};
    files[i]->bytes.push_back(std::byte{1});
    // Updating a mutable cached digest does not make the changed bytes trusted.
    files[i]->sha256 = seam::core::sha256Hex(files[i]->bytes);
    CHECK(!p::validatePreparedPhoneDonorSnapshot(std::move(changed), f.packetSha, f.id));
  }
  auto renamed = original; renamed.evidence.attribution.relativePath = "other.txt";
  CHECK(!p::validatePreparedPhoneDonorSnapshot(std::move(renamed), f.packetSha, f.id));
  auto audioName = original; audioName.audio.relativePath = "other.wav";
  CHECK(!p::validatePreparedPhoneDonorSnapshot(std::move(audioName), f.packetSha, f.id));
  auto selected = original; selected.evidence.donorId = "other_l00000_n1";
  CHECK(!p::validatePreparedPhoneDonorSnapshot(std::move(selected), f.packetSha, f.id));
  CHECK(!p::validatePreparedPhoneDonorSnapshot(original, std::string(64U, 'a'), f.id));
  CHECK(!p::validatePreparedPhoneDonorSnapshot(original, f.packetSha, "other_l00000_n1"));
}

TEST_CASE("Prepared donor evidence and owned validation honor pre-cancellation") {
  Fixture f; const auto snapshot = f.load();
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!p::loadPreparedPhoneDonorEvidence(f.root, f.packetSha, f.id, cancelled.get_token()));
  CHECK(!p::loadPreparedPhoneDonor(f.root, f.packetSha, f.id, cancelled.get_token()));
  CHECK(!p::validatePreparedPhoneDonorSnapshot(snapshot, f.packetSha, f.id, cancelled.get_token()));
}

TEST_CASE("Prepared donor selected fact types are checked before JSON normalization") {
  for (const auto& [key, rawValue] : {std::pair{"frames", "80.0"}, {"sampleRate", "8000.0"},
      {"startFrame", "0.0"}, {"endFrameExclusive", "80.0"}, {"nonzeroPcm", "1"}, {"durationSeconds", "\"0.01\""}}) {
    Fixture f; auto snapshot = f.load();
    const auto original = seam::core::readTextFileLimited(f.root / "donors.json", 1024U * 1024U); CHECK(original);
    auto packet = seam::formats::parseJson(original.value()); CHECK(packet);
    packet.value().asObject()["proposals"].asArray()[0].asObject()[key] = "__selected_fact__";
    auto text = seam::formats::stringifyJson(packet.value(), false);
    const std::string marker = "\"__selected_fact__\"";
    const auto at = text.find(marker); CHECK(at != std::string::npos);
    text.replace(at, marker.size(), rawValue);
    const auto expected = seam::core::sha256Hex(text);
    CHECK(seam::core::durableAtomicWriteText(f.root / "donors.json", text));
    const auto evidence = p::loadPreparedPhoneDonorEvidence(f.root, expected, f.id); CHECK(!evidence);
    CHECK(evidence.error().code == seam::core::ErrorCode::InvalidArgument);
    CHECK(evidence.error().message.find(key) != std::string::npos);
    const auto loaded = p::loadPreparedPhoneDonor(f.root, expected, f.id); CHECK(!loaded);
    CHECK(loaded.error().code == seam::core::ErrorCode::InvalidArgument);
    CHECK(loaded.error().message.find(key) != std::string::npos);
    snapshot.evidence.packet.bytes.assign(reinterpret_cast<const std::byte*>(text.data()),
        reinterpret_cast<const std::byte*>(text.data() + text.size()));
    snapshot.evidence.packet.sha256 = expected;
    const auto owned = p::validatePreparedPhoneDonorSnapshot(std::move(snapshot), expected, f.id); CHECK(!owned);
    CHECK(owned.error().code == seam::core::ErrorCode::InvalidArgument);
    CHECK(owned.error().message.find(key) != std::string::npos);
  }
}

TEST_CASE("Prepared donor evidence refuses inconsistent declared PCM geometry without reading audio") {
  for (const auto& [key, value] : {std::pair{"sampleRate", std::int64_t{0}}, {"sampleRate", std::int64_t{192001}},
      {"frames", std::int64_t{0}}, {"frames", std::int64_t{-1}}, {"startFrame", std::int64_t{-1}},
      {"startFrame", std::int64_t{1}}, {"endFrameExclusive", std::int64_t{81}}, {"endFrameExclusive", std::int64_t{0}},
      {"durationSeconds", std::int64_t{1}}}) {
    Fixture f; auto snapshot = f.load();
    auto packet = seam::formats::parseJson(std::string_view{reinterpret_cast<const char*>(snapshot.evidence.packet.bytes.data()),
        snapshot.evidence.packet.bytes.size()}); CHECK(packet);
    packet.value().asObject()["proposals"].asArray()[0].asObject()[key] = value;
    const auto text = seam::formats::stringifyJson(packet.value());
    const auto expected = seam::core::sha256Hex(text);
    CHECK(seam::core::durableAtomicWriteText(f.root / "donors.json", text));
    // A missing WAV must not be needed to reject inconsistent metadata.
    CHECK(std::filesystem::remove(f.root / (f.id + ".wav")));
    const auto evidence = p::loadPreparedPhoneDonorEvidence(f.root, expected, f.id); CHECK(!evidence);
    CHECK(evidence.error().code == seam::core::ErrorCode::InvalidArgument);
    CHECK(evidence.error().message.find("declared PCM facts") != std::string::npos);
    snapshot.evidence.packet.bytes.assign(reinterpret_cast<const std::byte*>(text.data()),
        reinterpret_cast<const std::byte*>(text.data() + text.size()));
    snapshot.evidence.packet.sha256 = expected;
    const auto owned = p::validatePreparedPhoneDonorSnapshot(std::move(snapshot), expected, f.id); CHECK(!owned);
    CHECK(owned.error().message.find("declared PCM facts") != std::string::npos);
  }
}

namespace {
struct DeclaredPcm final {
  const char* name;
  std::int64_t rate, frames, start;
};

// Change only the second donor's declared interval, rate and duration. The first
// donor and all retained metadata bindings remain valid. No measured WAV or
// source-annotation truth is claimed by these metadata-only fixtures.
std::string replaceDeclaredPcm(p::PreparedPhoneDonorSnapshot& snapshot, const DeclaredPcm& pcm) {
  auto packet = seam::formats::parseJson(std::string_view{
      reinterpret_cast<const char*>(snapshot.evidence.packet.bytes.data()), snapshot.evidence.packet.bytes.size()});
  CHECK(packet);
  auto& row = packet.value().asObject()["proposals"].asArray()[1].asObject();
  row["sampleRate"] = pcm.rate; row["frames"] = pcm.frames;
  row["startFrame"] = pcm.start; row["endFrameExclusive"] = pcm.start + pcm.frames;
  row["durationSeconds"] = static_cast<double>(pcm.frames) / static_cast<double>(pcm.rate);
  const auto text = seam::formats::stringifyJson(packet.value());
  const auto decoded = seam::formats::parseJson(text); CHECK(decoded);
  const auto& encoded = decoded.value().asObject().at("proposals").asArray()[1].asObject();
  CHECK(encoded.at("endFrameExclusive").asInt64() - encoded.at("startFrame").asInt64() == pcm.frames);
  CHECK(encoded.at("durationSeconds").asNumber() == static_cast<double>(pcm.frames) / static_cast<double>(pcm.rate));
  snapshot.evidence.packet.bytes.assign(reinterpret_cast<const std::byte*>(text.data()),
      reinterpret_cast<const std::byte*>(text.data() + text.size()));
  snapshot.evidence.packet.sha256 = seam::core::sha256Hex(text);
  return text;
}

auto heldMetadata(const p::PreparedPhoneDonorSnapshot& snapshot) {
  return std::array{snapshot.evidence.packet, snapshot.evidence.plan,
      snapshot.evidence.license, snapshot.evidence.attribution};
}
}

TEST_CASE("Prepared donor metadata accepts canonical PCM range boundaries without audio") {
  for (const auto& pcm : {DeclaredPcm{"lower bounds", 8000, 1, 0},
      DeclaredPcm{"upper bounds", 192000, 33554410, 0}}) {
    Fixture f(true);
    auto snapshot = p::loadPreparedPhoneDonor(f.root, f.packetSha, "fixture_l00001_n1"); CHECK(snapshot);
    CHECK(std::filesystem::remove(f.root / (f.id + ".wav")));
    CHECK(std::filesystem::remove(f.root / "fixture_l00001_n1.wav"));
    const auto text = replaceDeclaredPcm(snapshot.value(), pcm);
    const auto expected = snapshot.value().evidence.packet.sha256;
    CHECK(seam::core::durableAtomicWriteText(f.root / "donors.json", text));
    CHECK(p::loadPreparedPhoneDonorEvidence(f.root, expected, snapshot.value().evidence.donorId));
    const auto directory = p::loadPreparedPhoneDonorPacketFacts(f.root, expected); CHECK(directory);
    const auto owned = p::validatePreparedPhoneDonorPacketFacts(heldMetadata(snapshot.value()), expected); CHECK(owned);
    CHECK(directory.value().derivedAudioSha256ByDonor == owned.value().derivedAudioSha256ByDonor);
    CHECK(owned.value().derivedAudioSha256ByDonor.size() == 2U);
    std::cout << "[PCM-RANGES] " << pcm.name << ": PASS selected/directory-facts/owned-facts\n";
  }
}

TEST_CASE("Prepared donor metadata refuses isolated PCM range violations before reading audio") {
  for (const auto& pcm : {DeclaredPcm{"rate below minimum", 4000, 80, 0},
      DeclaredPcm{"rate above maximum", 200000, 80, 0}, DeclaredPcm{"zero frames", 8000, 0, 0},
      DeclaredPcm{"frames above cap", 8000, 33560000, 0}, DeclaredPcm{"negative start", 8000, 80, -1}}) {
    // Exactly one range category is invalid. Zero frames also makes end == start;
    // with an exact interval length these two conjuncts cannot be isolated.
    const int invalidCategories = static_cast<int>(pcm.rate < 8000 || pcm.rate > 192000) +
        static_cast<int>(pcm.frames <= 0 || pcm.frames > 33554410) + static_cast<int>(pcm.start < 0);
    CHECK(invalidCategories == 1);
    CHECK(pcm.frames == 0 || pcm.start + pcm.frames > pcm.start);
    Fixture f(true);
    auto snapshot = p::loadPreparedPhoneDonor(f.root, f.packetSha, "fixture_l00001_n1"); CHECK(snapshot);
    CHECK(p::loadPreparedPhoneDonorPacketFacts(f.root, f.packetSha));
    CHECK(p::validatePreparedPhoneDonorPacketFacts(heldMetadata(snapshot.value()), f.packetSha));
    CHECK(std::filesystem::remove(f.root / (f.id + ".wav")));
    CHECK(std::filesystem::remove(f.root / "fixture_l00001_n1.wav"));
    const auto text = replaceDeclaredPcm(snapshot.value(), pcm);
    const auto expected = snapshot.value().evidence.packet.sha256;
    CHECK(seam::core::durableAtomicWriteText(f.root / "donors.json", text));
    // Selected-only readers still accept the untouched first donor. Whole-packet
    // readers must reject the second row rather than cache it unchecked.
    CHECK(p::loadPreparedPhoneDonorEvidence(f.root, expected, f.id));
    const auto requireRangeRefusal = [](const auto& result) {
      CHECK(!result); CHECK(result.error().code == seam::core::ErrorCode::InvalidArgument);
      CHECK(result.error().message == "Prepared donor declared PCM facts are inconsistent or exceed canonical WAV bounds");
    };
    requireRangeRefusal(p::loadPreparedPhoneDonorEvidence(f.root, expected, snapshot.value().evidence.donorId));
    requireRangeRefusal(p::loadPreparedPhoneDonor(f.root, expected, snapshot.value().evidence.donorId));
    requireRangeRefusal(p::loadPreparedPhoneDonorPacketFacts(f.root, expected));
    requireRangeRefusal(p::validatePreparedPhoneDonorPacketFacts(heldMetadata(snapshot.value()), expected));
    requireRangeRefusal(p::validatePreparedPhoneDonorSnapshot(std::move(snapshot.value()), expected, "fixture_l00001_n1"));
    std::cout << "[PCM-RANGES] " << pcm.name << ": REFUSED selected/full/owned-snapshot/directory-facts/owned-facts\n";
  }
}

TEST_CASE("Shared packet facts identify two distinct donors without reading either WAV") {
  Fixture f(true);
  const auto first = f.load();
  const auto second = p::loadPreparedPhoneDonor(f.root, f.packetSha, "fixture_l00001_n1"); CHECK(second);
  CHECK(first.audio.sha256 != second.value().audio.sha256);
  CHECK(std::filesystem::remove(f.root / (f.id + ".wav")));
  CHECK(std::filesystem::remove(f.root / "fixture_l00001_n1.wav"));
  const auto facts = p::loadPreparedPhoneDonorPacketFacts(f.root, f.packetSha); CHECK(facts);
  CHECK(facts.value().packetSha256 == f.packetSha);
  CHECK(facts.value().intakePlanSha256 == first.evidence.plan.sha256);
  CHECK(facts.value().originalLicenseSha256 == first.evidence.license.sha256);
  CHECK(facts.value().attributionSha256 == first.evidence.attribution.sha256);
  CHECK(facts.value().derivedAudioSha256ByDonor.size() == 2U);
  CHECK(facts.value().derivedAudioSha256ByDonor.at(f.id) == first.audio.sha256);
  CHECK(facts.value().derivedAudioSha256ByDonor.at("fixture_l00001_n1") == second.value().audio.sha256);
  CHECK(!p::loadPreparedPhoneDonorPacketFacts(f.root, std::string(64U, '0')));
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!p::loadPreparedPhoneDonorPacketFacts(f.root, f.packetSha, cancelled.get_token()));
  CHECK(seam::core::durableAtomicWriteText(f.root / "attribution.txt", "changed after first facts read"));
  CHECK(!p::loadPreparedPhoneDonorPacketFacts(f.root, f.packetSha));
}

TEST_CASE("Shared packet facts check every donor declared geometry before returning a digest map") {
  for (const bool wrongWireType : {false, true}) {
    Fixture f(true);
    const auto raw = seam::core::readTextFileLimited(f.root / "donors.json", 1024U * 1024U); CHECK(raw);
    auto packet = seam::formats::parseJson(raw.value()); CHECK(packet);
    auto& fields = packet.value().asObject()["proposals"].asArray()[1].asObject();
    fields["sampleRate"] = wrongWireType ? J{"__wire_rate__"} : J{std::int64_t{0}};
    auto encoded = seam::formats::stringifyJson(packet.value());
    if (wrongWireType) {
      const auto at = encoded.find("\"__wire_rate__\""); CHECK(at != std::string::npos);
      encoded.replace(at, std::string{"\"__wire_rate__\""}.size(), "8000.0");
    }
    const auto expected = seam::core::sha256Hex(encoded);
    CHECK(seam::core::durableAtomicWriteText(f.root / "donors.json", encoded));
    // The selected-only API retains its existing contract. The whole-packet
    // facts API must not cache an unchecked second donor for a later binding.
    CHECK(p::loadPreparedPhoneDonorEvidence(f.root, expected, f.id));
    CHECK(!p::loadPreparedPhoneDonorPacketFacts(f.root, expected));
  }
}

namespace {
// Deliberately constructs a synthetic persisted model. This is not the future
// inspected prepared-donor importer and does not establish admission or QC.
struct BoundFixture final {
  Fixture prepared;
  std::filesystem::path workspace{prepared.root / "workspace"};
  p::VoicebankProductionProject project;
  p::ProductionProjectRepository repository{workspace};
  BoundFixture() {
    project.schemaVersion = p::kProductionPreparedDonorSchemaVersion;
    project.projectId = "synthetic-packet-binding"; project.language = "en";
    project.declaredPitchLayers = {60}; project.operators = {{"synthetic-producer", "PRODUCER"}};
    CHECK(repository.initialize(project, {"create", project.projectId, "synthetic-producer", "2026-10-15T00:00:00Z"}));
    const auto snapshot = prepared.load();
    const auto declaration = workspace / "source-evidence" / (seam::core::sha256Hex("SYNTHETIC REVIEWED DECLARATION") + ".txt");
    CHECK(seam::core::durableAtomicWriteTextNew(declaration, "SYNTHETIC REVIEWED DECLARATION"));
    p::SourceStrategyAssessment strategy{.id = "synthetic-source", .kind = p::SourceStrategyKind::HumanRecording,
        .rights = p::Feasibility::Pass, .permissions = {true, true, false, false},
        .licenseLocator = declaration.string(), .licenseSha256 = seam::core::sha256File(declaration).value(),
        .evidenceState = "SYNTHETIC_TEST_ONLY_NO_RIGHTS_OR_QC_ACCEPTANCE"};
    project.sourceStrategies.push_back(strategy); project.selectedSourceStrategyId = strategy.id;
    project.licenseLocator = strategy.licenseLocator; project.licenseSha256 = strategy.licenseSha256;
    const auto asset = p::ImmutableAssetStore{workspace / "assets"}.importFile(prepared.root / (prepared.id + ".wav"), p::AssetKind::Raw);
    CHECK(asset); project.assets.push_back(asset.value());
    project.inventoryId = "synthetic-inventory"; project.inventorySha256 = std::string(64U, 'a');
    const auto directory = workspace / "source-evidence/packets" / prepared.packetSha;
    for (const auto* file : {&snapshot.evidence.packet, &snapshot.evidence.plan, &snapshot.evidence.license, &snapshot.evidence.attribution})
      CHECK(seam::core::durableAtomicWriteNew(directory / file->relativePath, file->bytes));
    p::TakeSourceBinding binding{"", "take-neutral", asset.value().sha256, strategy, "synthetic-producer",
        "2026-10-15T00:01:00Z", "source-evidence/" + strategy.licenseSha256 + ".txt",
        p::PreparedDonorBinding{prepared.packetSha, prepared.id, snapshot.evidence.plan.sha256,
            snapshot.evidence.license.sha256, snapshot.evidence.attribution.sha256}};
    CHECK(binding.strategy.licenseSha256 != binding.preparedDonor->originalLicenseSha256);
    binding.id = p::takeSourceBindingId(project.projectId, binding); project.sourceBindings.push_back(binding);
    project.takes.push_back({.takeId = binding.takeId, .promptId = "prompt-neutral", .coverageKey = "sustain:a", .pitchLayer = 60,
        .rawAssetSha256 = asset.value().sha256, .state = p::UnitQueueState::MarkerReview, .sourceBindingId = binding.id, .style = "neutral"});
    project.unitAssignments.push_back({.coverageKey = "sustain:a", .pitchLayer = 60, .promptId = "prompt-neutral",
        .plannedTakeId = binding.takeId, .takeId = binding.takeId, .state = p::UnitQueueState::MarkerReview, .style = "neutral"});
    project.lifecycle = p::ProductionLifecycle::Experimental;
    CHECK(repository.save(project, {"save", "synthetic-model", "synthetic-producer", "2026-10-15T00:01:00Z"}));
  }
};
std::map<std::string, std::string> retainedFiles(const std::filesystem::path& root) {
  std::map<std::string, std::string> result;
  for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
    const auto name = item.path().lexically_relative(root).generic_string();
    if (item.is_symlink()) result[name] = "link:" + std::filesystem::read_symlink(item.path()).string();
    else if (item.is_regular_file()) { const auto digest = seam::core::sha256File(item.path()); CHECK(digest); result[name] = digest.value(); }
  }
  return result;
}
}

TEST_CASE("Prepared donor schema five round trips exact provenance and rejects partial or legacy bindings") {
  BoundFixture f;
  const auto encoded = p::encodeProductionProject(f.project);
  const auto decoded = p::decodeProductionProject(encoded); CHECK(decoded);
  CHECK(p::encodeProductionProject(decoded.value()) == encoded);
  CHECK(decoded.value().sourceBindings == f.project.sourceBindings);
  const auto json = seam::formats::parseJson(encoded); CHECK(json);
  for (const auto key : {"packetSha256", "donorId", "intakePlanSha256", "originalLicenseSha256", "attributionSha256"}) {
    auto missing = json.value(); missing.asObject()["sourceBindings"].asArray()[0].asObject()["preparedDonor"].asObject().erase(key);
    CHECK(!p::decodeProductionProject(seam::formats::stringifyJson(missing)));
    auto changed = f.project; auto& donor = *changed.sourceBindings[0].preparedDonor;
    if (std::string_view{key} == "packetSha256") donor.packetSha256 = "bad";
    else if (std::string_view{key} == "donorId") donor.donorId = "../untrusted";
    else if (std::string_view{key} == "intakePlanSha256") donor.intakePlanSha256 = "bad";
    else if (std::string_view{key} == "originalLicenseSha256") donor.originalLicenseSha256 = "bad";
    else donor.attributionSha256 = "bad";
    CHECK(p::takeSourceBindingId(changed.projectId, changed.sourceBindings[0]) != changed.sourceBindings[0].id);
    changed.sourceBindings[0].id = p::takeSourceBindingId(changed.projectId, changed.sourceBindings[0]);
    changed.takes[0].sourceBindingId = changed.sourceBindings[0].id;
    CHECK(!p::validateProductionProject(changed));
  }
  for (const auto schema : {std::int64_t{4}, std::int64_t{6}}) {
    auto changed = json.value(); changed.asObject()["schemaVersion"] = schema;
    const auto result = p::decodeProductionProject(seam::formats::stringifyJson(changed)); CHECK(!result);
    if (schema == 6) CHECK(result.error().code == seam::core::ErrorCode::Unsupported);
  }
  for (const bool unknown : {false, true}) {
    auto changed = json.value(); auto& value = changed.asObject()["sourceBindings"].asArray()[0].asObject()["preparedDonor"];
    if (unknown) value.asObject()["untrusted"] = true; else value = J{};
    CHECK(!p::decodeProductionProject(seam::formats::stringifyJson(changed)));
  }
  auto wrongSource = f.project; wrongSource.sourceBindings[0].strategy.kind = p::SourceStrategyKind::ProceduralSynthesis;
  CHECK(!p::validateProductionProject(wrongSource));
  auto wrongLanguage = f.project; wrongLanguage.language = "ja"; CHECK(!p::validateProductionProject(wrongLanguage));
}

TEST_CASE("Prepared donor retained evidence survives source deletion and supports independent reuse without reassigning a take") {
  BoundFixture f;
  for (const auto name : {"donors.json", "intake-plan.json", "license-source.txt", "attribution.txt"}) CHECK(std::filesystem::remove(f.prepared.root / name));
  CHECK(std::filesystem::remove(f.prepared.root / (f.prepared.id + ".wav")));
  CHECK(f.repository.verify(f.project));
  auto recovered = f.repository.recover(); CHECK(recovered);
  CHECK(p::encodeProductionProject(recovered.value()) == p::encodeProductionProject(f.project));
  const auto before = p::encodeProductionProject(f.project);
  auto changed = f.project; changed.sourceBindings[0].preparedDonor->attributionSha256 = std::string(64U, 'b');
  changed.sourceBindings[0].id = p::takeSourceBindingId(changed.projectId, changed.sourceBindings[0]);
  changed.takes[0].sourceBindingId = changed.sourceBindings[0].id;
  CHECK(!f.repository.save(changed, {"save", "rewrite", "synthetic-producer", "2026-10-15T00:02:00Z"}));
  CHECK(p::encodeProductionProject(f.repository.recover().value()) == before);
  auto binding = f.project.sourceBindings[0]; binding.takeId = "take-soft"; binding.id = p::takeSourceBindingId(f.project.projectId, binding);
  auto take = f.project.takes[0]; take.takeId = binding.takeId; take.promptId = "prompt-soft"; take.style = "soft"; take.sourceBindingId = binding.id;
  auto assignment = f.project.unitAssignments[0]; assignment.takeId = binding.takeId; assignment.plannedTakeId = binding.takeId;
  assignment.promptId = take.promptId; assignment.style = take.style;
  f.project.sourceBindings.push_back(binding); f.project.takes.push_back(take); f.project.unitAssignments.push_back(assignment);
  CHECK(f.repository.save(f.project, {"save", "independent-reuse", "synthetic-producer", "2026-10-15T00:03:00Z"}));
  CHECK(f.project.sourceBindings[0].id != f.project.sourceBindings[1].id);
  CHECK(f.repository.verify(f.project)); CHECK(f.project.assets.size() == 1U);
  const auto published = p::publishSampleCandidate(f.workspace, f.project, {}, f.prepared.root / "candidate"); CHECK(!published);
  // Schema-4 publication still requires a genuine complete reviewed request.
  CHECK(!std::filesystem::exists(f.prepared.root / "candidate"));
}

TEST_CASE("Prepared donor retained notice damage stops recovery and saving without rollback or writes") {
  for (const auto name : {"donors.json", "intake-plan.json", "license-source.txt", "attribution.txt"}) {
    for (int mode = 0; mode < 3; ++mode) {
      BoundFixture f;
      const auto file = f.workspace / "source-evidence/packets" / f.prepared.packetSha / name;
      if (mode == 0) CHECK(std::filesystem::remove(file));
      else if (mode == 1) CHECK(seam::core::durableAtomicWriteText(file, "changed synthetic notice"));
      else { const auto backup = file.string() + ".backup"; std::filesystem::rename(file, backup); std::filesystem::create_symlink(backup, file); }
      const auto before = retainedFiles(f.workspace); const auto project = p::encodeProductionProject(f.project);
      const auto recovered = f.repository.recover(); CHECK(!recovered);
      CHECK(recovered.error().message.find("Retained source evidence") != std::string::npos);
      CHECK(!f.repository.save(f.project, {"save", "damaged", "synthetic-producer", "2026-10-15T00:02:00Z"}));
      CHECK(retainedFiles(f.workspace) == before); CHECK(p::encodeProductionProject(f.project) == project);
    }
  }
}

#if !defined(_WIN32)
namespace {
struct PreparedImportFixture final {
  Fixture prepared;
  std::filesystem::path workspace{prepared.root / "import-workspace"};
  p::ProductionProjectRepository repository{workspace};
  p::VoicebankProductionProject project;
  explicit PreparedImportFixture(bool twoDonors = false) : prepared(twoDonors) {
    project.schemaVersion = p::kProductionStyleSchemaVersion;
    project.projectId = "synthetic-inspected-import"; project.language = "en";
    project.inventoryId = "synthetic-inventory"; project.inventorySha256 = std::string(64U, 'a');
    project.declaredPitchLayers = {60};
    project.operators = {{"fixture-producer", "PRODUCER"}, {"fixture-reviewer", "REVIEWER"}};
    for (const auto style : {"neutral", "soft"}) project.unitAssignments.push_back({.coverageKey = "sustain:aa", .pitchLayer = 60,
        .promptId = "prompt-" + std::string{style}, .plannedTakeId = "take-" + std::string{style}, .style = style});
    CHECK(repository.initialize(project, {"create", project.projectId, "fixture-producer", "2026-10-15T00:00:00Z"}));
    const auto declaration = prepared.root / "reviewed-declaration.txt";
    CHECK(seam::core::durableAtomicWriteTextNew(declaration, "SYNTHETIC IMPORT TEST DECLARATION; NO REAL SINGER OR RIGHTS ACCEPTANCE"));
    p::SourceStrategyAssessment source{.id = "fixture-human-source", .kind = p::SourceStrategyKind::HumanRecording,
        .rights = p::Feasibility::Pass, .permissions = {true, true, false, false},
        .licenseLocator = declaration.string(), .licenseSha256 = seam::core::sha256File(declaration).value()};
    CHECK(repository.registerSource(project, source, seam::core::sha256Hex(p::encodeProductionProject(project)),
        "fixture-producer", "2026-10-15T00:01:00Z"));
  }
  p::PreparedDonorImportExpectation expectation() const {
    return {seam::core::sha256Hex(p::encodeProductionProject(project)), prepared.packetSha, prepared.id, project.selectedSourceStrategyId};
  }
  p::RawTakeInput input(std::string style = "neutral") const {
    return {.takeId = "take-" + style, .promptId = "prompt-" + style, .coverageKey = "sustain:aa", .pitchLayer = 60, .style = style};
  }
  p::ProductionJournalEvent event(const p::RawTakeInput& take) const {
    return {take.supersedesTakeId.empty() ? "import" : "retake", take.takeId, "fixture-producer", "2026-10-15T00:02:00Z"};
  }
  void checkPrivateCaptureRemoved() const {
    for (const auto& item : std::filesystem::directory_iterator(workspace))
      CHECK(!item.path().filename().string().starts_with(".prepared-import-"));
  }
};
}

TEST_CASE("Prepared donor inspected import consumes held bytes and atomically retains distinct declarations") {
  PreparedImportFixture f; auto snapshot = f.prepared.load(); const auto held = snapshot;
  const auto expectation = f.expectation(); const auto take = f.input(); const auto generation = f.project.lastDurableGeneration;
  for (const auto name : {"donors.json", "intake-plan.json", "license-source.txt", "attribution.txt"}) CHECK(std::filesystem::remove(f.prepared.root / name));
  CHECK(std::filesystem::remove(f.prepared.root / (f.prepared.id + ".wav")));
  snapshot.evidence.proposalJson = "stale cached data"; snapshot.frames = 1U; snapshot.sampleRate = 1U;
  const auto result = f.repository.importPreparedDonor(f.project, std::move(snapshot), expectation, take, f.event(take)); CHECK(result);
  CHECK(result.value().durabilityConfirmed); CHECK(f.project.lastDurableGeneration == generation + 1U);
  CHECK(f.project.schemaVersion == 5); CHECK(f.project.takes.size() == 1U); CHECK(f.project.sourceBindings.size() == 1U);
  CHECK(f.project.takes[0].state == p::UnitQueueState::MarkerReview); CHECK(f.project.reviews.empty());
  CHECK(f.project.sourceQualityAssessments.empty()); CHECK(!p::selectedStrategyReady(f.project));
  const auto& binding = f.project.sourceBindings[0]; CHECK(binding.preparedDonor);
  CHECK(binding.preparedDonor->packetSha256 == expectation.packetSha256); CHECK(binding.preparedDonor->donorId == expectation.donorId);
  CHECK(binding.strategy.licenseSha256 != binding.preparedDonor->originalLicenseSha256);
  CHECK(binding.id == p::takeSourceBindingId(f.project.projectId, binding)); CHECK(binding.rawAssetSha256 == held.audio.sha256);
  const auto raw = seam::core::readFileBytesLimited(p::ImmutableAssetStore{f.workspace / "assets"}.pathFor(result.value()), 1024U); CHECK(raw);
  CHECK(raw.value() == held.audio.bytes);
  const auto inspection = p::currentTakeInspection(f.project, take.takeId); CHECK(inspection);
  CHECK(inspection->inspection.sourceSha256 == held.audio.sha256); CHECK(inspection->inspection.measurements.sampleRate == 8000U);
  CHECK(inspection->inspection.measurements.frameCount == 80U); CHECK(!inspection->inspection.accepted());
  const auto directory = f.workspace / "source-evidence/packets" / expectation.packetSha256;
  for (const auto* file : {&held.evidence.packet, &held.evidence.plan, &held.evidence.license, &held.evidence.attribution}) {
    const auto retained = seam::core::readFileBytesLimited(directory / file->relativePath, 1024U * 1024U); CHECK(retained);
    CHECK(retained.value() == file->bytes);
  }
  CHECK(std::filesystem::remove(f.prepared.root / "reviewed-declaration.txt"));
  CHECK(f.repository.verify(f.project));
  CHECK(p::encodeProductionProject(f.repository.recover().value()) == p::encodeProductionProject(f.project));
  f.checkPrivateCaptureRemoved();
}

TEST_CASE("Prepared donor inspected import refuses mismatched requests actors assignments and held mutation without writes") {
  for (int mode = 0; mode < 13; ++mode) {
    PreparedImportFixture f; auto snapshot = f.prepared.load(); auto expected = f.expectation(); auto take = f.input(); auto event = f.event(take);
    if (mode == 0) expected.projectSha256 = std::string(64U, '0');
    else if (mode == 1) expected.packetSha256 = std::string(64U, '0');
    else if (mode == 2) expected.donorId = "other_l00000_n1";
    else if (mode == 3) expected.sourceStrategyId = "other-source";
    else if (mode == 4) take.promptId = "other-prompt";
    else if (mode == 5) take.style = "other-style";
    else if (mode == 6) event.operatorId = "fixture-reviewer";
    else if (mode == 7) take.initialState = p::UnitQueueState::Approved;
    else if (mode == 8) snapshot.audio.bytes.push_back(std::byte{1});
    else if (mode == 9) snapshot.evidence.attribution.bytes.push_back(std::byte{1});
    else if (mode == 10) event.subjectId = "other-take";
    else if (mode == 11) event.occurredAtUtc = "invalid";
    const auto before = retainedFiles(f.workspace); const auto project = p::encodeProductionProject(f.project);
    std::stop_source cancelled; if (mode == 12) cancelled.request_stop();
    const auto result = f.repository.importPreparedDonor(f.project, std::move(snapshot), expected, take, event, cancelled.get_token()); CHECK(!result);
    CHECK(retainedFiles(f.workspace) == before); CHECK(p::encodeProductionProject(f.project) == project);
    CHECK(p::encodeProductionProject(f.repository.recover().value()) == project); f.checkPrivateCaptureRemoved();
  }
}

TEST_CASE("Prepared donor import cancels or propagates injected errors at each precommit checkpoint") {
  using Stage = p::PreparedDonorImportStage;
  const std::array stages{Stage::SnapshotValidated, Stage::RawTakeStaged, Stage::NoticesRetained};
  for (const bool cancel : {false, true}) {
    for (std::size_t at = 0U; at < stages.size(); ++at) {
      PreparedImportFixture f; const auto snapshot = f.prepared.load();
      const auto expected = f.expectation(); const auto take = f.input();
      const auto before = retainedFiles(f.workspace);
      const auto journals = retainedFiles(f.workspace / "journal"), generations = retainedFiles(f.workspace / "generations");
      const auto project = p::encodeProductionProject(f.project);
      std::stop_source stop;
      std::vector<Stage> observed;
      p::PreparedDonorImportOptions options;
      options.faultInjector = [&](Stage stage) -> seam::core::Result<void> {
        observed.push_back(stage);
        if (stage != stages[at]) return seam::core::success();
        if (cancel) { stop.request_stop(); return seam::core::success(); }
        return seam::core::failure(seam::core::ErrorCode::IoError, "Injected prepared-donor checkpoint failure");
      };
      const auto result = f.repository.importPreparedDonor(f.project, snapshot, expected, take, f.event(take), stop.get_token(), options);
      CHECK(!result);
      CHECK(result.error().code == (cancel ? seam::core::ErrorCode::Conflict : seam::core::ErrorCode::IoError));
      CHECK(result.error().message == (cancel ? "Prepared donor import cancelled before commit" : "Injected prepared-donor checkpoint failure"));
      CHECK(observed == std::vector<Stage>(stages.begin(), stages.begin() + static_cast<std::ptrdiff_t>(at + 1U)));
      CHECK(p::encodeProductionProject(f.project) == project);
      CHECK(p::encodeProductionProject(f.repository.recover().value()) == project);
      CHECK(f.repository.verify(f.project));
      CHECK(retainedFiles(f.workspace / "journal") == journals);
      CHECK(retainedFiles(f.workspace / "generations") == generations);
      const auto after = retainedFiles(f.workspace);
      for (const auto& [path, digest] : before) CHECK(after.at(path) == digest);
      if (at == 0U) CHECK(after == before);
      const auto packet = f.workspace / "source-evidence/packets" / expected.packetSha256;
      CHECK(std::filesystem::exists(packet) == (at == 2U));
      if (at == 2U) {
        for (const auto* file : {&snapshot.evidence.packet, &snapshot.evidence.plan, &snapshot.evidence.license, &snapshot.evidence.attribution})
          CHECK(seam::core::readFileBytesLimited(packet / file->relativePath, 1024U * 1024U).value() == file->bytes);
      }
      f.checkPrivateCaptureRemoved();
      const auto generation = f.project.lastDurableGeneration;
      const auto retry = f.repository.importPreparedDonor(f.project, snapshot, expected, take, f.event(take)); CHECK(retry);
      CHECK(retry.value().durabilityConfirmed); CHECK(f.project.lastDurableGeneration == generation + 1U);
      CHECK(f.project.takes.size() == 1U); CHECK(f.project.reviews.empty()); CHECK(f.repository.verify(f.project));
      f.checkPrivateCaptureRemoved();
      std::cout << "[PREPARED-TRANSACTION] " << (cancel ? "cancel " : "injected error ") << at << ": UNCOMMITTED_THEN_RETRY\n";
    }
  }
}

TEST_CASE("Prepared donor import distinguishes journal generation and pointer publication failures") {
  using Stage = p::PreparedDonorImportStage;
  for (const int mode : {0, 1, 2}) {
    PreparedImportFixture f; const auto snapshot = f.prepared.load();
    const auto expected = f.expectation(); const auto take = f.input();
    const auto project = p::encodeProductionProject(f.project); const auto generation = f.project.lastDurableGeneration;
    const auto number = std::to_string(generation + 1U);
    const auto filename = std::string(20U - number.size(), '0') + number + ".json";
    const auto blocked = mode == 2 ? f.workspace / "project.json" :
        f.workspace / (mode == 0 ? "journal" : "generations") / filename;
    std::vector<Stage> observed;
    p::PreparedDonorImportOptions options;
    options.faultInjector = [&](Stage stage) -> seam::core::Result<void> {
      observed.push_back(stage);
      if (stage == Stage::NoticesRetained) {
        if (mode == 2) CHECK(std::filesystem::remove(blocked));
        CHECK(std::filesystem::create_directory(blocked));
      }
      return seam::core::success();
    };
    const auto result = f.repository.importPreparedDonor(f.project, snapshot, expected, take, f.event(take), {}, options);
    CHECK(observed == std::vector<Stage>({Stage::SnapshotValidated, Stage::RawTakeStaged, Stage::NoticesRetained}));
    CHECK(std::filesystem::is_directory(blocked));
    const auto recovered = f.repository.recover(); CHECK(recovered);
    f.checkPrivateCaptureRemoved();
    if (mode < 2) {
      CHECK(!result); CHECK(result.error().message.starts_with("Import did not confirm a commit."));
      CHECK(p::encodeProductionProject(f.project) == project);
      CHECK(p::encodeProductionProject(recovered.value()) == project);
      CHECK(f.repository.verify(f.project));
      // A generation refusal leaves the earlier journal publication occupied.
      // Preserve it and prove retry appends after it instead of replacing it.
      const auto occupied = retainedFiles(f.workspace / "journal");
      CHECK(std::filesystem::remove(blocked));
      const auto retry = f.repository.importPreparedDonor(f.project, snapshot, expected, take, f.event(take)); CHECK(retry);
      CHECK(retry.value().durabilityConfirmed);
      CHECK(f.project.lastDurableGeneration == generation + (mode == 1 ? 2U : 1U));
      const auto after = retainedFiles(f.workspace / "journal");
      for (const auto& [path, digest] : occupied) CHECK(after.at(path) == digest);
    } else {
      CHECK(result); CHECK(!result.value().durabilityConfirmed);
      CHECK(result.value().diagnostic.starts_with("The exact import is recoverably committed, but pointer durability is uncertain."));
      CHECK(f.project.lastDurableGeneration == generation + 1U);
      CHECK(result.value().committedGeneration == f.project.lastDurableGeneration);
      CHECK(result.value().committedProjectSha256 == seam::core::sha256Hex(p::encodeProductionProject(f.project)));
      CHECK(p::encodeProductionProject(recovered.value()) == p::encodeProductionProject(f.project));
      CHECK(!f.repository.verify(f.project));
      const auto committed = p::encodeProductionProject(f.project);
      CHECK(!f.repository.importPreparedDonor(f.project, snapshot, expected, take, f.event(take)));
      CHECK(p::encodeProductionProject(f.project) == committed);
      CHECK(std::filesystem::remove(blocked));
      CHECK(f.repository.reconcileCurrentPointer(result.value().committedGeneration, result.value().committedProjectSha256));
      CHECK(p::encodeProductionProject(f.project) == committed);
    }
    CHECK(f.project.takes.size() == 1U); CHECK(f.project.sourceBindings.size() == 1U);
    CHECK(f.project.reviews.empty()); CHECK(f.project.sourceQualityAssessments.empty());
    CHECK(f.project.takes[0].state == p::UnitQueueState::MarkerReview); CHECK(!p::selectedStrategyReady(f.project));
    CHECK(f.repository.verify(f.project));
    const auto after = retainedFiles(f.workspace);
    const auto committed = p::encodeProductionProject(f.project);
    CHECK(!f.repository.importPreparedDonor(f.project, snapshot, expected, take, f.event(take)));
    CHECK(retainedFiles(f.workspace) == after); CHECK(p::encodeProductionProject(f.project) == committed);
    f.checkPrivateCaptureRemoved();
    std::cout << "[PREPARED-TRANSACTION] publication " << mode << (mode == 2 ? ": COMMITTED_POINTER_RECONCILED\n" : ": UNCOMMITTED_THEN_RETRY\n");
  }
}

TEST_CASE("Prepared donor inspected imports retain one shared packet while preserving style and retake ownership") {
  PreparedImportFixture f; const auto held = f.prepared.load(); const auto firstGeneration = f.project.lastDurableGeneration;
  for (const auto style : {"neutral", "soft", "neutral"}) {
    auto take = f.input(style);
    if (f.project.takes.size() == 2U) { take.takeId = "take-neutral-retake"; take.supersedesTakeId = "take-neutral"; }
    CHECK(f.repository.importPreparedDonor(f.project, held, f.expectation(), take, f.event(take)));
    f.checkPrivateCaptureRemoved();
  }
  CHECK(f.project.lastDurableGeneration == firstGeneration + 3U); CHECK(f.project.assets.size() == 1U);
  CHECK(f.project.sourceBindings.size() == 3U); CHECK(f.project.takes[0].state == p::UnitQueueState::Retake);
  CHECK(f.project.sourceBindings[0].id != f.project.sourceBindings[1].id);
  CHECK(f.project.sourceBindings[1].id != f.project.sourceBindings[2].id);
  std::size_t files = 0U;
  for (const auto& entry : std::filesystem::directory_iterator(f.workspace / "source-evidence/packets" / held.evidence.packet.sha256)) {
    CHECK(entry.is_regular_file()); ++files;
  }
  CHECK(files == 4U); CHECK(f.repository.verify(f.project)); CHECK(f.project.reviews.empty());
  CHECK(!f.project.sourceStrategies[0].permissions.singingBankRedistribution);
}

TEST_CASE("Prepared donor inspected import refuses unsafe or conflicting notice storage without partial project commit") {
  for (const bool linkedParent : {false, true}) {
    PreparedImportFixture f; const auto held = f.prepared.load(); const auto expectation = f.expectation(); const auto take = f.input();
    const auto outside = f.prepared.root / "unrelated"; std::filesystem::create_directory(outside);
    CHECK(seam::core::durableAtomicWriteTextNew(outside / "keep.txt", "unrelated bytes")); const auto unrelated = retainedFiles(outside);
    const auto packets = f.workspace / "source-evidence/packets";
    if (linkedParent) std::filesystem::create_directory_symlink(outside, packets);
    else CHECK(seam::core::durableAtomicWriteTextNew(packets / expectation.packetSha256 / "attribution.txt", "pre-existing different notice"));
    const auto project = p::encodeProductionProject(f.project); const auto generation = f.project.lastDurableGeneration;
    const auto result = f.repository.importPreparedDonor(f.project, held, expectation, take, f.event(take)); CHECK(!result);
    CHECK(f.project.lastDurableGeneration == generation); CHECK(p::encodeProductionProject(f.project) == project);
    CHECK(p::encodeProductionProject(f.repository.recover().value()) == project); CHECK(retainedFiles(outside) == unrelated);
    if (!linkedParent) {
      const auto notice = seam::core::readTextFileLimited(packets / expectation.packetSha256 / "attribution.txt", 1024U); CHECK(notice);
      CHECK(notice.value() == "pre-existing different notice");
    }
    f.checkPrivateCaptureRemoved();
  }
}

TEST_CASE("Prepared donor inspected import refuses a stale durable writer before staging") {
  PreparedImportFixture f; const auto held = f.prepared.load(); auto stale = f.project; const auto expectation = f.expectation(); const auto take = f.input();
  CHECK(f.repository.save(f.project, {"save", "another-writer", "fixture-producer", "2026-10-15T00:02:00Z"}));
  const auto before = retainedFiles(f.workspace); const auto originalStale = p::encodeProductionProject(stale);
  CHECK(!f.repository.importPreparedDonor(stale, held, expectation, take, f.event(take)));
  CHECK(p::encodeProductionProject(stale) == originalStale); CHECK(retainedFiles(f.workspace) == before); f.checkPrivateCaptureRemoved();
}

TEST_CASE("Shared packet verification preserves every distinct donor binding and rechecks later operations") {
  PreparedImportFixture f(true);
  for (const auto style : {"neutral", "soft"}) {
    const auto take = f.input(style); auto expected = f.expectation();
    if (take.style == "soft") expected.donorId = "fixture_l00001_n1";
    auto held = p::loadPreparedPhoneDonor(f.prepared.root, expected.packetSha256, expected.donorId); CHECK(held);
    CHECK(f.repository.importPreparedDonor(f.project, std::move(held.value()), expected, take, f.event(take)));
  }
  CHECK(f.project.assets.size() == 2U); CHECK(f.project.sourceBindings.size() == 2U);
  CHECK(f.project.sourceBindings[0].rawAssetSha256 != f.project.sourceBindings[1].rawAssetSha256);
  CHECK(f.repository.verify(f.project));
  const auto original = p::encodeProductionProject(f.project); const auto before = retainedFiles(f.workspace);
  for (int mode = 0; mode < 5; ++mode) {
    auto draft = f.project;
    auto& binding = draft.sourceBindings[1]; auto& donor = *binding.preparedDonor;
    if (mode == 0) donor.donorId = "fixture_l00002_n1";
    else if (mode == 1) donor.donorId = f.prepared.id; // Valid donor, wrong captured audio.
    else if (mode == 2) donor.intakePlanSha256 = std::string(64U, '0');
    else if (mode == 3) donor.originalLicenseSha256 = std::string(64U, '0');
    else donor.attributionSha256 = std::string(64U, '0');
    binding.id = p::takeSourceBindingId(draft.projectId, binding); draft.takes[1].sourceBindingId = binding.id;
    CHECK(p::validateProductionProject(draft));
    const auto refused = f.repository.save(draft, {"save", "wrong-second-donor", "fixture-producer", "2026-10-15T00:03:00Z"}); CHECK(!refused);
    CHECK(refused.error().message.find("Retained prepared donor differs") != std::string::npos);
    CHECK(retainedFiles(f.workspace) == before);
    CHECK(p::encodeProductionProject(f.repository.recover().value()) == original);
  }
  const auto directory = f.workspace / "source-evidence/packets" / f.prepared.packetSha;
  CHECK(seam::core::durableAtomicWriteText(directory / "license-source.txt", "changed after successful verification"));
  CHECK(!f.repository.verify(f.project)); CHECK(!f.repository.recover());
  CHECK(p::encodeProductionProject(f.project) == original); f.checkPrivateCaptureRemoved();
}
#endif
