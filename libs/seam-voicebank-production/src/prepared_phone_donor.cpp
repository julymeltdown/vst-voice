#include "seam/voicebank_production/prepared_phone_donor.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/asset_path.hpp"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <set>
#include <span>
#include <string_view>

namespace seam::voicebank_production {
namespace {
using J = formats::JsonValue;
constexpr std::uint64_t MiB = 1024ULL * 1024ULL;
struct Refusal { core::Error error; };
void require(bool condition, std::string message) {
  if (!condition) throw Refusal{{core::ErrorCode::InvalidArgument, std::move(message)}};
}
void checkpoint(std::stop_token stop) {
  if (stop.stop_requested()) throw Refusal{{core::ErrorCode::Conflict, "Prepared donor read cancelled"}};
}
bool digest(std::string_view s) {
  return s.size() == 64U && std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
void shape(const J& value, std::initializer_list<std::string_view> keys) {
  require(value.isObject() && value.asObject().size() == keys.size(), "Prepared donor record has missing or unknown fields");
  for (const auto key : keys) require(value.find(key) != nullptr, "Prepared donor record is incomplete");
}
const J& field(const J& value, std::string_view key) {
  const auto* found = value.find(key);
  require(found != nullptr, "Missing prepared donor field: " + std::string{key});
  return *found;
}
const std::string& text(const J& value, std::string_view key) {
  const auto& found = field(value, key);
  require(found.isString(), "Expected prepared donor text: " + std::string{key});
  return found.asString();
}
std::int64_t integer(const J& value, std::string_view key) {
  const auto& found = field(value, key);
  require(found.isInteger(), "Expected prepared donor integer: " + std::string{key});
  return found.asInt64();
}
void equal(const J& value, std::string_view key, std::string_view expected) {
  require(text(value, key) == expected, "Expected unapproved prepared donor field: " + std::string{key});
}
J parse(std::string_view raw) {
  auto parsed = formats::parseJson(raw, {.maximumInputBytes = 16U * MiB, .maximumDepth = 16U,
      .maximumNodes = 500000U, .maximumStringBytes = 2U * MiB, .maximumCollectionEntries = 16384U});
  if (!parsed) throw Refusal{parsed.error()};
  return std::move(parsed.value());
}
J parse(const SourceIntakeFileSnapshot& file) {
  return parse(std::string_view{reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()});
}
SourceIntakeFileSnapshot read(const std::filesystem::path& root, std::string name,
    std::string expected, std::uint64_t limit, std::stop_token stop) {
  checkpoint(stop);
  require(digest(expected), "Expected lowercase SHA-256 for prepared donor input");
  const auto resolved = voicebank::resolveBankAsset(root, name);
  if (!resolved) throw Refusal{resolved.error()};
  auto bytes = core::readFileBytesLimited(resolved.value(), limit);
  if (!bytes) throw Refusal{bytes.error()};
  require(core::sha256Hex(bytes.value()) == expected, "Prepared donor input SHA-256 mismatch: " + name);
  return {std::move(name), std::move(expected), std::move(bytes.value())};
}
J labelContract() {
  return J::Object{{"format", "festvox-end-times-v1"}, {"annotationStatus", "AUTOMATIC_UNREVIEWED"},
      {"timeUnit", "seconds"}, {"boundaryRule", "ceil-seconds-times-sample-rate-v1"}};
}
void sourceBinding(const J& value) {
  shape(value, {"path", "sha256"});
  const auto& name = text(value, "path");
  require(!name.empty() && !std::filesystem::path{name}.is_absolute() && digest(text(value, "sha256")),
      "Invalid original source binding");
  for (const auto& part : std::filesystem::path{name}) require(part != "..", "Original source binding contains traversal");
}
bool sourceId(std::string_view id) {
  const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
  return !id.empty() && id.size() <= 80U && alnum(id.front()) &&
      std::all_of(id.begin(), id.end(), [&](char c) { return alnum(c) || c == '_' || c == '-'; });
}
std::uint32_t le(std::span<const std::byte> bytes, std::size_t offset, std::size_t size) {
  std::uint32_t value = 0U;
  for (std::size_t i = 0U; i < size; ++i) value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
  return value;
}
void audioFacts(PreparedPhoneDonorSnapshot& result, const J& proposal) {
  const auto& bytes = result.audio.bytes;
  require(bytes.size() >= 44U, "Prepared donor WAV is truncated");
  const auto tag = [&](std::size_t offset, std::string_view expected) {
    return std::string_view{reinterpret_cast<const char*>(bytes.data() + offset), expected.size()} == expected;
  };
  // The preparation writer emits canonical 44-byte mono PCM16 WAV headers.
  // This loader is for that output contract, not a general WAV decoder.
  require(tag(0U, "RIFF") && tag(8U, "WAVEfmt ") && tag(36U, "data") &&
      le(bytes, 4U, 4U) == bytes.size() - 8U && le(bytes, 16U, 4U) == 16U &&
      le(bytes, 20U, 2U) == 1U && le(bytes, 22U, 2U) == 1U &&
      le(bytes, 32U, 2U) == 2U && le(bytes, 34U, 2U) == 16U,
      "Expected canonical prepared mono PCM16 WAV");
  result.sampleRate = le(bytes, 24U, 4U);
  const auto pcmBytes = bytes.size() - 44U;
  require(result.sampleRate >= 8000U && result.sampleRate <= 192000U &&
      le(bytes, 28U, 4U) == result.sampleRate * 2U && pcmBytes > 0U && pcmBytes % 2U == 0U &&
      le(bytes, 40U, 4U) == pcmBytes, "Invalid prepared donor WAV extent");
  result.frames = pcmBytes / 2U;
  const auto pcm = std::span{bytes}.subspan(44U);
  result.pcmSha256 = core::sha256Hex(pcm);
  result.nonzeroPcm = std::any_of(pcm.begin(), pcm.end(), [](std::byte b) { return b != std::byte{0}; });
  const auto& nonzero = field(proposal, "nonzeroPcm");
  const auto& duration = field(proposal, "durationSeconds");
  require(integer(proposal, "sampleRate") == result.sampleRate &&
      integer(proposal, "frames") == static_cast<std::int64_t>(result.frames) &&
      text(proposal, "pcmSha256") == result.pcmSha256 &&
      nonzero.isBool() && nonzero.asBool() == result.nonzeroPcm && duration.isNumber() &&
      duration.asNumber() == static_cast<double>(result.frames) / result.sampleRate,
      "Prepared donor PCM facts differ from the selected proposal");
  const auto start = integer(proposal, "startFrame");
  const auto end = integer(proposal, "endFrameExclusive");
  require(start >= 0 && end > start && static_cast<std::uint64_t>(end - start) == result.frames,
      "Prepared donor frame interval differs from stored PCM");
}

void checkDirectory(const std::filesystem::path& directory, std::stop_token stopToken) {
  checkpoint(stopToken);
  std::error_code error;
  const auto status = std::filesystem::symlink_status(directory, error);
  require(!error && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status),
      "Missing or linked prepared donor directory");
  const auto marker = std::filesystem::symlink_status(directory / "INCOMPLETE", error);
  require((!error && marker.type() == std::filesystem::file_type::not_found) ||
      error == std::errc::no_such_file_or_directory, "Prepared donor directory is incomplete");
}

void declaredPcmFacts(const J& row) {
  // Check wire types before canonical JSON can normalize integral doubles.
  for (const auto key : {"sampleRate", "frames", "startFrame", "endFrameExclusive"})
    (void)integer(row, key);
  require(field(row, "nonzeroPcm").isBool(), "Expected prepared donor boolean: nonzeroPcm");
  require(field(row, "durationSeconds").isNumber(), "Expected prepared donor number: durationSeconds");
  const auto rate = integer(row, "sampleRate"), frames = integer(row, "frames");
  const auto start = integer(row, "startFrame"), end = integer(row, "endFrameExclusive");
  require(rate >= 8000 && rate <= 192000 && frames > 0 && frames <= (64LL * 1024LL * 1024LL - 44LL) / 2LL &&
      start >= 0 && end > start && end - start == frames &&
      field(row, "durationSeconds").asNumber() == static_cast<double>(frames) / static_cast<double>(rate),
      "Prepared donor declared PCM facts are inconsistent or exceed canonical WAV bounds");
}

template<class Reader>
PreparedPhoneDonorEvidence evidenceFrom(Reader&& reader, std::string expectedPacketSha256,
    std::string donorId, std::stop_token stopToken,
    std::map<std::string, std::string>* donorDigests = nullptr) {
  PreparedPhoneDonorEvidence result;
  result.donorId = std::move(donorId);
  result.packet = reader("donors.json", std::move(expectedPacketSha256), 16U * MiB, stopToken);
  const auto packet = parse(result.packet);
  shape(packet, {"schemaVersion", "documentKind", "status", "language", "planSha256", "labelContract",
      "license", "retainedLicensePath", "retainedAttributionPath", "retainedAttributionSha256", "changeNotice",
      "producerRegistration", "performerRights", "rightsClearance", "bankAdmission", "proposals"});
  require(integer(packet, "schemaVersion") == 1 && field(packet, "labelContract") == labelContract(),
      "Unsupported prepared donor schema or label contract");
  for (const auto& [key, expected] : {std::pair{"documentKind", "UnassignedPhoneDonorProposals"},
      {"status", "UNAPPROVED"}, {"language", "en"}, {"producerRegistration", "NOT_REGISTERED"},
      {"performerRights", "NOT_ESTABLISHED"}, {"rightsClearance", "NOT_APPROVED"}, {"bankAdmission", "NOT_APPROVED"}})
    equal(packet, key, expected);
  equal(packet, "retainedLicensePath", "license-source.txt");
  equal(packet, "retainedAttributionPath", "attribution.txt");
  require(!text(packet, "changeNotice").empty(), "Missing prepared donor change notice");
  const auto& license = field(packet, "license");
  shape(license, {"path", "sha256", "sourceUrl", "attribution"});
  require(!text(license, "sourceUrl").empty() && !text(license, "attribution").empty(),
      "Missing prepared donor attribution or source URL");
  result.license = reader("license-source.txt", text(license, "sha256"), MiB, stopToken);
  result.attribution = reader("attribution.txt", text(packet, "retainedAttributionSha256"), 2U * MiB, stopToken);
  result.plan = reader("intake-plan.json", text(packet, "planSha256"), MiB, stopToken);
  const auto plan = parse(result.plan);
  shape(plan, {"schemaVersion", "documentKind", "language", "labelContract", "license", "sources", "spans"});
  require(integer(plan, "schemaVersion") == 1 && field(plan, "labelContract") == labelContract() &&
      field(plan, "license") == license, "Prepared donor plan provenance differs");
  equal(plan, "documentKind", "SourceLabelDonorPlan");
  equal(plan, "language", "en");
  const auto& proposals = field(packet, "proposals");
  const auto& spans = field(plan, "spans");
  const auto& sources = field(plan, "sources");
  require(proposals.isArray() && !proposals.asArray().empty() && proposals.asArray().size() <= 4096U &&
      spans.isArray() && spans.asArray().size() == proposals.asArray().size() &&
      sources.isArray() && !sources.asArray().empty() && sources.asArray().size() <= 32U,
      "Prepared donor plan/proposal bounds differ");
  std::set<std::string> sourceIds;
  for (const auto& source : sources.asArray()) {
    shape(source, {"id", "sourceType", "audio", "labels"});
    const auto& id = text(source, "id");
    require(sourceId(id) && sourceIds.insert(id).second, "Invalid or duplicate prepared donor source id");
    equal(source, "sourceType", "speech");
    sourceBinding(field(source, "audio")); sourceBinding(field(source, "labels"));
  }
  const J* selected = nullptr;
  std::set<std::string> ids;
  for (std::size_t i = 0U; i < proposals.asArray().size(); ++i) {
    checkpoint(stopToken);
    const auto& row = proposals.asArray()[i];
    const auto& span = spans.asArray()[i];
    shape(row, {"id", "derivedPath", "derivedSha256", "pcmSha256", "sourceId", "sourceType", "sourceAudio",
        "sourceLabels", "firstLabelIndex", "labelCount", "sourceAnnotations", "sourcePhones", "startFrame",
        "endFrameExclusive", "sampleRate", "frames", "durationSeconds", "nonzeroPcm", "assignment",
        "pitchReview", "phoneticReview", "listeningReview"});
    shape(span, {"sourceId", "firstLabelIndex", "labelCount"});
    for (const auto& [key, expected] : {std::pair{"assignment", "NOT_ASSIGNED"}, {"pitchReview", "NOT_REVIEWED"},
        {"phoneticReview", "NOT_REVIEWED"}, {"listeningReview", "NOT_REVIEWED"}}) equal(row, key, expected);
    const auto& id = text(row, "id");
    require(ids.insert(id).second, "Duplicate prepared donor id");
    const auto& source = text(span, "sourceId");
    const auto first = integer(span, "firstLabelIndex"), count = integer(span, "labelCount");
    require(sourceIds.contains(source) && first >= 0 && first < 16384 && count >= 1 && count <= 3,
        "Invalid prepared donor span identity");
    const auto index = std::to_string(first);
    const auto expectedId = source + "_l" + std::string(5U - index.size(), '0') + index + "_n" + std::to_string(count);
    require(id == expectedId && text(row, "derivedPath") == id + ".wav" &&
        text(row, "sourceId") == source && integer(row, "firstLabelIndex") == first && integer(row, "labelCount") == count,
        "Prepared donor identity differs from retained plan");
    equal(row, "sourceType", "speech");
    const auto sourceRow = std::find_if(sources.asArray().begin(), sources.asArray().end(),
        [&](const J& value) { return text(value, "id") == source; });
    require(field(row, "sourceAudio") == field(*sourceRow, "audio") &&
        field(row, "sourceLabels") == field(*sourceRow, "labels"), "Prepared donor original bindings differ");
    require(digest(text(row, "derivedSha256")) && digest(text(row, "pcmSha256")), "Invalid prepared donor audio digest");
    if (donorDigests) {
      declaredPcmFacts(row);
      donorDigests->emplace(id, text(row, "derivedSha256"));
    }
    if (id == result.donorId) selected = &row;
  }
  if (donorDigests) {
    checkpoint(stopToken);
    return result;
  }
  require(selected != nullptr, "Selected prepared donor id is absent");
  // Metadata can be internally checked without claiming to have measured WAV
  // bytes. The later full/owned paths still compare all facts with the audio.
  declaredPcmFacts(*selected);
  result.derivedAudioSha256 = text(*selected, "derivedSha256");
  result.proposalJson = formats::stringifyJson(*selected, false);
  checkpoint(stopToken);
  return result;
}

PreparedPhoneDonorSnapshot finishSnapshot(PreparedPhoneDonorEvidence evidence,
    SourceIntakeFileSnapshot audio, std::stop_token stop) {
  checkpoint(stop);
  require(audio.relativePath == evidence.donorId + ".wav" && audio.bytes.size() <= 64U * MiB &&
      audio.sha256 == evidence.derivedAudioSha256 && core::sha256Hex(audio.bytes) == evidence.derivedAudioSha256,
      "Held prepared donor audio differs from the selected proposal");
  const auto proposal = parse(evidence.proposalJson);
  PreparedPhoneDonorSnapshot result;
  result.evidence = std::move(evidence);
  result.audio = std::move(audio);
  audioFacts(result, proposal);
  checkpoint(stop);
  return result;
}
}  // namespace

core::Result<PreparedPhoneDonorPacketFacts> loadPreparedPhoneDonorPacketFacts(
    const std::filesystem::path& directory, std::string expectedPacketSha256,
    std::stop_token stopToken) {
  try {
    checkDirectory(directory, stopToken);
    PreparedPhoneDonorPacketFacts result;
    const auto evidence = evidenceFrom([&](std::string name, std::string expected, std::uint64_t limit, std::stop_token stop) {
      return read(directory, std::move(name), std::move(expected), limit, stop);
    }, std::move(expectedPacketSha256), {}, stopToken, &result.derivedAudioSha256ByDonor);
    result.packetSha256 = evidence.packet.sha256;
    result.intakePlanSha256 = evidence.plan.sha256;
    result.originalLicenseSha256 = evidence.license.sha256;
    result.attributionSha256 = evidence.attribution.sha256;
    return result;
  } catch (const Refusal& refusal) {
    return core::Result<PreparedPhoneDonorPacketFacts>{refusal.error};
  }
}

core::Result<PreparedPhoneDonorEvidence> loadPreparedPhoneDonorEvidence(
    const std::filesystem::path& directory, std::string expectedPacketSha256,
    std::string donorId, std::stop_token stopToken) {
  try {
    checkDirectory(directory, stopToken);
    return evidenceFrom([&](std::string name, std::string expected, std::uint64_t limit, std::stop_token stop) {
      return read(directory, std::move(name), std::move(expected), limit, stop);
    }, std::move(expectedPacketSha256), std::move(donorId), stopToken);
  } catch (const Refusal& refusal) {
    return core::Result<PreparedPhoneDonorEvidence>{refusal.error};
  }
}

core::Result<PreparedPhoneDonorSnapshot> loadPreparedPhoneDonor(
    const std::filesystem::path& directory, std::string expectedPacketSha256,
    std::string donorId, std::stop_token stopToken) {
  auto evidence = loadPreparedPhoneDonorEvidence(directory, std::move(expectedPacketSha256), std::move(donorId), stopToken);
  if (!evidence) return core::Result<PreparedPhoneDonorSnapshot>{evidence.error()};
  try {
    auto audio = read(directory, evidence.value().donorId + ".wav", evidence.value().derivedAudioSha256, 64U * MiB, stopToken);
    return finishSnapshot(std::move(evidence.value()), std::move(audio), stopToken);
  } catch (const Refusal& refusal) {
    return core::Result<PreparedPhoneDonorSnapshot>{refusal.error};
  }
}

core::Result<PreparedPhoneDonorSnapshot> validatePreparedPhoneDonorSnapshot(
    PreparedPhoneDonorSnapshot snapshot, std::string expectedPacketSha256,
    std::string expectedDonorId, std::stop_token stopToken) {
  try {
    checkpoint(stopToken);
    require(snapshot.evidence.donorId == expectedDonorId, "Held prepared donor selection changed");
    const std::array files{&snapshot.evidence.packet, &snapshot.evidence.plan, &snapshot.evidence.license, &snapshot.evidence.attribution};
    auto evidence = evidenceFrom([&](std::string name, std::string expected, std::uint64_t limit, std::stop_token stop) {
      checkpoint(stop);
      require(digest(expected), "Expected lowercase SHA-256 for held prepared donor input");
      const auto found = std::find_if(files.begin(), files.end(), [&](const auto* file) { return file->relativePath == name; });
      require(found != files.end(), "Held prepared donor evidence is absent or renamed: " + name);
      const auto& file = **found;
      require(file.bytes.size() <= limit && file.sha256 == expected && core::sha256Hex(file.bytes) == expected,
          "Held prepared donor evidence SHA-256 mismatch: " + name);
      return std::move(**found);
    }, std::move(expectedPacketSha256), std::move(expectedDonorId), stopToken);
    return finishSnapshot(std::move(evidence), std::move(snapshot.audio), stopToken);
  } catch (const Refusal& refusal) {
    return core::Result<PreparedPhoneDonorSnapshot>{refusal.error};
  }
}
}
