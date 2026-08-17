# Project SEAM CLAP State v1 (`SEAMCLP1`)

## Purpose

A bounded binary state used by `ProjectSEAM.clap` to persist a pre-rendered
multichannel vocal result and its Master Gain value. It is not a general Project
SEAM project file and contains no executable code, voicebank, character asset,
font, path, or network reference.

## Byte order

All numeric fields and PCM samples use little-endian encoding.

## Layout

```text
Offset  Size  Field
0       8     ASCII magic `SEAMCLP1`
8       4     format version = 1
12      4     source sample rate
16      4     channel count (1..8)
20      4     reserved = 0
24      8     frame count
32      8     IEEE-754 binary64 Master Gain dB bits
40      4     UTF-8 title byte count
44      8     PCM payload byte count
52      N     UTF-8 title
52+N    M     interleaved IEEE-754 binary32 PCM
end-32  32    SHA-256 of every preceding byte
```

## Limits

- State bytes: at most 256 MiB.
- Sample rate: 8 kHz through 192 kHz.
- Channels: one through eight.
- Duration: at most ten minutes.
- Title: at most 4096 bytes.
- PCM: channel aligned, finite, non-empty.
- Master Gain: finite and in `[-60, +6]` dB.

## Validation order

1. total byte bounds;
2. magic;
3. SHA-256;
4. fixed header fields;
5. checked title/payload size arithmetic;
6. frame/channel/payload consistency;
7. finite PCM and gain;
8. semantic `PluginSession` validation.

The codec never trusts a declared payload length before confirming it matches
the actual bounded input span.

## State stream behavior

CLAP streams may return partial reads or writes. The plug-in loops until all
encoded bytes are written and reads repeated bounded chunks until EOF. A
negative stream result is an error. Input that grows beyond the state limit is
rejected immediately.


## Command-line conversion

```bash
seam_clap_state_tool pack vocal-4ch.wav vocal.seamclapstate \
  --title "Rendered vocal" --gain-db 0
seam_clap_state_tool inspect vocal.seamclapstate
seam_clap_state_tool extract vocal.seamclapstate recovered.wav
```

The converter uses the same bounded state codec and multichannel WAV layer as
the test and plug-in paths. It does not bypass state validation.
