#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank/catalog.hpp"

#include <map>
#include <string>
#include <string_view>

namespace seam::authoring {

struct BankReferenceKey final {
  std::string id;
  std::string version;

  friend bool operator<(const BankReferenceKey& lhs,
                        const BankReferenceKey& rhs) noexcept {
    return lhs.id < rhs.id ||
           (lhs.id == rhs.id && lhs.version < rhs.version);
  }
};

class BankReferenceRegistry final {
public:
  void clear() noexcept { references_.clear(); }

  [[nodiscard]] core::Result<void> registerCandidate(
      const voicebank::VoicebankCandidate& candidate);
  [[nodiscard]] bool contains(std::string_view id,
                              std::string_view version,
                              std::string_view contentHash) const noexcept;
  [[nodiscard]] std::string contentHash(std::string_view id,
                                         std::string_view version) const;

private:
  std::map<BankReferenceKey, std::string> references_;
};

}
