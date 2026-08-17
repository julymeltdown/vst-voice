# Exact Voicebank Discovery and Resolution

## Saved identity

Every bound vocal track stores:

```text
Voicebank ID
Voicebank version
Synthesis content SHA-256
```

The content identity covers canonical synthesis metadata and every unique Unit
WAV digest. Package art and Character runtime resources do not participate in
PCM identity.

## Search roots

Resolution considers, in order of policy rather than accidental directory
order:

1. trusted installed `.seambank` versions;
2. explicitly allowed development fixtures;
3. untrusted installed candidates only when policy explicitly permits them.

Default platform roots:

```text
Windows  %LOCALAPPDATA%/ProjectSEAM/Voicebanks
macOS    ~/Library/Application Support/ProjectSEAM/Voicebanks
Linux    $XDG_DATA_HOME/project-seam/voicebanks
         or ~/.local/share/project-seam/voicebanks
```

Additional roots can come from `SEAM_VOICEBANK_PATH`, plug-in resources or the
explicit relink API. The source-tree public-domain fixture is compiled into
development builds as a search location; it is not a replacement for an
installed release bank.

## Trust model

An installed bank is trusted only when `install-receipt.json` matches the
manifest ID, version and recomputed content hash, and the installer recorded
both a valid signature and a trusted signer. A stale receipt or modified WAV
changes the content identity and makes the exact saved reference fail.

Development fixtures are separately labelled and accepted only by an explicit
runtime policy. They are never reported as signed installed products.

## Failure behavior

```text
missing ID                 → voicebank-missing
wrong version              → voicebank-version-mismatch
missing saved content hash → voicebank-content-hash-missing
changed bytes/metadata     → voicebank-content-mismatch
matching but untrusted     → voicebank-untrusted
```

Every failure publishes empty preview PCM and a diagnostic. Project state is
retained. The application may refresh roots, add a relink root or explicitly
select a candidate; it never silently swaps a different bank.
