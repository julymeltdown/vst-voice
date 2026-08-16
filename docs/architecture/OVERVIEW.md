# Architecture Overview

## Architectural style

Project SEAM is a modular monolith. Audio editing requires deterministic in-process state and low-latency data flow; splitting the core into network services would add failure modes without product value.

## Dependency graph

```text
seam_core
  └─ seam_domain
       └─ seam_application
            ├─ seam_editor_ui
            ├─ seam_formats
            └─ seam_platform adapters
```

`seam_editor_ui`, `seam_formats`, and `seam_platform` are replaceable adapters around the application/domain core.

## Modules

### seam_core

Provides only generic primitives: typed IDs, error/result, logging, and hashing.

### seam_domain

Owns canonical musical and project state. It cannot include or link graphics, serialization, databases, plugin SDKs, or OS APIs.

### seam_application

Owns use cases and reversible commands. It validates the domain after each command and increments a revision only after a successful state transition.

### seam_editor_ui

Owns editor geometry and interaction semantics. Phase 1 uses SVG as a proof painter. Phase 2 will add a Skia painter without changing note editing rules.

### seam_formats

Maps canonical state to external representation. Format code may depend on the domain, but the domain never depends on format code.

### seam_platform

Defines real-time and desktop adapter contracts. Device and plugin SDK integration stays here.

## State flow

```text
Input event
→ interaction state
→ application command
→ domain validation
→ project revision
→ spatial-index rebuild or dirty-region update
→ painter / future render scheduler
```

## Real-time boundary

The future audio callback may read only preallocated buffers and atomics. It may not traverse the project, parse files, select units, perform FFT planning, allocate memory, or block on a lock.
