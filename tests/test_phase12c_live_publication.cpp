#include "test_framework.hpp"

#include "seam/live_voice/realtime_publication.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace {

std::shared_ptr<const seam::live_voice::LiveVoicebankResources> makeResources(
    std::string id) {
  auto resources = std::make_shared<seam::live_voice::LiveVoicebankResources>();
  resources->identity.id = std::move(id);
  resources->identity.version = "1.0.0";
  resources->identity.contentHash = std::string(64U, 'a');
  resources->diagnosticIdentity = resources->identity.id;
  resources->units.push_back(
      seam::live_voice::LiveUnitAudio{.unitId = "unit"});
  return resources;
}

}

TEST_CASE("live resource publication is bounded and reader-safe") {
  seam::live_voice::LiveResourcePublication publication;
  CHECK(publication.publish(makeResources("generation-0")));
  std::atomic<bool> failed{false};
  std::jthread reader([&](std::stop_token token) {
    while (!token.stop_requested()) {
      auto handle = publication.acquire();
      if (!handle || handle->identity.id.empty() ||
          handle->identity.contentHash.size() != 64U) {
        failed.store(true, std::memory_order_relaxed);
        return;
      }
    }
  });
  for (int index = 1; index <= 1000; ++index) {
    auto candidate = makeResources("generation-" + std::to_string(index));
    while (!publication.publish(std::move(candidate))) {
      std::this_thread::yield();
      candidate = makeResources("generation-" + std::to_string(index));
    }
  }
  reader.request_stop();
  reader.join();
  CHECK(!failed.load(std::memory_order_relaxed));
  publication.clear();
  CHECK(!publication.acquire());
}
