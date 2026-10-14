"""Copy unassigned PCM16 donor spans from hash-bound, unreviewed phone labels."""

from __future__ import annotations

import argparse
from decimal import Decimal, ROUND_CEILING
import json
from pathlib import Path
import re
import struct
import sys
import wave

if __package__:
    from .prepare_sustain_cuts import bound_file, digest, require, strict_json
else:
    from prepare_sustain_cuts import bound_file, digest, require, strict_json

LABEL_CONTRACT = {
    "format": "festvox-end-times-v1",
    "annotationStatus": "AUTOMATIC_UNREVIEWED",
    "timeUnit": "seconds",
    "boundaryRule": "ceil-seconds-times-sample-rate-v1",
}
MAXIMUM_PLAN_BYTES = 1024 * 1024
MAXIMUM_LABEL_BYTES = 1024 * 1024
MAXIMUM_AUDIO_BYTES = 64 * 1024 * 1024
MAXIMUM_TOTAL_AUDIO_BYTES = 128 * 1024 * 1024
MAXIMUM_LABEL_ROWS = 16384
MAXIMUM_SPANS = 4096
MAXIMUM_TOTAL_DONOR_BYTES = 128 * 1024 * 1024
ID = re.compile(r"[a-z0-9][a-z0-9_-]{0,79}\Z")
SECONDS = re.compile(r"(?:0|[1-9][0-9]{0,6})(?:\.[0-9]{1,9})?\Z")
PHONE = re.compile(r"[A-Za-z0-9_@+.-]{1,32}\Z")


def pcm16_snapshot(raw: bytes) -> tuple[bytes, int, int]:
    require(len(raw) >= 12 and raw[:4] == b"RIFF" and raw[8:12] == b"WAVE" and
            struct.unpack_from("<I", raw, 4)[0] + 8 == len(raw), "Invalid RIFF/WAV container extent")
    offset, fmt, pcm = 12, None, None
    while offset < len(raw):
        require(offset + 8 <= len(raw), "Truncated WAV chunk header")
        kind, size = raw[offset:offset + 4], struct.unpack_from("<I", raw, offset + 4)[0]
        start = offset + 8
        stop = start + size
        offset = stop + (size % 2)
        require(offset <= len(raw), "Truncated WAV chunk or padding")
        if kind == b"fmt ":
            require(fmt is None and size >= 16, "Repeated or short WAV format")
            fmt = struct.unpack_from("<HHIIHH", raw, start)
        elif kind == b"data":
            require(pcm is None, "Repeated WAV data")
            pcm = raw[start:stop]
    require(fmt is not None and pcm is not None, "Missing WAV format or data")
    encoding, channels, rate, byte_rate, alignment, bits = fmt
    require(encoding == 1 and channels == 1 and bits == 16,
            "This donor route supports standard mono PCM16 only")
    require(8000 <= rate <= 192000 and alignment == 2 and byte_rate == rate * 2 and
            len(pcm) > 0 and len(pcm) % 2 == 0, "Invalid PCM format or extent")
    return pcm, rate, len(pcm) // 2


def read_labels(raw: bytes, rate: int, frames: int) -> list[dict]:
    rows = []
    previous = "0"
    for line in raw.decode("ascii").splitlines():
        require(len(line) <= 128, "Label line exceeds bounds")
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split()
        require(len(fields) == 3, "Expected end-seconds, opaque column and source phone")
        end, opaque, phone = fields
        require(SECONDS.fullmatch(end) is not None and PHONE.fullmatch(phone) is not None,
                "Invalid label time or source phone")
        require(re.fullmatch(r"[0-9]{1,3}", opaque) is not None and int(opaque) <= 255,
                "Invalid opaque label column")
        start_time, end_time = Decimal(previous), Decimal(end)
        require(start_time <= end_time and end_time * rate <= frames,
                "Label time order or extent mismatch")
        start_frame = int((start_time * rate).to_integral_value(rounding=ROUND_CEILING))
        end_frame = int((end_time * rate).to_integral_value(rounding=ROUND_CEILING))
        rows.append({"index": len(rows), "startSeconds": previous, "endSeconds": end,
                     "startFrame": start_frame, "endFrameExclusive": end_frame,
                     "sourcePhone": phone, "opaqueColumn": opaque})
        require(len(rows) <= MAXIMUM_LABEL_ROWS, "Too many label rows")
        previous = end
    require(bool(rows), "No source phone labels")
    return rows


def prepare(plan_path: Path, output: Path) -> dict:
    require(not output.exists() and not output.is_symlink(), "Output already exists; no overwrite")
    require(plan_path.is_file() and not plan_path.is_symlink() and
            plan_path.stat().st_size <= MAXIMUM_PLAN_BYTES, "Missing, symlink or oversized plan")
    raw_plan = plan_path.read_bytes()
    require(len(raw_plan) <= MAXIMUM_PLAN_BYTES, "Plan grew beyond its byte limit")
    plan = strict_json(raw_plan)
    require(set(plan) == {"schemaVersion", "documentKind", "language", "labelContract", "license", "sources", "spans"},
            "Donor plan has missing or unknown fields")
    require(type(plan.get("schemaVersion")) is int and plan["schemaVersion"] == 1 and
            plan.get("documentKind") == "SourceLabelDonorPlan" and
            plan.get("language") == "en" and plan.get("labelContract") == LABEL_CONTRACT,
            "Unsupported donor plan or label contract")
    root = plan_path.resolve().parent
    license_record = plan.get("license")
    require(isinstance(license_record, dict) and
            set(license_record) == {"path", "sha256", "sourceUrl", "attribution"} and
            all(isinstance(license_record.get(key), str) and license_record[key].strip()
                for key in ("sourceUrl", "attribution")), "License provenance and attribution are required")
    _, license_bytes = bound_file(root, license_record, MAXIMUM_PLAN_BYTES)
    require(bool(license_bytes.decode("utf-8").strip()), "License text must not be empty")
    sources = plan.get("sources")
    require(isinstance(sources, list) and 1 <= len(sources) <= 32, "Expected 1 to 32 source records")
    held = {}
    hashes = set()
    total_bytes = 0
    for source in sources:
        require(isinstance(source, dict), "Invalid source record")
        require(set(source) == {"id", "sourceType", "audio", "labels"}, "Source has missing or unknown fields")
        identifier = source.get("id")
        require(isinstance(identifier, str) and ID.fullmatch(identifier) is not None and
                identifier not in held, "Invalid or duplicate source id")
        require(source.get("sourceType") == "speech", "This route requires explicit speech source type")
        require(all(isinstance(source[key], dict) and set(source[key]) == {"path", "sha256"}
                    for key in ("audio", "labels")), "Invalid bound source record fields")
        _, audio = bound_file(root, source["audio"], MAXIMUM_AUDIO_BYTES)
        require(digest(audio) not in hashes, "Duplicate source audio hash")
        hashes.add(digest(audio))
        total_bytes += len(audio)
        require(total_bytes <= MAXIMUM_TOTAL_AUDIO_BYTES, "Total source audio exceeds bounds")
        pcm, rate, frames = pcm16_snapshot(audio)
        _, label_bytes = bound_file(root, source["labels"], MAXIMUM_LABEL_BYTES)
        rows = read_labels(label_bytes, rate, frames)
        held[identifier] = (source, pcm, rate, rows)
    spans = plan.get("spans")
    require(isinstance(spans, list) and 1 <= len(spans) <= MAXIMUM_SPANS, "Expected 1 to 4096 donor spans")
    prepared = []
    selected = set()
    donor_bytes = 0
    for span in spans:
        require(isinstance(span, dict), "Invalid span record")
        require(set(span) == {"sourceId", "firstLabelIndex", "labelCount"}, "Span has missing or unknown fields")
        source_id = span.get("sourceId")
        require(isinstance(source_id, str) and source_id in held, "Unknown span source id")
        first, count = span.get("firstLabelIndex"), span.get("labelCount")
        require(type(first) is int and first >= 0 and type(count) is int and 1 <= count <= 3,
                "Invalid label span index or count")
        source, pcm, rate, rows = held[source_id]
        require(first + count <= len(rows), "Label span exceeds source labels")
        key = (source_id, first, count)
        require(key not in selected, "Duplicate donor span")
        selected.add(key)
        labels = rows[first:first + count]
        start, end = labels[0]["startFrame"], labels[-1]["endFrameExclusive"]
        require(0 <= start < end <= len(pcm) // 2, "Selected label span has empty or invalid PCM extent")
        donor_bytes += (end - start) * 2
        require(donor_bytes <= MAXIMUM_TOTAL_DONOR_BYTES, "Total donor audio exceeds bounds")
        prepared.append((source, pcm[start * 2:end * 2], rate, labels, first, count))

    # Every input and requested span is checked before the exclusively owned directory is created.
    output.mkdir()
    marker = output / "INCOMPLETE"
    marker.write_text("Preparation has not completed. Do not import or approve this directory.\n")
    (output / "license-source.txt").write_bytes(license_bytes)
    (output / "intake-plan.json").write_bytes(raw_plan)
    change_notice = "Source label spans copied into new mono PCM16 WAV containers. Ancillary chunks removed; " \
                    "no resampling, normalization, pitch shift, phone alias or bank assignment."
    # Keep the caller's explicit attribution beside the derived audio, including original author
    # names where the source license requires them. These supplied words are not rights approval.
    (output / "attribution.txt").write_text(
        license_record["attribution"] + "\nSource license: " + license_record["sourceUrl"] +
        "\nOriginal license text: license-source.txt\nModifications: " + change_notice +
        "\nUnassigned and unapproved. Supplied attribution does not establish performer rights.\n",
        encoding="utf-8")
    proposals = []
    for source, pcm, rate, labels, first, count in prepared:
        name = f"{source['id']}_l{first:05d}_n{count}.wav"
        target = output / name
        with wave.open(str(target), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(rate)
            wav.writeframes(pcm)
        proposals.append({"id": target.stem, "derivedPath": name,
                          "derivedSha256": digest(target.read_bytes()), "pcmSha256": digest(pcm),
                          "sourceId": source["id"], "sourceType": "speech",
                          "sourceAudio": source["audio"], "sourceLabels": source["labels"],
                          "firstLabelIndex": first, "labelCount": count, "sourceAnnotations": labels,
                          "sourcePhones": [row["sourcePhone"] for row in labels],
                          "startFrame": labels[0]["startFrame"], "endFrameExclusive": labels[-1]["endFrameExclusive"],
                          "sampleRate": rate, "frames": len(pcm) // 2,
                          "durationSeconds": len(pcm) / (2 * rate), "nonzeroPcm": any(pcm),
                          "assignment": "NOT_ASSIGNED", "pitchReview": "NOT_REVIEWED",
                          "phoneticReview": "NOT_REVIEWED", "listeningReview": "NOT_REVIEWED"})
    result = {"schemaVersion": 1, "documentKind": "UnassignedPhoneDonorProposals", "status": "UNAPPROVED",
              "language": "en", "planSha256": digest(raw_plan), "labelContract": LABEL_CONTRACT,
              "license": license_record, "retainedLicensePath": "license-source.txt",
              "retainedAttributionPath": "attribution.txt", "changeNotice": change_notice,
              "producerRegistration": "NOT_REGISTERED", "performerRights": "NOT_ESTABLISHED",
              "rightsClearance": "NOT_APPROVED", "bankAdmission": "NOT_APPROVED", "proposals": proposals}
    (output / "donors.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    marker.unlink()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = prepare(args.plan, args.output)
    except (ValueError, OSError, KeyError, TypeError, EOFError, ArithmeticError, RecursionError, wave.Error) as error:
        print(f"Preparation refused: {error}", file=sys.stderr)
        return 2
    print(f"Prepared {len(result['proposals'])} unassigned, unreviewed phone donors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
