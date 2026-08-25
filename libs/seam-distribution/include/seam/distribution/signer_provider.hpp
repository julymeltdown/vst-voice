#pragma once

#include "seam/core/result.hpp"
#include "seam/distribution/signing.hpp"

#include <span>
#include <string>
#include <string_view>

namespace seam::distribution {

class IUpdateSignerProvider {
public:
  virtual ~IUpdateSignerProvider() = default;
  [[nodiscard]] virtual core::Result<Ed25519Signature> sign(
      std::span<const std::byte> payload) = 0;
  [[nodiscard]] virtual std::string_view keyId() const noexcept = 0;
};

}
