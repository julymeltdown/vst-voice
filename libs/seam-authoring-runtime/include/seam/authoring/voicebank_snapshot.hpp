#pragma once
#include "seam/voicebank/catalog.hpp"
#include <memory>
#include <utility>

namespace seam::authoring {
class VoicebankSession;
// Created only by the owner after catalog resolution. Consumers cannot mutate
// the payload or fabricate a snapshot from a mutable candidate alias.
class VoicebankSnapshot final {
public:
  [[nodiscard]] const voicebank::VoicebankResolution& resolution() const noexcept { return resolution_; }
private:
  friend class VoicebankSession;
  explicit VoicebankSnapshot(voicebank::VoicebankResolution value) : resolution_(std::move(value)) {}
  const voicebank::VoicebankResolution resolution_;
};
using VoicebankSnapshotPtr = std::shared_ptr<const VoicebankSnapshot>;
}
