# Project SEAM External Beta Security Response

Document version: `external-beta-security-response-1.0`

Report suspected package tampering, trust-policy misuse, key compromise,
unexpected network submission, path/link traversal, or privacy-boundary escape
as a security issue. Include only the exact public build/artifact identity,
stable diagnostic code, and a previewed support-bundle hash. Do not include
secrets, private keys, user media, lyrics, or bank bytes in an initial report.

The response owner freezes the affected channel, records the candidate or key
identity, verifies the raw evidence independently, and assigns a severity. A
compromised delegated key is revoked through a higher-authority root policy. A
root rotation or installed-bank reclassification requires a new signed
installer. A revoked candidate is not repaired by editing its manifest or
replacing a nested plug-in file.

The owner communicates a bounded workaround or signed repair, checks that the
predecessor remains coherent, and records an expiry. Cohort distribution pauses
while a Blocker/Critical incident is unresolved. Closure requires terminal
disposition of affected assignments and independent verification of the
replacement candidate.
