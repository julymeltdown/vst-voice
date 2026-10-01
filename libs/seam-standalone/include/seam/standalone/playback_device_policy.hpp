#pragma once

#include "seam/authoring/transport_controller.hpp"

#include <cstddef>

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
[[nodiscard]] constexpr DeviceAction decideDeviceAction(const authoring::TransportState& transport,
                                                        std::size_t bufferedFrames,
                                                        bool deviceExists,
                                                        bool deviceRunning) noexcept {
  if (!deviceExists || !transport.settled) return DeviceAction::None;
  if (!transport.playing) {
    return deviceRunning && bufferedFrames == 0U ? DeviceAction::Stop : DeviceAction::None;
  }
  return transport.available && !deviceRunning ? DeviceAction::Start : DeviceAction::None;
}

}  // namespace seam::standalone
