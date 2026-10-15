#pragma once
// Synthetic metadata around generated canonical PCM16 tones. Not factual phone
// labels, a real recording license, singer qualification or listening evidence.
#include "test_framework.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/prepared_phone_donor.hpp"
#include <span>

namespace seam::test {
inline std::pair<std::filesystem::path, std::vector<std::string>> preparedTonePacket(
    const std::filesystem::path& root, const std::vector<std::filesystem::path>& tones) {
  using J = formats::JsonValue;
  const auto directory = root / "prepared-tones";
  CHECK(std::filesystem::create_directory(directory));
  const std::string license = "SYNTHETIC ORIGINAL LICENSE; no real source grant\n";
  const std::string attribution = "Synthetic tone fixture author. Original license text: license-source.txt\n";
  const J contract = J::Object{{"format", "festvox-end-times-v1"}, {"annotationStatus", "AUTOMATIC_UNREVIEWED"},
      {"timeUnit", "seconds"}, {"boundaryRule", "ceil-seconds-times-sample-rate-v1"}};
  const J grant = J::Object{{"path", "license.txt"}, {"sha256", core::sha256Hex(license)},
      {"sourceUrl", "https://example.invalid/generated-tone"}, {"attribution", "Synthetic tone fixture author"}};
  J::Array sources, spans, proposals;
  std::vector<std::string> ids;
  for (std::size_t index = 0; index < tones.size(); ++index) {
    const auto bytes = core::readFileBytesLimited(tones[index], 1024U * 1024U); CHECK(bytes); CHECK(bytes.value().size() > 44U);
    const auto name = "tone" + std::to_string(index), id = name + "_l00000_n1";
    const auto digest = core::sha256Hex(bytes.value());
    const J audio = J::Object{{"path", name + ".wav"}, {"sha256", digest}};
    const J labels = J::Object{{"path", name + ".lab"}, {"sha256", core::sha256Hex("synthetic labels only")}};
    sources.emplace_back(J::Object{{"id", name}, {"sourceType", "speech"}, {"audio", audio}, {"labels", labels}});
    spans.emplace_back(J::Object{{"sourceId", name}, {"firstLabelIndex", std::int64_t{0}}, {"labelCount", std::int64_t{1}}});
    const auto frames = static_cast<std::int64_t>((bytes.value().size() - 44U) / 2U);
    proposals.emplace_back(J::Object{{"id", id}, {"derivedPath", id + ".wav"}, {"derivedSha256", digest},
        {"pcmSha256", core::sha256Hex(std::span{bytes.value()}.subspan(44U))}, {"sourceId", name}, {"sourceType", "speech"},
        {"sourceAudio", audio}, {"sourceLabels", labels}, {"firstLabelIndex", std::int64_t{0}}, {"labelCount", std::int64_t{1}},
        {"sourceAnnotations", J::Array{}}, {"sourcePhones", J::Array{"ay"}}, {"startFrame", std::int64_t{0}},
        {"endFrameExclusive", frames}, {"sampleRate", std::int64_t{16000}}, {"frames", frames},
        {"durationSeconds", static_cast<double>(frames) / 16000.0}, {"nonzeroPcm", true}, {"assignment", "NOT_ASSIGNED"},
        {"pitchReview", "NOT_REVIEWED"}, {"phoneticReview", "NOT_REVIEWED"}, {"listeningReview", "NOT_REVIEWED"}});
    CHECK(core::durableAtomicWriteNew(directory / (id + ".wav"), bytes.value()));
    ids.push_back(id);
  }
  const J plan = J::Object{{"schemaVersion", std::int64_t{1}}, {"documentKind", "SourceLabelDonorPlan"}, {"language", "en"},
      {"labelContract", contract}, {"license", grant}, {"sources", std::move(sources)}, {"spans", std::move(spans)}};
  const auto planJson = formats::stringifyJson(plan);
  const J packet = J::Object{{"schemaVersion", std::int64_t{1}}, {"documentKind", "UnassignedPhoneDonorProposals"},
      {"status", "UNAPPROVED"}, {"language", "en"}, {"planSha256", core::sha256Hex(planJson)}, {"labelContract", contract},
      {"license", grant}, {"retainedLicensePath", "license-source.txt"}, {"retainedAttributionPath", "attribution.txt"},
      {"retainedAttributionSha256", core::sha256Hex(attribution)}, {"changeNotice", "Generated tones; no real labels or admission"},
      {"producerRegistration", "NOT_REGISTERED"}, {"performerRights", "NOT_ESTABLISHED"}, {"rightsClearance", "NOT_APPROVED"},
      {"bankAdmission", "NOT_APPROVED"}, {"proposals", std::move(proposals)}};
  CHECK(core::durableAtomicWriteTextNew(directory / "donors.json", formats::stringifyJson(packet)));
  CHECK(core::durableAtomicWriteTextNew(directory / "intake-plan.json", planJson));
  CHECK(core::durableAtomicWriteTextNew(directory / "license-source.txt", license));
  CHECK(core::durableAtomicWriteTextNew(directory / "attribution.txt", attribution));
  return {directory, ids};
}
}
