# Project JSON Schema 4 — Multichannel Routing

Schema 4 persists the project routing graph instead of deriving every track directly into a stereo master.

## Top-level routing

```json
{
  "deviceOutputChannels": 4,
  "masterBus": "3",
  "buses": [],
  "sends": [],
  "deviceRoutes": []
}
```

Each bus has 1–8 channels, gain, mute, and solo state. Bus sends are acyclic and contain a row-major destination-by-source gain matrix. Device routes map any bus into the configured physical output channels.

Each vocal and audio track has an `outputRoute` containing a destination bus and an explicit source-to-bus matrix. Legacy schemas migrate to the default stereo master and an equal-power mono pan matrix.

Routing matrices are bounded to eight source and destination channels and finite gain values. The loader rejects missing buses, dimension mismatches, duplicate bus IDs, cycles, and projects without a physical output route.
