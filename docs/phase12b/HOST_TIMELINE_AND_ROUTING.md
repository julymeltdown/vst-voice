# Host Timeline and Multichannel Routing

## Timeline authority

`HostTimelineMapper` converts host transport state into a source-frame position.
Its authority order is:

1. seconds timeline when supplied by the host;
2. beats timeline using the current host tempo;
3. the plug-in free-running cursor when no host position exists.

The project-level `hostStartOffsetTick` is converted through the project's tempo
map and subtracted before source lookup. Positions before that offset are valid
pre-roll and produce silence. Host loops are wrapped in host time before the
project offset is applied. Time-signature values are retained as transport
metadata but do not change PCM by themselves.

## Offline render

The CLAP render extension maps:

```text
CLAP_RENDER_REALTIME → RenderQuality::Preview
CLAP_RENDER_OFFLINE  → RenderQuality::Final
```

Changing render quality schedules a new immutable project render. No synthesis,
project parsing or Voicebank resolution occurs in the audio callback.

## Project renderer

`ProductionProjectRenderer` receives the whole validated Project plus one exact
Voicebank source per vocal track. It:

1. renders every audible vocal region with `ProductionRegionRenderer`;
2. applies track mute/solo/gain and pan;
3. places region PCM at absolute project sample positions;
4. feeds those clips into the Phase 6 `RoutedPlaybackTimeline`;
5. evaluates track matrices, buses and bus sends;
6. maps the master bus to the requested 1–8 device channels.

The phrase snapshot resets its local track route to canonical stereo because
project routing is applied once, later, at the project-render layer. This avoids
valid four-channel project routes being incorrectly validated against the
phrase-local stereo snapshot and prevents double routing.

## CLAP port configuration

The editor plug-in exposes channel configurations from mono through eight
channels through `clap.audio-ports-config` and
`clap.audio-ports-config-info/1`. Configuration changes are main-thread-only,
update the canonical project routing through an undoable command, notify the
host using audio-port/config rescans and require reactivation when necessary.

## Exact limitations

The implementation currently exposes one main multichannel output, not separate
per-bus stem ports. Host tempo automation is sampled per process block; it does
not cause a fresh singing render because the rendered project is positioned in
absolute seconds. A future score-following mode may choose a different policy.
