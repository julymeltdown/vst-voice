#include <clap/clap.h>

#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#endif

namespace {

class Module final {
public:
  bool open(const std::filesystem::path& path) {
#if defined(__linux__)
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    return handle_ != nullptr;
#else
    static_cast<void>(path);
    return false;
#endif
  }
  void* symbol(const char* name) const {
#if defined(__linux__)
    return handle_ != nullptr ? dlsym(handle_, name) : nullptr;
#else
    static_cast<void>(name);
    return nullptr;
#endif
  }
  ~Module() {
#if defined(__linux__)
    if (handle_ != nullptr) dlclose(handle_);
#endif
  }
private:
  void* handle_{nullptr};
};

struct HostContext final {
  clap_id timerId{CLAP_INVALID_ID};
  std::uint32_t timerPeriod{0U};
  std::uint32_t restartRequests{0U};
  std::uint32_t processRequests{0U};
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

const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char* id) {
  return id != nullptr && std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0
             ? &kTimerHost
             : nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t* host) {
  ++static_cast<HostContext*>(host->host_data)->restartRequests;
}
void CLAP_ABI hostRequestProcess(const clap_host_t* host) {
  ++static_cast<HostContext*>(host->host_data)->processRequests;
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
  std::array<std::vector<float>, 2> planes;
  std::array<float*, 2> pointers{};
  clap_audio_buffer_t buffer{};
  explicit Output(std::uint32_t frames)
      : planes{std::vector<float>(frames, 0.0F),
               std::vector<float>(frames, 0.0F)} {
    pointers[0] = planes[0].data();
    pointers[1] = planes[1].data();
    buffer.data32 = pointers.data();
    buffer.data64 = nullptr;
    buffer.channel_count = 2U;
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

#if defined(__linux__)
bool writePpm(Display* display, Window window,
              const std::filesystem::path& path) {
  XWindowAttributes attributes{};
  if (XGetWindowAttributes(display, window, &attributes) == 0) return false;
  auto* image = XGetImage(display, window, 0, 0,
                          static_cast<unsigned int>(attributes.width),
                          static_cast<unsigned int>(attributes.height),
                          AllPlanes, ZPixmap);
  if (image == nullptr) return false;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << "P6\n" << attributes.width << ' ' << attributes.height
         << "\n255\n";
  for (int y = 0; y < attributes.height; ++y) {
    for (int x = 0; x < attributes.width; ++x) {
      const auto pixel = XGetPixel(image, x, y);
      const std::array<char, 3> rgb{
          static_cast<char>((pixel >> 16U) & 0xffU),
          static_cast<char>((pixel >> 8U) & 0xffU),
          static_cast<char>(pixel & 0xffU),
      };
      output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    }
  }
  XDestroyImage(image);
  return static_cast<bool>(output);
}
#endif

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
    }
  }
  if (pluginPath.empty()) {
    std::cerr << "Usage: seam_clap_editor_host --plugin FILE.clap "
                 "[--screenshot FILE.ppm] [--summary FILE.json] "
                 "[--audio FILE.wav]\n";
    return 2;
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
      std::strcmp(descriptor->id, "com.project-seam.editor") != 0) return 1;

  HostContext context;
  const clap_host_t host{
      CLAP_VERSION, &context, "SEAM Phase 11 Host", "Project SEAM", "",
      "0.11.0", &hostGetExtension, &hostRequestRestart,
      &hostRequestProcess, &hostRequestCallback};
  const auto* plugin = factory->create_plugin(factory, &host, descriptor->id);
  if (plugin == nullptr || !plugin->init(plugin)) return 1;

  const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
      plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
  const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
      plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
  const auto* state = static_cast<const clap_plugin_state_t*>(
      plugin->get_extension(plugin, CLAP_EXT_STATE));
  const auto* gui = static_cast<const clap_plugin_gui_t*>(
      plugin->get_extension(plugin, CLAP_EXT_GUI));
  const auto* timer = static_cast<const clap_plugin_timer_support_t*>(
      plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT));
  if (audioPorts == nullptr || notePorts == nullptr || state == nullptr ||
      gui == nullptr || timer == nullptr ||
      audioPorts->count(plugin, true) != 0U ||
      audioPorts->count(plugin, false) != 1U ||
      notePorts->count(plugin, true) != 1U) {
    return 1;
  }

  bool guiCreated = false;
  bool guiVisible = false;
  bool screenshotWritten = false;
#if defined(__linux__)
  Display* display = XOpenDisplay(nullptr);
  Window parent = 0U;
  if (display != nullptr &&
      gui->is_api_supported(plugin, CLAP_WINDOW_API_X11, false) &&
      gui->create(plugin, CLAP_WINDOW_API_X11, false)) {
    guiCreated = true;
    parent = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                 0, 0, 1100U, 720U, 0U, 0U, 0x101015U);
    XMapWindow(display, parent);
    XFlush(display);
    clap_window_t parentWindow{};
    parentWindow.api = CLAP_WINDOW_API_X11;
    parentWindow.x11 = parent;
    guiVisible = gui->set_parent(plugin, &parentWindow) &&
                 gui->set_size(plugin, 1100U, 720U) &&
                 gui->show(plugin);
    for (int frame = 0; frame < 45; ++frame) {
      if (context.timerId != CLAP_INVALID_ID) {
        timer->on_timer(plugin, context.timerId);
      }
      XSync(display, False);
      std::this_thread::sleep_for(std::chrono::milliseconds{8});
    }
    Window root = 0U;
    Window parentResult = 0U;
    Window* children = nullptr;
    unsigned int childCount = 0U;
    if (guiVisible && !screenshotPath.empty() &&
        XQueryTree(display, parent, &root, &parentResult, &children,
                   &childCount) != 0 && childCount > 0U) {
      screenshotWritten = writePpm(display, children[0], screenshotPath);
    }
    if (children != nullptr) XFree(children);
  }
#else
  static_cast<void>(screenshotPath);
#endif

  WriteStream saved;
  if (!state->save(plugin, &saved.stream) || saved.bytes.empty()) return 1;

  constexpr std::uint32_t frames = 512U;
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
  Output output{frames};
  clap_process_t process{};
  process.frames_count = frames;
  process.audio_outputs = &output.buffer;
  process.audio_outputs_count = 1U;
  std::vector<float> captured;
  captured.reserve(static_cast<std::size_t>(frames) * 48U * 2U);
  double liveEnergy = 0.0;
  bool processOk = true;
  for (std::uint32_t block = 0U; block < 48U; ++block) {
    EventList events;
    if (block == 0U) events.events = {&noteOn.header};
    if (block == 30U) events.events = {&noteOff.header};
    process.in_events = &events.input;
    const auto processResult = plugin->process(plugin, &process);
    processOk = processOk && processResult != CLAP_PROCESS_ERROR;
    liveEnergy += energy(output.planes[0]);
    for (std::uint32_t frame = 0U; frame < frames; ++frame) {
      captured.push_back(output.planes[0][frame]);
      captured.push_back(output.planes[1][frame]);
    }
  }
  bool audioWritten = audioPath.empty();
  if (!audioPath.empty()) {
    std::filesystem::create_directories(audioPath.parent_path());
    audioWritten = static_cast<bool>(seam::voicebank::writePcm16Wav(
        audioPath, 48000U, 2U, captured));
  }
  const auto livePass = processOk && liveEnergy > 0.01 && audioWritten;

  ReadStream activeLoad{saved.bytes};
  const auto activeLoadRejected = !state->load(plugin, &activeLoad.stream) &&
                                  context.restartRequests == 1U;
  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);

  const auto* restored = factory->create_plugin(factory, &host, descriptor->id);
  if (restored == nullptr || !restored->init(restored)) return 1;
  const auto* restoredState = static_cast<const clap_plugin_state_t*>(
      restored->get_extension(restored, CLAP_EXT_STATE));
  ReadStream stateRead{saved.bytes};
  const auto stateRoundTrip = restoredState != nullptr &&
                              restoredState->load(restored, &stateRead.stream);
  restored->destroy(restored);

  if (guiCreated) {
    if (guiVisible) static_cast<void>(gui->hide(plugin));
    gui->destroy(plugin);
  }
#if defined(__linux__)
  if (display != nullptr) {
    if (parent != 0U) XDestroyWindow(display, parent);
    XCloseDisplay(display);
  }
#endif
  plugin->destroy(plugin);
  entry->deinit();

  const auto passed = guiCreated && guiVisible && livePass &&
                      activeLoadRejected && stateRoundTrip &&
                      (screenshotPath.empty() || screenshotWritten);
  if (!summaryPath.empty()) {
    std::filesystem::create_directories(summaryPath.parent_path());
    std::ofstream summary(summaryPath, std::ios::binary | std::ios::trunc);
    summary << "{\n"
            << "  \"pluginId\": \"com.project-seam.editor\",\n"
            << "  \"guiCreated\": " << (guiCreated ? "true" : "false") << ",\n"
            << "  \"guiVisible\": " << (guiVisible ? "true" : "false") << ",\n"
            << "  \"screenshotWritten\": "
            << (screenshotWritten ? "true" : "false") << ",\n"
            << "  \"noteInputEnergy\": " << liveEnergy << ",\n"
            << "  \"capturedFrames\": " << (captured.size() / 2U) << ",\n"
            << "  \"audioWritten\": " << (audioWritten ? "true" : "false") << ",\n"
            << "  \"activeLoadRejected\": "
            << (activeLoadRejected ? "true" : "false") << ",\n"
            << "  \"stateRoundTrip\": "
            << (stateRoundTrip ? "true" : "false") << ",\n"
            << "  \"restartRequests\": " << context.restartRequests << ",\n"
            << "  \"result\": \"" << (passed ? "PASS" : "FAIL") << "\"\n"
            << "}\n";
  }
  std::cout << "Phase 11 CLAP editor host: "
            << (passed ? "PASS" : "FAIL")
            << ", live energy " << liveEnergy << '\n';
  return passed ? 0 : 1;
}
