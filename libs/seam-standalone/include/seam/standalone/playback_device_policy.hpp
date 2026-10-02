#pragma once

#include "seam/authoring/transport_controller.hpp"

#include <chrono>
#include <cstddef>
#include <optional>

namespace seam::standalone {

enum class DeviceAction { None, Start, Stop };

// What a painted frame does about the audio device, given what the transport reports. The device is
// the consumer of the transport's ring, and the feeder that fills the ring reports a state that
// follows the creator's commands by a moment and leads what is heard by as much as the ring holds,
// so neither the report nor an empty or a full ring can be taken at face value:
//  - A transport that is not playing stops the device only when the ring has been played out. The
//    feeder reports that it has stopped when it has handed over the last of the audio, and stopping
//    then would cut off whatever the ring still holds of the end. For a pause, a stop or a seek the
//    consumer has been asked to drop the ring, which empties it the same way.
//  - Neither a stop nor a start goes by a report that does not yet include every command the creator
//    sent: it says nothing about what was asked last, and acting on it undoes that command.
//  - A transport that is playing audio, with no device running, starts it.
//  - So does a Play that no device has taken up (TransportState::playAwaitsConsumer) when the feeder
//    has handed over all of its audio already, which is what a Play within a ring's length of the
//    end of the song does, and a Play from the Transport menu, which sends no command to the device,
//    waits on nothing else. Such a feeder reports that it is not playing, and a frame that waited
//    for it to report that it is would never see it: it hands over everything in one turn. The
//    audio is in the ring for the device to play, and what it plays is what the creator asked for.
//    A device that has run since the Play has taken it up, and one that stopped on its own is not
//    started again for the tail its ring holds. An empty ring has nothing to start a device for.
[[nodiscard]] constexpr DeviceAction decideDeviceAction(const authoring::TransportState& transport,
                                                        std::size_t bufferedFrames,
                                                        bool deviceExists,
                                                        bool deviceRunning) noexcept {
  if (!deviceExists || !transport.settled) return DeviceAction::None;
  if (!transport.playing) {
    if (transport.playAwaitsConsumer && transport.available && !deviceRunning &&
        bufferedFrames > 0U) {
      return DeviceAction::Start;
    }
    return deviceRunning && bufferedFrames == 0U ? DeviceAction::Stop : DeviceAction::None;
  }
  return transport.available && !deviceRunning ? DeviceAction::Start : DeviceAction::None;
}

// How long a Start that failed holds the next one back (see DeviceStartRetry).
inline constexpr std::chrono::milliseconds kDeviceRetryDelay{250};

// The time that a Start which failed holds the next one back until. A window paints for many reasons
// that have nothing to do with the device (an animation, a pointer, a worker that landed), and a frame
// that finds a Play that no device has taken up asks the device to start. Without a hold, a backend that
// cannot start would be asked at the rate of those frames, on the thread that paints, and the creator's
// Play, which stays theirs, would be tried dozens of times a second. With one, the first frame after the
// delay asks, and a window that has nothing else to paint is woken for it (nextFrameDue()).
//
// The hold belongs to the Play that wanted the device and to that device. It stays through the frames
// that come early, which neither ask the device nor move it, and through a report that does not yet
// include every command the creator sent, which says nothing about whether the Play is still wanted. It
// goes when the Start works; when a complete report no longer wants a device started (a Pause, a Stop,
// the audio taken away), because the next Play is a new one and is asked at once; when there is no
// device; and when the creator changes the audio settings (clear()), because the device that comes of
// that has its own first try.
class DeviceStartRetry final {
public:
  using Clock = std::chrono::steady_clock;

  // The action that a frame at `now` takes of what it decided: a Start that is held back is not taken.
  [[nodiscard]] constexpr DeviceAction gate(DeviceAction decided,
                                            Clock::time_point now) const noexcept {
    return decided == DeviceAction::Start && notBefore_.has_value() && now < *notBefore_
               ? DeviceAction::None
               : decided;
  }

  // What the frame leaves behind. `decided` is what decideDeviceAction said, `taken` what the frame
  // did of it (gate), `failed` whether doing it failed, and `at` the time when it was done, after the
  // attempt, which can take a while: a hold counts from when the device said no.
  constexpr void afterFrame(const authoring::TransportState& transport, bool deviceExists,
                            DeviceAction decided, DeviceAction taken, bool failed,
                            Clock::time_point at) noexcept {
    if (taken == DeviceAction::Start) {
      if (failed) {
        notBefore_ = at + kDeviceRetryDelay;
      } else {
        notBefore_.reset();
      }
      return;
    }
    // A Start that was held back leaves the hold where it was.
    if (decided == DeviceAction::Start) return;
    if (!deviceExists || transport.settled) notBefore_.reset();
  }

  // The creator changed what the hold was about: another device, with its own first try.
  constexpr void clear() noexcept { notBefore_.reset(); }

  // When a window that has nothing else to paint is to be woken for the next try, if one is held.
  [[nodiscard]] constexpr std::optional<Clock::time_point> notBefore() const noexcept {
    return notBefore_;
  }

private:
  std::optional<Clock::time_point> notBefore_;
};

// Whether the frame that has dealt with the device asks for another at once. A window that has nothing
// to animate runs no frame of its own accord (see INativeWindowClient::nextFrameDue), and some of what a
// frame does for the device goes on over frames:
//  - A device that runs is the consumer of the ring and plays a song whose playhead and level are to be
//    shown, so the window keeps painting for as long as it runs: not only while the output meter has a
//    block to show, which it has not until the first one has been played, and has not when the callback
//    stalls, or when the platform will not say that the device has stopped. A device that is to be
//    stopped and does not stop runs on, and is asked again by the next frame.
//  - A report that does not yet include every command the creator sent decides nothing
//    (decideDeviceAction), so a frame that meets one asks for another: the feeder applies the commands
//    on its own thread, a moment after they are sent, and a frame that is painted for a menu command
//    comes before that.
// A Start that failed is not in this: it is asked again later, once its hold is over
// (DeviceStartRetry). With no device there is nothing to ask for.
[[nodiscard]] constexpr bool deviceNeedsFrame(const authoring::TransportState& transport,
                                              bool deviceExists, bool deviceRunning) noexcept {
  return deviceExists && (deviceRunning || !transport.settled);
}

}  // namespace seam::standalone
