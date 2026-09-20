"""Remeasure a complete frozen campaign and bind its score/rest diagnostics."""
import argparse
import json
from pathlib import Path
import re

from .__main__ import load_config, publish_new
from .compare_campaigns import load_campaign, summarize
from .score_application_export import inspect


def aggregate(rows):
    """Never exclude failed rows from a supposedly full-cohort aggregate."""
    if not rows or any(row['execution'] != 'PASSED' for row in rows):
        return None
    result = summarize(rows)
    if result is None:
        return None
    keys = rows[0]['score']['errorLocations']['counts'].keys()
    result['errorLocations'] = {key: sum(row['score']['errorLocations']['counts'][key]
                                        for row in rows) for key in keys}
    rests = [rest for row in rows for rest in row['score']['silence']['rests']]
    result['restCount'] = len(rests)
    result['allRestsExactlyZero'] = (all(not any(rest['channelNonzeroSamples'])
                                          for rest in rests) if rests else None)
    return result


def diagnose(campaign, campaign_sha256, preparation, pitch_executable):
    original = load_config(campaign, campaign_sha256)
    verified = load_campaign(campaign, campaign_sha256, pitch_executable)
    selection = verified['selection']
    if selection.get('validationScope') != 'frozen-same-voice-unseen-phrase-engineering-only':
        raise ValueError('Expected a frozen evaluation-only campaign')
    prepared = load_config(preparation, selection['corpusSha256'])
    if (prepared.get('formatId') != 'com.project-seam.frozen-evaluation-preparation'
            or prepared.get('captureSha256') != selection.get('frozenCaptureSha256')
            or [r['sourceId'] for r in prepared['items']] != [r['sourceId'] for r in verified['rows']]):
        raise ValueError('Preparation differs from complete frozen campaign')
    for index, (row, pre) in enumerate(zip(verified['rows'], prepared['items'])):
        if row['execution'] != 'PASSED':
            continue
        if not re.fullmatch(r'procedural-song-[0-9]{5}', row['sourceId']):
            raise ValueError('Invalid frozen source identity')
        source = preparation.parent / row['sourceId']
        if source.is_symlink():
            raise ValueError('Prepared source must not be a symlink')
        receipt = load_config(source / 'preparation.json', pre['preparationSha256'])
        if receipt['sourceId'] != row['sourceId'] or receipt['sourceSha256'] != row['sourceSha256']:
            raise ValueError('Prepared score source differs from campaign source')
        song = campaign.parent / f'song-{index:03d}'
        comparison = song / 'comparison.json'
        row['score'] = inspect(comparison, original['items'][index]['comparisonSha256'],
            source / 'label-config.json', receipt['artifacts']['label-config.json'],
            song / 'export/master.wav')
    return dict(formatId='com.project-seam.frozen-campaign-diagnostic', schemaVersion=1,
        campaignSha256=campaign_sha256, preparationSha256=selection['corpusSha256'],
        rows=verified['rows'], summary=aggregate(verified['rows']),
        combinedModelHoldoutVerified=False, singerQualified=False, releaseEligible=False,
        policy='Fresh measurements; full cohort; written notes are not expressive pitch ground truth')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('campaign', 'preparation', 'pitch-executable', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--campaign-sha256', required=True)
    args = vars(parser.parse_args())
    output = args.pop('output')
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error('Output must be new with an existing parent')
    report = diagnose(**args)
    publish_new(output, report)
    print(json.dumps(report['summary']))


if __name__ == '__main__':
    main()
