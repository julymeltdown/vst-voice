# Mandatory Phase 13A Validation

> These validations are required, not optional. A source file, CI configuration, or generated binary does not count as a runtime, validator, signing, installer, or DAW PASS. Tests must execute on the actual target operating system and actual DAW or official validator with raw evidence.

The authoritative status registry is `mandatory-validation-matrix.json`. A PASS requires OS and host/validator versions, plug-in SHA-256, execution timestamp, responsible tester, raw logs, and screenshot/audio evidence.

Beta is blocked until Windows and macOS runtime plus REAPER, Bitwig Studio, and Logic Pro have actual PASS evidence. Release Candidate is blocked until every declared VST3/AU validator, commercial DAW, signing/notarization, and clean-installer row is PASS. General Availability additionally requires Official Voicebank 01 and final licences with zero unresolved mandatory rows.
