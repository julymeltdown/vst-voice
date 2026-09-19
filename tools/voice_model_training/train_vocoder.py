"""Persist and resume reviewed CPU vocoder GAN training with held-out audio.

Executes an explicitly trusted pinned SingingVocoders checkout. This is not
hostile-code isolation, source permission, singer qualification or release approval.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import random
import re
import subprocess
import sys
import time

from .__main__ import assemble_dataset, load_config, load_dataset_inputs
from .check_vocoder_model import TRAINING_REVISION, trusted_checkout, vocoder_configuration
from .train import load_targets
from .vocoder_checkpoint import restore_vocoder_checkpoint
from .gan_checkpoint_storage import require_disk_headroom

OBJECTIVE_ID = 'nsf-lsgan-logmel-48k80-v1'


def model_settings(value):
    fields = {'formatId', 'schemaVersion', 'seed', 'learningRate', 'learningRateDecay',
              'maximumUpdates', 'maximumSeconds', 'cpuThreads', 'evaluationSeed',
              'heldOutSources', 'labelOrigin'}
    if isinstance(value, dict) and type(value.get('schemaVersion')) is int and value['schemaVersion'] in (2, 3):
        fields.add('architectureProfile')
        if value['schemaVersion'] == 3:
            fields.add('trainingSegmentFrames')
    if (not isinstance(value, dict) or set(value) != fields
            or value['formatId'] != 'com.project-seam.vocoder-training-config'
            or type(value['schemaVersion']) is not int or value['schemaVersion'] not in (1, 2, 3)):
        raise ValueError('Unsupported vocoder training configuration')
    if value['schemaVersion'] == 3 and (type(value['trainingSegmentFrames']) is not int or
                                        not 16 <= value['trainingSegmentFrames'] <= 4096):
        raise ValueError('Training segments require 16..4096 analysis hops')
    for key, lower, upper in (('seed', 0, 2**63-1), ('evaluationSeed', 0, 2**63-1),
                               ('cpuThreads', 1, 32), ('maximumUpdates', 1, 100000)):
        if type(value[key]) is not int or not lower <= value[key] <= upper:
            raise ValueError(f'Invalid bounded vocoder setting: {key}')
    for key, upper in (('learningRate', .1), ('learningRateDecay', 1.), ('maximumSeconds', 86400)):
        if type(value[key]) not in (int, float) or not math.isfinite(value[key]) or not 0 < value[key] <= upper:
            raise ValueError(f'Invalid bounded vocoder setting: {key}')
    held = value['heldOutSources']
    if (not isinstance(held, list) or not 1 <= len(held) <= 256
            or any(not isinstance(s, str) or not 1 <= len(s.encode()) <= 256
                   or any(ord(c) < 32 or ord(c) == 127 for c in s) for s in held)
            or len(set(held)) != len(held)):
        raise ValueError('Select distinct held-out source IDs for reconstruction')
    origin = value['labelOrigin']
    if (not isinstance(origin, str) or not 1 <= len(origin.encode()) <= 256
            or any(ord(c) < 32 or ord(c) == 127 for c in origin)):
        raise ValueError('Capture the actual label origin; do not infer acoustic truth')
    # This is the architecture supported by export_vocoder today. Other models
    # need an explicit configuration/export change, not a silent conversion.
    return vocoder_configuration(value.get('architectureProfile', 'mini-nsf-32-smoke-v1'))


def load_pcm_sources(root, labels):
    """Resolve paths from already captured label inputs; byte checks occur at use."""
    root = Path(root).resolve(strict=True)
    rows = labels.get('sources') if isinstance(labels, dict) else None
    if not root.is_dir() or not isinstance(rows, list) or not 1 <= len(rows) <= 10000:
        raise ValueError('Vocoder needs captured source records')
    result = {}
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError('Invalid vocoder source row')
        identity, relative = row.get('sourceId'), row.get('path')
        if (not isinstance(identity, str) or not 1 <= len(identity.encode()) <= 256
                or identity in result or not isinstance(relative, str) or not 1 <= len(relative) <= 4096
                or '\\' in relative or ':' in relative
                or any(part in ('', '.', '..') for part in relative.split('/'))):
            raise ValueError('Vocoder source IDs/paths must be unique and contained')
        path = root
        for part in relative.split('/'):
            path /= part
            if path.is_symlink():
                raise ValueError('Vocoder source paths cannot contain symlinks')
        if not path.resolve().is_relative_to(root):
            raise ValueError('Vocoder source path escapes the source root')
        result[identity] = path
    return result


def resume_identity(directory, digest, *, metadata, profile, dataset, objective_id):
    """Inspect a captured receipt before allocating/loading its trusted Torch state."""
    receipt = load_config(Path(directory) / 'checkpoint.json', digest)
    previous = receipt.get('metadata') if isinstance(receipt, dict) else None
    epoch = receipt.get('epoch') if isinstance(receipt, dict) else None
    run = previous.get('run') if isinstance(previous, dict) else None
    if (not isinstance(receipt, dict) or receipt.get('formatId') != 'com.project-seam.gan-checkpoint'
            or not isinstance(run, dict) or not isinstance(epoch, dict)
            or {key: value for key, value in run.items()
                if key not in ('completedEpochs', 'parentReceiptSha256')} != metadata
            or previous.get('profileSha256') != profile or previous.get('datasetSha256') != dataset
            or previous.get('objectiveId') != objective_id or epoch.get('objectiveId') != objective_id
            or epoch.get('datasetSha256') != dataset or epoch.get('epochComplete') is not True
            or epoch.get('coverageVerified') is not True):
        raise ValueError('Resume requires identical captured inputs, environment and a complete epoch')
    completed = run.get('completedEpochs')
    if type(completed) is not int or not 1 <= completed < 100000:
        raise ValueError('Resume requires completed-epoch lineage')
    return {key: value for key, value in previous.items() if key != 'ganCheckpoint'}, completed


def _profile(targets):
    expected = dict(profileId='seam-full-hop-slaney-v1', sampleRate=48000,
        fftSize=1024, windowSize=1024, hopSize=256, bins=80, minimumHz=20, maximumHz=24000,
        tailPadding='zero-to-whole-hop', boundaryPadding='reflect-fft-minus-hop',
        window='periodic-hann', spectrum='unnormalized-magnitude', melNormalization='slaney-area',
        melFrequencyScale='slaney', amplitudeScale='ln-amplitude', floor=1e-5,
        layout='TF', dtype='float32-le')
    if not targets or any(record.get('profile') != expected for record, _ in targets.values()):
        raise ValueError('Vocoder requires the exact 48k/80/256/1024 acoustic profile')


def partial_resume_identity(directory, digest, *, metadata, profile, dataset, objective_id):
    """Inspect partial lineage; actual state restoration stays inside the admitted epoch."""
    receipt = load_config(Path(directory) / 'checkpoint.json', digest)
    if not isinstance(receipt, dict) or not isinstance(receipt.get('metadata'), dict):
        raise ValueError('Partial resume requires a captured checkpoint object')
    previous = receipt.get('metadata', {})
    run, cursor = previous.get('run', {}), receipt.get('epoch', {})
    if (receipt.get('formatId') != 'com.project-seam.gan-partial-checkpoint'
            or not isinstance(run, dict) or not isinstance(cursor, dict)
            or {key: value for key, value in run.items()
                if key not in ('completedEpochs', 'parentReceiptSha256')} != metadata
            or previous.get('profileSha256') != profile or previous.get('datasetSha256') != dataset
            or previous.get('objectiveId') != objective_id
            or cursor.get('datasetSha256') != dataset or cursor.get('profileSha256') != profile
            or cursor.get('epochComplete') is not False or cursor.get('coverageVerified') is not False):
        raise ValueError('Partial resume requires identical captured inputs and an incomplete epoch')
    number, parent = run.get('completedEpochs'), run.get('parentReceiptSha256')
    if (type(number) is not int or not 1 <= number <= 100000
            or (parent is not None if number == 1 else
                not isinstance(parent, str) or re.fullmatch(r'[0-9a-f]{64}', parent) is None)):
        raise ValueError('Partial checkpoint epoch lineage differs')
    return number - 1, parent


def _module(checkout, name, relative):
    spec = importlib.util.spec_from_file_location(name, checkout / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('training-config', 'dataset-config', 'targets', 'source-root',
                 'conditioning', 'trusted-checkout', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('training-sha256', 'dataset-sha256', 'targets-sha256',
                 'rights-policy-sha256', 'label-policy-sha256'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--resume', type=Path, help='Trusted local complete epoch directory')
    parser.add_argument('--resume-receipt-sha256')
    parser.add_argument('--resume-partial', type=Path, help='Trusted local partial-update directory; not a complete epoch')
    parser.add_argument('--resume-partial-sha256')
    parser.add_argument('--checkpoint-interval-updates', type=int, help='Save partial recovery state every N updates')
    parser.add_argument('--retain-partial-checkpoints', type=int,
                        help='Keep newest N partial binaries per new epoch; budget must fit N+1 before pruning')
    parser.add_argument('--maximum-recovery-bytes', type=int, default=2 * 1024**3,
                        help='Separate aggregate partial-checkpoint budget for this new run')
    parser.add_argument('--epochs', type=int, default=1)
    parser.add_argument('--maximum-run-seconds', type=float, default=3600)
    parser.add_argument('--maximum-total-checkpoint-bytes', type=int, default=2 * 1024**3)
    parser.add_argument('--retain-checkpoints', type=int,
        help='Keep newest N checkpoints from this new run; preserve all receipts and allow space for N+1')
    # Optional because it costs one subprocess per held-out signal. Supplying it is
    # what makes the reconstruction receipt's pitch term evaluable; omitting it leaves
    # that term UNRESOLVED, which the receipt states rather than disguises.
    parser.add_argument('--pitch-executable', type=Path,
        help='Trusted first-party feature extractor used for framewise reconstruction pitch')
    args = parser.parse_args(argv)
    try:
        if args.retain_partial_checkpoints is not None and (args.checkpoint_interval_updates is None
                or not 1 <= args.retain_partial_checkpoints <= 1000):
            raise ValueError('Partial retention requires periodic recovery and a bounded count')
        if (args.resume is None) != (args.resume_receipt_sha256 is None):
            raise ValueError('Resume requires a local checkpoint and its captured receipt digest')
        if ((args.resume_partial is None) != (args.resume_partial_sha256 is None)
                or args.resume is not None and args.resume_partial is not None
                or args.checkpoint_interval_updates is not None and not 1 <= args.checkpoint_interval_updates <= 100000
                or not 1 <= args.maximum_recovery_bytes <= 8 * 1024**3):
            raise ValueError('Select one paired resume mode and bounded recovery limits')
        if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
            raise ValueError('Training output must be new with an existing parent')
        if (not 1 <= args.epochs <= 1000 or not math.isfinite(args.maximum_run_seconds)
                or not 0 < args.maximum_run_seconds <= 86400
                or not 1 <= args.maximum_total_checkpoint_bytes <= 8 * 1024**3
                or args.retain_checkpoints is not None and not 1 <= args.retain_checkpoints <= 1000):
            raise ValueError('Invalid bounded vocoder run limits')
        settings = load_config(args.training_config, args.training_sha256)
        configuration = model_settings(settings)
        if ((args.resume_partial is not None or args.checkpoint_interval_updates is not None)
                and settings.get('trainingSegmentFrames') is None):
            raise ValueError('Partial recovery requires explicit segmented training configuration')
        # Refuse before corpus assembly or Torch model/optimizer allocation.
        require_disk_headroom(args.output.parent, min(args.maximum_total_checkpoint_bytes, 1024**3))
        if args.checkpoint_interval_updates is not None:
            require_disk_headroom(args.output.parent, min(args.maximum_total_checkpoint_bytes, 1024**3)
                                  + min(args.maximum_recovery_bytes,
                                        min(args.maximum_total_checkpoint_bytes, 1024**3) *
                                        (args.retain_partial_checkpoints + 1 if args.retain_partial_checkpoints is not None else 1)))
        inputs = load_dataset_inputs(args.dataset_config, args.dataset_sha256, args.source_root,
            rights_anchor=args.rights_policy_sha256, label_anchor=args.label_policy_sha256)
        targets, profile = load_targets(args.targets, args.targets_sha256)
        _profile(targets)
        labels = load_config(inputs['label_config'], inputs['label_hash'])
        sources = load_pcm_sources(inputs['root'], labels)
        snapshot = assemble_dataset(**inputs, now=int(time.time()), conditioning_directory=args.conditioning,
                                    reuse_conditioning=args.conditioning.exists())
        if (snapshot.get('preparationIssues') != [] or snapshot.get('sourcePermissionsAdmitted') is not True
                or snapshot.get('labelsAdmitted') is not True or set(targets) != set(sources)):
            raise ValueError('Training requires complete freshly admitted dataset and acoustic targets')
        partitions = {source: group['partition'] for group in snapshot['bindings']['split']['groups']
                      for source in group['sourceIds']}
        if any(partitions.get(source) not in ('validation', 'test') for source in settings['heldOutSources']):
            raise ValueError('Held-out sources must belong to admitted validation/test partitions')
        checkout = trusted_checkout(args.trusted_checkout, TRAINING_REVISION)
        import torch
        import numpy as np
        import scipy
        torch.set_num_threads(settings['cpuThreads'])
        torch.manual_seed(settings['seed'])
        random.seed(settings['seed'])
        np.random.seed(settings['seed'] % 2**32)
        metadata = dict(configuration=configuration, trainingRevision=TRAINING_REVISION, settings=settings,
            trainingConfigurationSha256=args.training_sha256, assemblyConfigurationSha256=args.dataset_sha256,
            targetInventorySha256=args.targets_sha256, torchVersion=str(torch.__version__),
            numpyVersion=np.__version__, scipyVersion=scipy.__version__, singerQualified=False)
        previous, completed = None, 0
        parent_receipt = args.resume_receipt_sha256
        if args.resume is not None:
            previous, completed = resume_identity(args.resume, args.resume_receipt_sha256, metadata=metadata,
                profile=profile, dataset=snapshot['datasetSha256'], objective_id=OBJECTIVE_ID)
        elif args.resume_partial is not None:
            completed, parent_receipt = partial_resume_identity(args.resume_partial, args.resume_partial_sha256,
                metadata=metadata, profile=profile, dataset=snapshot['datasetSha256'], objective_id=OBJECTIVE_ID)
        if completed + args.epochs > 100000:
            raise ValueError('Vocoder completed-epoch bound exceeded')
        source = _module(checkout, 'seam_train_vocoder_architecture', 'models/nsf_HiFigan/models.py')
        mel_module = _module(checkout, 'seam_train_vocoder_mel', 'utils/wav2mel.py')
        generator = source.Generator(source.AttrDict(configuration))
        discriminators = [source.MultiScaleDiscriminator(), source.MultiPeriodDiscriminator([3, 5])]
        go = torch.optim.AdamW(generator.parameters(), lr=settings['learningRate'], betas=(.8, .99), weight_decay=0)
        do = torch.optim.AdamW([p for model in discriminators for p in model.parameters()],
                              lr=settings['learningRate'], betas=(.8, .99), weight_decay=0)
        schedulers = {name: torch.optim.lr_scheduler.ExponentialLR(optimizer, gamma=settings['learningRateDecay'])
                      for name, optimizer in (('generator', go), ('discriminator', do))}
        transform = mel_module.PitchAdjustableMelSpectrogram(sample_rate=48000, n_fft=1024,
            win_length=1024, hop_length=256, f_min=20, f_max=24000, n_mels=80)
        def reconstruction(generated, real):
            return (transform(generated.squeeze(1)).clamp_min(1e-5).log() -
                    transform(real.squeeze(1)).clamp_min(1e-5).log()).abs().mean()
        if previous is not None:
            restore_vocoder_checkpoint(generator, discriminators, go, do, args.resume,
                receipt_sha256=args.resume_receipt_sha256, expected_metadata=previous, schedulers=schedulers)
        from .vocoder_epochs import run_reviewed_vocoder_epochs
        def progress(event):
            print(json.dumps(dict(formatId='com.project-seam.vocoder-training-progress', schemaVersion=1,
                                  **event), sort_keys=True, allow_nan=False), file=sys.stderr, flush=True)
        result = run_reviewed_vocoder_epochs(generator, discriminators, go, do,
            output=args.output, epochs=args.epochs, completed_epochs=completed,
            parent_receipt_sha256=parent_receipt, metadata=metadata,
            maximum_run_seconds=args.maximum_run_seconds,
            maximum_total_checkpoint_bytes=args.maximum_total_checkpoint_bytes,
            retain_checkpoints=args.retain_checkpoints,
            checkpoint_interval_updates=args.checkpoint_interval_updates,
            maximum_recovery_bytes=args.maximum_recovery_bytes,
            retain_partial_checkpoints=args.retain_partial_checkpoints,
            resume_partial=args.resume_partial, resume_partial_sha256=args.resume_partial_sha256,
            on_progress=progress,
            epoch_options=dict(dataset_inputs=inputs, conditioning_directory=args.conditioning,
                targets=targets, pcm_sources=sources, expected_profile_sha256=profile,
                reconstruction_loss=reconstruction, objective_id=OBJECTIVE_ID,
                maximum_updates=settings['maximumUpdates'], maximum_seconds=settings['maximumSeconds'],
                schedulers=schedulers, expected_dataset_sha256=snapshot['datasetSha256'],
                held_out_items=settings['heldOutSources'], label_origin=settings['labelOrigin'],
                evaluation_seed=settings['evaluationSeed'],
                pitch_executable=args.pitch_executable,
                training_segment_frames=settings.get('trainingSegmentFrames')))
        print(json.dumps(result, sort_keys=True))
        return 0
    except KeyboardInterrupt:
        print('Vocoder training interrupted; earlier verified checkpoints retained', file=sys.stderr)
        return 130
    except (ValueError, OSError, RuntimeError, ImportError, RecursionError, subprocess.SubprocessError) as error:
        print(str(error)[:512], file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
