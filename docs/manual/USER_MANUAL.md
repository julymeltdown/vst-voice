# Project SEAM External Beta User Manual

Document version: `external-beta-manual-1.0`

## Supported surface

The closed Beta targets Apple Silicon macOS and Windows x64. The standalone
authoring application is the primary surface. Canonical CLAP and projected
VST3 are supported on the named Beta host matrix; AUv2 is macOS-only. Linux is
a regression surface and is not a public Beta target.

The included Beta Voicebank is a rights-cleared evaluation bank and is not an
official voicebank or character release. Its displayed identity and version
must match the installed receipt.

## First launch

Install the signed package, launch the standalone, review and accept the EULA,
then choose audio output settings. Optional update and diagnostic choices are
separate. If no bank is available, use the documented install/import action;
the application must not silently bind a development fixture.

## Authoring

Create or open a project, add a vocal track and region, enter notes and lyrics,
and use the technical editor for phoneme, unit, seam, and pitch adjustments.
View-only changes do not trigger an audio render. Audio-affecting edits mark
the preview stale and schedule a bounded render. Save and Save As create a
recoverable project set; backing media is either an explicit reference or a
project-owned copy with a content hash.

## Playback and export

Transport follows the negotiated device sample rate, buffer size, and channel
count. The timeline supports play, stop, seek, loop, and bounded offline
render. Master export writes a finite WAV in PCM16, PCM24, or Float32 format.
Export stages a receipt before publication and preserves the previous output
when replacement is possible.

## Recovery and safe mode

After a crash, choose recovery and diagnostics independently. A missing or
changed project/media file is reported with a relink or replace action. Safe
mode disables automatic reopen, last-bank selection, and plug-in scanning so
you can reach recovery and support actions.

## Plug-ins and update

The standalone is the source of bank installation and recovery. In a host,
refresh the embedded editor after the bank is installed. Save the host project,
close and reopen it, and use the same installed bank identity. Updates are
manual, signed full-package handoffs; the application never patches a running
plug-in. Read the update and rollback policy before accepting a handoff.

## Uninstall

The normal uninstall removes system-installed application and plug-in files.
Per-user projects, banks, settings, autosaves, recovery data, and reports remain
until you explicitly request their separate deletion.
