#pragma once
// Synthetic test-only PCM and notices; establishes no real recording rights.
#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/prepared_phone_donor.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include <span>
namespace seam::test::support {
namespace donor_production = voicebank_production;
struct PreparedPhoneDonorFixture final {
  std::filesystem::path root{seam::test::support::temporaryDirectory("prepared-donor-snapshot")};
  std::string id{"fixture_l00000_n1"}, packetSha;
  std::vector<std::byte> audio = std::vector<std::byte>(204U, std::byte{0});
  explicit PreparedPhoneDonorFixture(bool twoDonors = false) {
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
    const formats::JsonValue sourceAudio = formats::JsonValue::Object{{"path", "original.wav"}, {"sha256", audioSha}};
    const formats::JsonValue sourceLabels = formats::JsonValue::Object{{"path", "original.lab"}, {"sha256", seam::core::sha256Hex("0.01 0 aa\n")}};
    const formats::JsonValue sourceLicense = formats::JsonValue::Object{{"path", "LICENSE.txt"}, {"sha256", seam::core::sha256Hex(license)},
        {"sourceUrl", "https://example.invalid/synthetic-test"}, {"attribution", "Synthetic fixture author"}};
    const formats::JsonValue contract = formats::JsonValue::Object{{"format", "festvox-end-times-v1"}, {"annotationStatus", "AUTOMATIC_UNREVIEWED"},
        {"timeUnit", "seconds"}, {"boundaryRule", "ceil-seconds-times-sample-rate-v1"}};
    formats::JsonValue::Array spans{formats::JsonValue::Object{{"sourceId", "fixture"}, {"firstLabelIndex", std::int64_t{0}}, {"labelCount", std::int64_t{1}}}};
    if (twoDonors) spans.push_back(formats::JsonValue::Object{{"sourceId", "fixture"}, {"firstLabelIndex", std::int64_t{1}}, {"labelCount", std::int64_t{1}}});
    const formats::JsonValue plan = formats::JsonValue::Object{{"schemaVersion", std::int64_t{1}}, {"documentKind", "SourceLabelDonorPlan"}, {"language", "en"},
        {"labelContract", contract}, {"license", sourceLicense},
        {"sources", formats::JsonValue::Array{formats::JsonValue::Object{{"id", "fixture"}, {"sourceType", "speech"}, {"audio", sourceAudio}, {"labels", sourceLabels}}}},
        {"spans", std::move(spans)}};
    const auto planText = seam::formats::stringifyJson(plan);
    const formats::JsonValue proposal = formats::JsonValue::Object{{"id", id}, {"derivedPath", id + ".wav"}, {"derivedSha256", audioSha},
        {"pcmSha256", seam::core::sha256Hex(std::span{audio}.subspan(44U))}, {"sourceId", "fixture"}, {"sourceType", "speech"},
        {"sourceAudio", sourceAudio}, {"sourceLabels", sourceLabels}, {"firstLabelIndex", std::int64_t{0}}, {"labelCount", std::int64_t{1}},
        {"sourceAnnotations", formats::JsonValue::Array{formats::JsonValue::Object{{"index", std::int64_t{0}}, {"startSeconds", "0"}, {"endSeconds", "0.01"},
          {"startFrame", std::int64_t{0}}, {"endFrameExclusive", std::int64_t{80}}, {"sourcePhone", "aa"}, {"opaqueColumn", "0"}}}},
        {"sourcePhones", formats::JsonValue::Array{"aa"}}, {"startFrame", std::int64_t{0}}, {"endFrameExclusive", std::int64_t{80}}, {"sampleRate", std::int64_t{8000}},
        {"frames", std::int64_t{80}}, {"durationSeconds", 0.01}, {"nonzeroPcm", true}, {"assignment", "NOT_ASSIGNED"},
        {"pitchReview", "NOT_REVIEWED"}, {"phoneticReview", "NOT_REVIEWED"}, {"listeningReview", "NOT_REVIEWED"}};
    formats::JsonValue::Array proposals{proposal};
    if (twoDonors) {
      const std::string secondId = "fixture_l00001_n1";
      auto secondAudio = audio; secondAudio[44] = std::byte{24};
      auto second = proposal;
      auto& fields = second.asObject();
      fields["id"] = secondId; fields["derivedPath"] = secondId + ".wav";
      fields["derivedSha256"] = seam::core::sha256Hex(secondAudio);
      fields["pcmSha256"] = seam::core::sha256Hex(std::span{secondAudio}.subspan(44U));
      fields["firstLabelIndex"] = std::int64_t{1}; fields["startFrame"] = std::int64_t{80}; fields["endFrameExclusive"] = std::int64_t{160};
      fields["sourceAnnotations"] = formats::JsonValue::Array{formats::JsonValue::Object{{"index", std::int64_t{1}}, {"startSeconds", "0.01"}, {"endSeconds", "0.02"},
          {"startFrame", std::int64_t{80}}, {"endFrameExclusive", std::int64_t{160}}, {"sourcePhone", "aa"}, {"opaqueColumn", "0"}}};
      proposals.push_back(std::move(second));
      CHECK(seam::core::durableAtomicWriteNew(root / (secondId + ".wav"), secondAudio));
    }
    const formats::JsonValue packet = formats::JsonValue::Object{{"schemaVersion", std::int64_t{1}}, {"documentKind", "UnassignedPhoneDonorProposals"},
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
  ~PreparedPhoneDonorFixture() { std::error_code error; std::filesystem::remove_all(root, error); }
  donor_production::PreparedPhoneDonorSnapshot load() const {
    auto result = donor_production::loadPreparedPhoneDonor(root, packetSha, id); CHECK(result); return std::move(result.value());
  }
};

// A registered synthetic producer/source and two explicit style assignments.
struct PreparedDonorWorkspaceFixture final {
  PreparedPhoneDonorFixture prepared;
  std::filesystem::path workspace{prepared.root / "producer"};
  donor_production::ProductionProjectRepository repository{workspace};
  donor_production::VoicebankProductionProject project;
  PreparedDonorWorkspaceFixture() {
    project.schemaVersion = donor_production::kProductionStyleSchemaVersion;
    project.projectId = "synthetic-studio-intake"; project.language = "en";
    project.inventoryId = "synthetic-inventory"; project.inventorySha256 = std::string(64U,'a');
    project.declaredPitchLayers = {60};
    project.operators = {{"producer","PRODUCER"},{"reviewer","REVIEWER"}};
    for (const auto style : {"neutral","soft"}) project.unitAssignments.push_back({.coverageKey="sustain:aa",.pitchLayer=60,
        .promptId="prompt-"+std::string{style},.plannedTakeId="take-"+std::string{style},.style=style});
    CHECK(repository.initialize(project,{"create",project.projectId,"producer","2026-10-15T00:00:00Z"}));
    const auto declaration = prepared.root / "reviewed-declaration.txt";
    CHECK(core::durableAtomicWriteTextNew(declaration,"SYNTHETIC PRODUCER DECLARATION; NO REAL SINGER OR RIGHTS ACCEPTANCE"));
    donor_production::SourceStrategyAssessment source{.id="fixture-human-source",.kind=donor_production::SourceStrategyKind::HumanRecording,
        .rights=donor_production::Feasibility::Pass,.permissions={true,true,false,false},
        .licenseLocator=declaration.string(),.licenseSha256=core::sha256File(declaration).value()};
    CHECK(repository.registerSource(project,source,core::sha256Hex(donor_production::encodeProductionProject(project)),"producer","2026-10-15T00:01:00Z"));
  }
};
}
