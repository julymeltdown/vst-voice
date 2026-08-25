#include "test_framework.hpp"

#include "seam/platform/audio_device_catalog.hpp"

#include <algorithm>

TEST_CASE("audio device catalog exposes immutable identity and bounded capabilities") {
  auto catalog = seam::platform::createSystemAudioDeviceCatalog();
  CHECK(catalog != nullptr);
  const auto snapshot = catalog->enumerate();
  if (!snapshot) {
    CHECK(snapshot.error().code == seam::core::ErrorCode::Unsupported ||
          snapshot.error().code == seam::core::ErrorCode::IoError);
    return;
  }
  CHECK(snapshot.value().generation > 0U);
  for (const auto& device : snapshot.value().devices) {
    CHECK(!device.id.empty());
    CHECK(!device.name.empty());
    CHECK(device.minimumBlockFrames <= device.maximumBlockFrames);
    CHECK(device.minimumOutputChannels <= device.maximumOutputChannels);
    CHECK(std::all_of(device.supportedSampleRates.begin(),
                      device.supportedSampleRates.end(),
                      [](std::uint32_t rate) {
                        return rate == 44100U || rate == 48000U || rate == 96000U;
                      }));
  }
}
