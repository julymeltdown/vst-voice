"""Partial GAN epoch cursor contract, never epoch completion or training authority.

The caller derives the plan from freshly admitted train sources. This module owns
only deterministic segment position and metric accounting, not model/RNG storage.
"""
import hashlib
import math
import re

from .__main__ import encode_report
from .vocoder_batches import segment_frame_ranges


def _digest(value):
    if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise ValueError("Recovery identity must be a captured SHA-256")
    return value


def build_recovery_plan(sources, *, dataset_sha256, profile_sha256, run_sha256,
                        segment_frames, hop_size):
    """Canonical source ordering matches the sharded training batch reader."""
    for value in (dataset_sha256, profile_sha256, run_sha256):
        _digest(value)
    if (not isinstance(sources, list) or not 1 <= len(sources) <= 10000
            or type(hop_size) is not int or not 1 <= hop_size <= 4096):
        raise ValueError("Recovery plan requires bounded train sources and hop size")
    seen, rows = set(), []
    for source in sources:
        if not isinstance(source, dict) or set(source) != {"sourceId", "analysisFrames", "sourceSamples"}:
            raise ValueError("Recovery source geometry has unexpected fields")
        identity, frames, samples = source["sourceId"], source["analysisFrames"], source["sourceSamples"]
        if (not isinstance(identity, str) or not 1 <= len(identity.encode()) <= 256
                or any(ord(c) < 32 or ord(c) == 127 for c in identity) or identity in seen
                or type(samples) is not int or not 1 <= samples <= 1048576
                or type(frames) is not int or frames != (samples + hop_size - 1) // hop_size):
            raise ValueError("Recovery source identity or frame geometry differs")
        seen.add(identity)
        for begin, end in segment_frame_ranges(frames, segment_frames):
            if len(rows) >= 100000:
                raise ValueError("Recovery plan exceeds the bounded update inventory")
            rows.append(dict(sourceId=identity, frameOffset=begin, frameCount=end-begin,
                             validSamples=min((end-begin)*hop_size, samples-begin*hop_size)))
    rows.sort(key=lambda row: (row["sourceId"], row["frameOffset"]))
    if len(rows) > 100000:
        raise ValueError("Recovery plan exceeds the bounded update inventory")
    body = dict(formatId="com.project-seam.vocoder-recovery-plan", schemaVersion=1,
                datasetSha256=dataset_sha256, profileSha256=profile_sha256, runSha256=run_sha256,
                segmentFrames=segment_frames, hopSize=hop_size, segments=rows)
    return dict(body, planSha256=hashlib.sha256(encode_report(body)).hexdigest())


def _verify_plan(plan):
    # Rebuild the exact canonical geometry, not just its self-asserted hash.
    fields = {"formatId", "schemaVersion", "datasetSha256", "profileSha256", "runSha256",
              "segmentFrames", "hopSize", "segments", "planSha256"}
    if not isinstance(plan, dict) or set(plan) != fields:
        raise ValueError("Invalid recovery plan fields")
    segments = plan["segments"]
    if not isinstance(segments, list) or not 1 <= len(segments) <= 100000:
        raise ValueError("Invalid recovery segment inventory")
    sources = {}
    for row in segments:
        if not isinstance(row, dict) or set(row) != {"sourceId", "frameOffset", "frameCount", "validSamples"}:
            raise ValueError("Invalid recovery segment fields")
        if (not isinstance(row["sourceId"], str)
                or any(type(row[key]) is not int or row[key] < 0
                       for key in ("frameOffset", "frameCount", "validSamples"))):
            raise ValueError("Invalid recovery segment geometry")
        source = sources.setdefault(row["sourceId"], dict(sourceId=row["sourceId"], analysisFrames=0, sourceSamples=0))
        source["analysisFrames"] += row["frameCount"]
        source["sourceSamples"] += row["validSamples"]
    expected = build_recovery_plan(list(sources.values()), dataset_sha256=plan["datasetSha256"],
        profile_sha256=plan["profileSha256"], run_sha256=plan["runSha256"],
        segment_frames=plan["segmentFrames"], hop_size=plan["hopSize"])
    if encode_report(expected) != encode_report(plan):
        raise ValueError("Recovery plan is not canonical complete source geometry")


def partial_cursor(plan, *, completed_updates, generator_loss_sum, discriminator_loss_sum):
    _verify_plan(plan)
    if (type(completed_updates) is not int or not 1 <= completed_updates < len(plan["segments"])
            or any(type(value) not in (int, float) or not math.isfinite(value)
                   for value in (generator_loss_sum, discriminator_loss_sum))):
        raise ValueError("Recovery cursor requires a strict nonempty epoch prefix and finite loss sums")
    covered, updates = {}, {}
    for row in plan["segments"][:completed_updates]:
        identity = row["sourceId"]
        covered[identity] = covered.get(identity, 0) + row["validSamples"]
        updates[identity] = updates.get(identity, 0) + 1
    return dict(formatId="com.project-seam.vocoder-partial-epoch", schemaVersion=1,
                planSha256=plan["planSha256"], datasetSha256=plan["datasetSha256"],
                profileSha256=plan["profileSha256"], runSha256=plan["runSha256"],
                completedUpdates=completed_updates, plannedUpdates=len(plan["segments"]),
                nextSegment=plan["segments"][completed_updates].copy(),
                coveredSourceSamples=covered, sourceUpdates=updates, validSamples=sum(covered.values()),
                generatorLossSum=generator_loss_sum, discriminatorLossSum=discriminator_loss_sum,
                epochComplete=False, coverageVerified=False, trainingAdmitted=False, releaseEligible=False)


def verify_partial_cursor(cursor, plan):
    if not isinstance(cursor, dict):
        raise ValueError("Recovery cursor must be an object")
    try:
        expected = partial_cursor(plan, completed_updates=cursor["completedUpdates"],
            generator_loss_sum=cursor["generatorLossSum"], discriminator_loss_sum=cursor["discriminatorLossSum"])
    except KeyError as error:
        raise ValueError("Recovery cursor is incomplete") from error
    if encode_report(cursor) != encode_report(expected):
        raise ValueError("Recovery cursor differs from exact admitted segment prefix")
    return expected
