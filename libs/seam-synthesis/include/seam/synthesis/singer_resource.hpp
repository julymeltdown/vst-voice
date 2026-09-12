#pragma once

#include "seam/synthesis/phrase_renderer.hpp"
#include <variant>
#include <stop_token>

namespace seam::synthesis {

struct SelectedUnitIdentity final {
  std::string unitId;
  std::string audioSha256;
  std::string sourceAlignmentSha256{};
  friend bool operator==(const SelectedUnitIdentity&, const SelectedUnitIdentity&) = default;
};

struct SampleSingerResource final {
  std::shared_ptr<const voicebank::Manifest> voicebank;
  std::shared_ptr<const UnitPlan> unitPlan;
  std::vector<SelectedUnitIdentity> selectedUnits;
  std::vector<FrozenUnitAudio> frozenAudio;
  std::filesystem::path bankRoot;
  PhraseRenderOptions renderOptions;
};

// Owns a deep copy, with no mutable byte accessor or file path. A caller's
// source span must remain stable during freeze; it may change afterwards.
class FrozenSingerData final {
public:
  [[nodiscard]] static core::Result<FrozenSingerData> freeze(std::span<const std::byte> bytes,
      std::string_view expectedSha256, std::size_t maximumBytes, std::stop_token stopToken = {});
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
  [[nodiscard]] const std::string& sha256() const noexcept { return sha256_; }
private:
  FrozenSingerData(std::vector<std::byte> bytes, std::string hash)
      : bytes_(std::move(bytes)), sha256_(std::move(hash)) {}
  std::vector<std::byte> bytes_;
  std::string sha256_;
};

// Frozen opaque bytes are not proof of a valid patch/model format, rights,
// executable backend or acoustic quality. Sample dispatch rejects both kinds.
struct ProceduralSingerResource final {
  domain::SingerResourceIdentity identity{domain::SingerResourceKind::Procedural, {}, {}, {}};
  std::shared_ptr<const FrozenSingerData> patch{};
  [[nodiscard]] core::Result<void> validate() const;
};
struct NeuralSingerResource final {
  domain::SingerResourceIdentity identity{domain::SingerResourceKind::Neural, {}, {}, {}};
  std::shared_ptr<const FrozenSingerData> model{};
  [[nodiscard]] core::Result<void> validate() const;
};

[[nodiscard]] core::Result<ProceduralSingerResource> freezeProceduralResource(
    domain::SingerResourceIdentity identity, std::span<const std::byte> patch,
    std::stop_token stopToken = {});
[[nodiscard]] core::Result<NeuralSingerResource> freezeNeuralResource(
    domain::SingerResourceIdentity identity, std::span<const std::byte> model,
    std::stop_token stopToken = {});

using SingerResource = std::variant<SampleSingerResource, ProceduralSingerResource, NeuralSingerResource>;

}
