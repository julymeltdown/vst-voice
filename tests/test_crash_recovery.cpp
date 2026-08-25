#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/platform/crash_capture.hpp"

#include <filesystem>

TEST_CASE("crash capture stores and clears a bounded local marker") {
  const auto root = seam::test::support::temporaryDirectory("crash-capture");
  auto capture = seam::platform::CrashCapture::install(
      seam::platform::CrashCaptureConfig{.root = root});
  CHECK(capture);
  auto marker = capture.value()->writeMarker("RENDER_FAILED");
  CHECK(marker);
  CHECK(marker.value().purpose == "local-crash-marker");
  CHECK(marker.value().code == "RENDER_FAILED");
  auto loaded = capture.value()->readMarker();
  CHECK(loaded);
  CHECK(loaded.value().has_value());
  CHECK(loaded.value()->code == "RENDER_FAILED");
  CHECK(capture.value()->clearMarker());
  auto cleared = capture.value()->readMarker();
  CHECK(cleared);
  CHECK(!cleared.value().has_value());
}

TEST_CASE("crash capture rejects unsafe marker codes and symlink roots") {
  const auto root = seam::test::support::temporaryDirectory("crash-capture-invalid");
  auto capture = seam::platform::CrashCapture::install(
      seam::platform::CrashCaptureConfig{.root = root});
  CHECK(capture);
  CHECK(!capture.value()->writeMarker("/private/path"));
  const auto link = root.parent_path() / "crash-capture-link";
  std::error_code error;
  std::filesystem::remove(link, error);
  std::filesystem::create_directory_symlink(root, link, error);
  if (!error) {
    CHECK(!seam::platform::CrashCapture::install(
        seam::platform::CrashCaptureConfig{.root = link}));
  }
}
