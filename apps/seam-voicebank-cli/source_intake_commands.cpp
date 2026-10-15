#include "source_intake_commands.hpp"
#include "signal_cancellation.hpp"
#include "seam/voicebank_production/prepared_phone_donor.hpp"
#include "seam/formats/json_value.hpp"
#include <iostream>
#include <string_view>

namespace seam::voicebank_cli {
std::optional<int> runSourceIntakeCommand(int argc, char** argv) {
  if (argc < 2 || std::string_view{argv[1]} != "verify-phone-donor") return std::nullopt;
  if (argc != 5) {
    std::cerr << "verify-phone-donor requires DIRECTORY PACKET_SHA256 DONOR_ID\n";
    return 2;
  }
  SignalCancellation cancellation;
  const auto snapshot = voicebank_production::loadPreparedPhoneDonor(argv[2], argv[3], argv[4], cancellation.token());
  if (!snapshot) {
    std::cerr << "Prepared donor refused: " << snapshot.error().message << "\n";
    return 2;
  }
  const auto& value = snapshot.value();
  const auto& evidence = value.evidence;
  std::cout << formats::stringifyJson(formats::JsonValue::Object{
      {"documentKind", "SelectedPreparedPhoneDonorSnapshot"}, {"status", "UNAPPROVED"},
      {"donorId", evidence.donorId}, {"packetSha256", evidence.packet.sha256},
      {"audioSha256", value.audio.sha256}, {"pcmSha256", value.pcmSha256},
      {"attributionSha256", evidence.attribution.sha256}, {"licenseSha256", evidence.license.sha256},
      {"planSha256", evidence.plan.sha256}, {"sampleRate", static_cast<std::int64_t>(value.sampleRate)},
      {"frames", static_cast<std::int64_t>(value.frames)}, {"nonzeroPcm", value.nonzeroPcm},
      {"bankAdmission", "NOT_APPROVED"}, {"wholePacketAudioVerified", false},
      {"sourceRightsApproved", false}, {"nativeImport", false}}) << '\n';
  return std::cout ? 0 : 2;
}
}
