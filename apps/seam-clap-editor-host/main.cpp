#include <clap/clap.h>

#include "platform_host.hpp"
#include "seam/build/version.hpp"
#include "seam/core/sha256.hpp"
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

const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char* id) {
  if (id == nullptr) return nullptr;
  if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &kTimerHost;
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPortsHost;
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG) == 0) return &kAudioConfigHost;
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
void CLAP_ABI hostRequestCallback(const clap_host_t*) {}

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

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path pluginPath;
  std::filesystem::path screenshotPath;
  std::filesystem::path summaryPath;
  std::filesystem::path audioPath;
  std::filesystem::path targetRuntimeFixtureRoot;
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
    }
  }
  if (pluginPath.empty()) {
    std::cerr << "Usage: seam_clap_editor_host --plugin FILE.clap "
                 "[--screenshot FILE.ppm] [--summary FILE.json] "
                 "[--audio FILE.wav] "
                 "[--target-runtime-fixture-root DIR]\n";
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
  if (hostWindow.create(1100U, 720U) &&
      gui->is_api_supported(plugin, hostWindow.api(), false) &&
      gui->create(plugin, hostWindow.api(), false)) {
    guiCreated = true;
    clap_window_t parentWindow{};
    guiVisible = hostWindow.attach(parentWindow) &&
                 gui->set_parent(plugin, &parentWindow) &&
                 gui->set_size(plugin, 1100U, 720U) &&
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

  WriteStream saved;
  if (!state->save(plugin, &saved.stream) || saved.bytes.empty()) return 1;

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
  const auto livePass = processOk && liveEnergy > 0.01 && audioWritten;

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

  const auto passed = guiCreated && guiVisible && livePass &&
                      offlineRenderAccepted && selectedPort.channel_count == 4U &&
                      inactiveGuiLoadAccepted && activeLoadRejected &&
                      stateRoundTrip &&
                      context.processRequests.load(
                          std::memory_order_relaxed) > 0U &&
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
            << "  \"audioWritten\": " << (audioWritten ? "true" : "false") << ",\n"
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
