#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/clap_editor/embedded_view.hpp"

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace seam::clap_editor {
namespace {

constexpr std::string_view kPluginId{"com.project-seam.editor"};
constexpr std::uint32_t kDefaultWidth = 1100U;
constexpr std::uint32_t kDefaultHeight = 720U;
constexpr std::uint32_t kMinimumWidth = 720U;
constexpr std::uint32_t kMinimumHeight = 480U;
constexpr std::size_t kMaximumStateBytes = 16U * 1024U * 1024U + 128U;

std::mutex entryMutex;
std::uint32_t entryReferenceCount = 0U;
std::filesystem::path entryPluginPath;

std::filesystem::path resolveCharacterPackage() {
  std::filesystem::path pluginPath;
  {
    std::scoped_lock lock(entryMutex);
    pluginPath = entryPluginPath;
  }
  if (!pluginPath.empty()) {
    std::error_code error;
    const auto bundleCandidate =
        pluginPath / "Contents" / "Resources" / "character-01";
    if (std::filesystem::is_directory(bundleCandidate, error)) {
      return bundleCandidate;
    }
    const auto sidecarCandidate =
        pluginPath.parent_path() / "ProjectSEAMEditor.resources" /
        "character-01";
    error.clear();
    if (std::filesystem::is_directory(sidecarCandidate, error)) {
      return sidecarCandidate;
    }
  }
#if defined(SEAM_SOURCE_CHARACTER_PACKAGE)
  return std::filesystem::path{SEAM_SOURCE_CHARACTER_PACKAGE};
#else
  return std::filesystem::path{"assets/character-01"};
#endif
}

const std::array<const char*, 4> kFeatures{
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor{
    .clap_version = CLAP_VERSION,
    .id = "com.project-seam.editor",
    .name = "Project SEAM Editor",
    .vendor = "Project SEAM",
    .url = "",
    .manual_url = "",
    .support_url = "",
    .version = "0.11.0",
    .description =
        "Sample-concatenative singing editor with embedded piano roll and live note input",
    .features = kFeatures.data(),
};

class PluginInstance final {
public:
  explicit PluginInstance(const clap_host_t* host)
      : host_(host),
        runtime_(std::make_unique<EditorRuntime>(
            std::nullopt, resolveCharacterPackage())) {
    runtime_->setRenderReadyCallback([this] {
      if (host_ != nullptr && host_->request_process != nullptr) {
        host_->request_process(host_);
      }
    });
    plugin_ = clap_plugin_t{
        .desc = &kDescriptor,
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

  ~PluginInstance() {
    if (runtime_) runtime_->setRenderReadyCallback({});
    unregisterTimer(*this);
    view_.reset();
  }

  [[nodiscard]] const clap_plugin_t* plugin() const noexcept { return &plugin_; }

  static const clap_plugin_descriptor_t& descriptor() noexcept {
    return kDescriptor;
  }

private:
  static PluginInstance* self(const clap_plugin_t* plugin) noexcept {
    return plugin != nullptr
               ? static_cast<PluginInstance*>(plugin->plugin_data)
               : nullptr;
  }

  static bool CLAP_ABI pluginInit(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->initialized_) return false;
    instance->initialized_ = true;
    instance->runtime_->requestRender(48000U);
    return true;
  }

  static void CLAP_ABI pluginDestroy(const clap_plugin_t* plugin) {
    delete self(plugin);
  }

  static bool CLAP_ABI pluginActivate(const clap_plugin_t* plugin,
                                      double sampleRate,
                                      std::uint32_t minimumFrames,
                                      std::uint32_t maximumFrames) {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->active_ || !std::isfinite(sampleRate) ||
        sampleRate < 8000.0 || sampleRate > 192000.0 ||
        minimumFrames == 0U || maximumFrames < minimumFrames ||
        maximumFrames > 1U << 20U) {
      return false;
    }
    instance->sampleRate_ = sampleRate;
    instance->maximumFrames_ = maximumFrames;
    instance->freeRunFrame_ = 0U;
    instance->runtime_->resetLive();
    instance->runtime_->setLiveSampleRate(sampleRate);
    instance->runtime_->requestRender(
        static_cast<std::uint32_t>(std::llround(sampleRate)));
    instance->active_ = true;
    return true;
  }

  static void CLAP_ABI pluginDeactivate(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance == nullptr) return;
    instance->processing_ = false;
    instance->active_ = false;
    instance->runtime_->resetLive();
  }

  static bool CLAP_ABI pluginStartProcessing(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance == nullptr || !instance->active_ || instance->processing_) {
      return false;
    }
    instance->processing_ = true;
    return true;
  }

  static void CLAP_ABI pluginStopProcessing(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance != nullptr) instance->processing_ = false;
  }

  static void CLAP_ABI pluginReset(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance == nullptr) return;
    instance->freeRunFrame_ = 0U;
    instance->runtime_->resetLive();
  }

  [[nodiscard]] static std::uint64_t transportFrame(
      const PluginInstance& instance,
      const clap_event_transport_t* transport) noexcept {
    if (transport == nullptr) return instance.freeRunFrame_;
    if ((transport->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) != 0U) {
      const auto seconds = std::max<std::int64_t>(0, transport->song_pos_seconds);
      return static_cast<std::uint64_t>(
          static_cast<long double>(seconds) * instance.sampleRate_ /
          static_cast<long double>(CLAP_SECTIME_FACTOR));
    }
    if ((transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0U) {
      const auto beats = static_cast<long double>(
          std::max<std::int64_t>(0, transport->song_pos_beats)) /
          static_cast<long double>(CLAP_BEATTIME_FACTOR);
      const auto tempo = (transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0U &&
                                 std::isfinite(transport->tempo) &&
                                 transport->tempo > 0.0
                             ? transport->tempo
                             : 154.0;
      return static_cast<std::uint64_t>(
          beats * 60.0L / static_cast<long double>(tempo) *
          instance.sampleRate_);
    }
    return instance.freeRunFrame_;
  }

  static void clearOutput(clap_audio_buffer_t& output,
                          std::uint32_t frames) noexcept {
    for (std::uint32_t channel = 0U; channel < output.channel_count; ++channel) {
      if (output.data32 != nullptr && output.data32[channel] != nullptr) {
        std::fill(output.data32[channel], output.data32[channel] + frames, 0.0F);
      }
      if (output.data64 != nullptr && output.data64[channel] != nullptr) {
        std::fill(output.data64[channel], output.data64[channel] + frames, 0.0);
      }
    }
  }

  static void writeOutput(clap_audio_buffer_t& output,
                          std::uint32_t frame, float left,
                          float right) noexcept {
    for (std::uint32_t channel = 0U; channel < output.channel_count; ++channel) {
      const auto value = channel == 0U ? left : channel == 1U ? right : 0.0F;
      if (output.data32 != nullptr && output.data32[channel] != nullptr) {
        output.data32[channel][frame] = value;
      }
      if (output.data64 != nullptr && output.data64[channel] != nullptr) {
        output.data64[channel][frame] = static_cast<double>(value);
      }
    }
  }

  static void applyNoteEvent(PluginInstance& instance,
                             const clap_event_header_t& header) noexcept {
    if (header.space_id != CLAP_CORE_EVENT_SPACE_ID ||
        header.size < sizeof(clap_event_note_t)) {
      return;
    }
    if (header.type != CLAP_EVENT_NOTE_ON &&
        header.type != CLAP_EVENT_NOTE_OFF &&
        header.type != CLAP_EVENT_NOTE_CHOKE) {
      return;
    }
    const auto& note = reinterpret_cast<const clap_event_note_t&>(header);
    if (note.port_index != 0) return;
    if (header.type == CLAP_EVENT_NOTE_ON) {
      if (note.key < 0 || note.key > 127) return;
      instance.runtime_->noteOn(
          note.note_id, note.key,
          static_cast<float>(std::clamp(note.velocity, 0.0, 1.0)));
    } else if (header.type == CLAP_EVENT_NOTE_OFF) {
      if (note.note_id < 0 && (note.key < 0 || note.key > 127)) return;
      instance.runtime_->noteOff(note.note_id, note.key);
    } else {
      if (note.note_id < 0 && (note.key < 0 || note.key > 127)) return;
      instance.runtime_->choke(note.note_id, note.key);
    }
  }

  static clap_process_status CLAP_ABI pluginProcess(
      const clap_plugin_t* plugin, const clap_process_t* process) {
    auto* instance = self(plugin);
    if (instance == nullptr || process == nullptr || !instance->processing_ ||
        process->audio_outputs_count == 0U || process->audio_outputs == nullptr ||
        process->frames_count > instance->maximumFrames_) {
      return CLAP_PROCESS_ERROR;
    }
    auto& output = process->audio_outputs[0];
    if (output.channel_count < 2U ||
        (output.data32 == nullptr && output.data64 == nullptr)) {
      return CLAP_PROCESS_ERROR;
    }
    clearOutput(output, process->frames_count);

    auto preview = instance->runtime_->acquireRenderedPreview();
    const auto baseFrame = transportFrame(*instance, process->transport);
    const auto transportPlaying = process->transport == nullptr ||
                                  (process->transport->flags &
                                   CLAP_TRANSPORT_IS_PLAYING) != 0U;
    std::uint32_t eventIndex = 0U;
    const auto eventCount = process->in_events != nullptr &&
                                    process->in_events->size != nullptr
                                ? process->in_events->size(process->in_events)
                                : 0U;
    const clap_event_header_t* event =
        eventIndex < eventCount && process->in_events->get != nullptr
            ? process->in_events->get(process->in_events, eventIndex)
            : nullptr;

    bool produced = false;
    for (std::uint32_t frame = 0U; frame < process->frames_count; ++frame) {
      while (event != nullptr && event->time <= frame) {
        applyNoteEvent(*instance, *event);
        ++eventIndex;
        event = eventIndex < eventCount && process->in_events->get != nullptr
                    ? process->in_events->get(process->in_events, eventIndex)
                    : nullptr;
      }
      float left = 0.0F;
      float right = 0.0F;
      if (transportPlaying && static_cast<bool>(preview) &&
          preview->sampleRate ==
              static_cast<std::uint32_t>(std::llround(instance->sampleRate_))) {
        const auto sourceFrame = baseFrame + frame;
        if (sourceFrame <= std::numeric_limits<std::size_t>::max() / 2U) {
          const auto source = static_cast<std::size_t>(sourceFrame) * 2U;
          if (source + 1U < preview->stereo.size()) {
            left = preview->stereo[source];
            right = preview->stereo[source + 1U];
          }
        }
      }
      const auto live = instance->runtime_->renderLiveSample();
      left = std::clamp(left + live, -1.0F, 1.0F);
      right = std::clamp(right + live, -1.0F, 1.0F);
      produced = produced || std::abs(left) > 1.0e-7F ||
                 std::abs(right) > 1.0e-7F;
      writeOutput(output, frame, left, right);
    }
    if (process->transport == nullptr) {
      instance->freeRunFrame_ += process->frames_count;
    }
    return produced ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
  }

  static std::uint32_t CLAP_ABI audioPortsCount(const clap_plugin_t*,
                                                bool isInput) {
    return isInput ? 0U : 1U;
  }

  static bool CLAP_ABI audioPortsGet(const clap_plugin_t*,
                                     std::uint32_t index, bool isInput,
                                     clap_audio_port_info_t* info) {
    if (isInput || index != 0U || info == nullptr) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0U;
    std::snprintf(info->name, sizeof(info->name), "%s", "SEAM Editor Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS;
    info->channel_count = 2U;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
  }

  static std::uint32_t CLAP_ABI notePortsCount(const clap_plugin_t*,
                                               bool isInput) {
    return isInput ? 1U : 0U;
  }

  static bool CLAP_ABI notePortsGet(const clap_plugin_t*,
                                    std::uint32_t index, bool isInput,
                                    clap_note_port_info_t* info) {
    if (!isInput || index != 0U || info == nullptr) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0U;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", "Live Singing Notes");
    return true;
  }

  static bool writeAll(const clap_ostream_t* stream,
                       std::span<const std::byte> bytes) {
    if (stream == nullptr || stream->write == nullptr) return false;
    std::size_t cursor = 0U;
    while (cursor < bytes.size()) {
      const auto written = stream->write(
          stream, bytes.data() + cursor, bytes.size() - cursor);
      if (written <= 0) return false;
      cursor += static_cast<std::size_t>(written);
    }
    return true;
  }

  static bool CLAP_ABI stateSave(const clap_plugin_t* plugin,
                                 const clap_ostream_t* stream) {
    const auto* instance = self(plugin);
    if (instance == nullptr) return false;
    const auto encoded = encodeEditorState(instance->runtime_->projectCopy());
    return encoded && writeAll(stream, encoded.value());
  }

  static bool CLAP_ABI stateLoad(const clap_plugin_t* plugin,
                                 const clap_istream_t* stream) {
    auto* instance = self(plugin);
    if (instance == nullptr || stream == nullptr || stream->read == nullptr) {
      return false;
    }
    if (instance->active_) {
      if (instance->host_ != nullptr &&
          instance->host_->request_restart != nullptr) {
        instance->host_->request_restart(instance->host_);
      }
      return false;
    }
    std::vector<std::byte> bytes;
    bytes.reserve(128U * 1024U);
    std::array<std::byte, 64U * 1024U> chunk{};
    for (;;) {
      const auto read = stream->read(stream, chunk.data(), chunk.size());
      if (read < 0) return false;
      if (read == 0) break;
      const auto count = static_cast<std::size_t>(read);
      if (bytes.size() > kMaximumStateBytes - count) return false;
      bytes.insert(bytes.end(), chunk.begin(), chunk.begin() +
                                            static_cast<std::ptrdiff_t>(count));
    }
    const auto decoded = decodeEditorState(bytes);
    if (!decoded) return false;
    const auto replaced = instance->runtime_->replaceProject(decoded.value());
    if (!replaced) return false;
    instance->freeRunFrame_ = 0U;
    return true;
  }

  static bool CLAP_ABI guiIsApiSupported(const clap_plugin_t*,
                                         const char* api,
                                         bool isFloating) {
    if (api == nullptr || isFloating) return false;
#if defined(SEAM_CLAP_EDITOR_X11)
    return std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
#elif defined(SEAM_CLAP_EDITOR_WIN32)
    return std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#elif defined(SEAM_CLAP_EDITOR_COCOA)
    return std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#else
    return false;
#endif
  }

  static bool CLAP_ABI guiGetPreferredApi(const clap_plugin_t*,
                                          const char** api,
                                          bool* isFloating) {
    if (api == nullptr || isFloating == nullptr) return false;
    *isFloating = false;
#if defined(SEAM_CLAP_EDITOR_X11)
    *api = CLAP_WINDOW_API_X11;
#elif defined(SEAM_CLAP_EDITOR_WIN32)
    *api = CLAP_WINDOW_API_WIN32;
#elif defined(SEAM_CLAP_EDITOR_COCOA)
    *api = CLAP_WINDOW_API_COCOA;
#else
    return false;
#endif
    return true;
  }

  static bool CLAP_ABI guiCreate(const clap_plugin_t* plugin,
                                 const char* api, bool isFloating) {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->guiCreated_ ||
        !guiIsApiSupported(plugin, api, isFloating)) {
      return false;
    }
    instance->view_ = createEmbeddedView(*instance->runtime_);
    if (!instance->view_) return false;
    const auto created = instance->view_->create(api, isFloating);
    if (!created) {
      instance->view_.reset();
      return false;
    }
    instance->guiCreated_ = true;
    return true;
  }

  static void unregisterTimer(PluginInstance& instance) noexcept {
    if (instance.timerId_ == CLAP_INVALID_ID || instance.host_ == nullptr ||
        instance.host_->get_extension == nullptr) {
      return;
    }
    const auto* timer = static_cast<const clap_host_timer_support_t*>(
        instance.host_->get_extension(instance.host_, CLAP_EXT_TIMER_SUPPORT));
    if (timer != nullptr && timer->unregister_timer != nullptr) {
      static_cast<void>(timer->unregister_timer(instance.host_,
                                                instance.timerId_));
    }
    instance.timerId_ = CLAP_INVALID_ID;
  }

  static void CLAP_ABI guiDestroy(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance == nullptr) return;
    unregisterTimer(*instance);
    instance->view_.reset();
    instance->guiCreated_ = false;
  }

  static bool CLAP_ABI guiSetScale(const clap_plugin_t* plugin, double scale) {
    auto* instance = self(plugin);
    if (instance == nullptr || !std::isfinite(scale) || scale < 0.5 ||
        scale > 4.0) {
      return false;
    }
    return instance->view_ == nullptr || instance->view_->setScale(scale);
  }

  static bool CLAP_ABI guiGetSize(const clap_plugin_t* plugin,
                                  std::uint32_t* width,
                                  std::uint32_t* height) {
    const auto* instance = self(plugin);
    if (instance == nullptr || width == nullptr || height == nullptr) return false;
    *width = instance->guiWidth_;
    *height = instance->guiHeight_;
    return true;
  }

  static bool CLAP_ABI guiCanResize(const clap_plugin_t*) { return true; }

  static bool CLAP_ABI guiGetResizeHints(const clap_plugin_t*,
                                         clap_gui_resize_hints_t* hints) {
    if (hints == nullptr) return false;
    *hints = clap_gui_resize_hints_t{
        .can_resize_horizontally = true,
        .can_resize_vertically = true,
        .preserve_aspect_ratio = false,
        .aspect_ratio_width = 0U,
        .aspect_ratio_height = 0U,
    };
    return true;
  }

  static bool CLAP_ABI guiAdjustSize(const clap_plugin_t*,
                                     std::uint32_t* width,
                                     std::uint32_t* height) {
    if (width == nullptr || height == nullptr) return false;
    *width = std::clamp(*width, kMinimumWidth, 4096U);
    *height = std::clamp(*height, kMinimumHeight, 2160U);
    return true;
  }

  static bool CLAP_ABI guiSetSize(const clap_plugin_t* plugin,
                                  std::uint32_t width,
                                  std::uint32_t height) {
    auto* instance = self(plugin);
    if (instance == nullptr) return false;
    if (!guiAdjustSize(plugin, &width, &height)) return false;
    instance->guiWidth_ = width;
    instance->guiHeight_ = height;
    return instance->view_ == nullptr ||
           static_cast<bool>(instance->view_->setSize(width, height));
  }

  static bool CLAP_ABI guiSetParent(const clap_plugin_t* plugin,
                                    const clap_window_t* parent) {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->view_ == nullptr || parent == nullptr) {
      return false;
    }
#if defined(SEAM_CLAP_EDITOR_X11)
    if (parent->api == nullptr ||
        std::strcmp(parent->api, CLAP_WINDOW_API_X11) != 0) return false;
    return instance->view_->setParent(static_cast<std::uintptr_t>(parent->x11));
#elif defined(SEAM_CLAP_EDITOR_WIN32)
    if (parent->api == nullptr ||
        std::strcmp(parent->api, CLAP_WINDOW_API_WIN32) != 0) return false;
    return instance->view_->setParent(
        reinterpret_cast<std::uintptr_t>(parent->win32));
#elif defined(SEAM_CLAP_EDITOR_COCOA)
    if (parent->api == nullptr ||
        std::strcmp(parent->api, CLAP_WINDOW_API_COCOA) != 0) return false;
    return instance->view_->setParent(
        reinterpret_cast<std::uintptr_t>(parent->cocoa));
#else
    static_cast<void>(parent);
    return false;
#endif
  }

  static bool CLAP_ABI guiSetTransient(const clap_plugin_t*,
                                       const clap_window_t*) {
    return false;
  }

  static void CLAP_ABI guiSuggestTitle(const clap_plugin_t*, const char*) {}

  static bool registerTimer(PluginInstance& instance) noexcept {
    if (instance.host_ == nullptr || instance.host_->get_extension == nullptr) {
      return false;
    }
    const auto* timer = static_cast<const clap_host_timer_support_t*>(
        instance.host_->get_extension(instance.host_, CLAP_EXT_TIMER_SUPPORT));
    if (timer == nullptr || timer->register_timer == nullptr) return false;
    clap_id identifier = CLAP_INVALID_ID;
    if (!timer->register_timer(instance.host_, 16U, &identifier)) return false;
    instance.timerId_ = identifier;
    return true;
  }

  static bool CLAP_ABI guiShow(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->view_ == nullptr ||
        !instance->view_->show()) {
      return false;
    }
    static_cast<void>(registerTimer(*instance));
    return true;
  }

  static bool CLAP_ABI guiHide(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->view_ == nullptr) return false;
    unregisterTimer(*instance);
    return static_cast<bool>(instance->view_->hide());
  }

  static void CLAP_ABI timerOnTimer(const clap_plugin_t* plugin,
                                    clap_id timerId) {
    auto* instance = self(plugin);
    if (instance != nullptr && instance->view_ != nullptr &&
        timerId == instance->timerId_) {
      instance->view_->onTimer();
    }
  }

  static const void* CLAP_ABI pluginGetExtension(const clap_plugin_t*,
                                                 const char* identifier) {
    if (identifier == nullptr) return nullptr;
    if (std::strcmp(identifier, CLAP_EXT_AUDIO_PORTS) == 0) {
      return &audioPortsExtension();
    }
    if (std::strcmp(identifier, CLAP_EXT_NOTE_PORTS) == 0) {
      return &notePortsExtension();
    }
    if (std::strcmp(identifier, CLAP_EXT_STATE) == 0) {
      return &stateExtension();
    }
    if (std::strcmp(identifier, CLAP_EXT_GUI) == 0) {
      return &guiExtension();
    }
    if (std::strcmp(identifier, CLAP_EXT_TIMER_SUPPORT) == 0) {
      return &timerExtension();
    }
    return nullptr;
  }

  static void CLAP_ABI pluginOnMainThread(const clap_plugin_t* plugin) {
    auto* instance = self(plugin);
    if (instance != nullptr && instance->view_ != nullptr &&
        instance->timerId_ == CLAP_INVALID_ID) {
      instance->view_->onTimer();
    }
  }

  static const clap_plugin_audio_ports_t& audioPortsExtension() {
    static const clap_plugin_audio_ports_t value{
        &audioPortsCount, &audioPortsGet};
    return value;
  }

  static const clap_plugin_note_ports_t& notePortsExtension() {
    static const clap_plugin_note_ports_t value{
        &notePortsCount, &notePortsGet};
    return value;
  }

  static const clap_plugin_state_t& stateExtension() {
    static const clap_plugin_state_t value{&stateSave, &stateLoad};
    return value;
  }

  static const clap_plugin_gui_t& guiExtension() {
    static const clap_plugin_gui_t value{
        &guiIsApiSupported, &guiGetPreferredApi, &guiCreate, &guiDestroy,
        &guiSetScale, &guiGetSize, &guiCanResize, &guiGetResizeHints,
        &guiAdjustSize, &guiSetSize, &guiSetParent, &guiSetTransient,
        &guiSuggestTitle, &guiShow, &guiHide};
    return value;
  }

  static const clap_plugin_timer_support_t& timerExtension() {
    static const clap_plugin_timer_support_t value{&timerOnTimer};
    return value;
  }

  clap_plugin_t plugin_{};
  const clap_host_t* host_{nullptr};
  std::unique_ptr<EditorRuntime> runtime_;
  std::unique_ptr<IEmbeddedView> view_;
  double sampleRate_{48000.0};
  std::uint32_t maximumFrames_{0U};
  std::uint64_t freeRunFrame_{0U};
  std::uint32_t guiWidth_{kDefaultWidth};
  std::uint32_t guiHeight_{kDefaultHeight};
  clap_id timerId_{CLAP_INVALID_ID};
  bool initialized_{false};
  bool active_{false};
  bool processing_{false};
  bool guiCreated_{false};
};

bool CLAP_ABI entryInit(const char* pluginPath) {
  std::scoped_lock lock(entryMutex);
  if (entryReferenceCount == 0U && pluginPath != nullptr) {
    entryPluginPath = std::filesystem::path{pluginPath};
  }
  ++entryReferenceCount;
  return true;
}

void CLAP_ABI entryDeinit() {
  std::scoped_lock lock(entryMutex);
  if (entryReferenceCount > 0U) --entryReferenceCount;
  if (entryReferenceCount == 0U) entryPluginPath.clear();
}

std::uint32_t CLAP_ABI factoryCount(const clap_plugin_factory_t*) {
  return 1U;
}

const clap_plugin_descriptor_t* CLAP_ABI factoryDescriptor(
    const clap_plugin_factory_t*, std::uint32_t index) {
  return index == 0U ? &PluginInstance::descriptor() : nullptr;
}

const clap_plugin_t* CLAP_ABI factoryCreate(const clap_plugin_factory_t*,
                                            const clap_host_t* host,
                                            const char* identifier) {
  if (host == nullptr || identifier == nullptr ||
      std::strcmp(identifier, kPluginId.data()) != 0 ||
      !clap_version_is_compatible(host->clap_version)) {
    return nullptr;
  }
  return (new PluginInstance(host))->plugin();
}

const clap_plugin_factory_t kFactory{
    &factoryCount, &factoryDescriptor, &factoryCreate};

const void* CLAP_ABI entryGetFactory(const char* identifier) {
  return identifier != nullptr &&
                 std::strcmp(identifier, CLAP_PLUGIN_FACTORY_ID) == 0
             ? &kFactory
             : nullptr;
}

}  // namespace
}  // namespace seam::clap_editor

extern "C" {
CLAP_EXPORT extern const clap_plugin_entry_t clap_entry{
    .clap_version = CLAP_VERSION,
    .init = &seam::clap_editor::entryInit,
    .deinit = &seam::clap_editor::entryDeinit,
    .get_factory = &seam::clap_editor::entryGetFactory,
};
}
