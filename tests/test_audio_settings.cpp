#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/audio_settings.hpp"

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
