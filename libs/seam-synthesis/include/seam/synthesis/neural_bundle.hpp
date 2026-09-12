#pragma once
#include "seam/synthesis/singer_resource.hpp"

namespace seam::synthesis {
enum class NeuralAssetRole { Acoustic, Vocoder, Vocabulary, Configuration, Variance, Tensor };
struct NeuralBundleAssetInput final {
  NeuralAssetRole role;
  std::string name;
  std::span<const std::byte> bytes;
  std::string sha256;
};
struct FrozenNeuralAsset final {
  NeuralAssetRole role;
  std::string name;
  std::shared_ptr<const FrozenSingerData> data;
};
// Data ownership and digest binding only. Not graph-format admission, execution,
// rights approval or acoustic qualification. No executable or filesystem paths.
// Source spans must remain stable during manifest/freeze calls. Copies share
// immutable backing; call valid() before accessing a moved-from handle.
// maximumTotalBytes bounds asset payloads; the manifest has a separate 32 KiB cap.
class FrozenNeuralBundle final {
public:
  [[nodiscard]] static core::Result<std::string> manifest(
      std::span<const NeuralBundleAssetInput> assets, std::size_t maximumTotalBytes);
  [[nodiscard]] static core::Result<FrozenNeuralBundle> freeze(
      domain::SingerResourceIdentity identity, std::span<const NeuralBundleAssetInput> assets,
      std::size_t maximumTotalBytes, std::stop_token stop = {});
  [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(data_); }
  [[nodiscard]] const domain::SingerResourceIdentity& identity() const { return data_->identity; }
  [[nodiscard]] std::span<const FrozenNeuralAsset> assets() const { return data_->assets; }
  [[nodiscard]] const FrozenSingerData& manifestData() const { return *data_->manifest; }
private:
  struct Data {
    domain::SingerResourceIdentity identity;
    std::shared_ptr<const FrozenSingerData> manifest;
    std::vector<FrozenNeuralAsset> assets;
  };
  explicit FrozenNeuralBundle(std::shared_ptr<const Data> data):data_(std::move(data)) {}
  std::shared_ptr<const Data> data_;
};
}
