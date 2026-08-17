# CLAP ABI subset

This directory contains a mechanically consolidated subset of the official
CLAP 1.2.10 public C ABI from `free-audio/clap` revision
`195b42a004144fab0b3cf95e9c067187d15365b7`.

Only declarations required by Project SEAM Phase 10 are included. ABI structure
field order and types are preserved. The consolidation avoids exposing unused
extensions as a false implementation promise. License: MIT.
