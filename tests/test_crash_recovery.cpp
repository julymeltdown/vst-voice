#include "test_framework.hpp"
#include "test_support.hpp"
#include "crash_capture_test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/platform/crash_capture.hpp"
#include "../libs/seam-platform/src/crash_capture_internal.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <latch>
#include <thread>

#ifndef _WIN32
#include <csignal>
#include <sys/stat.h>
#endif

namespace {

template <typename Handle, Handle Invalid>
void checkCrashWriterRetirement(Handle active) {
  seam::platform::detail::CrashWriterSlot<Handle, Invalid> slot;
  slot.publish(active);
  std::latch retained{1};
  std::latch releaseRetained{1};
  auto retainedHandle = Invalid;
  auto lateHandle = active;
  auto drainVisits = 0U;
  auto releasedRetained = false;
  std::thread writer([&] {
    retainedHandle = slot.acquire();
    retained.count_down();
    releaseRetained.wait();
    slot.release();
  });
  retained.wait();
  const auto retired = slot.retire(active, [&] {
    ++drainVisits;
    if (drainVisits == 1U) {
      std::thread lateWriter([&] {
        lateHandle = slot.acquire();
        slot.release();
      });
      lateWriter.join();
    }
    if (drainVisits == 2U) {
      releasedRetained = true;
      releaseRetained.count_down();
    }
    std::this_thread::yield();
  });
  if (!releasedRetained) releaseRetained.count_down();
  writer.join();
  CHECK(retainedHandle == active);
  CHECK(drainVisits >= 2U);
  CHECK(retired);
  CHECK(lateHandle == Invalid);
  CHECK(slot.load() == Invalid);
  slot.publish(active);
  CHECK(slot.acquire() == active);
  slot.release();
  CHECK(slot.retire(active, [] {}));
  CHECK(!slot.retire(active, [] {}));
}

}

TEST_CASE("crash descriptor retirement rejects late writers while retained writer drains") {
  checkCrashWriterRetirement<int, -1>(42);
}

TEST_CASE("crash handle retirement rejects late writers while retained writer drains") {
  checkCrashWriterRetirement<std::uintptr_t, 0U>(42U);
}

TEST_CASE("forced terminate crash leaves a bounded exact-candidate marker") {
  if (seam::platform::crashCaptureBackendName() == "unavailable") return;
  const auto root =
      seam::test::support::temporaryDirectory("crash-capture-terminate");
  const auto child = seam::test::support::runCrashCaptureProbe(root, "terminate");
  CHECK(child.launched);
  CHECK(child.completed);
  CHECK(child.abnormalExit);

  const seam::platform::CrashCaptureConfig config{.root = root};
  auto pending = seam::platform::CrashCapture::readPending(config);
  CHECK(pending);
  CHECK(pending.value().has_value());
  CHECK(pending.value()->cause == seam::platform::CrashCause::Terminate);
  CHECK(pending.value()->platformCode == 0U);
  CHECK(pending.value()->processId != 0U);
  CHECK(std::filesystem::file_size(root / "crash-marker.raw") == 32U);
#ifndef _WIN32
  struct stat markerStatus {};
  CHECK(::lstat((root / "crash-marker.raw").c_str(), &markerStatus) == 0);
  CHECK((markerStatus.st_mode & (S_IRWXG | S_IRWXO)) == 0);
  CHECK(markerStatus.st_nlink == 1);
#endif

  auto recovered = seam::platform::CrashCapture::recoverPending(config);
  CHECK(recovered);
  CHECK(recovered.value().has_value());
  CHECK(recovered.value()->schemaVersion == 2);
  CHECK(recovered.value()->purpose == "local-crash-recovery");
  CHECK(recovered.value()->code == "TERMINATE");
  CHECK(recovered.value()->contextAvailable);
  CHECK(recovered.value()->context.candidateId == "candidate-crash-probe");
  CHECK(recovered.value()->context.bankId == "beta-bank");
  CHECK(recovered.value()->context.bankVersion == "1.0.0");
  CHECK(recovered.value()->context.bankContentHash == std::string(64U, 'a'));
  CHECK(recovered.value()->context.audioUnderflowFrames == 17U);
  CHECK(recovered.value()->context.audioXruns == 3U);
  CHECK(!std::filesystem::exists(root / "crash-marker.raw"));

  auto capture = seam::platform::CrashCapture::install(config);
  CHECK(capture);
  auto loaded = capture.value()->readMarker();
  CHECK(loaded);
  CHECK(loaded.value().has_value());
  CHECK(loaded.value()->processId == recovered.value()->processId);
  CHECK(capture.value()->clearMarker());
  auto cleared = capture.value()->readMarker();
  CHECK(cleared);
  CHECK(!cleared.value().has_value());
  CHECK(std::filesystem::exists(root / "crash-context.json"));
}

TEST_CASE("forced native crash records the platform reason") {
  if (seam::platform::crashCaptureBackendName() == "unavailable") return;
  const auto root =
      seam::test::support::temporaryDirectory("crash-capture-native");
  const auto child = seam::test::support::runCrashCaptureProbe(root, "native");
  CHECK(child.launched);
  CHECK(child.completed);
  CHECK(child.abnormalExit);

  const seam::platform::CrashCaptureConfig config{.root = root};
  auto pending = seam::platform::CrashCapture::readPending(config);
  CHECK(pending);
  CHECK(pending.value().has_value());
#ifdef _WIN32
  CHECK(pending.value()->cause ==
        seam::platform::CrashCause::UnhandledException);
  CHECK(pending.value()->platformCode == 0xE000534DU);
#else
  CHECK(pending.value()->cause == seam::platform::CrashCause::FatalSignal);
  CHECK(pending.value()->platformCode == static_cast<std::uint32_t>(SIGABRT));
#endif
  auto recovered = seam::platform::CrashCapture::recoverPending(config);
  CHECK(recovered);
  CHECK(recovered.value().has_value());
  CHECK(recovered.value()->context.host ==
        seam::platform::crashCaptureBackendName());
}

TEST_CASE("startup crash without prior context remains recoverable") {
  if (seam::platform::crashCaptureBackendName() == "unavailable") return;
  const auto root =
      seam::test::support::temporaryDirectory("crash-capture-no-context");
  const auto child = seam::test::support::runCrashCaptureProbe(
      root, "terminate-no-context");
  CHECK(child.launched);
  CHECK(child.completed);
  CHECK(child.abnormalExit);

  const seam::platform::CrashCaptureConfig config{.root = root};
  auto recovered = seam::platform::CrashCapture::recoverPending(config);
  CHECK(recovered);
  CHECK(recovered.value().has_value());
  CHECK(!recovered.value()->contextAvailable);
  CHECK(recovered.value()->context.candidateId.empty());
  auto capture = seam::platform::CrashCapture::install(config);
  CHECK(capture);
  auto loaded = capture.value()->readMarker();
  CHECK(loaded);
  CHECK(loaded.value().has_value());
  CHECK(!loaded.value()->contextAvailable);
}

TEST_CASE("crash capture rejects duplicate unsafe and corrupted state") {
  if (seam::platform::crashCaptureBackendName() == "unavailable") return;
  const auto root =
      seam::test::support::temporaryDirectory("crash-capture-invalid");
  const seam::platform::CrashCaptureConfig config{.root = root};
  auto capture = seam::platform::CrashCapture::install(config);
  CHECK(capture);
  CHECK(!seam::platform::CrashCapture::install(config));
  CHECK(!capture.value()->updateContext(
      seam::platform::CrashRecoveryContext{
          .candidateId = "candidate\nprivate",
          .host = "standalone",
      }));
  CHECK(!capture.value()->updateContext(
      seam::platform::CrashRecoveryContext{
          .candidateId = "candidate",
          .bankContentHash = "not-a-hash",
          .host = "standalone",
      }));
  capture.value().reset();

  const std::array corrupt{std::byte{0x53}, std::byte{0x45}, std::byte{0x41}};
  CHECK(seam::core::durableAtomicWrite(root / "crash-marker.raw", corrupt));
  CHECK(!seam::platform::CrashCapture::readPending(config));
  CHECK(!seam::platform::CrashCapture::install(config));

  std::error_code error;
  std::filesystem::remove(root / "crash-marker.raw", error);
  CHECK(!error);
  const auto sentinel = root.parent_path() / "crash-capture-sentinel";
  const std::array sentinelBytes{std::byte{0x7A}};
  CHECK(seam::core::durableAtomicWrite(sentinel, sentinelBytes));
  std::filesystem::create_symlink(sentinel, root / "crash-marker.raw", error);
  if (!error) {
    CHECK(!seam::platform::CrashCapture::readPending(config));
    CHECK(!seam::platform::CrashCapture::install(config));
    auto preserved = seam::core::readFileBytesLimited(sentinel, 2U);
    CHECK(preserved);
    CHECK(preserved.value().size() == 1U);
    CHECK(preserved.value().front() == std::byte{0x7A});
  }

  const auto link = root.parent_path() / "crash-capture-root-link";
  error.clear();
  std::filesystem::remove(link, error);
  error.clear();
  std::filesystem::create_directory_symlink(root, link, error);
  if (!error) {
    CHECK(!seam::platform::CrashCapture::install(
        seam::platform::CrashCaptureConfig{.root = link}));
  }
}
