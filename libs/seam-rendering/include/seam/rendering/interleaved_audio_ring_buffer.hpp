#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace seam::rendering {

class SpscInterleavedAudioRingBuffer final {
public:
  // The position of a frame that was written without one.
  static constexpr std::int64_t kNoPosition = std::numeric_limits<std::int64_t>::min();

  SpscInterleavedAudioRingBuffer(std::size_t capacityFrames,
                                 std::uint8_t channelCount);

  [[nodiscard]] std::size_t writeFrames(
      std::span<const float> interleavedInput) noexcept;
  // The same, and `positions` holds one entry per frame of the input: where in the audio that frame
  // is. The ring keeps it with the frame, so that it can say where in the audio its consumer is
  // (nextFramePosition) without working it out from how much the producer has written, which is not
  // the same once the audio loops or starts somewhere other than its beginning. Returns 0 and
  // writes nothing when the number of positions is not the number of frames.
  [[nodiscard]] std::size_t writeFrames(
      std::span<const float> interleavedInput,
      std::span<const std::int64_t> positions) noexcept;
  [[nodiscard]] std::size_t readFrames(
      std::span<float> interleavedOutput) noexcept;
  [[nodiscard]] std::size_t availableReadFrames() const noexcept;
  [[nodiscard]] std::size_t availableWriteFrames() const noexcept;
  [[nodiscard]] std::size_t capacityFrames() const noexcept {
    return frameCapacity_;
  }
  [[nodiscard]] std::uint8_t channelCount() const noexcept {
    return channelCount_;
  }

  [[nodiscard]] std::uint64_t requestConsumerReset() noexcept;
  [[nodiscard]] bool resetAcknowledged(std::uint64_t epoch) const noexcept;
  // The consumer's half of a reset, without reading anything, for the owner of a consumer that is
  // not running (an audio device that is stopped). The producer asks for a reset and writes nothing
  // until it is acknowledged, and only the consumer acknowledges: with the device stopped nobody
  // would, so the owner answers in its place. Drops what the ring holds and acknowledges the request
  // that is waiting. Returns whether one was: with none waiting it changes nothing, so audio written
  // after the last reset is kept.
  // Call it only while no thread is inside readFrames(): the consumer's work is not shared.
  [[nodiscard]] bool serviceResetRequest() noexcept;
  // Whether the producer has asked for a reset that no consumer has answered yet. What the ring
  // holds is then audio that is going to be dropped unplayed, and not audio that is about to be
  // heard. Safe to call from any thread.
  [[nodiscard]] bool resetPending() const noexcept;
  // Where in the audio the frame is that the consumer plays next, going by the positions the
  // producer wrote with the frames. Empty when the ring holds no frame, or the next one was written
  // without a position. Safe to call from any thread, and the answer is exact: it is the position of
  // the frame that was next at one instant between the start of the call and its end.
  //
  // Why it is: the ring counts the frames written and the frames read in two counters that only ever
  // grow (64 bits, so they do not wrap), beside the indices that address the slots. The producer
  // stores a frame's position before the write counter that publishes the frame, and the consumer
  // stores the read counter before the read index that gives the frame's slot back to the producer.
  // The reader takes the read counter, then the write counter, and when the ring holds a frame
  // takes the position in that frame's slot and then the read counter again. An unchanged read
  // counter means the consumer had not played the frame while the position was taken, so the slot
  // had not been given back and was not rewritten: the position is the frame's. A changed one means
  // the position may be a later frame's, and the reader starts again. A reset moves the read counter
  // in one step, so a reader that straddles it starts again too. Indices that wrap, which a reader
  // cannot take as a pair without the two moving between its loads, take no part in it.
  [[nodiscard]] std::optional<std::int64_t> nextFramePosition() const noexcept;
  // For tests. Called at three points of nextFramePosition() with the number of the point (1: the
  // read counter has been taken, 2: the write counter has been taken, 3: the position has been
  // taken), so that a test can have the consumer and the producer move at any of them. It is empty
  // in the product. It must not throw and must not call nextFramePosition(), and it is to be set
  // only while no other thread uses the ring.
  using SnapshotProbe = std::function<void(int)>;
  void setSnapshotProbe(SnapshotProbe probe);
  [[nodiscard]] bool lastReadWasReset() const noexcept {
    return lastReadWasReset_.load(std::memory_order_acquire);
  }

private:
  [[nodiscard]] bool consumeResetRequest(
      std::span<float> interleavedOutput) noexcept;
  [[nodiscard]] std::size_t writeFramesAt(std::span<const float> interleavedInput,
                                          const std::int64_t* positions) noexcept;

  std::vector<float> buffer_;
  // The position of the frame in each slot, parallel to buffer_.
  std::vector<std::atomic<std::int64_t>> positions_;
  std::size_t frameCapacity_{0U};
  std::uint8_t channelCount_{0U};
  // Indices address logical frames in a ring with one hidden sentinel frame.
  alignas(64) std::atomic<std::size_t> readFrame_{0U};
  alignas(64) std::atomic<std::size_t> writeFrame_{0U};
  // How many frames were ever written and ever read. Each has one writer, the producer and the
  // consumer, and only grows; a reset moves the read counter up to the write counter.
  alignas(64) std::atomic<std::uint64_t> writeTotal_{0U};
  alignas(64) std::atomic<std::uint64_t> readTotal_{0U};
  alignas(64) std::atomic<std::uint64_t> requestedResetEpoch_{0U};
  alignas(64) std::atomic<std::uint64_t> acknowledgedResetEpoch_{0U};
  alignas(64) std::atomic<bool> lastReadWasReset_{false};
  SnapshotProbe snapshotProbe_;
};

}  // namespace seam::rendering
