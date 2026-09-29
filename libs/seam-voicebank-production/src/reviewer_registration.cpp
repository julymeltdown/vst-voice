#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/core/sha256.hpp"

#include <algorithm>

namespace seam::voicebank_production {
namespace {
constexpr std::size_t kMaximumOperators{256U};

bool boundedIdentity(std::string_view value) noexcept {
  return !value.empty() && value.size() <= 128U && value.front() != ' ' && value.back() != ' ' &&
      std::none_of(value.begin(), value.end(), [](char c) {
        return static_cast<unsigned char>(c) < 32U || c == 127;
      });
}
}  // namespace

core::Result<ProductionCommitReceipt> ProductionProjectRepository::registerReviewer(
    VoicebankProductionProject& project, std::string reviewerId,
    std::string_view expectedProjectSha256, std::string producerId,
    std::string occurredAtUtc, std::stop_token stop) {
  using Output = ProductionCommitReceipt;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Reviewer registration cancelled");
  const auto verified = verify(project);
  if (!verified) return core::Result<Output>{verified.error()};
  if (core::sha256Hex(encodeProductionProject(project)) != expectedProjectSha256)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Reviewer registration requires the exact current producer snapshot");
  if (!boundedIdentity(reviewerId) || !isProductionUtcTimestamp(occurredAtUtc) ||
      std::none_of(project.operators.begin(), project.operators.end(), [&](const auto& actor) {
        return actor.operatorId == producerId && actor.role == "PRODUCER";
      }))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Reviewer registration needs the registered producer, a UTC time and a reviewer ID of 1 to 128 "
        "bytes without control characters or surrounding spaces");
  if (std::any_of(project.operators.begin(), project.operators.end(),
                  [&](const auto& actor) { return actor.operatorId == reviewerId; }))
    return core::failure<Output>(core::ErrorCode::Conflict,
        "That identity is already registered; an identity is never renamed or given a second role",
        reviewerId);
  if (project.operators.size() >= kMaximumOperators)
    return core::failure<Output>(core::ErrorCode::Unsupported,
        "A producer workspace registers at most 256 identities");
  auto draft = project;
  draft.operators.push_back({reviewerId, "REVIEWER"});
  const auto valid = validateProductionProject(draft);
  if (!valid) return core::Result<Output>{valid.error()};
  const auto saved = save(draft, {"reviewer-register", reviewerId, std::move(producerId), std::move(occurredAtUtc)}, stop);
  bool durable = true;
  std::string diagnostic;
  if (!saved) {
    const auto recovered = recover();
    if (!recovered || recovered.value().lastDurableGeneration <= project.lastDurableGeneration)
      return core::Result<Output>{saved.error()};
    auto comparable = recovered.value();
    comparable.lastDurableGeneration = draft.lastDurableGeneration;
    if (encodeProductionProject(comparable) != encodeProductionProject(draft))
      return core::failure<Output>(core::ErrorCode::Conflict,
          "Reviewer registration was not confirmed; recovery found different work");
    draft = recovered.value();
    durable = false;
    diagnostic = "The reviewer identity is recoverably committed; inspect before retrying. " + saved.error().message;
  }
  project = std::move(draft);
  return Output{project.lastDurableGeneration, core::sha256Hex(encodeProductionProject(project)), durable,
                std::move(diagnostic)};
}

}  // namespace seam::voicebank_production
