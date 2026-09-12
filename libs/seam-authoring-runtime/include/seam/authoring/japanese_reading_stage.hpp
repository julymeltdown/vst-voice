#pragma once
#include "seam/authoring/japanese_reading_resource.hpp"
#include <memory>

namespace seam::authoring {
class StagedJapaneseReadingResource final {
public:
  // Private copies under an application-controlled parent. Keeps the original
  // verified identity; does not confer trust or prevent owner/root tampering.
  [[nodiscard]] static core::Result<StagedJapaneseReadingResource> prepare(
      const VerifiedJapaneseReadingResource& source, const std::filesystem::path& parent,
      std::stop_token stop = {});
  [[nodiscard]] const VerifiedJapaneseReadingResource& resource() const noexcept;
private:
  struct Owner;
  explicit StagedJapaneseReadingResource(std::shared_ptr<Owner> owner) : owner_(std::move(owner)) {}
  std::shared_ptr<Owner> owner_;
};
}
