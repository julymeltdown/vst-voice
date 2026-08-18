#include <clap/clap.h>
#include <clap/events.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#if defined(__linux__)
#include <dlfcn.h>
#endif
namespace {
struct Host {
  clap_host_t host{};
  Host() {
    host.clap_version = CLAP_VERSION_INIT;
    host.host_data = this;
    host.name = "Project SEAM Phase12C Host";
    host.vendor = "Project SEAM";
    host.url = "";
    host.version = "0.12.0";
    host.get_extension = [](const clap_host_t*, const char*) -> const void* { return nullptr; };
    host.request_restart = [](const clap_host_t*) {};
    host.request_process = [](const clap_host_t*) {};
    host.request_callback = [](const clap_host_t*) {};
  }
};
struct InputEvents {
  std::vector<std::array<std::uint8_t, sizeof(clap_event_note_expression_t)>> storage;
  std::vector<const clap_event_header_t*> events;
  clap_input_events_t iface{};
  InputEvents() {
    storage.reserve(16);
    events.reserve(16);
    iface.ctx = this;
    iface.size = [](const clap_input_events_t* x) {
      return static_cast<std::uint32_t>(static_cast<const InputEvents*>(x->ctx)->events.size());
    };
    iface.get = [](const clap_input_events_t* x, std::uint32_t i) -> const clap_event_header_t* {
      const auto* self = static_cast<const InputEvents*>(x->ctx);
      return i < self->events.size() ? self->events[i] : nullptr;
    };
  }
  void note(std::uint16_t type, std::uint32_t time, int id, int key, double velocity) {
    storage.emplace_back(); auto& b = storage.back(); b.fill(0);
    auto* e = reinterpret_cast<clap_event_note_t*>(b.data());
    e->header = {sizeof(*e), time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
    e->note_id = id; e->port_index = 0; e->channel = 0; e->key = static_cast<std::int16_t>(key); e->velocity = velocity;
    events.push_back(&e->header);
  }
  void expression(std::uint32_t time, int id, int key, int expression, double value) {
    storage.emplace_back(); auto& b = storage.back(); b.fill(0);
    auto* e = reinterpret_cast<clap_event_note_expression_t*>(b.data());
    e->header = {sizeof(*e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0};
    e->expression_id = expression; e->note_id = id; e->port_index = 0; e->channel = 0; e->key = static_cast<std::int16_t>(key); e->value = value;
    events.push_back(&e->header);
  }
  void midi(std::uint32_t time, std::uint8_t a, std::uint8_t b, std::uint8_t c) {
    storage.emplace_back(); auto& bytes = storage.back(); bytes.fill(0);
    auto* e = reinterpret_cast<clap_event_midi_t*>(bytes.data());
    e->header = {sizeof(*e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
    e->port_index = 0; e->data[0] = a; e->data[1] = b; e->data[2] = c;
    events.push_back(&e->header);
  }
};
}
int main(int argc, char** argv) {
#if !defined(__linux__)
  (void)argc; (void)argv; return 77;
#else
  if (argc < 2) { std::cerr << "plugin path required\n"; return 2; }
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); if (!lib) { std::cerr << dlerror() << '\n'; return 3; }
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  if (!entry || !entry->init(argv[1])) return 4;
  auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID)); if (!factory) return 5;
  Host host; const auto* plugin = factory->create_plugin(factory, &host.host, "com.project-seam.live-phase12c");
  if (!plugin || !plugin->init(plugin)) return 6;
  auto* notes = static_cast<const clap_plugin_note_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
  clap_note_port_info_t ni{}; if (!notes || !notes->get(plugin, 0, true, &ni) || (ni.supported_dialects & CLAP_NOTE_DIALECT_MIDI) == 0) return 7;
  if (!plugin->activate(plugin, 48000.0, 16, 1024) || !plugin->start_processing(plugin)) return 8;
  constexpr std::uint32_t frames = 512; std::array<float, frames> left{}, right{}; float* channels[2] = {left.data(), right.data()};
  clap_audio_buffer_t output{}; output.data32 = channels; output.channel_count = 2;
  InputEvents ev; ev.note(CLAP_EVENT_NOTE_ON, 64, 1, 60, 0.9); ev.expression(200, 1, 60, CLAP_NOTE_EXPRESSION_TUNING, 2.0); ev.note(CLAP_EVENT_NOTE_OFF, 400, 1, 60, 0.0);
  clap_process_t process{}; process.frames_count = frames; process.in_events = &ev.iface; process.audio_outputs = &output; process.audio_outputs_count = 1;
  const auto status = plugin->process(plugin, &process); double pre = 0, post = 0; for (std::uint32_t i=0;i<64;++i) pre += std::abs(left[i]); for (std::uint32_t i=64;i<frames;++i) post += std::abs(left[i]);
  InputEvents midi; midi.midi(0, 0x90, 67, 100); midi.midi(200, 0xE0, 0x7f, 0x7f); midi.midi(400, 0x80, 67, 0); process.in_events = &midi.iface; left.fill(0); right.fill(0); const auto midiStatus = plugin->process(plugin, &process); double midiEnergy = 0; for(float x:left)midiEnergy += std::abs(x);
  plugin->stop_processing(plugin); plugin->deactivate(plugin); plugin->destroy(plugin); entry->deinit(); dlclose(lib);
  const bool pass = status != CLAP_PROCESS_ERROR && midiStatus != CLAP_PROCESS_ERROR && pre == 0.0 && post > 1.0 && midiEnergy > 1.0;
  if (argc > 2) { std::ofstream out(argv[2]); out << "{\n  \"preEventEnergy\": " << pre << ",\n  \"clapEventEnergy\": " << post << ",\n  \"midi1Energy\": " << midiEnergy << ",\n  \"result\": \"" << (pass?"PASS":"FAIL") << "\"\n}\n"; }
  std::cout << "pre=" << pre << " clap=" << post << " midi=" << midiEnergy << " result=" << (pass?"PASS":"FAIL") << '\n'; return pass?0:9;
#endif
}
