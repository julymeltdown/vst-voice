# Female English phonetic source intake: CMU ARCTIC SLT

Date: 2026-10-15, Asia/Seoul. Integrating base:
`89d7cf1eee593c834feae0654bbacd3a844c4a3e`.
The internet source route remains selected. This unit downloads and verifies
**1,132 additional female WAVs / 3,395.5589375 seconds**, preserving the earlier
297 originals. It changes no production permission, assignment or acceptance row.

## Original publisher and grant

The [SLT publisher page](http://festvox.org/cmu_arctic/dbs_slt.html) identifies a
female US English speaker experienced in synthetic voice production. It links the
original distribution directories. The selected
[0.95 release archive](http://festvox.org/cmu_arctic/cmu_arctic/packed/cmu_us_slt_arctic-0.95-release.tar.bz2)
contains recorded read speech, transcripts and automatic phone labels. These are
unchanged publisher-distributed files; they are the 16 kHz distribution, rather
than the original 32 kHz waveform/EGG session masters.

The voice-specific [COPYING](http://festvox.org/cmu_arctic/cmu_arctic/cmu_us_slt_arctic/COPYING)
expressly permits commercial use and grants copying, modification and licensing
for any purpose. Preserve Carnegie Mellon University's 2003 notice, conditions,
disclaimer and author names; clearly mark modifications. This is the original
voice distribution's grant, not a license inferred from an unrelated software
repository. Archive COPYING and AREADME are byte-identical to the independently
retrieved publisher files. The accompanying README also identifies SLT as female.

That grant supplies a copyright basis for evaluating an attributed derivative
voicebank. It does not establish separate performer/personality permissions,
approve the project's public-license/EULA disposition or supply genuine production
actors. Those project decisions remain pending. No source permission flag or
provider agreement was manufactured.

The site served these files over HTTP. Receipts retain actual URLs, headers,
completion times, lengths and local SHA-256 values. No publisher checksum was
available in the inspected directory listing; the local hash is an integrity
record, not authenticated publisher provenance.

## Download and executable evidence

| Check | Observed result |
| --- | --- |
| Complete archive | 119,914,432 bytes; SHA-256 `9fddec16fbfbfb7d4989dff0fe77ccbe31f80b07b57be49d09994aa7a67d6dba` |
| Archive inventory | 5,797 members; regular files/directories only; no absolute/traversal/duplicate member paths |
| Selected unchanged members | 2,268: 1,132 WAVs, 1,132 label files, COPYING, AREADME, README and transcript file |
| WAV geometry | Every file: mono, PCM16, 16,000 Hz; 54,328,943 total frames |
| Duration | 3,395.5589375 seconds, approximately 56 minutes 36 seconds |
| Identity and alignment census | 1,132 unique WAV byte hashes; exact WAV/transcript/label ID-set equality |
| Automatic label extent | Ordered nonnegative endpoints; no final endpoint exceeds its WAV duration |
| Actual native reader | `seam_voicebank_cli inspect-wav`: 1,132/1,132 pass in 7.388150082988432 s |
| Reader diagnostics | All finite and non-silent; zero reported clipped samples; maximum decoded peak 0.649932861328125 |
| Illustrative pitch analysis | Three actual Direct-v3 analyses; held source hashes and geometry match; not whole-corpus pitch qualification |

The three analyses are `arctic_a0001`, `arctic_b0001` and `arctic_a0095` (the last
contains publisher-labelled `zh`). They contain 202/97/216 analysis frames and
report median pitch 196.7888/170.3851/178.3025 Hz. These whole-utterance statistics
do not define usable singing notes, stable vowel loops or acoustic truth. They
use the existing native binary, retained and hashed before/after along with the
source files. No new native build is claimed; no product source changed here.

Extraction writes only explicitly selected data members into a new owned root.
It does not execute the archive's Festival scripts or extract its binaries as
project tools. Every selected member is compared byte-for-byte with its archived
payload. No waveform is normalized, resampled, cropped or pitch-shifted.

## Code-level suitability and remaining gaps

The publisher says its labels were generated automatically using CMU Sphinx and
FestVox scripts, without manual correction. The retained labels contain **41 raw
symbols including pause**, and **1,327 adjacent symbol-pair types**, including
pause transitions. These are descriptive counts, not approved phonetic or CV/VC/VV
coverage. The opaque middle label column is retained without treating it as a
confidence or review score.

SEAM's frozen vocabulary is `seam-en-arpabet-v1`, declared in
`libs/seam-phonemizer/include/seam/phonemizer/english_resource.hpp:16` and listed
in `libs/seam-phonemizer/src/english_resource.cpp:25`. It contains 93 symbols:
24 consonants, 17 vowels with bare/0/1/2 stress forms, and pause. A static comparison
against the exact held source finds one raw spelling mismatch: `jh`. The existing
dictionary parser maps `jh` to `j` at `english_resource.cpp:145`; that is a basis
for a proposed explicit import alias, not an assignment applied in this intake.

After that proposed alias, the source still lacks **`axr`** and all **51 explicit
lexical stress forms**: 52 declared symbols remain absent. No stress labels or
`axr` recordings are inferred from the transcript or neighboring phones. A later
bank may choose the existing explicit stress-folding contract, but it must declare
that choice and prove coverage under it. The corpus is English speech at one
speaker's spoken pitch, with an 8 kHz Nyquist limit. It supplies neither Japanese/
Korean production mappings, sustained singing pitch layers, expression styles,
qualified neural weights nor the original singer created without supplied human
recordings required by AE1/R3/R9.

The native reader at `apps/seam-voicebank-cli/main.cpp:268` checks decode, geometry,
finiteness and simple audibility. Passing it is not production QC. Draft creation
requires an explicitly registered producer at
`apps/seam-voicebank-cli/sample_review_commands.cpp:154`; the review/publication
commands require a registered independent reviewer and supplied decisions
(`sample_review_commands.cpp:422`). This unit does not bypass those paths.

## Next bank work

1. Resolve the already pending genuine producer/distinct reviewer and source-rights/
   public-license policy decision. Preserve the full CMU notices with each source
   and make any later transformation explicit. Do not use fixture identities.
2. Have the producer and language reviewer examine consonant boundaries and vowel
   spans against the actual WAVs. Keep automatic labels as source annotations;
   separately capture reviewed timing, phoneme mapping, alias and stress policy.
3. Prepare a small unassigned English donor cohort for the missing consonant path,
   with held-original frame intervals, actual stored-audio diagnostics and explicit
   unsupported-phone reporting. First prove a short unseen English lyric through
   the normal draft/review/package/install/render path before enlarging the cohort.
   Read-speech duration and automatic pair counts cannot close singing coverage.
4. Retain the existing VocalSet singing vowels for independent pitch/loop/style
   evaluation. Assess a higher-rate original source if fricative/detail limitations
   matter; do not claim resampling restores lost bandwidth. Neither source route
   replaces the separate original/procedural/neural singer work or human listening.

The [evidence index](FEMALE_ARCTIC_SLT_INTAKE_2026-10-15.evidence.json) records the
local retained root and its replay command. Its final read-only audit checks 2,320
artifacts and 304 unique preserved paths, including the earlier 297 WAVs and all
canonical gate files. `verify.py` itself was executed; it runs Git reads, but no captured acquisition/runtime script or native binary. The initial passing manifest/log are preserved too; the
final manifest removes four duplicate canonical pin records. The combined census
is **1,429 distinct original female WAVs**: 297 earlier singing/growl sources and
1,132 new speech sources. It is not a 1,429-unit qualified bank.

Independent technical review of this new intake is **PENDING**; the peer is
currently reviewing exact `a30f7969`, a separate scorer repair. No human listening,
source/rights acceptance, production import or Beta promotion is claimed. Alpha
remains 0/20 recorded passes, U45 remains held, Beta remains BLOCKED and the full
goal stays ACTIVE. Original sources, frozen prior evidence, canonical requirements,
existing stash, `.github` and README Windows TODO are preserved.
