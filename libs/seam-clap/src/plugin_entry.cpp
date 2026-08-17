#include "seam/clap/plugin_identity.hpp"
#include "seam/clap/session.hpp"
#include "seam/clap/state_codec.hpp"

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace seam::clap {
namespace {

std::uint64_t doubleBits(double value) noexcept {
  return std::bit_cast<std::uint64_t>(value);
}
double bitsDouble(std::uint64_t value) noexcept {
  return std::bit_cast<double>(value);
}

PluginSession silentSession() {
  PluginSession session;
  session.sampleRate = 48000U;
  session.channelCount = 2U;
  session.title = "Empty Project SEAM render";
  session.interleavedSamples.assign(2U, 0.0F);
  return session;
}

class PluginInstance final {
public:
  explicit PluginInstance(const clap_host_t* host) : host_(host) {
    source_ = silentSession();
    prepared_ = source_;
    masterGainBits_.store(doubleBits(kDefaultMasterGainDb), std::memory_order_relaxed);
    plugin_ = clap_plugin_t{
        .desc = &descriptor(),
        .plugin_data = this,
        .init = &pluginInit,
        .destroy = &pluginDestroy,
        .activate = &pluginActivate,
        .deactivate = &pluginDeactivate,
        .start_processing = &pluginStartProcessing,
        .stop_processing = &pluginStopProcessing,
        .reset = &pluginReset,
        .process = &pluginProcess,
        .get_extension = &pluginGetExtension,
        .on_main_thread = &pluginOnMainThread,
    };
  }

  [[nodiscard]] const clap_plugin_t* plugin() const noexcept { return &plugin_; }

  static const clap_plugin_descriptor_t& descriptor() noexcept {
    static const char* const features[] = {
        CLAP_PLUGIN_FEATURE_INSTRUMENT,
        CLAP_PLUGIN_FEATURE_SAMPLER,
        CLAP_PLUGIN_FEATURE_STEREO,
        CLAP_PLUGIN_FEATURE_SURROUND,
        nullptr,
    };
    static const clap_plugin_descriptor_t value{
        .clap_version = CLAP_VERSION,
        .id = kPluginId,
        .name = kPluginName,
        .vendor = kPluginVendor,
        .url = "https://example.invalid/project-seam",
        .manual_url = "",
        .support_url = "",
        .version = kPluginVersion,
        .description = "Host-synchronized playback of pre-rendered Project SEAM multichannel vocal state",
        .features = features,
    };
    return value;
  }

private:
  static PluginInstance* self(const clap_plugin_t* plugin) noexcept {
    return plugin == nullptr ? nullptr : static_cast<PluginInstance*>(plugin->plugin_data);
  }

  static bool CLAP_ABI pluginInit(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->host_ == nullptr ||
        !clap_version_is_compatible(instance->host_->clap_version)) return false;
    instance->initialized_ = true;
    return true;
  }
  static void CLAP_ABI pluginDestroy(const clap_plugin_t* plugin) {
    delete self(plugin);
  }
  static bool CLAP_ABI pluginActivate(const clap_plugin_t* plugin, double sampleRate,
                                      std::uint32_t minFrames,
                                      std::uint32_t maxFrames) {
    auto* instance = self(plugin);
    if (instance == nullptr || !instance->initialized_ || instance->active_ ||
        !std::isfinite(sampleRate) || sampleRate < kMinimumSampleRate ||
        sampleRate > kMaximumSampleRate || minFrames == 0U ||
        maxFrames < minFrames || maxFrames > 1'048'576U) return false;
    const auto prepared = resampleSession(instance->source_,
                                          static_cast<std::uint32_t>(std::llround(sampleRate)));
    if (!prepared) return false;
    instance->prepared_ = std::move(prepared.value());
    instance->sampleRate_ = sampleRate;
    instance->maximumFrames_ = maxFrames;
    instance->freeRunFrame_ = 0U;
    instance->active_ = true;
    return true;
  }
  static void CLAP_ABI pluginDeactivate(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance == nullptr) return;
    instance->processing_ = false;
    instance->active_ = false;
    instance->maximumFrames_ = 0U;
  }
  static bool CLAP_ABI pluginStartProcessing(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance == nullptr || !instance->active_ || instance->processing_) return false;
    instance->processing_ = true;
    return true;
  }
  static void CLAP_ABI pluginStopProcessing(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance != nullptr) instance->processing_ = false;
  }
  static void CLAP_ABI pluginReset(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance != nullptr) instance->freeRunFrame_ = 0U;
  }

  static void clearOutput(const clap_process_t* process) noexcept {
    if (process == nullptr || process->audio_outputs_count == 0U ||
        process->audio_outputs == nullptr) return;
    auto& output = process->audio_outputs[0];
    output.constant_mask = output.channel_count >= 64U
                               ? std::numeric_limits<std::uint64_t>::max()
                               : ((1ULL << output.channel_count) - 1ULL);
    for (std::uint32_t channel = 0U; channel < output.channel_count; ++channel) {
      if (output.data32 != nullptr && output.data32[channel] != nullptr)
        std::fill_n(output.data32[channel], process->frames_count, 0.0F);
      if (output.data64 != nullptr && output.data64[channel] != nullptr)
        std::fill_n(output.data64[channel], process->frames_count, 0.0);
    }
  }

  [[nodiscard]] std::uint64_t transportFrame(const clap_process_t* process,
                                              bool& playing) const noexcept {
    playing = true;
    if (process->transport == nullptr) return freeRunFrame_;
    const auto& transport = *process->transport;
    playing = (transport.flags & CLAP_TRANSPORT_IS_PLAYING) != 0U;
    if ((transport.flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) != 0U) {
      const long double seconds = static_cast<long double>(transport.song_pos_seconds) /
                                  static_cast<long double>(CLAP_SECTIME_FACTOR);
      return seconds <= 0.0L ? 0U : static_cast<std::uint64_t>(
          std::llround(seconds * sampleRate_));
    }
    if ((transport.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0U &&
        (transport.flags & CLAP_TRANSPORT_HAS_TEMPO) != 0U &&
        std::isfinite(transport.tempo) && transport.tempo > 0.0) {
      const long double beats = static_cast<long double>(transport.song_pos_beats) /
                                static_cast<long double>(CLAP_BEATTIME_FACTOR);
      const long double seconds = beats * 60.0L / transport.tempo;
      return seconds <= 0.0L ? 0U : static_cast<std::uint64_t>(
          std::llround(seconds * sampleRate_));
    }
    return freeRunFrame_;
  }

  static bool applyEvents(const clap_input_events_t* events, std::uint32_t upToFrame,
                          std::uint32_t& eventIndex, double& gainDb) noexcept {
    if (events == nullptr || events->size == nullptr || events->get == nullptr) return false;
    bool changed = false;
    const auto count = events->size(events);
    while (eventIndex < count) {
      const auto* header = events->get(events, eventIndex);
      if (header == nullptr) {
        ++eventIndex;
        continue;
      }
      if (header->time > upToFrame) break;
      if (header->space_id == CLAP_CORE_EVENT_SPACE_ID &&
          header->type == CLAP_EVENT_PARAM_VALUE &&
          header->size >= sizeof(clap_event_param_value_t)) {
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
        if (event->param_id == kMasterGainParamId && std::isfinite(event->value)) {
          gainDb = std::clamp(event->value, kMinimumMasterGainDb, kMaximumMasterGainDb);
          changed = true;
        }
      }
      ++eventIndex;
    }
    return changed;
  }

  static clap_process_status CLAP_ABI pluginProcess(const clap_plugin_t* plugin,
                                                     const clap_process_t* process) {
    auto* instance = self(plugin);
    if (instance == nullptr || !instance->active_ || !instance->processing_ ||
        process == nullptr || process->frames_count > instance->maximumFrames_ ||
        process->audio_inputs_count != 0U || process->audio_outputs_count != 1U ||
        process->audio_outputs == nullptr) return CLAP_PROCESS_ERROR;
    auto& output = process->audio_outputs[0];
    if (output.channel_count != instance->prepared_.channelCount ||
        (output.data32 == nullptr && output.data64 == nullptr)) return CLAP_PROCESS_ERROR;

    clearOutput(process);
    bool playing = true;
    const auto startFrame = instance->transportFrame(process, playing);
    double gainDb = bitsDouble(instance->masterGainBits_.load(std::memory_order_relaxed));
    float linearGain = gainFromDecibels(gainDb);
    std::uint32_t eventIndex = 0U;
    for (std::uint32_t frame = 0U; frame < process->frames_count; ++frame) {
      if (applyEvents(process->in_events, frame, eventIndex, gainDb))
        linearGain = gainFromDecibels(gainDb);
      if (!playing) continue;
      const auto sourceFrame = startFrame + frame;
      if (sourceFrame >= instance->prepared_.frameCount()) continue;
      const auto sourceOffset = static_cast<std::size_t>(sourceFrame) *
                                instance->prepared_.channelCount;
      for (std::uint32_t channel = 0U; channel < output.channel_count; ++channel) {
        const auto value = instance->prepared_.interleavedSamples[sourceOffset + channel] * linearGain;
        if (output.data32 != nullptr && output.data32[channel] != nullptr)
          output.data32[channel][frame] = value;
        if (output.data64 != nullptr && output.data64[channel] != nullptr)
          output.data64[channel][frame] = value;
      }
    }
    static_cast<void>(applyEvents(process->in_events, process->frames_count, eventIndex, gainDb));
    instance->masterGainBits_.store(doubleBits(gainDb), std::memory_order_relaxed);
    output.constant_mask = 0U;
    if (process->transport == nullptr) instance->freeRunFrame_ += process->frames_count;
    return CLAP_PROCESS_CONTINUE;
  }

  static std::uint32_t CLAP_ABI audioPortsCount(const clap_plugin_t*, bool isInput) {
    return isInput ? 0U : 1U;
  }
  static bool CLAP_ABI audioPortsGet(const clap_plugin_t* plugin,
                                     std::uint32_t index, bool isInput,
                                     clap_audio_port_info_t* info) {
    const auto* instance = self(plugin);
    if (instance == nullptr || info == nullptr || isInput || index != 0U) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0U;
    std::snprintf(info->name, sizeof(info->name), "%s", "SEAM Render Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = instance->source_.channelCount;
    info->port_type = instance->source_.channelCount == 1U
                          ? CLAP_PORT_MONO
                          : (instance->source_.channelCount == 2U ? CLAP_PORT_STEREO : nullptr);
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
  }

  static std::uint32_t CLAP_ABI paramsCount(const clap_plugin_t*) { return 1U; }
  static bool CLAP_ABI paramsGetInfo(const clap_plugin_t*, std::uint32_t index,
                                     clap_param_info_t* info) {
    if (index != 0U || info == nullptr) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = kMasterGainParamId;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
    std::snprintf(info->name, sizeof(info->name), "%s", "Master Gain");
    std::snprintf(info->module, sizeof(info->module), "%s", "Output");
    info->min_value = kMinimumMasterGainDb;
    info->max_value = kMaximumMasterGainDb;
    info->default_value = kDefaultMasterGainDb;
    return true;
  }
  static bool CLAP_ABI paramsGetValue(const clap_plugin_t* plugin, clap_id id,
                                      double* out) {
    const auto* instance = self(plugin);
    if (instance == nullptr || out == nullptr || id != kMasterGainParamId) return false;
    *out = bitsDouble(instance->masterGainBits_.load(std::memory_order_relaxed));
    return true;
  }
  static bool CLAP_ABI paramsValueToText(const clap_plugin_t*, clap_id id,
                                         double value, char* out,
                                         std::uint32_t capacity) {
    if (id != kMasterGainParamId || out == nullptr || capacity == 0U ||
        !std::isfinite(value)) return false;
    const auto count = std::snprintf(out, capacity, "%.2f dB", value);
    return count >= 0 && static_cast<std::uint32_t>(count) < capacity;
  }
  static bool CLAP_ABI paramsTextToValue(const clap_plugin_t*, clap_id id,
                                         const char* text, double* out) {
    if (id != kMasterGainParamId || text == nullptr || out == nullptr) return false;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || !std::isfinite(value)) return false;
    *out = std::clamp(value, kMinimumMasterGainDb, kMaximumMasterGainDb);
    return true;
  }
  static void CLAP_ABI paramsFlush(const clap_plugin_t* plugin,
                                   const clap_input_events_t* input,
                                   const clap_output_events_t*) {
    auto* instance = self(plugin);
    if (instance == nullptr) return;
    double gain = bitsDouble(instance->masterGainBits_.load(std::memory_order_relaxed));
    std::uint32_t index = 0U;
    static_cast<void>(applyEvents(input, std::numeric_limits<std::uint32_t>::max(), index, gain));
    instance->masterGainBits_.store(doubleBits(gain), std::memory_order_relaxed);
  }

  static bool writeAll(const clap_ostream_t* stream,
                       const std::vector<std::byte>& bytes) {
    if (stream == nullptr || stream->write == nullptr) return false;
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
      const auto written = stream->write(stream, bytes.data() + offset,
                                         bytes.size() - offset);
      if (written <= 0) return false;
      offset += static_cast<std::size_t>(written);
    }
    return true;
  }
  static bool CLAP_ABI stateSave(const clap_plugin_t* plugin,
                                 const clap_ostream_t* stream) {
    const auto* instance = self(plugin);
    if (instance == nullptr) return false;
    PluginSession copy = instance->source_;
    copy.masterGainDb = bitsDouble(instance->masterGainBits_.load(std::memory_order_relaxed));
    const auto encoded = encodeState(copy);
    return encoded && writeAll(stream, encoded.value());
  }
  static bool CLAP_ABI stateLoad(const clap_plugin_t* plugin,
                                 const clap_istream_t* stream) {
    auto* instance = self(plugin);
    if (instance == nullptr || stream == nullptr || stream->read == nullptr) return false;
    if (instance->active_) {
      if (instance->host_ != nullptr && instance->host_->request_restart != nullptr)
        instance->host_->request_restart(instance->host_);
      return false;
    }
    std::vector<std::byte> bytes;
    bytes.reserve(64U * 1024U);
    std::byte chunk[64U * 1024U];
    while (true) {
      const auto read = stream->read(stream, chunk, sizeof(chunk));
      if (read < 0) return false;
      if (read == 0) break;
      if (bytes.size() + static_cast<std::size_t>(read) > kMaximumStateBytes) return false;
      bytes.insert(bytes.end(), chunk, chunk + read);
    }
    const auto decoded = decodeState(bytes);
    if (!decoded) return false;
    instance->source_ = decoded.value();
    instance->prepared_ = instance->source_;
    instance->masterGainBits_.store(doubleBits(instance->source_.masterGainDb),
                                    std::memory_order_relaxed);
    instance->freeRunFrame_ = 0U;
    return true;
  }

  static std::uint32_t CLAP_ABI latencyGet(const clap_plugin_t*) { return 0U; }
  static std::uint32_t CLAP_ABI tailGet(const clap_plugin_t*) { return 0U; }
  static bool CLAP_ABI renderHardRealtime(const clap_plugin_t*) { return false; }
  static bool CLAP_ABI renderSet(const clap_plugin_t* plugin,
                                 clap_plugin_render_mode mode) {
    auto* instance = self(plugin);
    if (instance == nullptr || (mode != CLAP_RENDER_REALTIME &&
                                mode != CLAP_RENDER_OFFLINE)) return false;
    instance->renderMode_ = mode;
    return true;
  }

  static const void* CLAP_ABI pluginGetExtension(const clap_plugin_t*, const char* id) {
    if (id == nullptr) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPortsExtension();
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExtension();
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExtension();
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latencyExtension();
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExtension();
    if (std::strcmp(id, CLAP_EXT_RENDER) == 0) return &renderExtension();
    return nullptr;
  }
  static void CLAP_ABI pluginOnMainThread(const clap_plugin_t*) {}

  static const clap_plugin_audio_ports_t& audioPortsExtension() {
    static const clap_plugin_audio_ports_t value{&audioPortsCount, &audioPortsGet};
    return value;
  }
  static const clap_plugin_params_t& paramsExtension() {
    static const clap_plugin_params_t value{&paramsCount, &paramsGetInfo,
                                             &paramsGetValue, &paramsValueToText,
                                             &paramsTextToValue, &paramsFlush};
    return value;
  }
  static const clap_plugin_state_t& stateExtension() {
    static const clap_plugin_state_t value{&stateSave, &stateLoad};
    return value;
  }
  static const clap_plugin_latency_t& latencyExtension() {
    static const clap_plugin_latency_t value{&latencyGet};
    return value;
  }
  static const clap_plugin_tail_t& tailExtension() {
    static const clap_plugin_tail_t value{&tailGet};
    return value;
  }
  static const clap_plugin_render_t& renderExtension() {
    static const clap_plugin_render_t value{&renderHardRealtime, &renderSet};
    return value;
  }

  clap_plugin_t plugin_{};
  const clap_host_t* host_{nullptr};
  PluginSession source_;
  PluginSession prepared_;
  std::atomic<std::uint64_t> masterGainBits_{};
  double sampleRate_{48000.0};
  std::uint32_t maximumFrames_{0U};
  std::uint64_t freeRunFrame_{0U};
  clap_plugin_render_mode renderMode_{CLAP_RENDER_REALTIME};
  bool initialized_{false};
  bool active_{false};
  bool processing_{false};
};

std::mutex entryMutex;
std::uint32_t entryReferenceCount = 0U;

bool CLAP_ABI entryInit(const char*) {
  std::scoped_lock lock(entryMutex);
  ++entryReferenceCount;
  return true;
}
void CLAP_ABI entryDeinit() {
  std::scoped_lock lock(entryMutex);
  if (entryReferenceCount > 0U) --entryReferenceCount;
}
std::uint32_t CLAP_ABI factoryCount(const clap_plugin_factory_t*) { return 1U; }
const clap_plugin_descriptor_t* CLAP_ABI factoryDescriptor(
    const clap_plugin_factory_t*, std::uint32_t index) {
  return index == 0U ? &PluginInstance::descriptor() : nullptr;
}
const clap_plugin_t* CLAP_ABI factoryCreate(const clap_plugin_factory_t*,
                                            const clap_host_t* host,
                                            const char* id) {
  if (host == nullptr || id == nullptr || std::strcmp(id, kPluginId) != 0 ||
      !clap_version_is_compatible(host->clap_version)) return nullptr;
  return (new PluginInstance(host))->plugin();
}
const clap_plugin_factory_t factory{&factoryCount, &factoryDescriptor, &factoryCreate};
const void* CLAP_ABI entryGetFactory(const char* id) {
  return id != nullptr && std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0
             ? &factory
             : nullptr;
}

}  // namespace
}  // namespace seam::clap

extern "C" {
CLAP_EXPORT extern const clap_plugin_entry_t clap_entry{
    .clap_version = CLAP_VERSION,
    .init = &seam::clap::entryInit,
    .deinit = &seam::clap::entryDeinit,
    .get_factory = &seam::clap::entryGetFactory,
};
}
