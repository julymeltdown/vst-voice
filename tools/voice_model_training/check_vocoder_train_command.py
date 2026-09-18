"""Execute the actual vocoder CLI on explicitly synthetic, fixture-policy inputs.

The enclosing check_reviewed_run supplies original oscillator fixtures. Public
fixture signing keys are not production authority and this is not singer training.
"""
import hashlib
import argparse
import json
from pathlib import Path
import subprocess
import sys

from .__main__ import encode_report, publish_new
from .gan_checkpoint_storage import load_local_checkpoint


def check_vocoder_train_command(checkout, *, root, dataset_hash, targets, profile_sha256,
                                rights_anchor, label_anchor, held_out_sources):
    import torch
    rows = []
    for identity, (record, binary) in targets.items():
        name = identity + '-vocoder-target.json'
        publish_new(root / name, record)
        rows.append(dict(sourceId=identity, record=name,
            recordSha256=hashlib.sha256(encode_report(record)).hexdigest(), binary=Path(binary).name))
    inventory = dict(formatId='com.project-seam.training-target-inventory', schemaVersion=1,
                     profileSha256=profile_sha256, targets=rows)
    config = dict(formatId='com.project-seam.vocoder-training-config', schemaVersion=1,
        seed=928, learningRate=.0001, learningRateDecay=.999, maximumUpdates=1, maximumSeconds=300,
        cpuThreads=1, evaluationSeed=932, heldOutSources=held_out_sources,
        labelOrigin='synthetic-oscillator-fixture-not-lyric-supervision')
    publish_new(root / 'vocoder-targets.json', inventory)
    publish_new(root / 'vocoder-training.json', config)
    command = [sys.executable, '-B', '-m', 'tools.voice_model_training.train_vocoder',
        '--training-config', str(root / 'vocoder-training.json'),
        '--training-sha256', hashlib.sha256(encode_report(config)).hexdigest(),
        '--dataset-config', str(root / 'dataset.json'), '--dataset-sha256', dataset_hash,
        '--targets', str(root / 'vocoder-targets.json'),
        '--targets-sha256', hashlib.sha256(encode_report(inventory)).hexdigest(),
        '--source-root', str(root), '--conditioning', str(root / 'conditioning'),
        '--trusted-checkout', str(checkout.resolve(strict=True)),
        '--rights-policy-sha256', rights_anchor, '--label-policy-sha256', label_anchor,
        '--maximum-run-seconds', '300', '--maximum-total-checkpoint-bytes', str(2 * 1024**3)]
    def invoke(name, extra):
        arguments = command + ['--output', str(root / name)] + extra
        publish_new(root / (name + '-command.json'), dict(argv=arguments, syntheticInputs=True, fixturePolicyOnly=True))
        completed = subprocess.run(arguments, capture_output=True, text=True, timeout=360)
        # Keep diagnostics even on a nonzero result. Never replace a previous run.
        with (root / (name + '-stdout.txt')).open('x') as stream:
            stream.write(completed.stdout)
        with (root / (name + '-stderr.txt')).open('x') as stream:
            stream.write(completed.stderr)
        if completed.returncode:
            raise ValueError(f'Vocoder CLI {name} failed: {completed.stderr[-512:]}')
        result = json.loads(completed.stdout)
        if result != json.loads((root / name / 'run.json').read_bytes()):
            raise AssertionError('Vocoder CLI stdout differs from its durable run record')
        return result
    continuous = invoke('vocoder-continuous', ['--epochs', '2'])
    first = continuous['checkpoints'][0]
    first_path = root / 'vocoder-continuous' / first['path']
    resumed = invoke('vocoder-resumed', ['--resume', str(first_path),
        '--resume-receipt-sha256', first['receiptSha256']])
    def load(run, name):
        item = run['checkpoints'][-1]
        return load_local_checkpoint(root / name / item['path'], receipt_sha256=item['receiptSha256'])
    left, left_receipt = load(continuous, 'vocoder-continuous')
    right, right_receipt = load(resumed, 'vocoder-resumed')
    def same(a, b):
        if isinstance(a, torch.Tensor):
            return isinstance(b, torch.Tensor) and torch.equal(a, b)
        if isinstance(a, dict):
            return isinstance(b, dict) and a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
        if isinstance(a, (list, tuple)):
            return type(a) is type(b) and len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
        return a == b
    if not all(same(left[key], right[key]) for key in ('model', 'optimizer', 'rng')):
        raise AssertionError('Continuous and resumed GAN state differs')
    for receipt in (left_receipt, right_receipt):
        if receipt['metadata']['run']['completedEpochs'] != 2:
            raise AssertionError('Vocoder resume lost epoch lineage')
    lsummary, rsummary = (r['epoch']['reconstructionSummary'] for r in (left_receipt, right_receipt))
    if lsummary != rsummary:
        raise AssertionError('Resumed held-out reconstruction changed')
    litems, ritems = (r['epoch']['reconstruction']['items'] for r in (left_receipt, right_receipt))
    if [x['outputAudioSha256'] for x in litems] != [x['outputAudioSha256'] for x in ritems]:
        raise AssertionError('Resumed held-out output bytes changed')
    result = dict(passed=True, continuousVersusResumedStateExact=True,
        heldOutAudioExact=True, completedEpochs=2,
        continuous=continuous, resumed=resumed, reconstructionSummary=lsummary,
        syntheticInputs=True, fixturePolicyOnly=True, singerQualified=False, releaseEligible=False)
    publish_new(root / 'vocoder-command-check.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trusted-checkout', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    import torch
    from .check_reviewed_run import check_reviewed_run
    torch.set_num_threads(1)
    class FixtureModel(torch.nn.Module):
        def __init__(self):
            super().__init__()
            self.level = torch.nn.Parameter(torch.tensor(0., dtype=torch.float32))
    class Objective:
        objective_id = 'integration-fixture-scalar-l2'
        def __call__(self, model, inputs, target):
            return (model.level.expand_as(target) - target).square()
    model = FixtureModel()
    report = check_reviewed_run(model, torch.optim.AdamW(model.parameters(), lr=.001),
        objective=Objective(), model_metadata={'testModel': 'scalar'},
        vocoder_command_checkout=args.trusted_checkout, retained_fixture_root=args.output)
    publish_new(args.output / 'fixture-check.json', report)
    print(json.dumps(dict(passed=report['passed'], checkpointRetained=report['checkpointRetained'],
        fixturePolicyOnly=True, syntheticInputs=True, singerQualified=False,
        command=report['vocoderCommand']['reconstructionSummary'])))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
