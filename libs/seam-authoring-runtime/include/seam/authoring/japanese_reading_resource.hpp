#pragma once
#include "seam/phonemizer/japanese_reading.hpp"
#include <array>
#include <filesystem>

namespace seam::authoring {
inline constexpr std::array<const char*, 4U> kJapaneseDictionaryFiles{"char.bin", "matrix.bin", "sys.dic", "unk.dic"};
// Expectations must come from application-controlled intake/distribution data,
// never hashes supplied by the same untrusted project that selects the files.
struct JapaneseReadingResourceSpec final {
  std::filesystem::path executable;
  std::string executableSha256;
  std::string engineRevision;
  std::filesystem::path dictionaryDirectory;
  std::array<std::string, 4U> dictionarySha256;
};
class VerifiedJapaneseReadingResource final {
public:
  [[nodiscard]] static core::Result<VerifiedJapaneseReadingResource> verify(
      JapaneseReadingResourceSpec spec, std::stop_token stop = {});
  [[nodiscard]] core::Result<void> revalidate(std::stop_token stop = {}) const;
  [[nodiscard]] const JapaneseReadingResourceSpec& spec() const noexcept { return spec_; }
  [[nodiscard]] const phonemizer::JapaneseReadingIdentity& identity() const noexcept { return identity_; }
private:
  VerifiedJapaneseReadingResource(JapaneseReadingResourceSpec spec, phonemizer::JapaneseReadingIdentity identity)
      : spec_(std::move(spec)), identity_(std::move(identity)) {}
  JapaneseReadingResourceSpec spec_;
  phonemizer::JapaneseReadingIdentity identity_;
};
}
