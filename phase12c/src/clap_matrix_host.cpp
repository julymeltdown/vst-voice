#include "seam/phase12c/clap_matrix_host.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"
#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
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
#else
#include <dlfcn.h>
#endif

namespace seam::phase12c {
namespace {
using Json = formats::JsonValue;
constexpr std::size_t kMaximumState = 16U * 1024U * 1024U + 48U;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error{message};
}

class Module final {
public:
  explicit Module(const std::filesystem::path& path) {
#if defined(_WIN32)
    handle_ = LoadLibraryW(path.wstring().c_str());
#else
    auto executable = path;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(executable)) executable /= "Contents/MacOS/ProjectSEAMEditor";
#endif
    handle_ = dlopen(executable.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    require(handle_ != nullptr, "Cannot load supplied CLAP binary");
  }
  Module(const Module&) = delete;
  Module& operator=(const Module&) = delete;
  const clap_plugin_entry_t* entry() const {
#if defined(_WIN32)
    return reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(handle_, "clap_entry"));
#else
    return static_cast<const clap_plugin_entry_t*>(dlsym(handle_, "clap_entry"));
#endif
  }
  ~Module() {
#if defined(_WIN32)
    if (handle_) FreeLibrary(handle_);
#else
    if (handle_) dlclose(handle_);
#endif
  }
private:
#if defined(_WIN32)
  HMODULE handle_{};
#else
  void* handle_{};
#endif
};

struct WriteStream {
  std::vector<std::byte> bytes;
  clap_ostream_t stream{};
  WriteStream() {
    stream.ctx = this;
    stream.write = [](const clap_ostream_t* base, const void* data, std::uint64_t size) -> std::int64_t {
      auto& self = *static_cast<WriteStream*>(base->ctx);
      if (size > kMaximumState || self.bytes.size() > kMaximumState - static_cast<std::size_t>(size)) return -1;
      const auto count = std::min<std::size_t>(31U, static_cast<std::size_t>(size));
      const auto* first = static_cast<const std::byte*>(data);
      self.bytes.insert(self.bytes.end(), first, first + count);
      return static_cast<std::int64_t>(count);
    };
  }
};
struct ReadStream {
  std::span<const std::byte> bytes;
  std::size_t cursor{};
  clap_istream_t stream{};
  explicit ReadStream(std::span<const std::byte> source) : bytes(source) {
    stream.ctx = this;
    stream.read = [](const clap_istream_t* base, void* target, std::uint64_t size) -> std::int64_t {
      auto& self = *static_cast<ReadStream*>(base->ctx);
      const auto count = std::min<std::uint64_t>({size, 29U, self.bytes.size() - self.cursor});
      if (count) std::memcpy(target, self.bytes.data() + self.cursor, static_cast<std::size_t>(count));
      self.cursor += static_cast<std::size_t>(count);
      return static_cast<std::int64_t>(count);
    };
  }
};

std::uint32_t read32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t result{};
  for (std::size_t index = 0; index < 4; ++index) {
    result |= std::to_integer<std::uint32_t>(bytes[offset + index]) << (index * 8U);
  }
  return result;
}
domain::Project decodeState(std::span<const std::byte> bytes) {
  require(bytes.size() >= 48U && bytes.size() <= kMaximumState, "State envelope exceeds bounds");
  require(std::memcmp(bytes.data(), "SEAMED11", 8U) == 0 && read32(bytes, 8U) == 1U,
      "Unexpected plugin state format");
  require(read32(bytes, 12U) == bytes.size() - 48U, "Plugin state length mismatch");
  const auto payload = bytes.subspan(48U);
  core::Sha256 digest;
  digest.update(payload);
  const auto actual = digest.digest();
  require(std::equal(actual.begin(), actual.end(), bytes.begin() + 16), "Plugin state digest mismatch");
  auto decoded = formats::ProjectJsonCodec{}.decode(
      {reinterpret_cast<const char*>(payload.data()), payload.size()});
  require(static_cast<bool>(decoded), "Cannot decode plugin project state");
  return std::move(decoded).value();
}
std::vector<std::byte> encodeState(const domain::Project& project) {
  const auto encoded = formats::ProjectJsonCodec{}.encode(project);
  require(static_cast<bool>(encoded) && encoded.value().size() <= kMaximumState - 48U,
      "Cannot encode matrix project");
  const auto& json = encoded.value();
  std::vector<std::byte> bytes(48U + json.size());
  std::memcpy(bytes.data(), "SEAMED11", 8U);
  const auto write32 = [&](std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  };
  write32(8U, 1U);
  write32(12U, static_cast<std::uint32_t>(json.size()));
  core::Sha256 digest;
  digest.update(json);
  const auto hash = digest.digest();
  std::copy(hash.begin(), hash.end(), bytes.begin() + 16);
  std::memcpy(bytes.data() + 48U, json.data(), json.size());
  return bytes;
}

class Plugin final {
public:
  Plugin(const std::filesystem::path& path, const CanonicalEvidenceIdentity& identity)
      : module_(path), identity_(identity) {
    host_ = {CLAP_VERSION, this, "SEAM binary matrix", "Project SEAM", "", "1",
        &extension, &request, &request, &callback};
    entry_ = module_.entry();
    require(entry_ && entry_->init && entry_->deinit && entry_->get_factory, "CLAP entry is missing");
    require(entry_->init(path.string().c_str()), "CLAP entry initialization failed");
    entryReady_ = true;
    try {
      const auto* factory = static_cast<const clap_plugin_factory_t*>(entry_->get_factory(CLAP_PLUGIN_FACTORY_ID));
      require(factory && factory->get_plugin_count && factory->get_plugin_descriptor && factory->create_plugin,
          "CLAP plugin factory is missing");
      const clap_plugin_descriptor_t* descriptor = nullptr;
      const auto count = factory->get_plugin_count(factory);
      require(count > 0U && count <= 64U, "Unexpected plugin descriptor count");
      for (std::uint32_t index = 0; index < count; ++index) {
        const auto* candidate = factory->get_plugin_descriptor(factory, index);
        if (candidate && candidate->id && std::strcmp(candidate->id, "com.project-seam.editor") == 0) descriptor = candidate;
      }
      require(descriptor != nullptr, "Supplied binary is not the canonical editor");
      plugin_ = factory->create_plugin(factory, &host_, descriptor->id);
      require(plugin_ && plugin_->init && plugin_->destroy && plugin_->get_extension &&
          plugin_->activate && plugin_->deactivate && plugin_->start_processing &&
          plugin_->stop_processing && plugin_->reset && plugin_->process,
          "Plugin lifecycle is incomplete");
      require(plugin_->init(plugin_), "Plugin initialization failed");
      state_ = static_cast<const clap_plugin_state_t*>(plugin_->get_extension(plugin_, CLAP_EXT_STATE));
      config_ = static_cast<const clap_plugin_audio_ports_config_t*>(plugin_->get_extension(plugin_, CLAP_EXT_AUDIO_PORTS_CONFIG));
      ports_ = static_cast<const clap_plugin_audio_ports_t*>(plugin_->get_extension(plugin_, CLAP_EXT_AUDIO_PORTS));
      require(state_ && state_->save && state_->load && config_ && config_->select &&
          ports_ && ports_->get && ports_->count, "Required state/audio-port extensions are missing");
      auto project = decodeState(save());
      require(!project.vocalTracks().empty() && !project.vocalTracks().front().regions.empty(),
          "Plugin has no initial vocal track and region");
      project.vocalTracks().resize(1U);
      project.audioTracks().clear();
      project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
      auto& track = project.vocalTracks().front();
      track.voicebank = {identity.voicebankId, identity.voicebankVersion, identity.voicebankContentHash};
      track.styleSelection = {domain::VoiceStyleOrigin::Explicit, identity.style};
      track.proceduralRecipe.reset();
      track.character = {};
      track.regions.resize(1U);
      auto& region = track.regions.front();
      region.notes.clear(); region.lyrics.clear();
      region.phonemeOverrides.clear(); region.unitSelectionOverrides.clear(); region.seamOverrides.clear();
      region.performance = {}; region.pitchAutomation = {}; region.dynamicsAutomation = {};
      load(encodeState(project));
      require(roundTrip(), "Initial bank-bound state round trip failed");
    } catch (...) {
      close();
      throw;
    }
  }
  ~Plugin() { close(); }
  Plugin(const Plugin&) = delete;
  Plugin& operator=(const Plugin&) = delete;
  const clap_plugin_t* value() const { return plugin_; }
  bool select(std::uint32_t channels) {
    if (!config_->select(plugin_, channels)) return false;
    clap_audio_port_info_t port{};
    return ports_->count(plugin_, false) == 1U && ports_->get(plugin_, 0U, false, &port) &&
        port.channel_count == channels;
  }
  bool roundTrip() {
    const auto first = save();
    load(first);
    const auto second = save();
    const auto project = decodeState(second);
    if (project.vocalTracks().size() != 1U) return false;
    const auto& track = project.vocalTracks().front();
    return first == second &&
        track.voicebank == domain::VoicebankReference{identity_.voicebankId, identity_.voicebankVersion, identity_.voicebankContentHash} &&
        track.styleSelection.styleId == identity_.style &&
        track.regions.size() == 1U && track.regions.front().notes.empty();
  }
  void mainCallback() {
    if (callbackPending_.exchange(false) && plugin_->on_main_thread) plugin_->on_main_thread(plugin_);
  }
private:
  static const void* CLAP_ABI extension(const clap_host_t*, const char*) { return nullptr; }
  static void CLAP_ABI request(const clap_host_t*) {}
  static void CLAP_ABI callback(const clap_host_t* host) {
    static_cast<Plugin*>(host->host_data)->callbackPending_.store(true);
  }
  std::vector<std::byte> save() {
    WriteStream output;
    require(state_->save(plugin_, &output.stream), "Plugin state save failed");
    return std::move(output.bytes);
  }
  void load(std::span<const std::byte> bytes) {
    ReadStream input{bytes};
    require(state_->load(plugin_, &input.stream), "Plugin state load failed");
  }
  void close() noexcept {
    if (plugin_ && plugin_->destroy) plugin_->destroy(plugin_);
    plugin_ = nullptr;
    if (entryReady_) entry_->deinit();
    entryReady_ = false;
  }
  Module module_;
  CanonicalEvidenceIdentity identity_;
  clap_host_t host_{};
  const clap_plugin_entry_t* entry_{};
  const clap_plugin_t* plugin_{};
  const clap_plugin_state_t* state_{};
  const clap_plugin_audio_ports_config_t* config_{};
  const clap_plugin_audio_ports_t* ports_{};
  std::atomic<bool> callbackPending_{false};
  bool entryReady_{};
};

// Independent published wire layout: do not build test events using the
// consolidated declaration under test. These offsets are pinned CLAP 1.2.10.
struct WireExpression {
  clap_event_header_t header{};
  std::int32_t expressionId{};
  std::int32_t noteId{-1};
  std::int16_t port{0}, channel{0}, key{60};
  double value{};
};
static_assert(sizeof(WireExpression) == 40U);
static_assert(offsetof(WireExpression, expressionId) == 16U);
static_assert(offsetof(WireExpression, noteId) == 20U);
static_assert(offsetof(WireExpression, value) == 32U);

struct EventList {
  std::array<const clap_event_header_t*, 4> pointers{};
  std::uint32_t count{};
  clap_input_events_t input{};
  EventList() {
    input.ctx = this;
    input.size = [](const clap_input_events_t* list) { return static_cast<const EventList*>(list->ctx)->count; };
    input.get = [](const clap_input_events_t* list, std::uint32_t index) -> const clap_event_header_t* {
      const auto& self = *static_cast<const EventList*>(list->ctx);
      return index < self.count ? self.pointers[index] : nullptr;
    };
  }
  void add(const clap_event_header_t* event) { pointers[count++] = event; }
};
struct AudioResult {
  bool finite{true}, preNoteSilent{true}, releaseSilent{true}, buffersIntact{true};
  std::array<double, 8> energies{};
  std::vector<float> firstChannel;
  std::uint64_t frames{}, processCalls{};
};

AudioResult render(const clap_plugin_t* plugin, std::uint32_t rate,
    std::uint32_t frames, std::uint32_t channels, bool midi, double bend, double pan) {
  plugin->reset(plugin);
  AudioResult result;
  std::array<std::vector<float>, 8> planes;
  std::array<float*, 8> pointers{};
  constexpr float guard = 12345.0F;
  for (std::uint32_t channel = 0; channel < channels; ++channel) {
    planes[channel].resize(frames + 2U, guard);
    pointers[channel] = planes[channel].data() + 1U;
  }
  clap_audio_buffer_t buffer{};
  buffer.data32 = pointers.data();
  buffer.channel_count = channels;
  clap_event_transport_t transport{};
  transport.header.size = sizeof(transport);
  transport.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE; // stopped: no pre-rendered score audio
  const auto offBlock = (rate / 10U + frames - 1U) / frames + 1U;
  const auto blocks = offBlock + (rate / 20U + frames - 1U) / frames + 2U;
  result.firstChannel.reserve(static_cast<std::size_t>(blocks) * frames);
  for (std::uint32_t block = 0; block < blocks; ++block) {
    EventList events;
    const auto offset = frames / 2U;
    clap_event_note_t note{};
    note.header = {sizeof(note), offset, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0U};
    note.note_id = 1079; note.port_index = 0; note.channel = 0; note.key = 60; note.velocity = 0.8;
    WireExpression tuning{{sizeof(WireExpression), offset, 0U, 4U, 0U}, 2, 1079, 0, 0, 60, bend};
    WireExpression balance{{sizeof(WireExpression), offset, 0U, 4U, 0U}, 1, 1079, 0, 0, 60, pan};
    clap_event_midi_t midiNote{};
    midiNote.header = {sizeof(midiNote), offset, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0U};
    midiNote.data[0] = 0x90U; midiNote.data[1] = 60U; midiNote.data[2] = 100U;
    auto midiBend = midiNote;
    const auto bendValue = static_cast<std::uint16_t>(8192.0 + bend * 4096.0);
    midiBend.data[0] = 0xe0U;
    midiBend.data[1] = static_cast<std::uint8_t>(bendValue & 0x7fU);
    midiBend.data[2] = static_cast<std::uint8_t>(bendValue >> 7U);
    auto midiPan = midiNote;
    midiPan.data[0] = 0xb0U; midiPan.data[1] = 10U;
    midiPan.data[2] = static_cast<std::uint8_t>(std::lround(pan * 127.0));
    if (block == 0U) {
      if (midi) { events.add(&midiNote.header); events.add(&midiBend.header); events.add(&midiPan.header); }
      else { events.add(&note.header); events.add(&tuning.header); events.add(&balance.header); }
    } else if (block == offBlock) {
      if (midi) { midiNote.data[0] = 0x80U; midiNote.data[2] = 0U; events.add(&midiNote.header); }
      else { note.header.type = CLAP_EVENT_NOTE_OFF; events.add(&note.header); }
    }
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      std::fill_n(pointers[channel], frames, std::numeric_limits<float>::quiet_NaN());
    }
    clap_process_t process{};
    process.steady_time = static_cast<std::int64_t>(result.frames);
    process.frames_count = frames; process.transport = &transport;
    process.audio_outputs = &buffer; process.audio_outputs_count = 1U;
    process.in_events = &events.input;
    const auto status = plugin->process(plugin, &process);
    require(status != CLAP_PROCESS_ERROR, "CLAP process returned ERROR");
    require(block >= offBlock || status != CLAP_PROCESS_SLEEP,
        "Plugin requested SLEEP while a held note still requires processing");
    ++result.processCalls; result.frames += frames;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      result.buffersIntact = result.buffersIntact && planes[channel].front() == guard && planes[channel].back() == guard;
      for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto sample = pointers[channel][frame];
        result.finite = result.finite && std::isfinite(sample);
        if (std::isfinite(sample)) result.energies[channel] += std::abs(static_cast<double>(sample));
        if (block == 0U && frame < offset) result.preNoteSilent = result.preNoteSilent && sample == 0.0F;
        if (block + 1U == blocks) result.releaseSilent = result.releaseSilent && std::abs(sample) < 1.0e-7F;
      }
    }
    result.firstChannel.insert(result.firstChannel.end(), pointers[0], pointers[0] + frames);
  }
  return result;
}

bool boundedEventAdmission(const clap_plugin_t* plugin, std::uint32_t frames,
    std::uint32_t channels) {
  std::uint32_t gets{};
  clap_input_events_t events{
      &gets,
      [](const clap_input_events_t*) -> std::uint32_t { return 1'000'000U; },
      [](const clap_input_events_t* list, std::uint32_t) -> const clap_event_header_t* {
        ++*static_cast<std::uint32_t*>(list->ctx); return nullptr;
      }};
  std::array<std::vector<float>, 8> planes;
  std::array<float*, 8> pointers{};
  for (std::uint32_t channel = 0; channel < channels; ++channel) {
    planes[channel].resize(frames, 123.0F); pointers[channel] = planes[channel].data();
  }
  clap_audio_buffer_t buffer{}; buffer.data32 = pointers.data(); buffer.channel_count = channels;
  clap_event_transport_t transport{}; transport.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
  clap_process_t process{}; process.steady_time = -1; process.frames_count = frames; process.transport = &transport;
  process.audio_outputs = &buffer; process.audio_outputs_count = 1U; process.in_events = &events;
  if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR || gets != 0U) return false;
  for (std::uint32_t channel = 0; channel < channels; ++channel) {
    for (const auto sample : planes[channel]) if (sample != 0.0F) return false;
  }
  return true;
}

Json runRow(Plugin& host, std::uint32_t rate, std::uint32_t frames,
    std::uint32_t channels, bool midi) {
  Json::Array errors;
  const auto check = [&](bool value, const char* message) { if (!value) errors.emplace_back(message); };
  bool channelCount{}, stateRoundTrip{}, finite{}, preSilent{}, released{}, bent{}, panIsolated{}, eventBounded{};
  double energy{};
  std::uint64_t processCalls{}, renderedFrames{};
  const auto* plugin = host.value();
  bool active = false;
  try {
    host.mainCallback();
    channelCount = host.select(channels);
    check(channelCount, "Audio port configuration mismatch");
    require(channelCount, "Cannot select output layout");
    require(plugin->activate(plugin, rate, frames, frames), "CLAP activation failed");
    active = true;
    std::exception_ptr audioError;
    std::jthread worker{[&] {
      bool processing = false;
      try {
        require(plugin->start_processing(plugin), "CLAP start_processing failed");
        processing = true;
        const auto neutral = render(plugin, rate, frames, channels, midi, 0.0, 0.5);
        const auto tuning = render(plugin, rate, frames, channels, midi, 1.0, 0.5);
        const auto left = render(plugin, rate, frames, channels, midi, 0.0, 0.0);
        const auto right = render(plugin, rate, frames, channels, midi, 0.0, 1.0);
        finite = neutral.finite && tuning.finite && left.finite && right.finite;
        preSilent = neutral.preNoteSilent && tuning.preNoteSilent && left.preNoteSilent && right.preNoteSilent;
        released = neutral.releaseSilent && tuning.releaseSilent && left.releaseSilent && right.releaseSilent;
        channelCount = channelCount && neutral.buffersIntact && tuning.buffersIntact && left.buffersIntact && right.buffersIntact;
        for (const auto value : neutral.energies) energy += value;
        double difference{};
        for (std::size_t index = 0; index < neutral.firstChannel.size(); ++index) {
          if (std::isfinite(neutral.firstChannel[index]) && std::isfinite(tuning.firstChannel[index])) {
            difference += std::abs(static_cast<double>(neutral.firstChannel[index] - tuning.firstChannel[index]));
          }
        }
        bent = difference > 1.0e-5;
        panIsolated = channels == 1U
            ? left.energies[0] > 0.0 && std::abs(left.energies[0] - right.energies[0]) < left.energies[0] * 1.0e-5
            : left.energies[0] > 0.0 && right.energies[1] > 0.0 &&
                left.energies[1] < left.energies[0] * 1.0e-5 &&
                right.energies[0] < right.energies[1] * 1.0e-5;
        renderedFrames = neutral.frames + tuning.frames + left.frames + right.frames;
        processCalls = neutral.processCalls + tuning.processCalls + left.processCalls + right.processCalls;
        eventBounded = boundedEventAdmission(plugin, frames, channels);
        ++processCalls; renderedFrames += frames;
      } catch (...) { audioError = std::current_exception(); }
      if (processing) plugin->stop_processing(plugin);
    }};
    worker.join();
    plugin->deactivate(plugin); active = false;
    if (audioError) std::rethrow_exception(audioError);
    stateRoundTrip = host.roundTrip();
    check(finite, "Nonfinite audio");
    check(energy > 0.0, "Silent live output");
    check(preSilent, "Audio started before note event");
    check(released, "Note release left audible output");
    check(bent, "Pitch bend did not change rendered samples");
    check(panIsolated, "Pan lost channel isolation or muted mono");
    check(eventBounded, "Oversized host event list was traversed");
    check(stateRoundTrip, "State round trip or resource identity mismatch");
    check(channelCount, "Audio buffer guard overwritten");
  } catch (const std::exception& error) { errors.emplace_back(error.what()); }
  if (active) plugin->deactivate(plugin);
  const auto passed = errors.empty();
  return Json::Object{
      {"sampleRate", static_cast<std::int64_t>(rate)}, {"blockFrames", static_cast<std::int64_t>(frames)},
      {"channels", static_cast<std::int64_t>(channels)}, {"dialect", midi ? "midi1" : "clap"},
      {"result", passed ? "PASS" : "FAIL"}, {"finite", finite}, {"absoluteEnergy", energy},
      {"processCalls", static_cast<std::int64_t>(processCalls)}, {"renderedFrames", static_cast<std::int64_t>(renderedFrames)},
      {"preNoteSilent", preSilent}, {"releaseSilent", released}, {"channelCountVerified", channelCount},
      {"stateRoundTrip", stateRoundTrip}, {"pitchBendChanged", bent}, {"panChannelIsolation", panIsolated},
      {"eventAdmissionBounded", eventBounded},
      {"processingStatusVerified", passed},
      {"errors", std::move(errors)}};
}
} // namespace

formats::JsonValue runClapProcessMatrix(const std::filesystem::path& plugin,
    const std::filesystem::path& bank, const CanonicalEvidenceIdentity& identity,
    bool developmentFixture) {
#if defined(_WIN32)
  require(_putenv_s("SEAM_TARGET_RUNTIME_FIXTURE_ROOT", developmentFixture ? bank.string().c_str() : "") == 0,
      "Cannot set fixture admission mode");
  if (!developmentFixture) require(_putenv_s("SEAM_VOICEBANK_PATH", bank.string().c_str()) == 0, "Cannot set installed bank root");
#else
  if (developmentFixture) require(setenv("SEAM_TARGET_RUNTIME_FIXTURE_ROOT", bank.c_str(), 1) == 0, "Cannot set fixture root");
  else {
    require(unsetenv("SEAM_TARGET_RUNTIME_FIXTURE_ROOT") == 0, "Cannot disable fixture admission");
    require(setenv("SEAM_VOICEBANK_PATH", bank.c_str(), 1) == 0, "Cannot set installed bank root");
  }
#endif
  Plugin host{plugin, identity};
  Json::Array rows;
  for (const auto rate : {44100U, 48000U, 88200U, 96000U, 176400U, 192000U}) {
    for (const auto frames : {16U, 32U, 64U, 128U, 256U, 512U, 1024U}) {
      for (const auto channels : {1U, 2U, 4U, 8U}) {
        for (const bool midi : {false, true}) rows.push_back(runRow(host, rate, frames, channels, midi));
      }
    }
  }
  return rows;
}
} // namespace seam::phase12c
