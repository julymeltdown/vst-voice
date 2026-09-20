"""CLI for the bounded single-phrase A/B optimizer probe.

Reads a verified checkpoint and a captured prepared phrase, then trains the same
seed and steps twice: with and without the periodicity term. Reports raw
unvoiced-window statistics for both arms. Diagnostic only, never a promoted model.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from .__main__ import load_config, publish_new
from .acoustics import wav_log_mel_targets
from .audio_source import decode_pcm_source
from .check_vocoder_model import TRAINING_REVISION, trusted_checkout
from .export_vocoder import export_identity
from .gan_checkpoint_storage import load_local_checkpoint
from .native_input_replay import prepare_inputs
from .periodicity_optimizer_probe import unvoiced_statistics
from .train_vocoder import _module
from .unvoiced_periodicity import phone_mask
from .vocoder_optimization import vocoder_gan_step


def build_pcm(source, padded):
    """Pad the captured source to whole hops; the excluded tail is reported."""
    audio = np.asarray(source, np.float32)
    if len(audio) >= padded:
        return audio[:padded]
    return np.concatenate([audio, np.zeros(padded - len(audio), np.float32)])


def probe(*, prepared_root, corpus, corpus_sha256, replay,
          checkpoint, checkpoint_sha256, profile, profile_sha256, checkout,
          source_id, steps, learning_rate, seed):
    """Run both arms and return a diagnostic receipt."""
    import torch
    torch.set_num_threads(1)
    corpus = load_config(corpus, corpus_sha256)
    song = next((row for row in corpus['songs'] if row['sourceId'] == source_id), None)
    if song is None:
        raise ValueError('Select a captured source present in the corpus')
    directory = Path(prepared_root) / song['directory']
    labels = load_config(directory / 'label-config.json', song['artifacts']['label-config.json'])['labels'][0]
    payload = (directory / 'source.wav').read_bytes()
    if hashlib.sha256(payload).hexdigest() != song['sourceSha256']:
        raise ValueError('Captured source changed since preparation')
    _, source = decode_pcm_source(payload, expected_sha256=song['sourceSha256'], sample_rate=48000)
    mel, = wav_log_mel_targets(payload, expected_sha256=song['sourceSha256'], sample_rate=48000)[1:]
    replay_bytes = Path(replay).read_bytes()
    capture = load_config(replay, hashlib.sha256(replay_bytes).hexdigest())
    inputs, _, gains = prepare_inputs(capture, capture['steps'])
    padded = ((len(gains) + 255) // 256) * 256
    pcm = build_pcm(source, padded)
    state, receipt = load_local_checkpoint(checkpoint, receipt_sha256=checkpoint_sha256)
    configuration = export_identity(state, receipt, load_config(profile, profile_sha256))
    source_module = trusted_checkout(checkout, TRAINING_REVISION)
    arch = _module(source_module, 'seam_probe_architecture', 'models/nsf_HiFigan/models.py')
    mel_module = _module(source_module, 'seam_probe_mel', 'utils/wav2mel.py')
    transform = mel_module.PitchAdjustableMelSpectrogram(sample_rate=48000, n_fft=1024,
        win_length=1024, hop_length=256, f_min=20, f_max=24000, n_mels=80)

    def reconstruction(generated, real):
        return (transform(generated.squeeze(1)).clamp_min(1e-5).log()
                - transform(real.squeeze(1)).clamp_min(1e-5).log()).abs().mean()

    mel_tensor = torch.from_numpy(np.ascontiguousarray(mel.T)).unsqueeze(0).float()
    f0_tensor = torch.from_numpy(inputs['f0'])
    pcm_tensor = torch.from_numpy(pcm).reshape(1, 1, -1)
    reference = pcm[:len(gains)]
    phones = [dict(symbol=p['symbol'], startFrame=p['startFrame'], endFrame=p['endFrame'])
              for p in labels['label']['phonemes']]
    mask = phone_mask(labels, sample_offset=0, sample_count=padded, valid_samples=len(gains))
    rows = []
    for arm, use_mask in (('control', False), ('periodicity', True)):
        torch.manual_seed(seed)
        generator = arch.Generator(arch.AttrDict(configuration))
        discriminators = [arch.MultiScaleDiscriminator(), arch.MultiPeriodDiscriminator([3, 5])]
        owners = torch.nn.ModuleDict({'generator': generator, **{
            f'discriminator_{index}': model for index, model in enumerate(discriminators)}})
        owners.load_state_dict(state['model'], strict=True)
        generator_optimizer = torch.optim.AdamW(generator.parameters(), lr=learning_rate,
                                                betas=(.8, .99), weight_decay=0)
        discriminator_optimizer = torch.optim.AdamW(
            [p for model in discriminators for p in model.parameters()], lr=learning_rate,
            betas=(.8, .99), weight_decay=0)
        losses = []
        for _ in range(steps):
            result = vocoder_gan_step(generator, discriminators, generator_optimizer,
                discriminator_optimizer, mel=mel_tensor, f0=f0_tensor, pcm=pcm_tensor,
                hop_size=256, partition='train', reconstruction_loss=reconstruction,
                **({'periodicity_mask': mask} if use_mask else {}))
            losses.append(float(result['generatorLoss']))
        generator.eval()
        with torch.no_grad():
            # Generator output is (batch, channel, samples); keep the sample axis only.
            wave = generator(mel_tensor, f0_tensor)[0, 0, :padded].numpy().astype(np.float32) * pcm
        parameters_finite = all(bool(torch.isfinite(p).all()) for p in generator.parameters())
        rows.append(dict(arm=arm, firstLoss=losses[0], lastLoss=losses[-1],
            generatorParametersFinite=parameters_finite,
            statistics=unvoiced_statistics(reference, wave[:len(gains)], phones)))
    return dict(formatId='com.project-seam.periodicity-optimizer-probe', schemaVersion=1,
        sourceId=song['sourceId'], sourceSha256=song['sourceSha256'],
        checkpointReceiptSha256=checkpoint_sha256, steps=steps, seed=seed,
        learningRate=learning_rate, comparedSamples=len(gains),
        excludedTailSamples=padded - len(gains), arms=rows,
        note='Single-phrase optimizer probe; not a training run or promoted model',
        trainingAdmitted=False, singerQualified=False, releaseEligible=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prepared-root', 'corpus', 'replay', 'checkpoint', 'profile', 'checkout', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('corpus-sha256', 'checkpoint-sha256', 'profile-sha256'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--source-id', required=True)
    parser.add_argument('--steps', type=int, default=5)
    parser.add_argument('--learning-rate', type=float, default=2e-4)
    parser.add_argument('--seed', type=int, default=933)
    args = parser.parse_args()
    if not 1 <= args.steps <= 50:
        parser.error('Probe steps must be between 1 and 50')
    output = args.output
    report = probe(**{key: value for key, value in vars(args).items() if key != 'output'})
    publish_new(output, report)
    print(json.dumps({row['arm']: {key: value for key, value in row['statistics'].items()
                                   if key != 'rows'} for row in report['arms']}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
