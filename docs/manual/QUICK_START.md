# Project SEAM Quick Start

Document version: `external-beta-quick-start-1.0`

This is a ten-minute first-run path for the closed External Beta. It uses the
rights-cleared evaluation bank and does not claim Official Voicebank 01 or
commercial content rights.

## 1. Launch and configure audio

Launch `Project SEAM.app`, accept the EULA, and open Audio Settings. Select the
physical output device, confirm the negotiated sample rate/block size/channel
count, and make sure the status bar does not report an unavailable device.

## 2. Create a project

Choose New Project, enter a project name, keep the default 4/4 meter, choose a
sample rate and output channel count, and select the exact installed evaluation
voicebank. If the bank is not listed, install or relink it; never substitute a
development fixture silently.

## 3. Author a phrase

Add a vocal track and region, enter Japanese lyrics and notes in the piano roll,
then wait for the render status to return to Ready. Select a phoneme boundary,
change a unit variant or renderer, add a pitch point, and adjust a seam amount.
Use Undo and Redo to confirm each edit is part of the project history.

## 4. Play, save, and reopen

Use Play, Pause, Stop, Seek, and Loop. Save to a user-selected `.seam` path,
quit the application, reopen the same file, and confirm the voicebank identity
and audio status are unchanged. If the bank or media is missing, use Relink;
the editor must fail closed rather than bind another bank.

## 5. Export and report issues

Export a master or stem WAV, inspect the committed receipt, and verify the file
in an external player. For support, open the Support Bundle preview first and
confirm that paths, lyrics, audio, and bank files are not included unless you
deliberately attach them. Record the build/OS/voicebank tuple and stable
diagnostic code for any issue.

The full behavior and limitations are in `USER_MANUAL.md`,
`KNOWN_LIMITATIONS.md`, and `BETA_TESTER_CHECKLIST.md`.
