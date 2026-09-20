"""Explicit same-dataset GAN weight initialization; never exact resume."""
from .gan_checkpoint_storage import load_local_checkpoint


def initialize(generator, discriminators, go, do, directory, digest, *, metadata,
               profile, dataset, objective):
    import torch
    import numpy as np
    import random
    from .train_vocoder import model_settings, training_objective, OBJECTIVE_ID
    from .unvoiced_periodicity import OBJECTIVE_ID as PERIODIC_OBJECTIVE
    state, receipt = load_local_checkpoint(directory, receipt_sha256=digest)
    previous, epoch = state['metadata'], state['epoch']
    run = previous['run']
    number = run.get('completedEpochs')
    if (receipt.get('formatId') != 'com.project-seam.gan-checkpoint'
            or epoch.get('epochComplete') is not True or epoch.get('coverageVerified') is not True
            or type(number) is not int or not 1 <= number < 100000
            or previous.get('datasetSha256') != dataset or epoch.get('datasetSha256') != dataset
            or previous.get('profileSha256') != profile or epoch.get('profileSha256') != profile
            or previous.get('objectiveId') not in (OBJECTIVE_ID, PERIODIC_OBJECTIVE)
            or epoch.get('objectiveId') != previous.get('objectiveId')
            or objective != training_objective(metadata['settings'])
            or model_settings(run['settings']) != model_settings(metadata['settings'])
            or training_objective(run['settings']) != previous['objectiveId']):
        raise ValueError('Warm start requires a complete compatible same-dataset vocoder')
    allowed_run = {'settings', 'trainingConfigurationSha256', 'completedEpochs', 'parentReceiptSha256', 'warmStart'}
    if ({k:v for k,v in run.items() if k not in allowed_run}
            != {k:v for k,v in metadata.items() if k not in allowed_run}):
        raise ValueError('Warm start cannot change architecture, source bindings or runtime')
    # Objective, learning rate and seed are the governed experiment axes:
    # a paired replicate may change any of them while dataset, architecture,
    # source bindings and runtime stay identical. The changed seed is
    # recorded in the returned lineage so a replicate is never mistaken
    # for a resume of the same draws.
    allowed_settings = {'schemaVersion', 'objectiveId', 'learningRate', 'seed'}
    if ({k:v for k,v in run['settings'].items() if k not in allowed_settings}
            != {k:v for k,v in metadata['settings'].items() if k not in allowed_settings}):
        raise ValueError('Warm start only permits objective/learning-rate/seed changes')
    if go.state or do.state:
        raise ValueError('Warm start requires fresh optimizers')
    if any(group['lr'] != metadata['settings']['learningRate']
           for optimizer in (go, do) for group in optimizer.param_groups):
        raise ValueError('Fresh optimizer learning rate differs from captured settings')
    owners = torch.nn.ModuleDict({'generator':generator, **{
        f'discriminator_{i}':model for i,model in enumerate(discriminators)}})
    expected, weights = owners.state_dict(), state['model']
    if (expected.keys() != weights.keys() or any(
            expected[k].shape != weights[k].shape or expected[k].dtype != weights[k].dtype
            or not torch.isfinite(weights[k]).all() for k in expected)):
        raise ValueError('Warm-start weights differ from complete GAN architecture')
    owners.load_state_dict(weights, strict=True)
    seed = metadata['settings']['seed']
    torch.manual_seed(seed); random.seed(seed); np.random.seed(seed % 2**32)
    return dict(sourceReceiptSha256=digest, sourceCheckpointSha256=receipt['checkpointSha256'],
        sourceTrainingConfigurationSha256=run['trainingConfigurationSha256'],
        sourceCompletedEpochs=number, sourceObjectiveId=previous['objectiveId'],
        sourceSeed=run['settings'].get('seed'),
        seedChanged=run['settings'].get('seed') != seed,
        optimizerReset=True, schedulerReset=True, rngReset=True,
        epochNumbering='new-experiment-from-one')
