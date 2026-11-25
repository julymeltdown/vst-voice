# Project SEAM External Beta Known Limitations

Document version: `external-beta-limitations-1.0`

- External Beta is blocked. The current engineering build runs on Apple Silicon
  macOS. Windows x64 and the commercial host matrix require candidate-specific
  testing. Storefront, billing and public updates belong to commercial release
  planning and are outside this evaluation.
- A fresh installation ships without a singer. The evaluation bank needs rights
  clearance, a signed package and independent review before distribution.
- Supported claims are limited to exact OS, architecture, host, format, and
  application versions with retained PASS evidence. A nearby version is not an
  implicit support claim.
- Linux is useful for regression testing only.
- Unsupported compressed media and unsupported phoneme/range/style requests
  are rejected with an actionable diagnostic rather than silently substituted.
- Audio output depends on the physical device and host negotiation. An
  unavailable device is distinct from a deterministic test fallback.
- VST3 and AUv2 are projections of canonical CLAP. A format may expose fewer
  host capabilities where its ABI cannot represent a CLAP feature; musical
  state remains canonical.
- Update verification requires a valid signed policy and manifest. Offline or
  unverifiable metadata leaves authoring available and does not install bytes.
- Support bundles exclude user content by default. Deliberately selected
  attachments are separate and may contain sensitive information.
