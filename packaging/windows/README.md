# Windows package

The Phase 11 CI output is an unsigned ZIP containing `ProjectSEAMEditor.clap`, the public-domain demo fixture and notices. Authenticode signing is performed only when `WINDOWS_SIGN_CERT_SHA1` is available. A signed binary is never simulated with a self-signed release claim.
