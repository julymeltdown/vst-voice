// Synthetic PCM and declarations only. These tests establish byte handling,
// not a recording license, a singer, annotation truth or listening acceptance.
#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/prepared_phone_donor.hpp"
#include <array>
#include <span>

namespace {
namespace p = seam::voicebank_production;
using J = seam::formats::JsonValue;
struct Fixture final {
  std::filesystem::path root{seam::test::support::temporaryDirectory("prepared-donor-snapshot")};
  std::string id{"fixture_l00000_n1"}, packetSha;
  std::vector<std::byte> audio = std::vector<std::byte>(204U, std::byte{0});
  Fixture() {
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
    const J plan = J::Object{{"schemaVersion", std::int64_t{1}}, {"documentKind", "SourceLabelDonorPlan"}, {"language", "en"},
        {"labelContract", contract}, {"license", sourceLicense},
        {"sources", J::Array{J::Object{{"id", "fixture"}, {"sourceType", "speech"}, {"audio", sourceAudio}, {"labels", sourceLabels}}}},
        {"spans", J::Array{J::Object{{"sourceId", "fixture"}, {"firstLabelIndex", std::int64_t{0}}, {"labelCount", std::int64_t{1}}}}}};
    const auto planText = seam::formats::stringifyJson(plan);
    const J proposal = J::Object{{"id", id}, {"derivedPath", id + ".wav"}, {"derivedSha256", audioSha},
        {"pcmSha256", seam::core::sha256Hex(std::span{audio}.subspan(44U))}, {"sourceId", "fixture"}, {"sourceType", "speech"},
        {"sourceAudio", sourceAudio}, {"sourceLabels", sourceLabels}, {"firstLabelIndex", std::int64_t{0}}, {"labelCount", std::int64_t{1}},
        {"sourceAnnotations", J::Array{J::Object{{"index", std::int64_t{0}}, {"startSeconds", "0"}, {"endSeconds", "0.01"},
          {"startFrame", std::int64_t{0}}, {"endFrameExclusive", std::int64_t{80}}, {"sourcePhone", "aa"}, {"opaqueColumn", "0"}}}},
        {"sourcePhones", J::Array{"aa"}}, {"startFrame", std::int64_t{0}}, {"endFrameExclusive", std::int64_t{80}}, {"sampleRate", std::int64_t{8000}},
        {"frames", std::int64_t{80}}, {"durationSeconds", 0.01}, {"nonzeroPcm", true}, {"assignment", "NOT_ASSIGNED"},
        {"pitchReview", "NOT_REVIEWED"}, {"phoneticReview", "NOT_REVIEWED"}, {"listeningReview", "NOT_REVIEWED"}};
    const J packet = J::Object{{"schemaVersion", std::int64_t{1}}, {"documentKind", "UnassignedPhoneDonorProposals"},
        {"status", "UNAPPROVED"}, {"language", "en"}, {"planSha256", seam::core::sha256Hex(planText)},
        {"labelContract", contract}, {"license", sourceLicense}, {"retainedLicensePath", "license-source.txt"},
        {"retainedAttributionPath", "attribution.txt"}, {"retainedAttributionSha256", seam::core::sha256Hex(notice)},
        {"changeNotice", "Synthetic PCM copied; no pitch or phone assignment."}, {"producerRegistration", "NOT_REGISTERED"},
        {"performerRights", "NOT_ESTABLISHED"}, {"rightsClearance", "NOT_APPROVED"}, {"bankAdmission", "NOT_APPROVED"},
        {"proposals", J::Array{proposal}}};
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
