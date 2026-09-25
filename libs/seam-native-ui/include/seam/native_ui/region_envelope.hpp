#pragma once

#include "seam/authoring/render_coordinator.hpp"
#include "seam/domain/ids.hpp"
#include "seam/rendering/shared_pcm_buffer.hpp"
#include "seam/time/tick.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace seam::native_ui {

// A signed minimum/maximum pair. Asymmetric audio stays asymmetric.
struct EnvelopePair final {
  float minimum{0.0F};
  float maximum{0.0F};
};

// A min/max summary of one region's published mono audio, for drawing its waveform inside the
// notes. Level 0 holds one pair per kBlock samples; each further level merges four pairs of the
// level below, so a query costs a few pairs whatever the zoom.
class RegionEnvelope final {
public:
  static constexpr std::size_t kBlock = 256U;

  // Returns nullptr when stopped before completion.
  [[nodiscard]] static std::shared_ptr<const RegionEnvelope> build(std::span<const float> mono,
                                                                   std::stop_token stop = {});

  // Min/max over samples [first, last), at block resolution, clipped to the audio. Empty when the
  // interval does not overlap the audio.
  [[nodiscard]] std::optional<EnvelopePair> range(std::int64_t first, std::int64_t last) const;
  [[nodiscard]] std::size_t sampleCount() const noexcept { return samples_; }
  // Largest absolute sample, for display scaling.
  [[nodiscard]] float peak() const noexcept { return peak_; }
  [[nodiscard]] std::size_t levelCount() const noexcept { return levels_.size(); }

private:
  std::vector<std::vector<EnvelopePair>> levels_;
  std::size_t samples_{0U};
  float peak_{0.0F};
};

// Everything that identifies the audio an envelope was built from (spec: publication identity,
// quality, PCM identity, sample rate and origin, not the revision alone).
struct RegionEnvelopeKey final {
  std::uint64_t projectRevision{0U};
  std::uint64_t requestId{0U};
  const void* publicationIdentity{nullptr};
  std::uint8_t quality{0U};
  domain::TrackId track{};
  domain::RegionId region{};
  const void* pcmIdentity{nullptr};
  std::size_t pcmSamples{0U};
  std::uint32_t sampleRate{0U};
  time::SampleFrame originFrame{0};
  friend bool operator==(const RegionEnvelopeKey&, const RegionEnvelopeKey&) = default;
};

// The envelope bound to its region, sample rate and absolute project-frame origin.
struct RegionEnvelopeView final {
  RegionEnvelopeKey key;
  std::shared_ptr<const RegionEnvelope> envelope;
};

// Builds envelopes off the paint thread. request() returns the view for exactly this key once it
// is built, and nothing before that or for any other key; a new key cancels an older build.
// request() never waits for a worker: a host may call it under its own lock while a finishing
// worker's ready() callback asks that host for a repaint.
class RegionEnvelopeCache final {
public:
  explicit RegionEnvelopeCache(std::function<void()> ready = {});
  ~RegionEnvelopeCache();
  RegionEnvelopeCache(const RegionEnvelopeCache&) = delete;
  RegionEnvelopeCache& operator=(const RegionEnvelopeCache&) = delete;

  [[nodiscard]] std::shared_ptr<const RegionEnvelopeView> request(const RegionEnvelopeKey& key,
                                                                  rendering::SharedPcmBuffer pcm);
  // Forgets the current envelope (no current audio to show).
  void clear();

private:
  struct Worker final {
    std::shared_ptr<std::atomic<bool>> done;
    std::jthread thread;
  };
  void retireLocked();

  std::function<void()> ready_;
  std::mutex mutex_;
  std::optional<RegionEnvelopeKey> requested_;
  std::shared_ptr<const RegionEnvelopeView> built_;
  std::optional<Worker> worker_;
  std::vector<Worker> retired_;
};

// What the SING grid draws inside its notes: the selected region's own published mono render, or
// nothing and the reason. It is never a stand-in taken from the project mix.
struct RegionWaveform final {
  std::shared_ptr<const RegionEnvelopeView> view;
  std::string caption;  // a short label for the tool strip
  std::string reason;   // why nothing is drawn; empty while a current waveform is shown
  [[nodiscard]] bool shown() const noexcept { return view != nullptr; }
};

struct RegionWaveformRequest final {
  std::shared_ptr<const authoring::PublishedProjectAudio> audio;
  bool stale{false};
  std::uint64_t documentRevision{0U};
  domain::TrackId track{};
  domain::RegionId region{};
};

// Binds the audible publication to the region on screen. The waveform shows only for a Ready
// render of this exact document revision, track and region with its own mono audio; any other
// state draws nothing and says why.
[[nodiscard]] RegionWaveform bindRegionWaveform(const RegionWaveformRequest& request,
                                                RegionEnvelopeCache& cache);

}  // namespace seam::native_ui
