"""Assert explicit source-pitch diagnostics for every listed bank unit.

These estimator-relative classifications are not physical F0 ground truth or
production suitability. The demo's spoken recording is a technical fixture;
nearby or distant median estimates cannot establish a sung vowel or phoneme bank.
Historical v1 figures must not be treated as current v3 measurements. See
docs/implementation/BANK_ROOT_DIAGNOSTIC_2026-10-15.md.

Native metadata/source identity, confidence and range are checked before using
window centres inside the legacy or schema-3 loop span. Every unit is inspected,
not only the first. Adding or removing a shipped bank requires explicit review.
Generated sustained-tone controls separately test known-answer pitch behaviour.
"""

from __future__ import annotations

from contextlib import redirect_stdout
import io
import json
import hashlib
import math
import statistics
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from tools.singing_quality.score_rendered_notes import feature_metadata, usable


ROOT = Path(__file__).resolve().parents[2]

# Shipped fixtures require explicit diagnostic classification. Optional local
# phase7 tone copies are inspected when present, but are not shipped resources.
# These bounds are regression classifications, never production approval.
KNOWN_BANKS: dict[str, dict[str, object]] = {
    "assets/demo-human-voicebank-public-domain/manifest.json": {
        # Legacy loop5292..21609: v3 centre-selected median681.868Hz (+958.388c).
        "tolerance_cents": 500.0,
        "expect_match": False,
        "why": "spoken utterance reused under one phoneme label; see the listening-packet entry",
    },
    "assets/demo-human-voicebank-public-domain/production-bank/manifest.json": {
        # Same bytes and loop span, stored under schema-3 markers for all8 units.
        "tolerance_cents": 500.0,
        "expect_match": False,
        "why": "same spoken utterance under eight phoneme labels",
    },
    "out/phase7/source/manifest.json": {
        "tolerance_cents": 50.0,
        "expect_match": True,
        "why": "a 440 Hz tone declared at rootMidi 69; the positive control",
    },
    "out/phase7/installed/official.voice.01.demo/0.7.0/manifest.json": {
        "tolerance_cents": 50.0,
        "expect_match": True,
        "why": "installed copy of the phase7 source bank",
    },
}

# Directories that hold only local build or evidence artifacts. A manifest copied into one
# of these is a copy of a bank that is already listed, and out/ is gitignored, so requiring
# those copies to be classified would make this case depend on whatever the machine happens
# to have built. Shipped banks are the ones that can reach a user.
ARTIFACT_PREFIXES = ("out/",)


def _is_shipped(rel: str) -> bool:
    return not rel.startswith(ARTIFACT_PREFIXES)


def _extractor() -> str | None:
    for candidate in ("build/release/seam_voicebank_cli", "build/dev/seam_voicebank_cli"):
        path = ROOT / candidate
        if path.exists():
            return str(path)
    return shutil.which("seam_voicebank_cli")


def _measure(extractor: str, audio: Path, loop_start, loop_end):
    """Return estimator median Hz, usable frame count and frequency spread."""
    result = subprocess.run([extractor, "extract-pitch", str(audio)],
                            capture_output=True, text=True, check=True, timeout=120)
    features = json.loads(result.stdout)
    meta = feature_metadata(features, audio)
    start = 0 if loop_start is None else loop_start
    end = meta['frameCount'] if loop_end is None else loop_end
    if type(start) is not int or type(end) is not int or not 0 <= start < end <= meta['frameCount']:
        raise ValueError('Loop span must lie inside the source audio')
    inside = [f['f0Hz'] for f in features['pitchFrames']
              if start <= f['sourceFrame'] + meta['windowFrames'] / 2 < end
              and usable(f, meta['minimumHz'], meta['maximumHz'])]
    if not inside:
        return 0.0, 0, 0.0
    inside.sort()
    spread = inside[-1] / max(inside[0], 1e-9)
    return statistics.median(inside), len(inside), spread


def _unit_span(unit):
    if 'markers' in unit:
        markers = unit['markers']
        if not isinstance(markers, dict):
            raise ValueError('Unit markers must be an object')
        start, end = markers.get('loopStart'), markers.get('loopEnd')
    else:
        start, end = unit.get('loopStartFrame'), unit.get('loopEndFrame')
    if start is None and end is None:
        return None, None
    if type(start) is not int or type(end) is not int or not 0 <= start < end:
        raise ValueError('Unit loop span must have two ordered integer bounds')
    return start, end


class BankRootPitchTest(unittest.TestCase):
    def test_every_shipped_bank_manifest_is_listed(self) -> None:
        """No manifest carrying units may exist outside KNOWN_BANKS.

        This is what generalises the original single-bank check: a new voicebank
        added anywhere in the repository has to be classified here, so a wrong root
        cannot be introduced by shipping a new manifest.

        The search is by CONTENT over every .json under these roots, not by the filename
        "manifest.json". The first version globbed for that exact name and a probe bank
        deliberately saved under a different filename sailed straight through the guard,
        which is the failure this case exists to prevent; it was caught by trying to break
        the guard rather than by trusting it.
        """
        found = {}
        for pattern in ("assets/**/*.json", "tests/**/*.json"):
            for path in ROOT.glob(pattern):
                rel = str(path.relative_to(ROOT))
                if not _is_shipped(rel):
                    continue
                try:
                    data = json.loads(path.read_text())
                except (OSError, ValueError):
                    continue
                if not isinstance(data, dict):
                    continue
                units = data.get("units")
                if not isinstance(units, list) or not units:
                    continue
                if not any(isinstance(unit, dict) and "rootMidi" in unit for unit in units):
                    continue
                found[rel] = len(units)
        unlisted = sorted(set(found) - set(KNOWN_BANKS))
        self.assertEqual(unlisted, [],
                         "voicebank manifests not classified in KNOWN_BANKS: " + str(unlisted))
        missing = sorted(rel for rel in KNOWN_BANKS if _is_shipped(rel) and rel not in found)
        self.assertEqual(missing, [], 'listed shipped bank manifests are missing: ' + str(missing))

    def test_nonbank_json_does_not_hide_an_unlisted_shipped_bank(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'assets').mkdir()
            (root / 'out').mkdir()
            (root / 'assets' / 'report.json').write_text('["not a bank"]')
            (root / 'out' / 'retention-index.json').write_text('[{"sha256": "receipt"}]')
            with mock.patch.dict(globals(), {'ROOT': root, 'KNOWN_BANKS': {}}):
                self.test_every_shipped_bank_manifest_is_listed()
                # Discovery is still content-based, and a malformed first unit
                # cannot hide the actual bank unit that follows it.
                (root / 'assets' / 'unexpected.json').write_text(
                    '{"units": [null, {"rootMidi": 69}]}')
                with self.assertRaisesRegex(AssertionError, 'unexpected.json'):
                    self.test_every_shipped_bank_manifest_is_listed()

    def test_declared_root_mismatches_are_all_known(self) -> None:
        """Measure every unit against its explicit diagnostic class, not qualification."""
        extractor = _extractor()
        if extractor is None:
            self.skipTest("seam_voicebank_cli is not built")
        for rel, expect in KNOWN_BANKS.items():
            manifest_path = ROOT / rel
            if not _is_shipped(rel) and not manifest_path.exists():
                continue  # Optional local artifacts must not skip required banks.
            self.assertTrue(manifest_path.is_file(), 'missing shipped bank ' + rel)
            manifest = json.loads(manifest_path.read_text())
            for index, unit in enumerate(manifest['units']):
                with self.subTest(bank=rel, unit=index):
                    audio = manifest_path.parent / unit["audio"]
                    self.assertTrue(audio.exists(), rel + " names missing audio " + str(unit["audio"]))
                    start, end = _unit_span(unit)
                    median_hz, frames, spread = _measure(extractor, audio, start, end)
                    self.assertGreater(frames, 0, rel + " audio has no voiced frames to measure")
                    declared_hz = 440.0 * (2.0 ** ((unit["rootMidi"] - 69) / 12.0))
                    cents = 1200 * math.log2(median_hz / declared_hz)
                    tolerance = float(expect["tolerance_cents"])
                    print('BANK_ROOT_DIAGNOSTIC ' + json.dumps(dict(
                        bank=rel, unitIndex=index, unitId=unit.get('id'),
                        sourceSha256=hashlib.sha256(audio.read_bytes()).hexdigest(),
                        loopStart=start, loopEnd=end, medianHz=median_hz, usableFrames=frames,
                        frequencySpread=spread, declaredMidi=unit['rootMidi'], cents=cents,
                        interpretation='estimator-relative diagnostic; not production qualification')))
                    if bool(expect["expect_match"]):
                        self.assertLess(abs(cents), tolerance,
                                        rel + " should match its declared root within "
                                        + str(tolerance) + " cents but reads " + format(cents, "+.0f")
                                        + " (" + str(expect["why"]) + ")")
                    else:
                        self.assertGreater(
                            abs(cents), tolerance,
                            rel + " estimator diagnostic changed class and now reads "
                            + format(cents, "+.0f") + " cents. If the fixture was replaced with a "
                            "different recording or the analyser changed, reassess exact source/method/span "
                            "instead of widening this bound or inferring suitability. Context: " + str(expect["why"]))

    def test_a_matching_bank_really_is_measurable_as_matching(self) -> None:
        """The positive control must pass, or the case above proves nothing.

        Without this, a broken measurement returning 0 Hz could make every bank look
        mismatched and the classification would hold vacuously. The control is a tone
        generated here rather than a checked-in bank, because shipped banks are
        diagnostic fixtures and out/ is a gitignored artifact directory, so relying on
        one would make this case depend on what the machine happens to have built.
        """
        extractor = _extractor()
        if extractor is None:
            self.skipTest("seam_voicebank_cli is not built")
        rate = 48000
        root_midi = 69
        declared_hz = 440.0
        frames_written = rate  # one second
        with tempfile.TemporaryDirectory() as tmp:
            wav = Path(tmp) / "control.wav"
            samples = bytearray()
            for index in range(frames_written):
                value = int(12000 * math.sin(2 * math.pi * declared_hz * index / rate))
                samples += struct.pack("<h", value)
            header = b"RIFF" + struct.pack("<I", 36 + len(samples)) + b"WAVEfmt "
            header += struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
            header += b"data" + struct.pack("<I", len(samples))
            wav.write_bytes(header + bytes(samples))
            median_hz, frames, spread = _measure(extractor, wav, None, None)
        cents = 1200 * math.log2(median_hz / declared_hz)
        self.assertGreater(frames, 0)
        self.assertLess(abs(cents), 50.0, "control bank reads " + format(cents, "+.0f") + " cents")
        # A sustained tone occupies a narrow range; a spoken utterance spans octaves.
        # The spread is what tells the two apart and is why each bank carries its own tolerance.
        self.assertLess(spread, 1.5, "a sustained tone should not span octaves, got "
                        + format(spread, ".2f"))

    def run_mocked_features(self, transform, start=2048, end=3072):
        # Source/protocol mechanics only; these bytes are not a native WAV.
        with tempfile.TemporaryDirectory() as temporary:
            wav = Path(temporary) / 'mock.wav'
            wav.write_bytes(b'mocked source identity, not native audio')
            features = {'formatId': 'com.project-seam.training-pitch-features', 'schemaVersion': 1,
                        'sourceSha256': hashlib.sha256(wav.read_bytes()).hexdigest(),
                        'sampleRate': 48000, 'frameCount': 8192, 'windowFrames': 2048, 'hopSize': 256,
                        'minimumHz': 60, 'maximumHz': 1200, 'algorithm': 'fft-autocorrelation-v3',
                        'coverage': 'full-hop-zero-padded',
                        'pitchFrames': [{'sourceFrame': source, 'voiced': True, 'confidence': 1.0,
                                         'f0Hz': 440.0 if 1024 <= source < 2048 else 880.0}
                                        for source in range(0, 8192, 256)]}
            transform(features)
            result = subprocess.CompletedProcess([], 0, json.dumps(features), '')
            with mock.patch.object(subprocess, 'run', return_value=result):
                return _measure('mock-extractor', wav, start, end)

    def test_window_centres_cannot_measure_a_neighboring_span(self):
        self.assertEqual((440.0, 4, 1.0), self.run_mocked_features(lambda features: None))

    def test_low_confidence_and_ceiling_estimates_are_not_root_evidence(self):
        def change(features):
            for frame in features['pitchFrames']:
                frame['confidence'] = 0.2
            features['pitchFrames'][4].update(confidence=1.0, f0Hz=1200.0)
        self.assertEqual((0.0, 0, 0.0), self.run_mocked_features(change))

    def test_changed_native_algorithm_requires_explicit_reassessment(self):
        with self.assertRaisesRegex(ValueError, 'algorithm'):
            self.run_mocked_features(lambda f: f.update(algorithm='unreviewed-estimator'))

    def test_a_later_unit_cannot_hide_behind_the_first_matching_unit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bank = root / 'assets' / 'test-bank'
            bank.mkdir(parents=True)
            (bank / 'source.wav').write_bytes(b'mocked measurement source; not native WAV')
            (bank / 'manifest.json').write_text(json.dumps({'units': [
                {'audio': 'source.wav', 'rootMidi': 69}, {'audio': 'source.wav', 'rootMidi': 57}]}))
            known = {'assets/test-bank/manifest.json': {'expect_match': True, 'tolerance_cents': 50,
                                                      'why': 'mocked root-classification regression'}}
            with mock.patch.dict(globals(), {'ROOT': root, 'KNOWN_BANKS': known}), \
                 mock.patch.dict(globals(), {'_extractor': lambda: 'mock'}), \
                 mock.patch.dict(globals(), {'_measure': lambda *args: (440.0, 100, 1.0)}):
                probe = BankRootPitchTest('test_declared_root_mismatches_are_all_known')
                result = unittest.TestResult()
                with redirect_stdout(io.StringIO()):
                    probe.run(result)
                self.assertEqual(1, len(result.failures))
                self.assertEqual([], result.errors)

    def test_schema_three_markers_take_precedence_over_legacy_loop_fields(self):
        unit = {'loopStartFrame': 0, 'loopEndFrame': 100,
                'markers': {'loopStart': 5292, 'loopEnd': 21609}}
        self.assertEqual((5292, 21609), _unit_span(unit))
        self.assertEqual((0, 100), _unit_span({'loopStartFrame': 0, 'loopEndFrame': 100}))

    def test_partial_loop_span_is_refused(self):
        for unit in ({'loopStartFrame': 10}, {'markers': {'loopEnd': 100}}):
            with self.subTest(unit=unit), self.assertRaisesRegex(ValueError, 'loop span'):
                _unit_span(unit)

    def test_missing_listed_shipped_manifest_cannot_disappear_from_discovery(self):
        with tempfile.TemporaryDirectory() as temporary:
            with mock.patch.dict(globals(), {'ROOT': Path(temporary),
                                            'KNOWN_BANKS': {'assets/missing/manifest.json': {}}}):
                with self.assertRaisesRegex(AssertionError, 'missing'):
                    self.test_every_shipped_bank_manifest_is_listed()

    def test_absent_optional_artifact_does_not_skip_a_later_required_bank(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); bank = root / 'assets' / 'test-bank'
            bank.mkdir(parents=True)
            (bank / 'source.wav').write_bytes(b'mocked optional-fixture traversal; not native WAV')
            (bank / 'manifest.json').write_text(json.dumps({'units': [{'audio': 'source.wav', 'rootMidi': 69}]}))
            positive = {'expect_match': True, 'tolerance_cents': 50, 'why': 'mocked traversal'}
            known = {'out/absent/manifest.json': positive, 'assets/test-bank/manifest.json': positive}
            with mock.patch.dict(globals(), {'ROOT': root, 'KNOWN_BANKS': known}), \
                 mock.patch.dict(globals(), {'_extractor': lambda: 'mock'}), \
                 mock.patch.dict(globals(), {'_measure': lambda *args: (440.0, 100, 1.0)}):
                probe = BankRootPitchTest('test_declared_root_mismatches_are_all_known')
                result = unittest.TestResult()
                with redirect_stdout(io.StringIO()):
                    probe.run(result)
                self.assertEqual([], result.skipped)
                self.assertEqual([], result.errors)
                self.assertEqual([], result.failures)


if __name__ == "__main__":
    unittest.main()
