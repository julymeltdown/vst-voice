#include "test_framework.hpp"

#include "seam/core/log_event.hpp"

TEST_CASE("log export projection omits private and forbidden fields") {
  const seam::core::LogEvent event{
      .code = "MEDIA_MISSING",
      .level = seam::core::LogLevel::Error,
      .category = "media",
      .message = "bounded diagnostic",
      .fields = {
          {"safe", "yes", seam::core::LogPrivacyClass::ExportSafe},
          {"path", "/Users/alice/private/song.wav", seam::core::LogPrivacyClass::LocalPrivate},
          {"secret", "token", seam::core::LogPrivacyClass::Forbidden},
      },
      .occurrenceCount = 2U,
  };
  CHECK(seam::core::isValidLogEvent(event));
  const auto projection = seam::core::exportSafeProjection(event);
  CHECK(projection.find("safe=yes") != std::string::npos);
  CHECK(projection.find("private/song.wav") == std::string::npos);
  CHECK(projection.find("token") == std::string::npos);
}

TEST_CASE("log event validation bounds fields and counts") {
  seam::core::LogEvent event{
      .code = "RENDER_FAILED",
      .level = seam::core::LogLevel::Error,
      .category = "render",
      .message = "failure",
      .occurrenceCount = 0U,
  };
  CHECK(!seam::core::isValidLogEvent(event));
  event.occurrenceCount = 1U;
  event.fields.push_back({std::string(65U, 'k'), "value",
                          seam::core::LogPrivacyClass::ExportSafe});
  CHECK(!seam::core::isValidLogEvent(event));
}
