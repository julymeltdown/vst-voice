# CLAP ABI editor subset

This directory contains a mechanically consolidated subset of the official CLAP 1.2.10 public C ABI from `free-audio/clap` revision `195b42a004144fab0b3cf95e9c067187d15365b7`.

Phase 11 extends the previous subset with note ports/events, GUI and timer-support declarations required by the embedded editor instrument. The September 9, 2026 ABI repair restores the upstream note-expression field order and enum values; independent wire-layout regression checks prevent equal-sized but incompatible layouts. The consolidated header SHA-256 is `bcc964d92a82c450cf43f35a32b78decfcc6e970ddb73362216f157ae6631387`. License: MIT.
