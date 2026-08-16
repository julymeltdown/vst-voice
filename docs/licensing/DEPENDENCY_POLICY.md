# Dependency Policy

## Goal

The distributed closed-source product may include only dependencies approved for commercial redistribution without reciprocal source-disclosure obligations.

## Allowed SPDX families

- MIT
- MIT-0
- BSD-2-Clause
- BSD-3-Clause
- ISC
- Apache-2.0
- Zlib
- BSL-1.0
- CC0-1.0
- OFL-1.1 for fonts only

## Denied by default

- GPL
- AGPL
- LGPL
- MPL
- SSPL
- non-commercial Creative Commons variants
- proprietary/unknown/no-assertion dependencies

A denied license is not necessarily unusable in every commercial product; it is excluded because it violates this project's stricter permissive-only runtime policy.

## Intake requirements

Every distributed dependency must have:

- official source URL;
- exact immutable commit or release digest;
- source archive SHA-256;
- verified license identifier;
- copied license file;
- transitive/build-closure review;
- distribution and linking mode;
- modification status;
- approval record.

Repository-root licensing alone is insufficient when a project contains differently licensed subdirectories, codecs, fonts, models, examples, or fetched build dependencies.

## Phase 1 state

No third-party production source is vendored. iPlug2, Skia, WORLD, Signalsmith Stretch, and other planned libraries remain references or future integrations until exact revisions are admitted through this process.
