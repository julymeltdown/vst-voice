"""Audit declared local checkpoint ancestry, not singer or holdout qualification.

Receipts survive binary retention. This traces their declared dataset histories;
it does not restore pruned weights or prove that an undeclared pretrain did not
occur. Source/recipe provenance and an independently frozen cohort remain separate.
"""
import argparse
import hashlib
import json
from pathlib import Path

from .__main__ import encode_report, load_config, publish_new
from .pitch_comparison import _digest
from .split import split_sources


SOURCE_FIELDS = ("sourceId", "songId", "sessionId", "lineageId", "audioSha256")


def trace(leaf, resolve):
    """resolve(digest) must return a receipt captured against that exact digest."""
    chain, seen, current = [], set(), leaf
    while current is not None:
        if not _digest(current) or current in seen or len(chain) >= 256:
            raise ValueError("Invalid, cyclic or oversized checkpoint ancestry")
        seen.add(current)
        receipt = resolve(current)
        metadata, epoch = receipt["metadata"], receipt["epoch"]
        captured = json.dumps(dict(metadata=metadata, epoch=epoch), sort_keys=True,
            ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()
        run = metadata["run"]
        number = run.get("completedEpochs")
        if (receipt.get("formatId") not in ("com.project-seam.training-checkpoint", "com.project-seam.gan-checkpoint")
                or type(receipt.get("schemaVersion")) is not int or receipt["schemaVersion"] != 1
                or len(captured) > 1024 * 1024
                or hashlib.sha256(captured).hexdigest() != receipt.get("metadataSha256")
                or epoch.get("epochComplete") is not True or epoch.get("coverageVerified") is not True
                or epoch.get("datasetSha256") != metadata.get("datasetSha256")
                or hashlib.sha256(encode_report(metadata["datasetBindings"])).hexdigest() != metadata["datasetSha256"]
                or type(number) is not int or not 1 <= number < 100000
                or "parentReceiptSha256" not in run):
            raise ValueError("Expected complete, internally bound checkpoint ancestry")
        if chain:
            child = chain[-1]["receipt"]
            child_run = child["metadata"]["run"]
            if child["formatId"] != receipt["formatId"]:
                raise ValueError("Checkpoint model family changed across ancestry")
            if child_run["completedEpochs"] == 1:
                origin = child_run.get("warmStart", {})
                if (origin.get("sourceReceiptSha256") != current
                        or origin.get("sourceCompletedEpochs") != number
                        or origin.get("sourceCheckpointSha256") != receipt.get("checkpointSha256")
                        or origin.get("sourceTrainingConfigurationSha256") != run.get("trainingConfigurationSha256")
                        or origin.get("optimizerReset") is not True or origin.get("rngReset") is not True):
                    raise ValueError("Warm-start ancestry differs from its captured origin")
            elif (child_run["completedEpochs"] != number + 1
                    or {k: v for k, v in child_run.items() if k not in ("completedEpochs", "parentReceiptSha256")}
                    != {k: v for k, v in run.items() if k not in ("completedEpochs", "parentReceiptSha256")}
                    or child["metadata"]["datasetSha256"] != metadata["datasetSha256"]):
                raise ValueError("Exact-resume ancestry changed settings, dataset or epoch numbering")
        chain.append(dict(receiptSha256=current, receipt=receipt))
        current = run["parentReceiptSha256"]
        if current is None and (number != 1 or "warmStart" in run):
            raise ValueError("Checkpoint history ends before its declared origin")
    if not chain:
        raise ValueError("A checkpoint leaf is required")
    return chain


def training_inventory(chain, snapshots):
    records, datasets = {}, set()
    for node in chain:
        receipt = node["receipt"]
        metadata, epoch = receipt["metadata"], receipt["epoch"]
        dataset = metadata["datasetSha256"]
        if dataset not in snapshots:
            raise ValueError("Missing dataset snapshot for a checkpoint ancestor")
        snapshot = snapshots[dataset]
        if (snapshot.get("formatId") != "com.project-seam.training-dataset-snapshot"
                or snapshot.get("schemaVersion") != 3
                or snapshot.get("bindings") != metadata["datasetBindings"]
                or snapshot.get("datasetSha256") != dataset):
            raise ValueError("Snapshot differs from the checkpoint dataset bindings")
        rows = [{key: source[key] for key in SOURCE_FIELDS} for source in snapshot["sources"]]
        split = metadata["datasetBindings"]["split"]
        if split_sources(rows, seed=split["seed"], held_out_songs=split["heldOutSongIds"]) != split:
            raise ValueError("Source inventory differs from the captured split")
        trained = {source for group in split["groups"] if group["partition"] == "train" for source in group["sourceIds"]}
        coverage = epoch.get("sourceUpdates") if receipt["formatId"] == "com.project-seam.gan-checkpoint" else epoch.get("coveredSourceFrames")
        if (not isinstance(coverage, dict) or set(coverage) != trained
                or any(type(count) is not int or count <= 0 for count in coverage.values())):
            raise ValueError("Checkpoint coverage differs from its training source inventory")
        if dataset not in datasets:
            for row in rows:
                if row["sourceId"] in trained:
                    records[(dataset, row["sourceId"])] = dict(row, datasetSha256=dataset)
            datasets.add(dataset)
    return list(records.values())


def overlaps(candidates, training):
    if not isinstance(candidates, list) or not 1 <= len(candidates) <= 256:
        raise ValueError("Select 1..256 explicit evaluation sources")
    seen, result = set(), []
    for candidate in candidates:
        if (set(candidate) != set(SOURCE_FIELDS) or candidate["sourceId"] in seen
                or any(not isinstance(value, str) or not 1 <= len(value.encode()) <= 256 for value in candidate.values())
                or not _digest(candidate["audioSha256"])):
            raise ValueError("Evaluation identities must be complete, bounded and unique")
        seen.add(candidate["sourceId"])
        matches = []
        for source in training:
            fields = [key for key in SOURCE_FIELDS if source[key] == candidate[key]]
            if fields:
                matches.append(dict(trainingSourceId=source["sourceId"], datasetSha256=source["datasetSha256"], fields=fields))
        result.append(dict(sourceId=candidate["sourceId"], overlaps=matches,
                           status="TRAINING_OVERLAP" if matches else "NO_OVERLAP_IN_DECLARED_FIELDS"))
    return result


def audit(config):
    if (not isinstance(config, dict)
            or config.get("formatId") != "com.project-seam.training-ancestry-audit-config"
            or type(config.get("schemaVersion")) is not int or config["schemaVersion"] != 1):
        raise ValueError("Unsupported ancestry audit configuration")
    index = config["receipts"]
    if not isinstance(index, dict) or not 1 <= len(index) <= 512 or any(not _digest(key) for key in index):
        raise ValueError("Expected a bounded digest-to-receipt index")
    def resolve(digest):
        if digest not in index:
            raise ValueError("Missing ancestor receipt: " + digest)
        return load_config(Path(index[digest]), digest)
    refs = config["snapshots"]
    if not isinstance(refs, list) or not 1 <= len(refs) <= 32:
        raise ValueError("Expected bounded captured snapshots")
    snapshots = {}
    for ref in refs:
        snapshot = load_config(Path(ref["path"]), ref["sha256"])
        key = snapshot["datasetSha256"]
        if key in snapshots:
            raise ValueError("Duplicate dataset snapshot")
        snapshots[key] = snapshot
    result = {}
    for role in ("acoustic", "vocoder"):
        chain = trace(config[role + "Leaf"], resolve)
        expected = "com.project-seam.training-checkpoint" if role == "acoustic" else "com.project-seam.gan-checkpoint"
        if chain[0]["receipt"]["formatId"] != expected:
            raise ValueError("Checkpoint leaf has the wrong model role")
        records = training_inventory(chain, snapshots)
        result[role] = dict(declaredReceiptChainComplete=True,
            receiptSha256s=[node["receiptSha256"] for node in chain],
            datasetSha256s=sorted({row["datasetSha256"] for row in records}), trainingSourceCount=len(records),
            candidates=overlaps(config["candidates"], records))
    return dict(formatId="com.project-seam.training-ancestry-audit", schemaVersion=1, **result,
        recipeEquivalenceAudited=False, undeclaredPretrainingExcluded=False,
        combinedModelHoldoutVerified=False, singerQualified=False, releaseEligible=False,
        limitation="Declared receipt/split/source identity audit only; no recipe, rights, independence or musical qualification")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--config-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
        parser.error("Output must be new with an existing parent")
    report = audit(load_config(args.config, args.config_sha256))
    report["configurationSha256"] = args.config_sha256
    publish_new(args.output, report)
    print(json.dumps({role: dict(receipts=len(report[role]["receiptSha256s"]),
        trainingSources=report[role]["trainingSourceCount"]) for role in ("acoustic", "vocoder")}))


if __name__ == "__main__":
    main()
