"""Objective acoustic measurements over a collected diagnostic packet.

This module consumes only retained packet evidence: the per-case
diagnostics.json timing records and the analyzer analysis.json pitch frames.
It reports measurements against the frozen evaluation profile thresholds and
never upgrades a measurement into a musical acceptance claim.

Frozen numeric thresholds come from
docs/product/full-product-beta-contract.json evaluationProfile:
  pitch-median         maximum 30 cents
  pitch-within-50      minimum 90 percent of steady frames within 50 cents
  timing-displacement  maximum 1 sample error for a 30 ms edit

Only voiced analysis frames inside a rendered placement steady span are
scored. Unvoiced or low-confidence frames are reported separately and are
never counted as accurate pitch.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
import math
from pathlib import Path
from typing import Final

from .packet_io import read_bounded

MEDIAN_CENTS_LIMIT: Final = 30.0
WITHIN_50_PERCENT_LIMIT: Final = 90.0
WITHIN_50_CENTS: Final = 50.0
DISPLACEMENT_LIMIT_SAMPLES: Final = 1
MINIMUM_CONFIDENCE: Final = 0.5
# The analyzer F0 estimator is bounded by maximumHz 1200.0. A frame reported at
# or above that ceiling is a lock to the estimator bound (or a harmonic of it),
# not a credible measurement of the rendered note. Such frames are counted
# separately and excluded from the scored set instead of being reported as
# renderer octave errors.
ANALYZER_CEILING_HZ: Final = 1200.0


class AcousticMetricError(Exception):
    """A packet cannot be measured as requested."""


def _midi_hz(midi: int) -> float:
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


def _cents(measured_hz: float, target_hz: float) -> float:
    if measured_hz <= 0.0 or target_hz <= 0.0:
        raise AcousticMetricError("non-positive frequency")
    return 1200.0 * math.log2(measured_hz / target_hz)


def _median(values: list[float]) -> float:
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) / 2.0


def _object(payload: bytes, name: str) -> dict:
    try:
        value = json.loads(payload)
    except (ValueError, UnicodeError) as error:
        raise AcousticMetricError(name + ": invalid JSON: " + str(error)) from error
    if not isinstance(value, dict):
        raise AcousticMetricError(name + ": expected an object")
    return value


@dataclass(frozen=True, slots=True)
class PlacementTiming:
    """One requested placement and the frames the renderer actually produced."""

    unit_id: str
    target_midi: int
    note_on_frame: int
    desired_vowel_onset_frame: int
    destination_start_frame: int
    destination_end_frame: int
    aligned_start_frame: int | None
    vowel_onset_frame: int | None
    frame_count: int | None
    requested_renderer: str
    actual_renderer: str
    used_fallback: bool


@dataclass(frozen=True, slots=True)
class PhraseMeasurement:
    phrase_id: str
    sample_rate: int
    placements: tuple[PlacementTiming, ...]
    pitch_frames: tuple[dict, ...]


@dataclass(frozen=True, slots=True)
class RendererSubstitution:
    """One placement whose actual renderer disagrees with the requested one."""

    unit_id: str
    requested_renderer: str
    actual_renderer: str
    reported_fallback: bool


@dataclass(frozen=True, slots=True)
class RendererAudit:
    """Whether any placement was silently substituted, as V06 requires."""

    placements: int
    fallbacks: int
    substitutions: tuple[RendererSubstitution, ...]

    def hidden_substitutions(self) -> tuple[RendererSubstitution, ...]:
        """A change of backend that the record did not report as a fallback.

        A reported fallback is a truthful degradation. An unreported change of
        renderer is the hidden fallback V06 forbids, because the exported audio
        would then come from a backend the record does not name.
        """
        return tuple(item for item in self.substitutions if not item.reported_fallback)

    def has_hidden_fallback(self) -> bool:
        return bool(self.hidden_substitutions())

    def to_json(self) -> str:
        return json.dumps({
            "placements": self.placements,
            "fallbacks": self.fallbacks,
            "substitutions": [
                {
                    "unit_id": item.unit_id,
                    "requested_renderer": item.requested_renderer,
                    "actual_renderer": item.actual_renderer,
                    "reported_fallback": item.reported_fallback,
                }
                for item in self.substitutions
            ],
            "has_hidden_fallback": self.has_hidden_fallback(),
        }, indent=2)


def audit_renderer_substitutions(placements: tuple[PlacementTiming, ...]) -> RendererAudit:
    """Compare each placement requested and actual renderer name.

    The comparison is by name, so a placement that asked for classic-psola and
    was served by raw is a substitution regardless of how it was reported. Only
    substitutions that were not reported as a fallback are hidden.
    """
    substitutions = []
    fallbacks = 0
    for placement in placements:
        if placement.used_fallback:
            fallbacks += 1
        if placement.requested_renderer != placement.actual_renderer:
            substitutions.append(RendererSubstitution(
                unit_id=placement.unit_id,
                requested_renderer=placement.requested_renderer,
                actual_renderer=placement.actual_renderer,
                reported_fallback=placement.used_fallback,
            ))
    return RendererAudit(
        placements=len(placements),
        fallbacks=fallbacks,
        substitutions=tuple(substitutions),
    )
    phrase_id: str
    sample_rate: int
    placements: tuple[PlacementTiming, ...]
    pitch_frames: tuple[dict, ...]


def _optional_int(value: object) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    return value


def load_phrase_measurements(case_directory: Path) -> tuple[PhraseMeasurement, ...]:
    diagnostics = _object(read_bounded(case_directory / "diagnostics.json"), "diagnostics.json")
    sample_rate = diagnostics.get("sample_rate")
    if isinstance(sample_rate, bool) or not isinstance(sample_rate, int) or sample_rate <= 0:
        raise AcousticMetricError("diagnostics.json: sample_rate must be a positive integer")
    analysis = _object(read_bounded(case_directory / "analysis" / "analysis.json"), "analysis.json")
    progress = analysis.get("pitchFrames")
    if not isinstance(progress, list) or not progress:
        raise AcousticMetricError("analysis.json: pitchFrames must be a nonempty array")
    frames = tuple(progress)
    phrases = diagnostics.get("phrases")
    if not isinstance(phrases, list) or not phrases:
        raise AcousticMetricError("diagnostics.json: phrases must be a nonempty array")
    result = []
    for phrase in phrases:
        if not isinstance(phrase, dict):
            raise AcousticMetricError("diagnostics.json: phrase must be an object")
        targets = phrase.get("target_timing")
        rendered = phrase.get("rendered_placements")
        if not isinstance(targets, list) or not isinstance(rendered, list):
            raise AcousticMetricError("diagnostics.json: phrase timing arrays are missing")
        if len(targets) != len(rendered):
            raise AcousticMetricError("diagnostics.json: target and placement counts differ")
        placements = []
        for target, placement in zip(targets, rendered):
            if not isinstance(target, dict) or not isinstance(placement, dict):
                raise AcousticMetricError("diagnostics.json: placement must be an object")
            placements.append(PlacementTiming(
                unit_id=str(target.get("unit_id", "")),
                target_midi=int(target.get("target_midi", -1)),
                note_on_frame=int(target.get("note_on_frame", -1)),
                desired_vowel_onset_frame=int(target.get("desired_vowel_onset_frame", -1)),
                destination_start_frame=int(target.get("destination_start_frame", -1)),
                destination_end_frame=int(target.get("destination_end_frame", -1)),
                aligned_start_frame=_optional_int(placement.get("aligned_start_frame")),
                vowel_onset_frame=_optional_int(placement.get("vowel_onset_frame")),
                frame_count=_optional_int(placement.get("frame_count")),
                requested_renderer=str(placement.get("requested_renderer", "")),
                actual_renderer=str(placement.get("actual_renderer", "")),
                used_fallback=bool(placement.get("used_fallback", False)),
            ))
        result.append(PhraseMeasurement(
            phrase_id=str(phrase.get("phrase_id", "")),
            sample_rate=sample_rate,
            placements=tuple(placements),
            pitch_frames=frames,
        ))
    return tuple(result)


@dataclass(frozen=True, slots=True)
class PitchErrorReport:
    """Cents error over voiced frames in each placement steady span."""

    scored_frames: int
    voiced_frames: int
    low_confidence_frames: int
    unvoiced_frames: int
    median_absolute_cents: float | None
    within_50_percent: float | None
    octave_errors: int
    saturated_frames: int = 0

    def within_limits(self) -> bool:
        return (self.scored_frames > 0
                and self.median_absolute_cents is not None
                and self.median_absolute_cents <= MEDIAN_CENTS_LIMIT
                and self.within_50_percent is not None
                and self.within_50_percent >= WITHIN_50_PERCENT_LIMIT
                and self.octave_errors == 0)


def _collect_cents(measured: PhraseMeasurement,
                   minimum_confidence: float = MINIMUM_CONFIDENCE) -> tuple[list[float], int, int, int, int, int]:
    errors: list[float] = []
    voiced = low_confidence = unvoiced = octave_errors = saturated = 0
    for placement in measured.placements:
        if placement.target_midi < 0 or placement.target_midi > 127:
            continue
        start = placement.vowel_onset_frame
        end = placement.destination_end_frame
        if start is None or end <= start:
            start = placement.destination_start_frame
            end = placement.destination_end_frame
        target_hz = _midi_hz(placement.target_midi)
        for frame in measured.pitch_frames:
            index = frame.get("sourceFrame")
            if not isinstance(index, int) or index < start or index >= end:
                continue
            if not frame.get("voiced"):
                unvoiced += 1
                continue
            voiced += 1
            confidence = float(frame.get("confidence", 0.0))
            f0 = float(frame.get("f0Hz", 0.0))
            if confidence < minimum_confidence or f0 <= 0.0:
                low_confidence += 1
                continue
            if f0 >= ANALYZER_CEILING_HZ:
                saturated += 1
                continue
            error = _cents(f0, target_hz)
            errors.append(error)
            if abs(error) >= 600.0:
                octave_errors += 1
    return errors, voiced, low_confidence, unvoiced, octave_errors, saturated


def _summarise(errors: list[float], voiced: int, low_confidence: int,
               unvoiced: int, octave_errors: int, saturated: int = 0) -> PitchErrorReport:
    if not errors:
        return PitchErrorReport(0, voiced, low_confidence, unvoiced, None, None, octave_errors, saturated)
    magnitudes = sorted(abs(value) for value in errors)
    middle = len(magnitudes) // 2
    median = (magnitudes[middle] if len(magnitudes) % 2
              else (magnitudes[middle - 1] + magnitudes[middle]) / 2.0)
    within = 100.0 * sum(1 for value in magnitudes if value <= WITHIN_50_CENTS) / len(magnitudes)
    return PitchErrorReport(len(errors), voiced, low_confidence, unvoiced,
                            median, within, octave_errors, saturated)


def measure_pitch_error(measured: PhraseMeasurement,
                        minimum_confidence: float = MINIMUM_CONFIDENCE) -> PitchErrorReport:
    """Score voiced frames within each placement against its own target note.

    The steady span for a placement is the half-open range between the vowel
    onset and the destination end, which excludes the consonant transition that
    is expected to carry pitch movement.
    """
    errors, voiced, low_confidence, unvoiced, octave_errors, saturated = _collect_cents(
        measured, minimum_confidence)
    return _summarise(errors, voiced, low_confidence, unvoiced, octave_errors, saturated)


@dataclass(frozen=True, slots=True)
class TimingPlacement:
    unit_id: str
    requested_start_frame: int
    aligned_start_frame: int | None
    vowel_onset_frame: int | None
    desired_vowel_onset_frame: int

    @property
    def vowel_displacement(self) -> int | None:
        if self.vowel_onset_frame is None:
            return None
        return self.vowel_onset_frame - self.desired_vowel_onset_frame


def measure_timing_placement(measured: PhraseMeasurement) -> tuple[TimingPlacement, ...]:
    return tuple(
        TimingPlacement(
            unit_id=placement.unit_id,
            requested_start_frame=placement.destination_start_frame,
            aligned_start_frame=placement.aligned_start_frame,
            vowel_onset_frame=placement.vowel_onset_frame,
            desired_vowel_onset_frame=placement.desired_vowel_onset_frame,
        )
        for placement in measured.placements
    )


def timing_displacement_within_limit(placements: tuple[TimingPlacement, ...],
                                     limit: int = DISPLACEMENT_LIMIT_SAMPLES) -> bool:
    """A placement vowel onset must land on its requested anchor within one sample.

    This is a deterministic placement property, not an acoustic boundary
    tolerance.
    """
    measured = [placement for placement in placements if placement.vowel_displacement is not None]
    if not measured:
        return False
    return all(abs(placement.vowel_displacement) <= limit for placement in measured)


@dataclass(frozen=True, slots=True)
class CaseAcousticSummary:
    case_id: str
    pitch: PitchErrorReport
    placements: tuple[TimingPlacement, ...]
    used_fallback: bool
    timing_displacement_ok: bool
    renderers: RendererAudit = RendererAudit(0, 0, ())

    def to_json(self) -> str:
        payload = {
            "schema_version": 1,
            "evidence_class": "auditory-diagnostic-measurement",
            "qualification": "none",
            "case_id": self.case_id,
            "pitch": {
                "threshold_median_cents": MEDIAN_CENTS_LIMIT,
                "threshold_within_50_percent": WITHIN_50_PERCENT_LIMIT,
                "scored_frames": self.pitch.scored_frames,
                "voiced_frames": self.pitch.voiced_frames,
                "low_confidence_frames": self.pitch.low_confidence_frames,
                "unvoiced_frames": self.pitch.unvoiced_frames,
                "median_absolute_cents": self.pitch.median_absolute_cents,
                "within_50_percent": self.pitch.within_50_percent,
                "octave_errors": self.pitch.octave_errors,
                "saturated_frames": self.pitch.saturated_frames,
                "analyzer_ceiling_hz": ANALYZER_CEILING_HZ,
                "within_frozen_limits": self.pitch.within_limits(),
            },
            "timing": {
                "threshold_samples": DISPLACEMENT_LIMIT_SAMPLES,
                "within_frozen_limit": self.timing_displacement_ok,
                "placements": [
                    {
                        "unit_id": placement.unit_id,
                        "requested_start_frame": placement.requested_start_frame,
                        "aligned_start_frame": placement.aligned_start_frame,
                        "vowel_onset_frame": placement.vowel_onset_frame,
                        "desired_vowel_onset_frame": placement.desired_vowel_onset_frame,
                        "vowel_displacement_samples": placement.vowel_displacement,
                    }
                    for placement in self.placements
                ],
            },
            "used_fallback": self.used_fallback,
            "renderers": json.loads(self.renderers.to_json()),
        }
        return json.dumps(payload, indent=2) + "\n"


def summarise_case(case_directory: Path, case_id: str) -> CaseAcousticSummary:
    phrases = load_phrase_measurements(case_directory)
    all_placements = tuple(
        placement for phrase in phrases for placement in phrase.placements)
    errors: list[float] = []
    voiced = low_confidence = unvoiced = octave_errors = saturated = 0
    placements: list[TimingPlacement] = []
    used_fallback = False
    for phrase in phrases:
        (local_errors, local_voiced, local_low, local_unvoiced, local_octave,
         local_saturated) = _collect_cents(phrase)
        errors.extend(local_errors)
        voiced += local_voiced
        low_confidence += local_low
        unvoiced += local_unvoiced
        octave_errors += local_octave
        saturated += local_saturated
        placements.extend(measure_timing_placement(phrase))
        used_fallback = used_fallback or any(
            placement.used_fallback for placement in phrase.placements)
    pitch = _summarise(errors, voiced, low_confidence, unvoiced, octave_errors, saturated)
    return CaseAcousticSummary(
        case_id=case_id,
        pitch=pitch,
        placements=tuple(placements),
        used_fallback=used_fallback,
        timing_displacement_ok=timing_displacement_within_limit(tuple(placements)),
        renderers=audit_renderer_substitutions(all_placements),
    )


@dataclass(frozen=True, slots=True)
class PilotNoteMeasurement:
    """One score note measured from a singer-pilot pitch record."""

    note_index: int
    expected_hz: float
    analysis_frames: int
    voiced_frames: int
    within_50_cents_frames: int
    large_pitch_error_frames: int
    median_absolute_cents: float | None


@dataclass(frozen=True, slots=True)
class PilotPitchReport:
    """Declared-range transposition measured from a pilot pitch record."""

    variant: str
    audio_sha256: str
    status: str
    notes: tuple[PilotNoteMeasurement, ...]

    def median_absolute_cents(self) -> float | None:
        values = [note.median_absolute_cents for note in self.notes
                  if note.median_absolute_cents is not None]
        return _median(values) if values else None

    def within_50_percent(self) -> float | None:
        scored = sum(note.within_50_cents_frames for note in self.notes)
        voiced = sum(note.voiced_frames for note in self.notes)
        if voiced <= 0:
            return None
        return 100.0 * scored / voiced

    def large_pitch_error_frames(self) -> int:
        return sum(note.large_pitch_error_frames for note in self.notes)

    def lowest_target_hz(self) -> float | None:
        return min((note.expected_hz for note in self.notes), default=None)

    def highest_target_hz(self) -> float | None:
        return max((note.expected_hz for note in self.notes), default=None)

    def within_limits(self) -> bool:
        median = self.median_absolute_cents()
        within = self.within_50_percent()
        return (median is not None and median <= MEDIAN_CENTS_LIMIT
                and within is not None and within >= WITHIN_50_PERCENT_LIMIT
                and self.large_pitch_error_frames() == 0)

    def to_json(self) -> str:
        payload = {
            "schema_version": 1,
            "evidence_class": "auditory-diagnostic-measurement",
            "qualification": "none",
            "source": "singer-pilot",
            "variant": self.variant,
            "status": self.status,
            "audio_sha256": self.audio_sha256,
            "threshold_median_cents": MEDIAN_CENTS_LIMIT,
            "threshold_within_50_percent": WITHIN_50_PERCENT_LIMIT,
            "note_count": len(self.notes),
            "lowest_target_hz": self.lowest_target_hz(),
            "highest_target_hz": self.highest_target_hz(),
            "median_absolute_cents": self.median_absolute_cents(),
            "within_50_percent": self.within_50_percent(),
            "large_pitch_error_frames": self.large_pitch_error_frames(),
            "within_frozen_limits": self.within_limits(),
            "notes": [
                {
                    "note_index": note.note_index,
                    "expected_hz": note.expected_hz,
                    "analysis_frames": note.analysis_frames,
                    "voiced_frames": note.voiced_frames,
                    "within_50_cents_frames": note.within_50_cents_frames,
                    "large_pitch_error_frames": note.large_pitch_error_frames,
                    "median_absolute_cents": note.median_absolute_cents,
                }
                for note in self.notes
            ],
        }
        return json.dumps(payload, indent=2) + "\n"


def load_pilot_pitch(path: Path, variant: str) -> PilotPitchReport:
    """Read one singer-pilot <name>-pitch.json record.

    The pilot already refuses to present its own record as a qualification; this
    loader keeps that boundary and only re-expresses the retained per-note numbers
    against the frozen thresholds.
    """
    payload = _object(read_bounded(path), path.name)
    raw_notes = payload.get("notes")
    if not isinstance(raw_notes, list) or not raw_notes:
        raise AcousticMetricError(path.name + ": notes must be a nonempty array")
    notes = []
    for entry in raw_notes:
        if not isinstance(entry, dict):
            raise AcousticMetricError(path.name + ": note must be an object")
        expected = entry.get("expectedHz")
        if not isinstance(expected, (int, float)) or isinstance(expected, bool) or expected <= 0.0:
            raise AcousticMetricError(path.name + ": expectedHz must be a positive number")
        median = entry.get("medianAbsoluteCents")
        if isinstance(median, bool) or not isinstance(median, (int, float)):
            median = None
        else:
            median = float(median)
        notes.append(PilotNoteMeasurement(
            note_index=int(entry.get("noteIndex", -1)),
            expected_hz=float(expected),
            analysis_frames=int(entry.get("analysisFrames", 0)),
            voiced_frames=int(entry.get("voicedFrames", 0)),
            within_50_cents_frames=int(entry.get("within50CentsFrames", 0)),
            large_pitch_error_frames=int(entry.get("largePitchErrorFrames", 0)),
            median_absolute_cents=median,
        ))
    return PilotPitchReport(
        variant=variant,
        audio_sha256=str(payload.get("audioSha256", "")),
        status=str(payload.get("status", "")),
        notes=tuple(notes),
    )
