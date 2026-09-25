#include <clap/clap.h>

#include "platform_host.hpp"
#include "seam/build/version.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/time/tempo_map.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace {

class Module final {
public:
  bool open(const std::filesystem::path& path) {
#if defined(_WIN32)
    handle_ = LoadLibraryW(path.wstring().c_str());
    return handle_ != nullptr;
#elif defined(__unix__) || defined(__APPLE__)
    auto libraryPath = path;
#if defined(__APPLE__)
    std::error_code error;
    if (std::filesystem::is_directory(libraryPath, error)) {
      libraryPath /= "Contents/MacOS/ProjectSEAMEditor";
    }
#endif
    handle_ = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    return handle_ != nullptr;
#else
    static_cast<void>(path);
    return false;
#endif
  }
  void* symbol(const char* name) const {
#if defined(_WIN32)
    return handle_ != nullptr ? reinterpret_cast<void*>(GetProcAddress(handle_, name))
                              : nullptr;
#elif defined(__unix__) || defined(__APPLE__)
    return handle_ != nullptr ? dlsym(handle_, name) : nullptr;
#else
    static_cast<void>(name);
    return nullptr;
#endif
  }
  ~Module() {
#if defined(_WIN32)
    if (handle_ != nullptr) FreeLibrary(handle_);
#elif defined(__unix__) || defined(__APPLE__)
    if (handle_ != nullptr) dlclose(handle_);
#endif
  }
private:
#if defined(_WIN32)
  HMODULE handle_{nullptr};
#elif defined(__unix__) || defined(__APPLE__)
  void* handle_{nullptr};
#endif
};

struct HostContext final {
  clap_id timerId{CLAP_INVALID_ID};
  std::uint32_t timerPeriod{0U};
  std::atomic<std::uint32_t> restartRequests{0U};
  std::atomic<std::uint32_t> processRequests{0U};
  std::atomic<std::uint32_t> audioPortRescans{0U};
  std::atomic<std::uint32_t> audioConfigRescans{0U};
  std::atomic<std::uint32_t> stateDirtyCalls{0U};
  std::atomic<std::uint32_t> callbackRequests{0U};
};

bool CLAP_ABI registerTimer(const clap_host_t* host, std::uint32_t period,
                            clap_id* identifier) {
  auto* context = static_cast<HostContext*>(host->host_data);
  context->timerId = 7U;
  context->timerPeriod = period;
  if (identifier != nullptr) *identifier = context->timerId;
  return true;
}

bool CLAP_ABI unregisterTimer(const clap_host_t* host, clap_id identifier) {
  auto* context = static_cast<HostContext*>(host->host_data);
  if (identifier != context->timerId) return false;
  context->timerId = CLAP_INVALID_ID;
  return true;
}

const clap_host_timer_support_t kTimerHost{&registerTimer, &unregisterTimer};

bool CLAP_ABI audioRescanSupported(const clap_host_t*, std::uint32_t) {
  return true;
}
void CLAP_ABI audioRescan(const clap_host_t* host, std::uint32_t) {
  static_cast<HostContext*>(host->host_data)->audioPortRescans.fetch_add(
      1U, std::memory_order_relaxed);
}
void CLAP_ABI audioConfigRescan(const clap_host_t* host) {
  static_cast<HostContext*>(host->host_data)->audioConfigRescans.fetch_add(
      1U, std::memory_order_relaxed);
}
const clap_host_audio_ports_t kAudioPortsHost{&audioRescanSupported, &audioRescan};
const clap_host_audio_ports_config_t kAudioConfigHost{&audioConfigRescan};
void CLAP_ABI stateMarkDirty(const clap_host_t* host) {
  static_cast<HostContext*>(host->host_data)->stateDirtyCalls.fetch_add(
      1U, std::memory_order_relaxed);
}
const clap_host_state_t kStateHost{&stateMarkDirty};

const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char* id) {
  if (id == nullptr) return nullptr;
  if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &kTimerHost;
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPortsHost;
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG) == 0) return &kAudioConfigHost;
  if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kStateHost;
  return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t* host) {
  static_cast<HostContext*>(host->host_data)->restartRequests.fetch_add(
      1U, std::memory_order_relaxed);
}
void CLAP_ABI hostRequestProcess(const clap_host_t* host) {
  static_cast<HostContext*>(host->host_data)->processRequests.fetch_add(
      1U, std::memory_order_relaxed);
}
void CLAP_ABI hostRequestCallback(const clap_host_t* host) {
  static_cast<HostContext*>(host->host_data)->callbackRequests.fetch_add(
      1U, std::memory_order_relaxed);
}

struct EventList final {
  std::vector<const clap_event_header_t*> events;
  clap_input_events_t input{};
  EventList() {
    input.ctx = this;
    input.size = [](const clap_input_events_t* base) -> std::uint32_t {
      const auto* self = static_cast<const EventList*>(base->ctx);
      return static_cast<std::uint32_t>(self->events.size());
    };
    input.get = [](const clap_input_events_t* base,
                   std::uint32_t index) -> const clap_event_header_t* {
      const auto* self = static_cast<const EventList*>(base->ctx);
      return index < self->events.size() ? self->events[index] : nullptr;
    };
  }
};

struct Output final {
  std::vector<std::vector<float>> planes;
  std::vector<float*> pointers;
  clap_audio_buffer_t buffer{};
  Output(std::uint32_t frames, std::uint32_t channels)
      : planes(channels, std::vector<float>(frames, 0.0F)),
        pointers(channels, nullptr) {
    for (std::size_t channel = 0U; channel < planes.size(); ++channel) {
      pointers[channel] = planes[channel].data();
    }
    buffer.data32 = pointers.data();
    buffer.data64 = nullptr;
    buffer.channel_count = channels;
  }
};

struct WriteStream final {
  std::vector<std::byte> bytes;
  clap_ostream_t stream{};
  WriteStream() {
    stream.ctx = this;
    stream.write = [](const clap_ostream_t* base, const void* source,
                      std::uint64_t requested) -> std::int64_t {
      auto* self = static_cast<WriteStream*>(base->ctx);
      const auto count = std::min<std::size_t>(
          static_cast<std::size_t>(requested), 31U);
      if ((count != 0U && source == nullptr) || self->bytes.size() > 16U * 1024U * 1024U + 48U - count)
        return -1;
      const auto* first = static_cast<const std::byte*>(source);
      self->bytes.insert(self->bytes.end(), first, first +
                                             static_cast<std::ptrdiff_t>(count));
      return static_cast<std::int64_t>(count);
    };
  }
};

struct ReadStream final {
  std::span<const std::byte> bytes;
  std::size_t cursor{0U};
  clap_istream_t stream{};
  explicit ReadStream(std::span<const std::byte> source) : bytes(source) {
    stream.ctx = this;
    stream.read = [](const clap_istream_t* base, void* destination,
                     std::uint64_t requested) -> std::int64_t {
      auto* self = static_cast<ReadStream*>(base->ctx);
      if (self->cursor >= self->bytes.size()) return 0;
      const auto count = std::min<std::size_t>(
          {static_cast<std::size_t>(requested), 37U,
           self->bytes.size() - self->cursor});
      std::memcpy(destination, self->bytes.data() + self->cursor, count);
      self->cursor += count;
      return static_cast<std::int64_t>(count);
    };
  }
};

double energy(std::span<const float> values) {
  double result = 0.0;
  for (const auto value : values) result += std::abs(static_cast<double>(value));
  return result;
}

// The fixture host constructs independent wire state, then tests exclusively
// through the loaded plugin's public CLAP API. It never accesses its runtime.
seam::domain::Project savedProject(std::span<const std::byte> bytes) {
  if (bytes.size() < 48U || bytes.size() > 16U * 1024U * 1024U + 48U ||
      std::memcmp(bytes.data(), "SEAMED11", 8U) != 0)
    throw std::runtime_error{"Unexpected editor state envelope"};
  const auto payload = bytes.subspan(48U);
  const auto word = [&bytes](std::size_t offset) {
    std::uint32_t value=0U;
    for (std::uint32_t index=0U; index<4U; ++index)
      value |= std::to_integer<std::uint32_t>(bytes[offset+index]) << (index*8U);
    return value;
  };
  seam::core::Sha256 hash; hash.update(payload);
  const auto digest=hash.digest();
  if (word(8U)!=1U || word(12U)!=payload.size() ||
      !std::equal(digest.begin(),digest.end(),bytes.begin()+16))
    throw std::runtime_error{"Offline fixture state envelope failed integrity validation"};
  const auto decoded = seam::formats::ProjectJsonCodec{}.decode(std::string{
      reinterpret_cast<const char*>(payload.data()), payload.size()});
  if (!decoded) throw std::runtime_error{decoded.error().message};
  return decoded.value();
}

std::vector<std::byte> projectState(const seam::domain::Project& project) {
  const auto encoded = seam::formats::ProjectJsonCodec{}.encode(project);
  if (!encoded || encoded.value().size() > 16U * 1024U * 1024U)
    throw std::runtime_error{"Cannot encode bounded offline fixture state"};
  const auto& text = encoded.value();
  std::vector<std::byte> bytes(48U + text.size());
  std::memcpy(bytes.data(), "SEAMED11", 8U);
  const auto word = [&bytes](std::size_t offset, std::uint32_t value) {
    for (std::uint32_t index = 0U; index < 4U; ++index)
      bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  };
  word(8U, 1U); word(12U, static_cast<std::uint32_t>(text.size()));
  seam::core::Sha256 hash; hash.update(text);
  const auto digest = hash.digest();
  std::copy(digest.begin(), digest.end(), bytes.begin() + 16);
  std::memcpy(bytes.data() + 48U, text.data(), text.size());
  return bytes;
}

struct OfflineChecks final {
  bool completeScore{false};
  bool rateChange{false};
  bool missingRejected{false};
  bool failedFinalRejected{false};
  bool beatsOnlyRejected{false};
  bool eventOverflowFailClosed{false};
  bool realtimeOverflowKeepsAdvancing{false};
  bool followHostOffsetBounce{false};
  bool followHostStaleRejected{false};
  bool followHostTransportEventRejected{false};
  double followHostExpectedOnsetSeconds{0.0};
  double followHostEarlyEnergy{0.0};
  double followHostOnsetEnergy{0.0};
  std::string followHostStage{"not-started"};
  std::size_t noteWindows{0U};
  std::uint64_t frames{0U};
  double scoreEnergy{0.0};
  std::vector<float> scorePcm48k;
  std::uint8_t channels{0U};
};

class ProbePlugin final {
public:
  ProbePlugin(const clap_plugin_factory_t* factory, const clap_host_t& host, const char* id)
      : plugin(factory->create_plugin(factory, &host, id)) {
    if (!plugin) throw std::runtime_error{"Cannot create offline probe plugin"};
    try {
      if (!plugin->init(plugin)) throw std::runtime_error{"Cannot initialize offline probe plugin"};
      state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
      render = static_cast<const clap_plugin_render_t*>(plugin->get_extension(plugin, CLAP_EXT_RENDER));
      if (!state || !render) throw std::runtime_error{"Offline probe extensions are missing"};
    } catch (...) {
      plugin->destroy(plugin); plugin=nullptr; throw;
    }
  }
  ~ProbePlugin() {
    stop();
    if (plugin) plugin->destroy(plugin);
  }
  ProbePlugin(const ProbePlugin&) = delete;
  ProbePlugin& operator=(const ProbePlugin&) = delete;
  void load(std::span<const std::byte> bytes) {
    ReadStream input{bytes};
    if (!state->load(plugin, &input.stream)) throw std::runtime_error{"Offline probe state load failed"};
  }
  bool activate(std::uint32_t rate) {
    if (active) return false;
    active = plugin->activate(plugin, rate, 1U, 512U);
    if (!active) return false;
    processing = plugin->start_processing(plugin);
    return processing;
  }
  void stop() {
    if (processing) plugin->stop_processing(plugin);
    if (active) plugin->deactivate(plugin);
    processing = false; active = false;
  }
  const clap_plugin_t* plugin{};
  const clap_plugin_state_t* state{};
  const clap_plugin_render_t* render{};
  bool active{false}, processing{false};
};

bool rejectedFinal(ProbePlugin& probe, std::span<const std::byte> state, std::uint8_t channels) {
  probe.stop(); probe.load(state);
  if (probe.render->set(probe.plugin, CLAP_RENDER_OFFLINE)) return false;
  // A host ignoring the rejection must not activate and render stale Preview.
  if (probe.activate(48000U)) return false;
  Output output{32U, channels};
  for (auto& plane : output.planes) std::fill(plane.begin(), plane.end(), 0.0F);
  clap_process_t process{};
  process.frames_count=32U; process.audio_outputs=&output.buffer; process.audio_outputs_count=1U;
  if (probe.plugin->process(probe.plugin, &process) != CLAP_PROCESS_ERROR) return false;
  // Explicitly returning to realtime remains supported after a rejected bounce.
  if (!probe.render->set(probe.plugin, CLAP_RENDER_REALTIME) || !probe.activate(48000U)) return false;
  probe.stop();
  return true;
}

bool completeScoreBounce(ProbePlugin& probe, const seam::domain::Project& project,
                        std::uint32_t rate, OfflineChecks& evidence) {
  probe.stop();
  const auto fail = [&probe] { probe.stop(); return false; };
  if (!probe.activate(rate)) return fail();
  const auto channels=project.routing().deviceOutputChannels;
  Output output{512U, channels};
  const auto offset=project.tempoMap().secondsAt(project.settings().hostStartOffsetTick);
  std::vector<std::pair<double,double>> noteWindows;
  double end=0.0;
  for (const auto& track : project.vocalTracks()) {
    if (track.muted) continue;
    for (const auto& region : track.regions) {
      for (const auto& note : region.notes) {
        if (noteWindows.size() >= 256U) return fail();
        const auto start=project.tempoMap().secondsAt(region.startTick + note.startTick) + offset;
        const auto finish=project.tempoMap().secondsAt(region.startTick + note.startTick + note.durationTick) + offset;
        noteWindows.emplace_back(start, finish); end=std::max(end, finish);
      }
    }
  }
  if (noteWindows.empty() || end <= 0.0 || end > 30.0) return fail();
  const auto count=static_cast<std::uint64_t>(std::ceil((end + 0.25) * rate));
  if (rate==48000U) {
    evidence.scorePcm48k.clear();
    evidence.scorePcm48k.reserve(static_cast<std::size_t>(count)*channels);
    evidence.channels=channels;
  }
  std::vector<double> noteEnergies(noteWindows.size(), 0.0);
  double total=0.0;
  clap_event_transport_t transport{};
  transport.header={sizeof(transport),0U,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_TRANSPORT,0U};
  transport.flags=CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
  clap_process_t process{};
  process.audio_outputs=&output.buffer; process.audio_outputs_count=1U; process.transport=&transport;
  // No NOTE_ON/MIDI events: every audible sample must come from the score.
  for (std::uint64_t cursor=0U; cursor<count; cursor+=process.frames_count) {
    process.frames_count=static_cast<std::uint32_t>(std::min<std::uint64_t>(512U,count-cursor));
    process.steady_time=static_cast<std::int64_t>(cursor);
    transport.song_pos_seconds=static_cast<clap_sectime>(std::llround(
        static_cast<double>(cursor) / rate * static_cast<double>(CLAP_SECTIME_FACTOR)));
    if (probe.plugin->process(probe.plugin,&process) != CLAP_PROCESS_CONTINUE) return fail();
    for (std::uint32_t frame=0U; frame<process.frames_count; ++frame) {
      double sampleEnergy=0.0;
      for (const auto& plane : output.planes) {
        if (!std::isfinite(plane[frame])) return fail();
        sampleEnergy+=std::abs(static_cast<double>(plane[frame]));
        if (rate==48000U) evidence.scorePcm48k.push_back(plane[frame]);
      }
      total+=sampleEnergy;
      const auto seconds=static_cast<double>(cursor+frame)/rate;
      for (std::size_t index=0U; index<noteWindows.size(); ++index) {
        if (seconds>=noteWindows[index].first && seconds<noteWindows[index].second)
          noteEnergies[index]+=sampleEnergy;
      }
    }
  }
  evidence.frames+=count; evidence.noteWindows+=noteWindows.size(); evidence.scoreEnergy+=total;
  // No seconds history is an explicit error, not a guessed current-BPM bounce.
  transport.flags=CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING;
  transport.tempo=60.0; transport.song_pos_beats=8 * CLAP_BEATTIME_FACTOR;
  process.frames_count=32U;
  evidence.beatsOnlyRejected=probe.plugin->process(probe.plugin,&process)==CLAP_PROCESS_ERROR;
  for (const auto& plane : output.planes)
    evidence.beatsOnlyRejected=evidence.beatsOnlyRejected && energy(std::span{plane.data(),32U})==0.0;
  if (rate == 48000U) {
    constexpr std::size_t maximumEventsPerBlock = 1024U;
    const auto makeMidiEvents = [](std::size_t count) {
      std::vector<clap_event_midi_t> events(count);
      for (auto& event : events) {
        event.header = {static_cast<std::uint32_t>(sizeof(event)), 0U, CLAP_CORE_EVENT_SPACE_ID,
                        CLAP_EVENT_MIDI, 0U};
        event.port_index = 0U;
        event.data[0] = 0xB0U;
        event.data[1] = 1U;
        event.data[2] = 0U;
      }
      return events;
    };
    const auto allSilent = [](const Output& block, std::uint32_t frames) {
      return std::all_of(block.planes.begin(), block.planes.end(),
          [frames](const auto& channel) {
            return energy(std::span{channel.data(), frames}) == 0.0;
          });
    };
    clap_event_transport_t inBlockTransport{};
    inBlockTransport.header = {sizeof(inBlockTransport), 16U,
        CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0U};
    inBlockTransport.flags = CLAP_TRANSPORT_IS_PLAYING |
        CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
    inBlockTransport.song_pos_seconds = static_cast<clap_sectime>(
        100 * CLAP_SECTIME_FACTOR);
    transport.flags = CLAP_TRANSPORT_IS_PLAYING |
        CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
    transport.song_pos_seconds = 0;
    Output limitOutput{32U, channels};
    clap_process_t limitProcess{};
    limitProcess.audio_outputs = &limitOutput.buffer;
    limitProcess.audio_outputs_count = 1U;
    limitProcess.transport = &transport;
    limitProcess.frames_count = 32U;

    // Exactly 1,024 events is accepted; a transport event at the final slot still
    // invalidates a Final bounce at its in-block sample offset and clears the block.
    auto exactMidi = makeMidiEvents(maximumEventsPerBlock - 1U);
    EventList exactList;
    for (const auto& event : exactMidi) exactList.events.push_back(&event.header);
    exactList.events.push_back(&inBlockTransport.header);
    limitProcess.in_events = &exactList.input;
    const auto exactRejected = probe.plugin->process(probe.plugin, &limitProcess) ==
        CLAP_PROCESS_ERROR && allSilent(limitOutput, limitProcess.frames_count);

    probe.stop();
    const bool rebound = probe.render->set(probe.plugin, CLAP_RENDER_OFFLINE) &&
                         probe.activate(48000U);
    bool exactOrdinaryAccepted = false;
    bool overflowRejected = false;
    bool overflowStaleRejected = false;
    if (rebound) {
      auto boundedMidi = makeMidiEvents(maximumEventsPerBlock);
      EventList boundedList;
      for (const auto& event : boundedMidi) boundedList.events.push_back(&event.header);
      limitProcess.in_events = &boundedList.input;
      exactOrdinaryAccepted = probe.plugin->process(probe.plugin, &limitProcess) ==
          CLAP_PROCESS_CONTINUE;

      // The 1,025th event is the seek that the previous prefix-truncation path lost.
      auto overflowingMidi = makeMidiEvents(maximumEventsPerBlock);
      EventList overflowingList;
      for (const auto& event : overflowingMidi)
        overflowingList.events.push_back(&event.header);
      overflowingList.events.push_back(&inBlockTransport.header);
      limitProcess.in_events = &overflowingList.input;
      overflowRejected = probe.plugin->process(probe.plugin, &limitProcess) ==
          CLAP_PROCESS_ERROR && allSilent(limitOutput, limitProcess.frames_count);
      limitProcess.in_events = nullptr;
      probe.plugin->on_main_thread(probe.plugin);
      overflowStaleRejected = probe.plugin->process(probe.plugin, &limitProcess) ==
          CLAP_PROCESS_ERROR && allSilent(limitOutput, limitProcess.frames_count);
    }
    evidence.eventOverflowFailClosed = exactRejected && rebound &&
        exactOrdinaryAccepted && overflowRejected && overflowStaleRejected;
    probe.stop();

    // In realtime, overflow mutes only the affected block. It must still return
    // CONTINUE while the host says playback is active, so a conforming host advances
    // into the next event-free block and the score can recover from its fresh clock.
    const bool finalRebound = probe.render->set(probe.plugin, CLAP_RENDER_OFFLINE);
    const bool realtimeReady = finalRebound &&
        probe.render->set(probe.plugin, CLAP_RENDER_REALTIME) && probe.activate(48000U);
    if (realtimeReady) {
      const auto longest = std::max_element(noteWindows.begin(), noteWindows.end(),
          [](const auto& left, const auto& right) {
            return (left.second - left.first) < (right.second - right.first);
          });
      if (longest != noteWindows.end()) {
        const auto audibleAnchor = longest->first + (longest->second - longest->first) * 0.5;
        auto overflowingRealtimeMidi = makeMidiEvents(maximumEventsPerBlock + 1U);
        EventList realtimeEvents;
        for (const auto& event : overflowingRealtimeMidi)
          realtimeEvents.events.push_back(&event.header);
        clap_event_transport_t realtimeTransport{};
        realtimeTransport.flags = CLAP_TRANSPORT_IS_PLAYING |
            CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
        realtimeTransport.song_pos_seconds = static_cast<clap_sectime>(std::llround(
            audibleAnchor * static_cast<double>(CLAP_SECTIME_FACTOR)));
        Output realtimeOutput{512U, channels};
        clap_process_t realtimeProcess{};
        realtimeProcess.audio_outputs = &realtimeOutput.buffer;
        realtimeProcess.audio_outputs_count = 1U;
        realtimeProcess.transport = &realtimeTransport;
        realtimeProcess.in_events = &realtimeEvents.input;
        realtimeProcess.frames_count = 512U;
        const auto overflowStatus = probe.plugin->process(probe.plugin,
                                                           &realtimeProcess);
        const bool overflowMuted = allSilent(realtimeOutput,
                                              realtimeProcess.frames_count);
        probe.plugin->on_main_thread(probe.plugin);
        if (overflowStatus == CLAP_PROCESS_CONTINUE) {
          // A CLAP host stops polling after SLEEP; model that contract here rather
          // than forcing a recovery process call after a sleeping status.
          realtimeTransport.song_pos_seconds = static_cast<clap_sectime>(std::llround(
              (audibleAnchor + 512.0 / 48000.0) *
              static_cast<double>(CLAP_SECTIME_FACTOR)));
          realtimeProcess.in_events = nullptr;
          const auto recoveryStatus = probe.plugin->process(probe.plugin,
                                                             &realtimeProcess);
          const auto recoveryEnergy = energy(std::span{
              realtimeOutput.planes.front().data(), realtimeProcess.frames_count});
          evidence.realtimeOverflowKeepsAdvancing = overflowMuted &&
              (recoveryStatus == CLAP_PROCESS_CONTINUE ||
               recoveryStatus == CLAP_PROCESS_SLEEP) && recoveryEnergy > 0.0;
        }
      }
    }
    probe.stop();
  }
  probe.stop();
  return total>0.01 && std::all_of(noteEnergies.begin(),noteEnergies.end(),[](double value){return value>0.01;});
}

bool followHostOffsetBounce(const clap_plugin_factory_t* factory,
                            const clap_host_t& host, const char* id,
                            seam::domain::Project project, OfflineChecks& evidence) {
  evidence.followHostStage = "construct";
  const auto ppq = project.tempoMap().ppq();
  const auto tickAtBeat = [ppq](std::int64_t beats) {
    return seam::time::Tick{beats * static_cast<std::int64_t>(ppq)};
  };
  // The document says 60 BPM; the host starts at 120, changes before the
  // project's two-beat placement, and changes again inside its score.
  if (!project.tempoMap().addOrReplace(seam::time::Tick{0}, 60.0)) return false;
  project.settings().hostStartOffsetTick = tickAtBeat(2);
  project.settings().bounceTimingAuthority = seam::domain::BounceTimingAuthority::FollowHost;
  for (auto& track : project.vocalTracks()) {
    for (auto& region : track.regions) region.startTick += tickAtBeat(1);
  }
  seam::time::TempoMap hostMap{ppq};
  if (!hostMap.addOrReplace(seam::time::Tick{0}, 120.0) ||
      !hostMap.addOrReplace(tickAtBeat(1), 60.0) ||
      !hostMap.addOrReplace(tickAtBeat(3), 240.0)) return false;
  std::int64_t scoreEndTicks = 0;
  double firstNoteSeconds = 1e9;
  for (const auto& track : project.vocalTracks()) {
    if (track.muted) continue;
    for (const auto& region : track.regions) {
      scoreEndTicks = std::max(scoreEndTicks,
          (region.startTick + region.durationTick).value());
      for (const auto& note : region.notes) {
        firstNoteSeconds = std::min(firstNoteSeconds,
            hostMap.secondsAt(project.settings().hostStartOffsetTick +
                              region.startTick + note.startTick));
      }
    }
  }
  if (scoreEndTicks <= 0 || firstNoteSeconds >= 1e9) return false;
  const auto scoreEndBeats = static_cast<double>(scoreEndTicks) / ppq;
  const auto lastHostBeat = static_cast<std::int64_t>(std::ceil(2.0 + scoreEndBeats));
  if (lastHostBeat > 64) return false;

  ProbePlugin probe{factory, host, id};
  evidence.followHostStage = "load";
  probe.load(projectState(project));
  evidence.followHostStage = "realtime-activate";
  if (!probe.activate(48000U)) return false;
  const auto channels = project.routing().deviceOutputChannels;
  Output output{512U, channels};
  clap_event_transport_t transport{};
  transport.header = {sizeof(transport), 0U, CLAP_CORE_EVENT_SPACE_ID,
                      CLAP_EVENT_TRANSPORT, 0U};
  transport.flags = CLAP_TRANSPORT_IS_PLAYING |
                    CLAP_TRANSPORT_HAS_SECONDS_TIMELINE |
                    CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                    CLAP_TRANSPORT_HAS_TEMPO;
  clap_process_t process{};
  process.audio_outputs = &output.buffer;
  process.audio_outputs_count = 1U;
  process.transport = &transport;
  process.frames_count = 32U;
  for (std::int64_t beat = 0; beat <= lastHostBeat; ++beat) {
    evidence.followHostStage = "capture-beat-" + std::to_string(beat);
    const auto tick = tickAtBeat(beat);
    transport.song_pos_beats = beat * CLAP_BEATTIME_FACTOR;
    transport.song_pos_seconds = static_cast<clap_sectime>(std::llround(
        hostMap.secondsAt(tick) * static_cast<double>(CLAP_SECTIME_FACTOR)));
    transport.tempo = hostMap.bpmAt(tick);
    const auto captureStatus = probe.plugin->process(probe.plugin, &process);
    if (captureStatus != CLAP_PROCESS_CONTINUE && captureStatus != CLAP_PROCESS_SLEEP)
      return false;
    probe.plugin->on_main_thread(probe.plugin);
  }
  probe.stop();
  evidence.followHostStage = "offline-set";
  if (!probe.render->set(probe.plugin, CLAP_RENDER_OFFLINE) ||
      !probe.activate(48000U)) return false;

  double earlyEnergy = 0.0;
  double onsetEnergy = 0.0;
  const auto endSeconds = hostMap.secondsAt(tickAtBeat(lastHostBeat)) + 0.25;
  const auto totalFrames = static_cast<std::uint64_t>(std::ceil(endSeconds * 48000.0));
  transport.flags = CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
  for (std::uint64_t cursor = 0U; cursor < totalFrames; cursor += process.frames_count) {
    evidence.followHostStage = "bounce-frame-" + std::to_string(cursor);
    process.frames_count = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(512U, totalFrames - cursor));
    transport.song_pos_seconds = static_cast<clap_sectime>(std::llround(
        static_cast<double>(cursor) / 48000.0 * CLAP_SECTIME_FACTOR));
    if (probe.plugin->process(probe.plugin, &process) != CLAP_PROCESS_CONTINUE) return false;
    for (std::uint32_t frame = 0U; frame < process.frames_count; ++frame) {
      const auto seconds = static_cast<double>(cursor + frame) / 48000.0;
      for (const auto& plane : output.planes) {
        const auto sample = std::abs(static_cast<double>(plane[frame]));
        if (!std::isfinite(sample)) return false;
        if (seconds >= firstNoteSeconds - 0.45 &&
            seconds < firstNoteSeconds - 0.25) earlyEnergy += sample;
        if (seconds >= firstNoteSeconds &&
            seconds < firstNoteSeconds + 0.20) onsetEnergy += sample;
      }
    }
  }
  evidence.followHostExpectedOnsetSeconds = firstNoteSeconds;
  evidence.followHostEarlyEnergy = earlyEnergy;
  evidence.followHostOnsetEnergy = onsetEnergy;
  evidence.followHostOffsetBounce = earlyEnergy < 0.001 && onsetEnergy > 0.01;
  evidence.followHostStage = "mid-bounce-transport-event";

  // A sample-offset transport revision during the bounce must not let this block escape
  // under the previously frozen authority. The event is deliberately inside the block.
  clap_event_transport_t inBlockTransport = transport;
  inBlockTransport.header = {sizeof(inBlockTransport), 16U,
                             CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0U};
  inBlockTransport.flags = CLAP_TRANSPORT_IS_PLAYING |
      CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
      CLAP_TRANSPORT_HAS_TEMPO;
  inBlockTransport.song_pos_beats = 3 * CLAP_BEATTIME_FACTOR;
  inBlockTransport.song_pos_seconds = static_cast<clap_sectime>(std::llround(
      hostMap.secondsAt(tickAtBeat(3)) * CLAP_SECTIME_FACTOR));
  inBlockTransport.tempo = 180.0;
  process.frames_count = 32U;
  EventList transportEvents;
  transportEvents.events = {&inBlockTransport.header};
  process.in_events = &transportEvents.input;
  evidence.followHostTransportEventRejected =
      probe.plugin->process(probe.plugin, &process) == CLAP_PROCESS_ERROR;
  for (const auto& plane : output.planes) {
    evidence.followHostTransportEventRejected =
        evidence.followHostTransportEventRejected &&
        energy(std::span{plane.data(), process.frames_count}) == 0.0;
  }
  process.in_events = nullptr;
  probe.plugin->on_main_thread(probe.plugin);
  evidence.followHostStage = "stale-report-after-in-block-event";
  process.frames_count = 32U;
  evidence.followHostStaleRejected =
      probe.plugin->process(probe.plugin, &process) == CLAP_PROCESS_ERROR;
  for (const auto& plane : output.planes) {
    evidence.followHostStaleRejected = evidence.followHostStaleRejected &&
        energy(std::span{plane.data(), 32U}) == 0.0;
  }
  evidence.followHostStage = "complete";
  return evidence.followHostOffsetBounce && evidence.followHostStaleRejected;
}

OfflineChecks probeOffline(const clap_plugin_factory_t* factory, const clap_host_t& host,
                          const char* id, std::span<const std::byte> original, bool missingOnly) {
  OfflineChecks result;
  auto project=savedProject(original);
  const auto channels=project.routing().deviceOutputChannels;
  // Fresh plugin/state load followed immediately by offline mode: no GUI pump,
  // polling for Preview, sleep, warm-up notes, or linked runtime shortcuts.
  ProbePlugin probe{factory,host,id};
  if (!missingOnly) {
    // Force a leading rest so SLEEP cannot hide a subsequent vocal entrance.
    for (auto& track : project.vocalTracks())
      for (auto& region : track.regions) region.startTick += seam::time::Tick{960};
    const auto preparedState=projectState(project);
    probe.load(preparedState);
    if (probe.render->set(probe.plugin,CLAP_RENDER_OFFLINE)) {
      result.completeScore=completeScoreBounce(probe,project,44100U,result);
      // Same offline intent, new activation rate: must prepare new Final audio.
      result.rateChange=completeScoreBounce(probe,project,48000U,result);
    }
    static_cast<void>(followHostOffsetBounce(factory, host, id, project, result));
    auto failed=project;
    bool hasOverride=false;
    for (auto& track : failed.vocalTracks()) {
      for (auto& region : track.regions) {
        if (!region.unitSelectionOverrides.empty()) {
          region.unitSelectionOverrides.front().unitId="offline-probe.required-unit-does-not-exist";
          hasOverride=true;
        }
      }
    }
    if (hasOverride) result.failedFinalRejected=rejectedFinal(probe,projectState(failed),channels);
  }
  for (auto& track : project.vocalTracks()) {
    track.proceduralRecipe.reset();
    track.voicebank=seam::domain::VoicebankReference{.id="offline-probe.required-bank-does-not-exist",
        .version="1.0.0",.contentHash=std::string(64U,'f')};
  }
  result.missingRejected=rejectedFinal(probe,projectState(project),channels);
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path pluginPath;
  std::filesystem::path screenshotPath;
  std::filesystem::path summaryPath;
  std::filesystem::path audioPath;
  std::filesystem::path targetRuntimeFixtureRoot;
  bool expectMissingBank = false;
  bool offlineOnly = false;
  bool stateDirtyProbe = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--plugin" && index + 1 < argc) {
      pluginPath = argv[++index];
    } else if (argument == "--screenshot" && index + 1 < argc) {
      screenshotPath = argv[++index];
    } else if (argument == "--summary" && index + 1 < argc) {
      summaryPath = argv[++index];
    } else if (argument == "--audio" && index + 1 < argc) {
      audioPath = argv[++index];
    } else if (argument == "--target-runtime-fixture-root" &&
               index + 1 < argc) {
      targetRuntimeFixtureRoot = argv[++index];
    } else if (argument == "--expect-missing-bank") {
      expectMissingBank = true;
    } else if (argument == "--offline-only") {
      offlineOnly = true;
    } else if (argument == "--state-dirty-probe") {
      stateDirtyProbe = true;
    }
  }
  if (pluginPath.empty() ||
      (expectMissingBank && !targetRuntimeFixtureRoot.empty())) {
    std::cerr << "Usage: seam_clap_editor_host --plugin FILE.clap "
                 "[--screenshot FILE.ppm] [--summary FILE.json] "
                 "[--audio FILE.wav] "
                 "[--offline-only] "
                 "[--state-dirty-probe] "
                 "[--target-runtime-fixture-root DIR | --expect-missing-bank]\n";
    return 2;
  }

  if (!targetRuntimeFixtureRoot.empty()) {
#if defined(_WIN32)
    if (_putenv_s("SEAM_TARGET_RUNTIME_FIXTURE_ROOT",
                  targetRuntimeFixtureRoot.string().c_str()) != 0) {
      return 1;
    }
#else
    if (setenv("SEAM_TARGET_RUNTIME_FIXTURE_ROOT",
               targetRuntimeFixtureRoot.string().c_str(), 1) != 0) {
      return 1;
    }
#endif
  }
  if (expectMissingBank) {
#if defined(_WIN32)
    if (_putenv_s("SEAM_TARGET_RUNTIME_FIXTURE_ROOT", "") != 0 ||
        _putenv_s("SEAM_VOICEBANK_PATH", "") != 0) {
      return 1;
    }
#else
    if (unsetenv("SEAM_TARGET_RUNTIME_FIXTURE_ROOT") != 0 ||
        unsetenv("SEAM_VOICEBANK_PATH") != 0) {
      return 1;
    }
#endif
  }

  std::optional<seam::voicebank::VoicebankCandidate> fixtureCandidate;
  if (!targetRuntimeFixtureRoot.empty()) {
    seam::voicebank::VoicebankCatalog catalog;
    const auto roots = std::vector<seam::voicebank::VoicebankSearchRoot>{
        seam::voicebank::VoicebankSearchRoot{
            .path = targetRuntimeFixtureRoot,
            .kind = seam::voicebank::VoicebankRootKind::Development,
        }};
    const auto scanned = catalog.scan(roots);
    if (!scanned || scanned.value().empty()) return 1;
    fixtureCandidate = scanned.value().front();
  }

  Module module;
  if (!module.open(pluginPath)) {
    std::cerr << "Unable to load Phase 11 CLAP module\n";
    return 1;
  }
  const auto* entry = static_cast<const clap_plugin_entry_t*>(
      module.symbol("clap_entry"));
  if (entry == nullptr || !entry->init(pluginPath.string().c_str())) return 1;
  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (factory == nullptr || factory->get_plugin_count(factory) != 1U) return 1;
  const auto* descriptor = factory->get_plugin_descriptor(factory, 0U);
  if (descriptor == nullptr ||
      std::strcmp(descriptor->id, "com.project-seam.editor") != 0 ||
      descriptor->version == nullptr ||
      std::strcmp(descriptor->version,
                  seam::build::kApplicationVersion.data()) != 0) {
    std::cerr << "CLAP descriptor identity does not match this host build\n";
    return 1;
  }

  HostContext context;
  const clap_host_t host{
      CLAP_VERSION, &context, "SEAM Phase 11 Host", "Project SEAM", "",
      seam::build::kApplicationVersion.data(), &hostGetExtension, &hostRequestRestart,
      &hostRequestProcess, &hostRequestCallback};
  const auto* plugin = factory->create_plugin(factory, &host, descriptor->id);
  if (plugin == nullptr || !plugin->init(plugin)) return 1;

  const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
      plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
  const auto* audioConfigs =
      static_cast<const clap_plugin_audio_ports_config_t*>(
          plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS_CONFIG));
  const auto* audioConfigInfo =
      static_cast<const clap_plugin_audio_ports_config_info_t*>(
          plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO));
  const auto* render = static_cast<const clap_plugin_render_t*>(
      plugin->get_extension(plugin, CLAP_EXT_RENDER));
  const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
      plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
  const auto* state = static_cast<const clap_plugin_state_t*>(
      plugin->get_extension(plugin, CLAP_EXT_STATE));
  const auto* gui = static_cast<const clap_plugin_gui_t*>(
      plugin->get_extension(plugin, CLAP_EXT_GUI));
  const auto* timer = static_cast<const clap_plugin_timer_support_t*>(
      plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT));
  if (audioPorts == nullptr || audioConfigs == nullptr ||
      audioConfigInfo == nullptr || render == nullptr || notePorts == nullptr ||
      state == nullptr || gui == nullptr || timer == nullptr ||
      audioPorts->count(plugin, true) != 0U ||
      audioPorts->count(plugin, false) != 1U ||
      notePorts->count(plugin, true) != 1U) {
    return 1;
  }

  bool guiCreated = false;
  bool guiVisible = false;
  bool screenshotWritten = false;
  seam::clap_host::HostWindow hostWindow;
  const auto hostWidth = stateDirtyProbe ? 1440U : 1100U;
  if (!offlineOnly && hostWindow.create(hostWidth, 720U) &&
      gui->is_api_supported(plugin, hostWindow.api(), false) &&
      gui->create(plugin, hostWindow.api(), false)) {
    guiCreated = true;
    clap_window_t parentWindow{};
    guiVisible = hostWindow.attach(parentWindow) &&
                 gui->set_parent(plugin, &parentWindow) &&
                 gui->set_size(plugin, hostWidth, 720U) &&
                 gui->show(plugin);
    for (int frame = 0; frame < 45; ++frame) {
      if (context.timerId != CLAP_INVALID_ID) {
        timer->on_timer(plugin, context.timerId);
      }
      if (!hostWindow.pump()) break;
      std::this_thread::sleep_for(std::chrono::milliseconds{8});
    }
    if (guiVisible && !screenshotPath.empty()) {
      screenshotWritten = hostWindow.capture(screenshotPath);
    }
  }

  if (stateDirtyProbe) {
    bool passed = false;
#if defined(__APPLE__)
    if (guiVisible && context.timerId != CLAP_INVALID_ID) {
      const auto before = context.stateDirtyCalls.load(std::memory_order_relaxed);
      const auto callbacksBefore = context.callbackRequests.load(std::memory_order_relaxed);
      const auto edited = hostWindow.setAccessibilityValue("toolbar.tempo", "137");
      const auto callbackRequested =
          context.callbackRequests.load(std::memory_order_relaxed) > callbacksBefore;
      plugin->on_main_thread(plugin);
      const auto marked = context.stateDirtyCalls.load(std::memory_order_relaxed) == before + 1U;
      timer->on_timer(plugin, context.timerId);
      const auto exactlyOnce =
          context.stateDirtyCalls.load(std::memory_order_relaxed) == before + 1U;
      const auto editedAgain = hostWindow.setAccessibilityValue("toolbar.tempo", "138");
      timer->on_timer(plugin, context.timerId);
      const auto timerMarked =
          context.stateDirtyCalls.load(std::memory_order_relaxed) == before + 2U;
      const auto bounceChanged = hostWindow.activateAccessibility("toolbar.bounce");
      plugin->on_main_thread(plugin);
      const auto bounceMarked =
          context.stateDirtyCalls.load(std::memory_order_relaxed) == before + 3U;
      WriteStream editedState;
      const auto savedEdit = state->save(plugin, &editedState.stream);
      bool reopened = false;
      if (savedEdit) {
        const auto* duplicate = factory->create_plugin(factory, &host, descriptor->id);
        if (duplicate != nullptr && duplicate->init(duplicate)) {
          const auto* duplicateState = static_cast<const clap_plugin_state_t*>(
              duplicate->get_extension(duplicate, CLAP_EXT_STATE));
          ReadStream incoming{editedState.bytes};
          reopened = duplicateState != nullptr &&
              duplicateState->load(duplicate, &incoming.stream);
          if (reopened) {
            WriteStream savedAgain;
            reopened = duplicateState->save(duplicate, &savedAgain.stream) &&
                savedAgain.bytes == editedState.bytes;
            duplicate->on_main_thread(duplicate);
            reopened = reopened &&
                context.stateDirtyCalls.load(std::memory_order_relaxed) == before + 3U;
          }
          duplicate->destroy(duplicate);
        } else if (duplicate != nullptr) {
          duplicate->destroy(duplicate);
        }
      }
      bool tempoRetained = false;
      bool bounceRetained = false;
      if (savedEdit) {
        try {
          const auto project = savedProject(editedState.bytes);
          tempoRetained = std::abs(project.tempoMap().bpmAt(
              seam::time::Tick{0}) - 138.0) < 1.0e-9;
          bounceRetained = project.settings().bounceTimingAuthority ==
              seam::domain::BounceTimingAuthority::FollowHost;
        } catch (const std::exception&) {}
      }
      passed = edited && callbackRequested && marked && exactlyOnce &&
          editedAgain && timerMarked && bounceChanged && bounceMarked &&
          tempoRetained && bounceRetained && reopened;
      std::cout << "state-dirty-probe: edited=" << edited
                << " callback=" << callbackRequested
                << " marked=" << marked << " once=" << exactlyOnce
                << " timer=" << timerMarked
                << " bounce=" << bounceMarked << "/" << bounceRetained
                << " tempo=" << tempoRetained << " reopened=" << reopened << '\n';
    }
#endif
    if (guiCreated) {
      if (guiVisible) static_cast<void>(gui->hide(plugin));
      gui->destroy(plugin);
    }
    hostWindow.destroy();
    plugin->destroy(plugin);
    entry->deinit();
    return passed ? 0 : 1;
  }

  WriteStream saved;
  if (!state->save(plugin, &saved.stream) || saved.bytes.empty()) return 1;

  OfflineChecks offlineChecks;
  try {
    offlineChecks=probeOffline(factory,host,descriptor->id,saved.bytes,expectMissingBank);
  } catch (const std::exception& error) {
    std::cerr << "Offline CLAP probe failed: " << error.what() << '\n';
  }
  const auto offlineChecksPassed=offlineChecks.missingRejected && (expectMissingBank ||
      (offlineChecks.completeScore && offlineChecks.rateChange &&
       offlineChecks.failedFinalRejected && offlineChecks.beatsOnlyRejected &&
       offlineChecks.eventOverflowFailClosed &&
       offlineChecks.realtimeOverflowKeepsAdvancing &&
       offlineChecks.followHostOffsetBounce && offlineChecks.followHostStaleRejected &&
       offlineChecks.followHostTransportEventRejected));
  if (offlineOnly) {
    bool audioWritten=audioPath.empty();
    if (!audioPath.empty() && !offlineChecks.scorePcm48k.empty()) {
      audioWritten=static_cast<bool>(seam::voicebank::writeWav(audioPath,
          {.sampleRate=48000U,.channels=offlineChecks.channels,.sampleFormat=seam::voicebank::WavSampleFormat::Float32},
          offlineChecks.scorePcm48k));
    }
    using Json=seam::formats::JsonValue;
    const Json report{Json::Object{
        {"result",offlineChecksPassed && audioWritten ? "PASS" : "FAIL"},
        {"evidenceScope","engineering"},{"releaseEligible",false},
        {"executionPath","loaded-clap-cold-score-bounce-v1"},
        {"timingAuthority","fixed-audio-seconds-timeline"},
        {"followHostQualified",false},{"noteEventsSent",std::int64_t{0}},
        {"completeScoreBounce",offlineChecks.completeScore},{"rateChangeReprepared",offlineChecks.rateChange},
        {"missingFinalRejected",offlineChecks.missingRejected},{"failedFinalRejected",offlineChecks.failedFinalRejected},
        {"beatsOnlyRejected",offlineChecks.beatsOnlyRejected},{"expectedMissingBank",expectMissingBank},
        {"eventOverflowFailClosed",offlineChecks.eventOverflowFailClosed},
        {"realtimeOverflowKeepsAdvancing",offlineChecks.realtimeOverflowKeepsAdvancing},
        {"followHostOffsetBounce",offlineChecks.followHostOffsetBounce},
        {"followHostStaleRejected",offlineChecks.followHostStaleRejected},
        {"followHostTransportEventRejected",offlineChecks.followHostTransportEventRejected},
        {"followHostExpectedOnsetSeconds",offlineChecks.followHostExpectedOnsetSeconds},
        {"followHostEarlyEnergy",offlineChecks.followHostEarlyEnergy},
        {"followHostOnsetEnergy",offlineChecks.followHostOnsetEnergy},
        {"followHostStage",offlineChecks.followHostStage},
        {"noteWindows",static_cast<std::int64_t>(offlineChecks.noteWindows)},
        {"capturedFrames",static_cast<std::int64_t>(offlineChecks.frames)},
        {"scoreEnergy",offlineChecks.scoreEnergy},{"audioWritten",audioWritten}}};
    const auto text=seam::formats::stringifyJson(report,true)+"\n";
    std::cout << text;
    bool summaryWritten=true;
    if (!summaryPath.empty()) summaryWritten=static_cast<bool>(seam::core::durableAtomicWriteText(summaryPath,text));
    plugin->destroy(plugin); entry->deinit();
    return offlineChecksPassed && audioWritten && summaryWritten ? 0 : 1;
  }

  ReadStream inactiveGuiLoad{saved.bytes};
  const auto inactiveGuiLoadAccepted =
      state->load(plugin, &inactiveGuiLoad.stream);
  if (!inactiveGuiLoadAccepted) return 1;
  for (int frame = 0; frame < 12; ++frame) {
    if (context.timerId != CLAP_INVALID_ID) {
      timer->on_timer(plugin, context.timerId);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{4});
  }

  if (audioConfigs->count(plugin) != 8U ||
      !audioConfigs->select(plugin, 4U)) return 1;
  clap_audio_port_info_t selectedPort{};
  if (!audioConfigInfo->get(plugin, 4U, 0U, false, &selectedPort) ||
      selectedPort.channel_count != 4U ||
      audioConfigInfo->current_config(plugin) != 4U ||
      !audioPorts->get(plugin, 0U, false, &selectedPort) ||
      selectedPort.channel_count != 4U) return 1;
  const auto offlineRenderAccepted = render->set(plugin, CLAP_RENDER_OFFLINE);
  const auto offlineModeExpectation=expectMissingBank ? !offlineRenderAccepted : offlineRenderAccepted;
  // Live-note/GUI evidence is a realtime workload; score-bounce evidence above
  // has its own cold state and never borrows live-note energy as proof.
  if (!render->set(plugin,CLAP_RENDER_REALTIME)) return 1;

  constexpr std::uint32_t frames = 512U;
  constexpr std::uint32_t outputChannels = 4U;
  if (!plugin->activate(plugin, 48000.0, 32U, frames) ||
      !plugin->start_processing(plugin)) {
    return 1;
  }

  clap_event_note_t noteOn{};
  noteOn.header = clap_event_header_t{
      .size = sizeof(noteOn), .time = 0U,
      .space_id = CLAP_CORE_EVENT_SPACE_ID,
      .type = CLAP_EVENT_NOTE_ON, .flags = 0U};
  noteOn.note_id = 1;
  noteOn.port_index = 0;
  noteOn.channel = 0;
  noteOn.key = 67;
  noteOn.velocity = 0.9;
  clap_event_note_t noteOff = noteOn;
  noteOff.header.type = CLAP_EVENT_NOTE_OFF;
  noteOff.velocity = 0.0;
  Output output{frames, outputChannels};
  clap_event_transport_t transport{};
  transport.header = clap_event_header_t{
      .size = sizeof(transport), .time = 0U,
      .space_id = CLAP_CORE_EVENT_SPACE_ID,
      .type = CLAP_EVENT_TRANSPORT, .flags = 0U};
  transport.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE |
                    CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                    CLAP_TRANSPORT_HAS_TEMPO |
                    CLAP_TRANSPORT_HAS_TIME_SIGNATURE |
                    CLAP_TRANSPORT_IS_PLAYING |
                    CLAP_TRANSPORT_IS_LOOP_ACTIVE;
  transport.tempo = 154.0;
  transport.tsig_num = 4U;
  transport.tsig_denom = 4U;
  transport.loop_start_seconds = 0;
  transport.loop_end_seconds = static_cast<clap_sectime>(
      std::llround(1.0 * static_cast<double>(CLAP_SECTIME_FACTOR)));
  transport.loop_start_beats = 0;
  transport.loop_end_beats = static_cast<clap_beattime>(
      std::llround((154.0 / 60.0) * static_cast<double>(CLAP_BEATTIME_FACTOR)));
  clap_process_t process{};
  process.frames_count = frames;
  process.audio_outputs = &output.buffer;
  process.audio_outputs_count = 1U;
  process.transport = &transport;
  std::vector<float> captured;
  captured.reserve(static_cast<std::size_t>(frames) * 48U * outputChannels);
  double liveEnergy = 0.0;
  bool processOk = true;
  for (std::uint32_t block = 0U; block < 48U; ++block) {
    EventList events;
    if (block == 0U) events.events = {&noteOn.header};
    if (block == 30U) events.events = {&noteOff.header};
    process.in_events = &events.input;
    const auto seconds = static_cast<double>(block * frames) / 48000.0;
    transport.song_pos_seconds = static_cast<clap_sectime>(
        std::llround(seconds * static_cast<double>(CLAP_SECTIME_FACTOR)));
    transport.song_pos_beats = static_cast<clap_beattime>(
        std::llround(seconds * 154.0 / 60.0 *
                     static_cast<double>(CLAP_BEATTIME_FACTOR)));
    const auto processResult = plugin->process(plugin, &process);
    processOk = processOk && processResult != CLAP_PROCESS_ERROR;
    liveEnergy += energy(output.planes[0]);
    for (std::uint32_t frame = 0U; frame < frames; ++frame) {
      for (std::uint32_t channel = 0U; channel < outputChannels; ++channel) {
        captured.push_back(output.planes[channel][frame]);
      }
    }
  }
  bool audioWritten = audioPath.empty();
  if (!audioPath.empty()) {
    std::filesystem::create_directories(audioPath.parent_path());
    audioWritten = static_cast<bool>(seam::voicebank::writePcm16Wav(
        audioPath, 48000U, static_cast<std::uint16_t>(outputChannels), captured));
  }
  const auto missingBankSilence = liveEnergy <= 1.0e-12;
  const auto livePass = processOk && audioWritten &&
                        (expectMissingBank ? missingBankSilence
                                           : liveEnergy > 0.01);

  ReadStream activeLoad{saved.bytes};
  const auto activeLoadRejected = !state->load(plugin, &activeLoad.stream) &&
                                  context.restartRequests.load(
                                      std::memory_order_relaxed) == 1U;
  const auto stateSha256 = seam::core::sha256Hex(
      std::span<const std::byte>{saved.bytes});
  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);

  const auto* restored = factory->create_plugin(factory, &host, descriptor->id);
  if (restored == nullptr || !restored->init(restored)) return 1;
  const auto* restoredState = static_cast<const clap_plugin_state_t*>(
      restored->get_extension(restored, CLAP_EXT_STATE));
  ReadStream stateRead{saved.bytes};
  const auto restoredStateLoadAccepted =
      restoredState != nullptr && restoredState->load(restored, &stateRead.stream);
  WriteStream restoredSaved;
  const auto restoredStateSaved =
      restoredState != nullptr && restoredState->save(restored, &restoredSaved.stream);
  const auto restoredStateSha256 = seam::core::sha256Hex(
      std::span<const std::byte>{restoredSaved.bytes});
  const auto stateRoundTrip = restoredStateLoadAccepted && restoredStateSaved &&
                              restoredSaved.bytes == saved.bytes;
  restored->destroy(restored);

  if (guiCreated) {
    if (guiVisible) static_cast<void>(gui->hide(plugin));
    gui->destroy(plugin);
  }
  hostWindow.destroy();
  plugin->destroy(plugin);
  entry->deinit();

  const auto processRequestPass =
      expectMissingBank ||
      context.processRequests.load(std::memory_order_relaxed) > 0U;
  const auto passed = guiCreated && guiVisible && livePass &&
                      offlineModeExpectation && offlineChecksPassed && selectedPort.channel_count == 4U &&
                      inactiveGuiLoadAccepted && activeLoadRejected &&
                      stateRoundTrip && processRequestPass &&
                      (screenshotPath.empty() || screenshotWritten);
  if (!summaryPath.empty()) {
    std::filesystem::create_directories(summaryPath.parent_path());
    std::ofstream summary(summaryPath, std::ios::binary | std::ios::trunc);
    summary << "{\n"
            << "  \"pluginId\": \"com.project-seam.editor\",\n"
            << "  \"hostApi\": \"" << hostWindow.api() << "\",\n"
            << "  \"fixtureId\": \""
            << (fixtureCandidate.has_value()
                    ? fixtureCandidate->manifest.id
                    : std::string{})
            << "\",\n"
            << "  \"fixtureVersion\": \""
            << (fixtureCandidate.has_value()
                    ? fixtureCandidate->manifest.version
                    : std::string{})
            << "\",\n"
            << "  \"fixtureContentHash\": \""
            << (fixtureCandidate.has_value() ? fixtureCandidate->contentHash
                                              : std::string{})
            << "\",\n"
            << "  \"guiCreated\": " << (guiCreated ? "true" : "false") << ",\n"
            << "  \"guiVisible\": " << (guiVisible ? "true" : "false") << ",\n"
            << "  \"screenshotWritten\": "
            << (screenshotWritten ? "true" : "false") << ",\n"
            << "  \"noteInputEnergy\": " << liveEnergy << ",\n"
            << "  \"capturedFrames\": " << (captured.size() / outputChannels) << ",\n"
            << "  \"outputChannels\": " << outputChannels << ",\n"
            << "  \"audioPortRescans\": "
            << context.audioPortRescans.load(std::memory_order_relaxed) << ",\n"
            << "  \"audioConfigRescans\": "
            << context.audioConfigRescans.load(std::memory_order_relaxed) << ",\n"
            << "  \"offlineRenderAccepted\": "
            << (offlineRenderAccepted ? "true" : "false") << ",\n"
            << "  \"completeScoreBounce\": " << (offlineChecks.completeScore ? "true" : "false") << ",\n"
            << "  \"rateChangeReprepared\": " << (offlineChecks.rateChange ? "true" : "false") << ",\n"
            << "  \"missingFinalRejected\": " << (offlineChecks.missingRejected ? "true" : "false") << ",\n"
            << "  \"failedFinalRejected\": " << (offlineChecks.failedFinalRejected ? "true" : "false") << ",\n"
            << "  \"beatsOnlyRejected\": " << (offlineChecks.beatsOnlyRejected ? "true" : "false") << ",\n"
            << "  \"offlineScoreFrames\": " << offlineChecks.frames << ",\n"
            << "  \"offlineScoreNoteWindows\": " << offlineChecks.noteWindows << ",\n"
            << "  \"offlineScoreEnergy\": " << offlineChecks.scoreEnergy << ",\n"
            << "  \"audioWritten\": " << (audioWritten ? "true" : "false") << ",\n"
            << "  \"expectedMissingBank\": "
            << (expectMissingBank ? "true" : "false") << ",\n"
            << "  \"missingBankSilence\": "
            << (missingBankSilence ? "true" : "false") << ",\n"
            << "  \"activeLoadRejected\": "
            << (activeLoadRejected ? "true" : "false") << ",\n"
            << "  \"inactiveGuiLoadAccepted\": "
            << (inactiveGuiLoadAccepted ? "true" : "false") << ",\n"
            << "  \"stateRoundTrip\": "
            << (stateRoundTrip ? "true" : "false") << ",\n"
            << "  \"stateBytes\": " << saved.bytes.size() << ",\n"
            << "  \"restoredStateBytes\": " << restoredSaved.bytes.size()
            << ",\n"
            << "  \"stateSha256\": \"" << stateSha256 << "\",\n"
            << "  \"restoredStateSha256\": \"" << restoredStateSha256
            << "\",\n"
            << "  \"stateBytesEqual\": "
            << (restoredSaved.bytes == saved.bytes ? "true" : "false")
            << ",\n"
            << "  \"restartRequests\": "
            << context.restartRequests.load(std::memory_order_relaxed) << ",\n"
            << "  \"processRequests\": "
            << context.processRequests.load(std::memory_order_relaxed) << ",\n"
            << "  \"result\": \"" << (passed ? "PASS" : "FAIL") << "\"\n"
            << "}\n";
  }
  std::cout << "Phase 11 CLAP editor host: "
            << (passed ? "PASS" : "FAIL")
            << ", live energy " << liveEnergy << '\n';
  return passed ? 0 : 1;
}
