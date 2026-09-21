# Stage-localization listening packet: assembled, listening pending

Artifact root: /Users/lhs/Downloads/seam-listening-artifacts/2026-09-22-stage-localization
Source commit: e5dc505. Manifest SHA-256:
1f1a18fef0b023573d53d6ff94a40d2baf7e8a8193624c68a37fbdffda2e15c1 (also
copied beside this file). The sealed clip-to-role key is
key.sealed.json in the artifact root; open it only after scoring.

## Frozen negative result (closed, not reopened by this packet)

The unvoiced-target-log-flatness auxiliary family is STOPPED at the
tested setting (lambda = 0.08040502229238589, e8 parent 04f72263,
dataset 09756315, seed 933, one 267-update epoch per cell). The 2x2
ablation (receipt flatness-ablation-eval-e9-r1/assessment.json, sha
0023b724ad8cd9d1b866be53abf235f59fa26f7c1c8d94692d25173466446dbe)
showed every auxiliary cell moved unvoiced flatness further from the
reference than base on final sampler output (base 2.8325, combined
3.7984, flat-only 3.9403, level-only 3.8243 nats) and fired guardrails.
This closes the family at this setting only; it is not proof that
global-flatness objectives universally fail.

Candidate identities: base acoustic export a7dbde0f (run
acoustic-flatness-control-e9-r1, receipt fc73c90f); fixed vocoder
0731edd1 graph 9a733810 zero-v1. The baseline itself fails the unvoiced
RMS range guardrail (0.657 vs [0.8, 1.2]) and is NOT product-ready or
perceptually best; the previously accepted research candidate stands.

Remaining blocker: qualified neural singing is mandatory for Beta
acceptance (EB-009 scope). The unvoiced-aperiodicity defect is parked,
not waived.

## Packet contents

46 blind clips (clip-01.wav..clip-46.wav), deterministic shuffle seed
20260922, three-stage comparison where available:

- Held-out panel (12 songs x 3 stages): reference source.wav,
  reference-mel rendered through the fixed vocoder (control arm of
  vocoder-multilag-paired-r1), and base acoustic-to-vocoder output
  (one draw each).
- Development panel (5 songs x 2 stages): reference source.wav and
  base acoustic-to-vocoder draw-00 (captured dynamics gains applied).
  The vocoder-reconstruction stage is UNAVAILABLE here: no
  reference-mel render under vocoder 0731edd1 exists for these sources
  (the e2 validation renders used a different vocoder identity).

No new training, sampling or vocoder inference was run for this packet;
every clip is a byte copy of an existing artifact, hash-bound in the
manifest. No gain-matched previews were generated; if needed later they
must be separately labeled with logged gain, never phone-normalized.

## Reviewer questions (per clip or per comparison)

1. Consonant audibility: are unvoiced consonants (s, h, f, k, t, ch)
   present, weak, absent, or wrong in character?
2. Noise character: where noise exists, is it breathy/static-like
   (reference-like) or tonal/buzzy (over-concentrated)?
3. Voiced contamination: do unvoiced spans carry pitch or vowel bleed?
4. Intelligibility: are lyrics understandable phrase by phrase?
5. Usability: would this pass as a demo vocal take (yes/no per song)?
6. Time-localization: note approximate timestamps of the worst defects.

Compare stages within each song: reference vs vocoder-reconstruction
isolates vocoder reconstruction loss; vocoder-reconstruction vs
acoustic-to-vocoder isolates acoustic prediction loss. On the
development panel only the reference vs acoustic-to-vocoder contrast
is available.

## Exit criteria

This packet exits with EITHER an actionable, stage-localized audible
defect plus ONE falsifiable next hypothesis, OR a parked research
blocker pending human evidence. Listening stays NOT_REVIEWED until
actual human results exist; a favorable listen alone does not override
the failed formal gates. singerQualified, releaseEligible and
combinedModelHoldoutVerified remain false.
