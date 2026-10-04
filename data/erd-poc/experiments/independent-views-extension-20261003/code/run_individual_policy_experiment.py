#!/usr/bin/env python3
"""One bounded, fully audited ML rollout for an isolated individual view.

Run under run_memory_bounded.py. This never installs a product layout or changes
overview behavior. Detailed child output stays in the experiment directory so
an interrupted CLI connection can resume from the saved, verified candidate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]


def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--checkpoint', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--payload', type=Path, required=True)
    parser.add_argument('--seed', type=int, required=True)
    parser.add_argument('--budget', type=int, default=20000)
    parser.add_argument('--observations', type=int, default=768)
    args = parser.parse_args()
    if os.environ.get('OMP_NUM_THREADS') != '1':
        parser.error('run inside scripts/erd-poc/run_memory_bounded.py')
    if not 1 <= args.budget <= 20000:
        parser.error('proposal budget must be between 1 and 20000')
    if ROOT != Path.cwd() or not args.out.resolve().is_relative_to(ROOT / '.tmp'):
        parser.error('run from the repository root and use a fresh .tmp output')
    source = args.previous / 'candidate.individual.layout.json'
    audit = json.loads((args.previous / 'individual.audit.json').read_text())
    assert audit['actualProductRendererVerified']
    assert audit['candidateSha256'] == digest(source)
    assert audit['models'] == 1244 and audit['relations'] == 1727
    assert audit['spacingViolations'] == 0
    assert audit['validBoundaryEndpoints'] == 3454
    args.out.mkdir(parents=True, exist_ok=False)
    files = {'nodes.tsv': 'individual.nodes.tsv', 'edges.tsv': 'individual.edges.tsv',
             'positions.tsv': 'learned.tsv.individual',
             'routes.tsv': 'learned.tsv.individual.routes.tsv'}
    for target, previous in files.items():
        for prefix in ['', 'individual.']:
            shutil.copyfile(args.previous / previous, args.out / (prefix + target))
    for source_name, target in [('nodes.tsv', 'components.tsv'), ('edges.tsv', 'groups.tsv')]:
        ids = [line.split('\t')[0] for line in (args.out / source_name).read_text().splitlines()]
        (args.out / target).write_text(''.join(f'{node}\t{node}\n' for node in ids))
    proposal = args.out / 'learned.tsv'
    report = {'scope': 'independent individual-view experiment', 'promoted': False,
              'source': str(source), 'sourceSha256': digest(source),
              'payloadSha256': digest(args.payload),
              'checkpointSha256': digest(args.checkpoint),
              'environmentSha256': digest(args.environment),
              'sourceIndividualVisual': audit['visualCrossings'], 'steps': []}
    (args.out / 'experiment.json').write_text(json.dumps(report, indent=2) + '\n')
    started = time.monotonic()
    with (args.out / 'workflow.log').open('w') as log:
        def run(step, command):
            before = time.monotonic()
            log.write(json.dumps({'step': step, 'command': list(map(str, command))}) + '\n')
            log.flush()
            subprocess.run(list(map(str, command)), stdout=log, stderr=log, check=True)
            report['steps'].append({'name': step, 'seconds': time.monotonic() - before})

        run('inference', ['nice', '-n', '10', sys.executable, 'scripts/erd-poc/learn_card_policy.py',
            'infer', '--checkpoint', args.checkpoint, '--environment', args.environment,
            '--directory', args.out, '--out', proposal, '--budget', args.budget,
            '--rounds', 16, '--samples', 12, '--observations', args.observations,
            '--seconds', 20, '--seed', args.seed])
        run('frozen-action-replay', ['nice', '-n', '10', sys.executable,
            'scripts/erd-poc/learn_card_policy.py', 'replay',
            '--checkpoint', args.checkpoint, '--out', proposal])
        run('product-renderer-audit', ['nice', '-n', '10', 'node',
            'scripts/erd-poc/audit_individual_policy_experiment.cjs', source, args.payload, args.out])
    verified = json.loads((args.out / 'individual.audit.json').read_text())
    assert verified['visualCrossings'] <= audit['visualCrossings']
    report.update(candidate=verified['candidate'], candidateSha256=verified['candidateSha256'],
                  individualVisual=verified['visualCrossings'],
                  seconds=time.monotonic() - started, allChecksPassed=True)
    (args.out / 'workflow.audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'steps'}))


if __name__ == '__main__':
    main()
