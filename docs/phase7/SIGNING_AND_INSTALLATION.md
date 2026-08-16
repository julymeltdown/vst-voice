# Phase 7 Signing and Installation

## Release-key workflow

```text
Offline release workstation
  seam_bank_tool keygen official-private.json official-public.json
  seam_bank_tool pack bank-directory official.seambank official-private.json

Distribution
  official.seambank
  official-public.json through an authenticated channel

User installation
  seam_bank_tool verify official.seambank --public-key official-public.json
  seam_bank_tool install official.seambank VOICEBANK_ROOT \
    --public-key official-public.json
```

The private key is never placed in a voicebank, source archive, installer, application resource, or repository. The current key file is raw Ed25519 material in a permission-restricted JSON envelope; production releases should keep it offline and may wrap it with a hardware or organizational key-management process.

## Installer transaction

1. Verify the package signature and every entry checksum.
2. Require an explicitly trusted signer.
3. Extract only safe data paths into a staging directory.
4. Revalidate the installed manifest and write a signed-package receipt.
5. Recheck the package digest to detect replacement during installation.
6. Rename the staging directory into `<root>/<voicebank-id>/<version>`.
7. On replacement, preserve the old directory until the new directory is published.

A failed installation does not publish a partial voicebank. Existing installations are not replaced unless `--replace` is supplied.

## Character package relationship

The official Character 01 assets may be included as data inside the signed voicebank package. They remain presentation assets: changing or omitting character data does not alter synthesis or phrase PCM cache identity. The voicebank manifest binds the character product ID and version.
