#pragma once
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/prepared_phone_donor.hpp"
#include "seam/voicebank_production/repository.hpp"

namespace seam::voicebank_production {
// Owns the revalidated material whose exact notices and explicit assignment are
// displayed. No phone/pitch/style inference, import, review or rights approval.
struct PreparedDonorIntake final {
  formats::JsonValue display;
  RawTakeInput take;
  PreparedPhoneDonorSnapshot snapshot;
};
[[nodiscard]] core::Result<PreparedDonorIntake> capturePreparedDonorIntake(
    const std::filesystem::path& workspace, const VoicebankProductionProject& project,
    PreparedPhoneDonorSnapshot snapshot, std::string expectedPacketSha256,
    std::string expectedDonorId, std::string_view sourceId, std::string_view coverage,
    std::int32_t pitch, std::string_view style, std::stop_token stop = {});
}
