/*
 * CLAP - CLever Audio Plugin
 * Copyright (c) 2014...2022 Alexandre BIQUE <bique.alexandre@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Project SEAM vendors a mechanically consolidated subset of the CLAP 1.2.10
 * public C ABI from upstream revision
 * 195b42a004144fab0b3cf95e9c067187d15365b7. Declarations not used by the
 * Phase 10 plug-in are intentionally omitted; field order and types of the
 * included ABI structures are unchanged.
 */
#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(__GNUC__)
#    define CLAP_EXPORT __attribute__((dllexport))
#  else
#    define CLAP_EXPORT __declspec(dllexport)
#  endif
#  define CLAP_ABI __cdecl
#else
#  if defined(__GNUC__) || defined(__clang__)
#    define CLAP_EXPORT __attribute__((visibility("default")))
#  else
#    define CLAP_EXPORT
#  endif
#  define CLAP_ABI
#endif

#ifdef __cplusplus
#  include <cstddef>
#  include <cstdint>
#  define CLAP_CONSTEXPR constexpr
#  define CLAP_NODISCARD [[nodiscard]]
extern "C" {
#else
#  include <stdbool.h>
#  include <stddef.h>
#  include <stdint.h>
#  define CLAP_CONSTEXPR
#  define CLAP_NODISCARD
#endif

typedef struct clap_version { uint32_t major, minor, revision; } clap_version_t;
#define CLAP_VERSION_MAJOR 1
#define CLAP_VERSION_MINOR 2
#define CLAP_VERSION_REVISION 10
#define CLAP_VERSION_INIT {(uint32_t)1, (uint32_t)2, (uint32_t)10}
static const CLAP_CONSTEXPR clap_version_t CLAP_VERSION = CLAP_VERSION_INIT;
static inline CLAP_CONSTEXPR bool clap_version_is_compatible(clap_version_t v) { return v.major >= 1; }

typedef uint32_t clap_id;
static const CLAP_CONSTEXPR clap_id CLAP_INVALID_ID = UINT32_MAX;
enum { CLAP_NAME_SIZE = 256, CLAP_PATH_SIZE = 1024 };

static const CLAP_CONSTEXPR int64_t CLAP_BEATTIME_FACTOR = 1LL << 31;
static const CLAP_CONSTEXPR int64_t CLAP_SECTIME_FACTOR = 1LL << 31;
typedef int64_t clap_beattime;
typedef int64_t clap_sectime;

typedef struct clap_event_header {
  uint32_t size;
  uint32_t time;
  uint16_t space_id;
  uint16_t type;
  uint32_t flags;
} clap_event_header_t;
static const CLAP_CONSTEXPR uint16_t CLAP_CORE_EVENT_SPACE_ID = 0;
enum {
  CLAP_EVENT_NOTE_ON = 0, CLAP_EVENT_NOTE_OFF = 1, CLAP_EVENT_NOTE_CHOKE = 2,
  CLAP_EVENT_NOTE_END = 3, CLAP_EVENT_NOTE_EXPRESSION = 4,
  CLAP_EVENT_PARAM_VALUE = 5, CLAP_EVENT_PARAM_MOD = 6,
  CLAP_EVENT_PARAM_GESTURE_BEGIN = 7, CLAP_EVENT_PARAM_GESTURE_END = 8,
  CLAP_EVENT_TRANSPORT = 9, CLAP_EVENT_MIDI = 10,
  CLAP_EVENT_MIDI_SYSEX = 11, CLAP_EVENT_MIDI2 = 12
};

typedef struct clap_event_param_value {
  clap_event_header_t header;
  clap_id param_id;
  void *cookie;
  int32_t note_id;
  int16_t port_index;
  int16_t channel;
  int16_t key;
  double value;
} clap_event_param_value_t;

enum clap_transport_flags {
  CLAP_TRANSPORT_HAS_TEMPO = 1 << 0,
  CLAP_TRANSPORT_HAS_BEATS_TIMELINE = 1 << 1,
  CLAP_TRANSPORT_HAS_SECONDS_TIMELINE = 1 << 2,
  CLAP_TRANSPORT_HAS_TIME_SIGNATURE = 1 << 3,
  CLAP_TRANSPORT_IS_PLAYING = 1 << 4,
  CLAP_TRANSPORT_IS_RECORDING = 1 << 5,
  CLAP_TRANSPORT_IS_LOOP_ACTIVE = 1 << 6,
  CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL = 1 << 7
};
typedef struct clap_event_transport {
  clap_event_header_t header;
  uint32_t flags;
  clap_beattime song_pos_beats;
  clap_sectime song_pos_seconds;
  double tempo;
  double tempo_inc;
  clap_beattime loop_start_beats;
  clap_beattime loop_end_beats;
  clap_sectime loop_start_seconds;
  clap_sectime loop_end_seconds;
  clap_beattime bar_start;
  int32_t bar_number;
  uint16_t tsig_num;
  uint16_t tsig_denom;
} clap_event_transport_t;

typedef struct clap_input_events {
  void *ctx;
  uint32_t (CLAP_ABI *size)(const struct clap_input_events *list);
  const clap_event_header_t *(CLAP_ABI *get)(const struct clap_input_events *list, uint32_t index);
} clap_input_events_t;
typedef struct clap_output_events {
  void *ctx;
  bool (CLAP_ABI *try_push)(const struct clap_output_events *list, const clap_event_header_t *event);
} clap_output_events_t;

typedef struct clap_audio_buffer {
  float **data32;
  double **data64;
  uint32_t channel_count;
  uint32_t latency;
  uint64_t constant_mask;
} clap_audio_buffer_t;

enum {
  CLAP_PROCESS_ERROR = 0, CLAP_PROCESS_CONTINUE = 1,
  CLAP_PROCESS_CONTINUE_IF_NOT_QUIET = 2, CLAP_PROCESS_TAIL = 3,
  CLAP_PROCESS_SLEEP = 4
};
typedef int32_t clap_process_status;
typedef struct clap_process {
  int64_t steady_time;
  uint32_t frames_count;
  const clap_event_transport_t *transport;
  const clap_audio_buffer_t *audio_inputs;
  clap_audio_buffer_t *audio_outputs;
  uint32_t audio_inputs_count;
  uint32_t audio_outputs_count;
  const clap_input_events_t *in_events;
  const clap_output_events_t *out_events;
} clap_process_t;

typedef struct clap_host {
  clap_version_t clap_version;
  void *host_data;
  const char *name, *vendor, *url, *version;
  const void *(CLAP_ABI *get_extension)(const struct clap_host *host, const char *extension_id);
  void (CLAP_ABI *request_restart)(const struct clap_host *host);
  void (CLAP_ABI *request_process)(const struct clap_host *host);
  void (CLAP_ABI *request_callback)(const struct clap_host *host);
} clap_host_t;

typedef struct clap_plugin_descriptor {
  clap_version_t clap_version;
  const char *id, *name, *vendor, *url, *manual_url, *support_url, *version, *description;
  const char *const *features;
} clap_plugin_descriptor_t;

typedef struct clap_plugin {
  const clap_plugin_descriptor_t *desc;
  void *plugin_data;
  bool (CLAP_ABI *init)(const struct clap_plugin *plugin);
  void (CLAP_ABI *destroy)(const struct clap_plugin *plugin);
  bool (CLAP_ABI *activate)(const struct clap_plugin *plugin, double sample_rate,
                            uint32_t min_frames_count, uint32_t max_frames_count);
  void (CLAP_ABI *deactivate)(const struct clap_plugin *plugin);
  bool (CLAP_ABI *start_processing)(const struct clap_plugin *plugin);
  void (CLAP_ABI *stop_processing)(const struct clap_plugin *plugin);
  void (CLAP_ABI *reset)(const struct clap_plugin *plugin);
  clap_process_status (CLAP_ABI *process)(const struct clap_plugin *plugin, const clap_process_t *process);
  const void *(CLAP_ABI *get_extension)(const struct clap_plugin *plugin, const char *id);
  void (CLAP_ABI *on_main_thread)(const struct clap_plugin *plugin);
} clap_plugin_t;

typedef struct clap_plugin_entry {
  clap_version_t clap_version;
  bool (CLAP_ABI *init)(const char *plugin_path);
  void (CLAP_ABI *deinit)(void);
  const void *(CLAP_ABI *get_factory)(const char *factory_id);
} clap_plugin_entry_t;
CLAP_EXPORT extern const clap_plugin_entry_t clap_entry;

typedef struct clap_plugin_factory {
  uint32_t (CLAP_ABI *get_plugin_count)(const struct clap_plugin_factory *factory);
  const clap_plugin_descriptor_t *(CLAP_ABI *get_plugin_descriptor)(const struct clap_plugin_factory *factory, uint32_t index);
  const clap_plugin_t *(CLAP_ABI *create_plugin)(const struct clap_plugin_factory *factory,
                                                 const clap_host_t *host, const char *plugin_id);
} clap_plugin_factory_t;
static const CLAP_CONSTEXPR char CLAP_PLUGIN_FACTORY_ID[] = "clap.plugin-factory";

typedef struct clap_istream {
  void *ctx;
  int64_t (CLAP_ABI *read)(const struct clap_istream *stream, void *buffer, uint64_t size);
} clap_istream_t;
typedef struct clap_ostream {
  void *ctx;
  int64_t (CLAP_ABI *write)(const struct clap_ostream *stream, const void *buffer, uint64_t size);
} clap_ostream_t;

static const CLAP_CONSTEXPR char CLAP_EXT_AUDIO_PORTS[] = "clap.audio-ports";
static const CLAP_CONSTEXPR char CLAP_PORT_MONO[] = "mono";
static const CLAP_CONSTEXPR char CLAP_PORT_STEREO[] = "stereo";
enum { CLAP_AUDIO_PORT_IS_MAIN = 1 << 0, CLAP_AUDIO_PORT_SUPPORTS_64BITS = 1 << 1,
       CLAP_AUDIO_PORT_PREFERS_64BITS = 1 << 2,
       CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE = 1 << 3 };
typedef struct clap_audio_port_info {
  clap_id id;
  char name[CLAP_NAME_SIZE];
  uint32_t flags;
  uint32_t channel_count;
  const char *port_type;
  clap_id in_place_pair;
} clap_audio_port_info_t;
typedef struct clap_plugin_audio_ports {
  uint32_t (CLAP_ABI *count)(const clap_plugin_t *plugin, bool is_input);
  bool (CLAP_ABI *get)(const clap_plugin_t *plugin, uint32_t index, bool is_input, clap_audio_port_info_t *info);
} clap_plugin_audio_ports_t;

enum { CLAP_PARAM_IS_STEPPED = 1 << 0, CLAP_PARAM_IS_PERIODIC = 1 << 1,
       CLAP_PARAM_IS_HIDDEN = 1 << 2, CLAP_PARAM_IS_READONLY = 1 << 3,
       CLAP_PARAM_IS_BYPASS = 1 << 4, CLAP_PARAM_IS_AUTOMATABLE = 1 << 5,
       CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID = 1 << 6,
       CLAP_PARAM_IS_AUTOMATABLE_PER_KEY = 1 << 7,
       CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL = 1 << 8,
       CLAP_PARAM_IS_AUTOMATABLE_PER_PORT = 1 << 9,
       CLAP_PARAM_IS_MODULATABLE = 1 << 10,
       CLAP_PARAM_REQUIRES_PROCESS = 1 << 15,
       CLAP_PARAM_IS_ENUM = 1 << 16 };
typedef uint32_t clap_param_info_flags;
typedef struct clap_param_info {
  clap_id id;
  clap_param_info_flags flags;
  void *cookie;
  char name[CLAP_NAME_SIZE];
  char module[CLAP_PATH_SIZE];
  double min_value, max_value, default_value;
} clap_param_info_t;
typedef struct clap_plugin_params {
  uint32_t (CLAP_ABI *count)(const clap_plugin_t *plugin);
  bool (CLAP_ABI *get_info)(const clap_plugin_t *plugin, uint32_t param_index, clap_param_info_t *param_info);
  bool (CLAP_ABI *get_value)(const clap_plugin_t *plugin, clap_id param_id, double *out_value);
  bool (CLAP_ABI *value_to_text)(const clap_plugin_t *plugin, clap_id param_id, double value,
                                 char *out_buffer, uint32_t out_buffer_capacity);
  bool (CLAP_ABI *text_to_value)(const clap_plugin_t *plugin, clap_id param_id,
                                 const char *param_value_text, double *out_value);
  void (CLAP_ABI *flush)(const clap_plugin_t *plugin, const clap_input_events_t *in,
                         const clap_output_events_t *out);
} clap_plugin_params_t;
static const CLAP_CONSTEXPR char CLAP_EXT_PARAMS[] = "clap.params";

typedef struct clap_plugin_state {
  bool (CLAP_ABI *save)(const clap_plugin_t *plugin, const clap_ostream_t *stream);
  bool (CLAP_ABI *load)(const clap_plugin_t *plugin, const clap_istream_t *stream);
} clap_plugin_state_t;
static const CLAP_CONSTEXPR char CLAP_EXT_STATE[] = "clap.state";

typedef struct clap_plugin_latency { uint32_t (CLAP_ABI *get)(const clap_plugin_t *plugin); } clap_plugin_latency_t;
static const CLAP_CONSTEXPR char CLAP_EXT_LATENCY[] = "clap.latency";
typedef struct clap_plugin_tail { uint32_t (CLAP_ABI *get)(const clap_plugin_t *plugin); } clap_plugin_tail_t;
static const CLAP_CONSTEXPR char CLAP_EXT_TAIL[] = "clap.tail";
enum { CLAP_RENDER_REALTIME = 0, CLAP_RENDER_OFFLINE = 1 };
typedef int32_t clap_plugin_render_mode;
typedef struct clap_plugin_render {
  bool (CLAP_ABI *has_hard_realtime_requirement)(const clap_plugin_t *plugin);
  bool (CLAP_ABI *set)(const clap_plugin_t *plugin, clap_plugin_render_mode mode);
} clap_plugin_render_t;
static const CLAP_CONSTEXPR char CLAP_EXT_RENDER[] = "clap.render";

#define CLAP_PLUGIN_FEATURE_INSTRUMENT "instrument"
#define CLAP_PLUGIN_FEATURE_SYNTHESIZER "synthesizer"
#define CLAP_PLUGIN_FEATURE_SAMPLER "sampler"
#define CLAP_PLUGIN_FEATURE_STEREO "stereo"
#define CLAP_PLUGIN_FEATURE_SURROUND "surround"

#ifdef __cplusplus
} // extern "C"
#endif
