"""Export completed arms and evaluate them pairwise; never promotes a model.

Each arm is exported from its own published checkpoint with the same profile and
trusted checkout. Evaluation reuses the frozen native replay inputs, so both arms
receive identical features and controls. A missing or still-running arm is
reported as not ready rather than substituting a different checkpoint.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from .__main__ import load_config, publish_new


def arm_state(arm):
    """Report readiness without reading or copying any checkpoint bytes."""
    run = Path(arm['output']) / 'run.json'
    return dict(name=arm['name'], output=arm['output'], runReceipt=str(run),
                completed=run.is_file())


def export_arms(arms, *, profile, profile_sha256, checkout, output_root):
    """Export every ready arm; unready arms are skipped and reported."""
    output_root = Path(output_root)
    if output_root.exists() or output_root.is_symlink() or not output_root.parent.is_dir():
        raise ValueError('Export root must be new with an existing parent')
    loaded = load_config(profile, profile_sha256)
    if loaded.get('profileId') != 'seam-full-hop-slaney-v1':
        raise ValueError('Expected the declared acousic profile')
    output_root.mkdir(mode=0o700)
    results = []
    for arm in arms:
        state = arm_state(arm)
        if not state['completed']:
            state['state'] = 'NOT_READY'
            results.append(state)
            continue
        run_bytes = (Path(arm['output']) / 'run.json').read_bytes()
        run = load_config(Path(arm['output']) / 'run.json',
                          hashlib.sha256(run_bytes).hexdigest())
        checkpoints = run.get('checkpoints') if isinstance(run, dict) else None
        if not isinstance(run, dict) or run.get('formatId') != 'com.project-seam.vocoder-training-run':
            raise ValueError('Expected a completed vocoder training run receipt')
        if (not isinstance(checkpoints, list) or len(checkpoints) != 1
                or checkpoints[0].get('binariesRetained') is not True
                or not isinstance(checkpoints[0].get('path'), str)):
            raise ValueError('Expected exactly one complete retained epoch checkpoint per arm')
        epoch = Path(arm['output']) / checkpoints[0]['path']
        receipt = hashlib.sha256((epoch / 'checkpoint.json').read_bytes()).hexdigest()
        if receipt != checkpoints[0]['receiptSha256']:
            raise ValueError('Arm checkpoint receipt differs from its run record')
        destination = output_root / arm['name']
        completed = subprocess.run([sys.executable, '-m', 'tools.voice_model_training.export_vocoder',
            '--checkpoint', str(epoch), '--receipt-sha256', receipt, '--profile', str(profile),
            '--profile-sha256', profile_sha256, '--trusted-checkout', str(checkout),
            '--output', str(destination)], capture_output=True, text=True, timeout=3600)
        if completed.returncode:
            raise RuntimeError(f"Export failed for {arm['name']}: {completed.stderr[-500:]}")
        results.append(dict(state, receiptSha256=receipt, exportRoot=str(destination),
                            exportSha256=hashlib.sha256((destination / 'export.json').read_bytes()).hexdigest(),
                            state='EXPORTED'))
    report = dict(formatId='com.project-seam.paired-vocoder-exports', schemaVersion=1,
        profileSha256=profile_sha256, arms=results,
        allReady=all(row['state'] == 'EXPORTED' for row in results),
        singerQualified=False, releaseEligible=False)
    publish_new(output_root / 'exports.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--plan-sha256', required=True)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--profile-sha256', required=True)
    parser.add_argument('--checkout', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    plan = load_config(args.plan, args.plan_sha256)
    if (plan.get('formatId') != 'com.project-seam.paired-vocoder-experiment-plan'
            or plan.get('releaseEligible') is not False):
        parser.error('Expected a paired vocoder experiment plan')
    report = export_arms(plan['arms'], profile=args.profile, profile_sha256=args.profile_sha256,
                         checkout=args.checkout, output_root=args.output)
    print(json.dumps({row['name']: row['state'] for row in report['arms']}))
    return 0 if report['allReady'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
