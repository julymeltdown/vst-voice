#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/audio_settings.hpp"
#include "seam/authoring/audio_settings_controller.hpp"

#include <vector>

TEST_CASE("audio settings store round trips versioned device configuration") {
  const auto root = seam::test::support::temporaryDirectory("audio-settings");
  seam::authoring::AudioSettingsStore store{root / "audio-settings.json"};
  const seam::authoring::AudioSettings expected{
      .deviceId = "default-output",
      .sampleRate = 96000U,
      .blockFrames = 512U,
      .outputChannels = 4U,
      .revision = 2U,
  };
  CHECK(store.save(expected));
  const auto loaded = store.load();
  CHECK(loaded);
  CHECK(loaded.value().deviceId == expected.deviceId);
  CHECK(loaded.value().sampleRate == expected.sampleRate);
  CHECK(loaded.value().blockFrames == expected.blockFrames);
  CHECK(loaded.value().outputChannels == expected.outputChannels);
  CHECK(loaded.value().revision == expected.revision);
}

TEST_CASE("audio settings reject values outside the Beta device matrix") {
  const auto root = seam::test::support::temporaryDirectory("audio-settings-invalid");
  seam::authoring::AudioSettingsStore store{root / "audio-settings.json"};
  auto invalidRate = seam::authoring::AudioSettings{
      .deviceId = "device",
      .sampleRate = 88200U,
      .blockFrames = 256U,
      .outputChannels = 2U,
      .revision = 1U,
  };
  CHECK(!store.save(invalidRate));
  invalidRate.sampleRate = 48000U;
  invalidRate.blockFrames = 96U;
  CHECK(!store.save(invalidRate));
  invalidRate.blockFrames = 256U;
  invalidRate.outputChannels = 3U;
  CHECK(!store.save(invalidRate));
}

TEST_CASE("audio settings controller rolls back a failed device restart") {
  const seam::authoring::AudioSettings initial{
      .deviceId = "device-a",
      .sampleRate = 48000U,
      .blockFrames = 256U,
      .outputChannels = 2U,
      .revision = 4U,
  };
  std::vector<seam::authoring::AudioSettings> attempts;
  seam::authoring::AudioSettingsController controller{
      initial,
      [&attempts](const auto& settings) -> seam::core::Result<void> {
        attempts.push_back(settings);
        if (settings.deviceId == "device-b") {
          return seam::core::failure(seam::core::ErrorCode::IoError,
                                     "Requested device failed to open");
        }
        return seam::core::success();
      }};
  auto requested = initial;
  requested.deviceId = "device-b";
  requested.sampleRate = 96000U;
  requested.blockFrames = 512U;
  CHECK(!controller.apply(requested));
  CHECK(controller.current() == initial);
  CHECK(attempts.size() == 2U);
  CHECK(attempts.back() == initial);
}
