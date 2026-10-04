"""Fixed NN parameter controls, never ranked, submitted or adopted."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from shared_card_phase_policy import SharedCardPhasePolicy
from geometry_world_model import digest
from joint_neural_ports import perimeter_points
from joint_reward_training import head_vector, set_head_vector
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder


def endpoint_change(action, decoder):
    command = wire(action, 'MEASURE')
    phases = np.fromstring(command[8:], sep=' ').reshape(-1, 2)[len(decoder.positions):]
    provider = decoder.provider
    ports = provider.original_ports + perimeter_points(provider.phase + phases, provider.endpoint_sizes)[0] - provider.base_boundary
    rounded = np.copysign(np.floor(abs(ports) * 100 + .5), ports) / 100
    source = np.copysign(np.floor(abs(provider.original_ports) * 100 + .5), provider.original_ports) / 100
    difference = rounded - source
    return dict(maximumEndpointDisplacementPixels=float(abs(difference).max()),
        changedEndpoints=int(np.any(difference != 0, axis=2).sum()))


def run(args):
    spec = json.loads(args.source_binding.read_text())['viewSources'][args.view]
    directory = Path(spec['directory'])
    inputs = {name:digest(directory / name) for name in INPUT_FILES}
    assert inputs == spec['inputs']
    args.out.mkdir(parents=True, exist_ok=False)
    decoder = WalkDecoder(directory)
    native = Native(args.environment, directory, args.out / 'unused.tsv', args.view == 'overview', False)
    records = []
    try:
        assert native.initial['visual'] == spec['expectedVisual']
        nodes = np.array([row['features'] for row in native.initial['nodes']])
        model = SharedCardPhasePolicy(nodes, decoder, args.seed, args.span)
        np.savez_compressed(args.out / 'observations.npz', nodes=nodes)
        model.save(args.out / 'initial-model.npz', dict(trainedUpdates=0, diagnosticControl=True))
        zero = model.forward(nodes)[0]
        assert not np.any(zero)
        control = native.request(wire(zero, 'MEASURE'))
        assert control['legal'] and control['visual'] == spec['expectedVisual']
        with (args.out / 'controls.jsonl').open('x') as stream:
            for level, sigma in enumerate((.025, .1, .4)):
                for direction in range(2):
                    seed = args.seed + level * 100 + direction + 800
                    noise = np.random.default_rng(seed).normal(size=9)
                    for sign in (1., -1.):
                        set_head_vector(model, sign * sigma * noise, ('wo', 'bo'), (1., 1.))
                        action, info = model.forward(nodes)
                        assert not np.any(action[:len(decoder.positions)])
                        command = wire(action, 'MEASURE')
                        result = native.request(command)
                        checkpoint = args.out / f'noise-level-{level}-direction-{direction}-sign-{int(sign)}.npz'
                        model.save(checkpoint, dict(trainedUpdates=0, diagnosticControl=True, seed=seed, sigma=sigma, sign=sign))
                        row = dict(sigma=sigma, noiseSeed=seed, sign=sign, result=result, info=info,
                            checkpoint=str(checkpoint), checkpointSha256=digest(checkpoint),
                            wireSha256=hashlib.sha256(command.encode()).hexdigest(), **endpoint_change(action, decoder))
                        records.append(row)
                        stream.write(json.dumps(row) + '\n')
                        stream.flush()
    finally:
        native.close()
    legal = [row for row in records if row['result']['legal']]
    report = dict(kind='shared-card-phase-fixed-parameter-controls-v1', view=args.view, seed=args.seed, span=args.span,
        sourceDirectory=str(directory), sourceInputs=inputs, sourceBinding=str(args.source_binding),
        sourceBindingSha256=digest(args.source_binding), environment=str(args.environment), environmentSha256=digest(args.environment),
        zeroHeadControl=control, controls=len(records), legalControls=len(legal),
        minimumLegalVisual=min((row['result']['visual'] for row in legal), default=None),
        maximumLegalEndpointPixels=max((row['maximumEndpointDisplacementPixels'] for row in legal), default=None),
        observedHardValues=sorted(set(row['result']['hard'] for row in records)),
        fullNativeMeasurements=1 + len(records), newTrainingUpdates=0, nativeTryCalls=0,
        candidatePromoted=False, geometryRepairs=0, futureMetricsUsedForInference=False,
        controlScheduleIndependentOfNativeMetrics=True,
        observationsSha256=digest(args.out / 'observations.npz'), initialModelSha256=digest(args.out / 'initial-model.npz'),
        controlsSha256=digest(args.out / 'controls.jsonl'),
        codeSha256={name:digest(Path(__file__).parent / name) for name in
            ('probe_shared_card_phase.py', 'shared_card_phase_policy.py', 'joint_reward_training.py',
             'joint_neural_ports.py', 'joint_grouped_routes.py', 'run_anchor_pair_walk.py', 'run_anchor_pair_policy.py')})
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key:report[key] for key in ('view', 'controls', 'legalControls', 'minimumLegalVisual',
        'maximumLegalEndpointPixels', 'observedHardValues', 'newTrainingUpdates')}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-binding', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--view', choices=('overview', 'individual'), required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=118107)
    parser.add_argument('--span', type=float, default=.005)
    run(parser.parse_args())
