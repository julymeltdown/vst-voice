#include "seam/clap/plugin_identity.hpp"
#include "seam/clap/session.hpp"
#include "seam/clap/state_codec.hpp"

#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace {
class Module final {
public:
  bool open(const std::filesystem::path& path) {
#ifdef _WIN32
    handle_ = LoadLibraryW(path.wstring().c_str());
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    return handle_ != nullptr;
  }
  void* symbol(const char* name) const {
#ifdef _WIN32
    return handle_ == nullptr ? nullptr : reinterpret_cast<void*>(GetProcAddress(handle_, name));
#else
    return handle_ == nullptr ? nullptr : dlsym(handle_, name);
#endif
  }
  ~Module() {
#ifdef _WIN32
    if (handle_ != nullptr) FreeLibrary(handle_);
#else
    if (handle_ != nullptr) dlclose(handle_);
#endif
  }
private:
#ifdef _WIN32
  HMODULE handle_{nullptr};
#else
  void* handle_{nullptr};
#endif
};

struct HostContext final { std::uint32_t restartRequests{0U}; };
const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI hostRequestRestart(const clap_host_t* host) {
  ++static_cast<HostContext*>(host->host_data)->restartRequests;
}
void CLAP_ABI hostRequestProcess(const clap_host_t*) {}
void CLAP_ABI hostRequestCallback(const clap_host_t*) {}

struct ReadStream final {
  std::span<const std::byte> bytes;
  std::size_t offset{0U};
  std::size_t maximumChunk{37U};
  clap_istream_t stream{};
  explicit ReadStream(std::span<const std::byte> value) : bytes(value) {
    stream.ctx = this;
    stream.read = [](const clap_istream_t* base, void* destination,
                     std::uint64_t requested) -> std::int64_t {
      auto& self = *static_cast<ReadStream*>(base->ctx);
      if (self.offset >= self.bytes.size()) return 0;
      const auto count = std::min<std::size_t>(
          {static_cast<std::size_t>(requested), self.maximumChunk,
           self.bytes.size() - self.offset});
      std::memcpy(destination, self.bytes.data() + self.offset, count);
      self.offset += count;
      return static_cast<std::int64_t>(count);
    };
  }
};
struct WriteStream final {
  std::vector<std::byte> bytes;
  std::size_t maximumChunk{29U};
  clap_ostream_t stream{};
  WriteStream() {
    stream.ctx = this;
    stream.write = [](const clap_ostream_t* base, const void* source,
                      std::uint64_t requested) -> std::int64_t {
      auto& self = *static_cast<WriteStream*>(base->ctx);
      const auto count = std::min<std::size_t>(static_cast<std::size_t>(requested),
                                               self.maximumChunk);
      const auto* first = static_cast<const std::byte*>(source);
      self.bytes.insert(self.bytes.end(), first, first + count);
      return static_cast<std::int64_t>(count);
    };
  }
};

struct EventList final {
  std::vector<const clap_event_header_t*> events;
  clap_input_events_t input{};
  EventList() {
    input.ctx = this;
    input.size = [](const clap_input_events_t* base) -> std::uint32_t {
      return static_cast<std::uint32_t>(static_cast<const EventList*>(base->ctx)->events.size());
    };
    input.get = [](const clap_input_events_t* base,
                   std::uint32_t index) -> const clap_event_header_t* {
      const auto& values = static_cast<const EventList*>(base->ctx)->events;
      return index < values.size() ? values[index] : nullptr;
    };
  }
};

struct Output final {
  std::vector<std::vector<float>> planes;
  std::vector<float*> pointers;
  clap_audio_buffer_t buffer{};
  Output(std::uint32_t channels, std::uint32_t frames)
      : planes(channels, std::vector<float>(frames, 0.0F)), pointers(channels) {
    for (std::uint32_t channel = 0U; channel < channels; ++channel)
      pointers[channel] = planes[channel].data();
    buffer.data32 = pointers.data();
    buffer.data64 = nullptr;
    buffer.channel_count = channels;
  }
};

double difference(std::span<const float> lhs, std::span<const float> rhs) {
  double total = 0.0;
  for (std::size_t index = 0U; index < lhs.size(); ++index)
    total += std::abs(static_cast<double>(lhs[index]) - rhs[index]);
  return total;
}

bool processBlock(const clap_plugin_t* plugin, std::uint32_t channels,
                  std::uint32_t frames, const clap_input_events_t* events,
                  Output& output, clap_sectime position, bool playing = true) {
  clap_event_transport_t transport{};
  transport.header.size = sizeof(transport);
  transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  transport.header.type = CLAP_EVENT_TRANSPORT;
  transport.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE |
                    (playing ? static_cast<std::uint32_t>(CLAP_TRANSPORT_IS_PLAYING) : 0U);
  transport.song_pos_seconds = position;
  clap_process_t process{};
  process.steady_time = 0;
  process.frames_count = frames;
  process.transport = &transport;
  process.audio_outputs = &output.buffer;
  process.audio_outputs_count = 1U;
  process.in_events = events;
  return output.buffer.channel_count == channels &&
         plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

const clap_plugin_t* createPlugin(const clap_plugin_factory_t* factory,
                                  const clap_host_t* host) {
  const auto* plugin = factory->create_plugin(factory, host, seam::clap::kPluginId);
  return plugin != nullptr && plugin->init(plugin) ? plugin : nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path pluginPath;
  std::filesystem::path statePath;
  std::filesystem::path summaryPath;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--plugin" && index + 1 < argc) pluginPath = argv[++index];
    else if (argument == "--state" && index + 1 < argc) statePath = argv[++index];
    else if (argument == "--summary" && index + 1 < argc) summaryPath = argv[++index];
  }
  if (pluginPath.empty() || statePath.empty()) {
    std::cerr << "Usage: seam_clap_host --plugin FILE.clap --state FILE [--summary FILE]\n";
    return 2;
  }
  auto state = seam::clap::readStateFile(statePath);
  auto encoded = state ? seam::clap::encodeState(state.value())
                       : seam::core::Result<std::vector<std::byte>>{state.error()};
  if (!state || !encoded) {
    std::cerr << "Unable to read CLAP state\n";
    return 1;
  }

  Module module;
  if (!module.open(pluginPath)) {
    std::cerr << "Unable to load CLAP module\n";
    return 1;
  }
  const auto* entry = static_cast<const clap_plugin_entry_t*>(module.symbol("clap_entry"));
  if (entry == nullptr || !clap_version_is_compatible(entry->clap_version) ||
      !entry->init(pluginPath.string().c_str())) {
    std::cerr << "Invalid CLAP entry\n";
    return 1;
  }
  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (factory == nullptr || factory->get_plugin_count(factory) != 1U) return 1;
  const auto* descriptor = factory->get_plugin_descriptor(factory, 0U);
  if (descriptor == nullptr || std::strcmp(descriptor->id, seam::clap::kPluginId) != 0) return 1;

  HostContext context;
  const clap_host_t host{CLAP_VERSION, &context, "SEAM Phase 10 Host", "Project SEAM",
                         "", "0.10.0", &hostGetExtension, &hostRequestRestart,
                         &hostRequestProcess, &hostRequestCallback};
  const auto* reference = createPlugin(factory, &host);
  const auto* automated = createPlugin(factory, &host);
  if (reference == nullptr || automated == nullptr) return 1;

  const auto* stateExtension = static_cast<const clap_plugin_state_t*>(
      reference->get_extension(reference, CLAP_EXT_STATE));
  const auto* automatedStateExtension = static_cast<const clap_plugin_state_t*>(
      automated->get_extension(automated, CLAP_EXT_STATE));
  const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
      reference->get_extension(reference, CLAP_EXT_AUDIO_PORTS));
  const auto* params = static_cast<const clap_plugin_params_t*>(
      reference->get_extension(reference, CLAP_EXT_PARAMS));
  if (stateExtension == nullptr || automatedStateExtension == nullptr ||
      ports == nullptr || params == nullptr || params->count(reference) != 1U) return 1;
  ReadStream referenceRead(encoded.value());
  ReadStream automatedRead(encoded.value());
  if (!stateExtension->load(reference, &referenceRead.stream) ||
      !automatedStateExtension->load(automated, &automatedRead.stream)) return 1;

  clap_audio_port_info_t portInfo{};
  if (ports->count(reference, true) != 0U || ports->count(reference, false) != 1U ||
      !ports->get(reference, 0U, false, &portInfo) ||
      portInfo.channel_count != state.value().channelCount) return 1;
  constexpr std::uint32_t frames = 256U;
  if (!reference->activate(reference, 48000.0, 1U, frames) ||
      !automated->activate(automated, 48000.0, 1U, frames) ||
      !reference->start_processing(reference) ||
      !automated->start_processing(automated)) return 1;
  ReadStream activeLoadAttempt(encoded.value());
  if (stateExtension->load(reference, &activeLoadAttempt.stream) ||
      context.restartRequests != 1U) return 1;

  Output referenceOutput(portInfo.channel_count, frames);
  Output automatedOutput(portInfo.channel_count, frames);
  EventList noEvents;
  clap_event_param_value_t gainEvent{};
  gainEvent.header.size = sizeof(gainEvent);
  gainEvent.header.time = 128U;
  gainEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  gainEvent.header.type = CLAP_EVENT_PARAM_VALUE;
  gainEvent.param_id = seam::clap::kMasterGainParamId;
  gainEvent.note_id = gainEvent.port_index = gainEvent.channel = gainEvent.key = -1;
  gainEvent.value = -6.0;
  EventList automation;
  automation.events.push_back(&gainEvent.header);
  if (!processBlock(reference, portInfo.channel_count, frames, &noEvents.input,
                    referenceOutput, 0) ||
      !processBlock(automated, portInfo.channel_count, frames, &automation.input,
                    automatedOutput, 0)) return 1;

  const auto firstDifference = difference(
      std::span<const float>{referenceOutput.planes[0]}.first(128U),
      std::span<const float>{automatedOutput.planes[0]}.first(128U));
  double ratioTotal = 0.0;
  std::size_t ratioCount = 0U;
  for (std::size_t frame = 128U; frame < frames; ++frame) {
    const auto referenceValue = referenceOutput.planes[0][frame];
    if (std::abs(referenceValue) > 1.0e-5F) {
      ratioTotal += automatedOutput.planes[0][frame] / referenceValue;
      ++ratioCount;
    }
  }
  const auto ratio = ratioCount == 0U ? 0.0 : ratioTotal / static_cast<double>(ratioCount);
  if (firstDifference > 1.0e-6 || std::abs(ratio - seam::clap::gainFromDecibels(-6.0)) > 0.01)
    return 1;

  WriteStream saved;
  if (!stateExtension->save(reference, &saved.stream)) return 1;
  const auto decodedSaved = seam::clap::decodeState(saved.bytes);
  if (!decodedSaved || decodedSaved.value().channelCount != state.value().channelCount) return 1;

  Output paused(portInfo.channel_count, frames);
  if (!processBlock(reference, portInfo.channel_count, frames, &noEvents.input,
                    paused, CLAP_SECTIME_FACTOR / 2, false)) return 1;
  for (const auto value : paused.planes[0]) if (value != 0.0F) return 1;

  reference->stop_processing(reference);
  automated->stop_processing(automated);
  reference->deactivate(reference);
  automated->deactivate(automated);
  reference->destroy(reference);
  automated->destroy(automated);
  entry->deinit();

  if (!summaryPath.empty()) {
    std::filesystem::create_directories(summaryPath.parent_path());
    std::ofstream summary(summaryPath, std::ios::binary);
    summary << "{\n"
            << "  \"pluginId\": \"" << seam::clap::kPluginId << "\",\n"
            << "  \"channels\": " << portInfo.channel_count << ",\n"
            << "  \"framesProcessed\": " << frames << ",\n"
            << "  \"automationRatio\": " << ratio << ",\n"
            << "  \"stateRoundTrip\": true,\n"
            << "  \"activeLoadRejected\": true,\n"
            << "  \"restartRequests\": " << context.restartRequests << ",\n"
            << "  \"transportPauseSilence\": true\n"
            << "}\n";
  }
  std::cout << "CLAP smoke PASS: " << descriptor->name << ", "
            << portInfo.channel_count << " channels\n";
  return 0;
}
