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

}  // namespace seam::standalone
