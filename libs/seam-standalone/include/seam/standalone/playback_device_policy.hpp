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

// How long a window that paints only on request waits before a frame asks the device again, after a
// Start that failed. A device that cannot start is not asked at the display's rate.
inline constexpr std::chrono::milliseconds kDeviceRetryDelay{250};

// What a frame that has dealt with the device leaves for the next one. A window that has nothing to
// animate runs no frame of its own accord (see INativeWindowClient::nextFrameDue), and what a frame
// does for the device is work that goes on over frames: a decision that waits for the feeder, a start
// that failed, a device that is still to be asked to stop, a song that is still to be played. So a
// frame that leaves any of it asks for the next one, and a window with none of it goes to sleep.
struct DeviceFollowUp final {
  // Another frame at the display's rate.
  bool frameNow{false};
  // Another frame after this long, for work that is not to be asked again at once.
  std::optional<std::chrono::milliseconds> retryAfter;
};

// The follow-up of a frame, given the transport report that the frame decided by, what it decided
// (action) and whether doing it failed, and whether the device runs once that is done:
//  - A device that runs is the consumer of the ring and plays a song whose playhead and level are to
//    be shown, so the window keeps painting for as long as it runs: not only while the output meter
//    has a block to show, which it has not until the first one has been played, and has not when the
//    callback stalls, or when the platform will not say that the device has stopped. A device that is
//    to be stopped and does not stop runs on, and is asked again by the next frame.
//  - A report that does not yet include every command the creator sent decides nothing
//    (decideDeviceAction), so a frame that meets one asks for another: the feeder applies the commands
//    on its own thread, a moment after they are sent, and a frame that is painted for a menu command
//    comes before that.
//  - A Start that failed is asked again, later (kDeviceRetryDelay): the Play that wanted it is the
//    creator's still, and nothing else would ask the device again.
// With no device there is nothing to follow up.
[[nodiscard]] constexpr DeviceFollowUp deviceFollowUp(const authoring::TransportState& transport,
                                                      bool deviceExists,
                                                      DeviceAction action,
                                                      bool actionFailed,
                                                      bool deviceRunning) noexcept {
  DeviceFollowUp followUp;
  if (!deviceExists) return followUp;
  followUp.frameNow = deviceRunning || !transport.settled;
  if (action == DeviceAction::Start && actionFailed) followUp.retryAfter = kDeviceRetryDelay;
  return followUp;
}

}  // namespace seam::standalone
