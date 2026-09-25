# Project JSON schema 20

Schema 20 adds an optional `readingHint` to each lyric token. It is a kana or
other user-authored pronunciation reading, not a replacement for the displayed
`surface` and not a phone sequence. The latter remains the note-level
`phoneticHint` field.

Every schema-20 lyric object must contain `readingHint`, encoded as either a
UTF-8 string or `null`. A string must contain 1–4096 Unicode scalar values.
Missing `readingHint` in schemas 1–19 migrates to `null`; schema-20 documents
that omit it or provide a non-string/non-null value are rejected.

The built-in Japanese resolver uses a lyric's `readingHint` when present and
otherwise resolves its visible surface. An explicit note `phoneticHint` remains
the higher-priority phone-level override. Changing lyric surface or language
through the canonical lyric command clears an existing reading hint in the
same undoable transaction; a reading edit does not modify the visible surface.

The current USTX 0.9 and Standard MIDI File score exporters cannot preserve this
SEAM-specific field and report it among their losses. Project JSON is the
round-trip format for authored reading intent.
