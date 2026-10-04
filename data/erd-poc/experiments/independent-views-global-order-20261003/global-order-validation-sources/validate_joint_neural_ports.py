#!/usr/bin/env python3
"""Bounded derivative and source-reproduction checks for both actual views."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import numpy as np
from joint_neural_ports import PerimeterRoutes, JointPortPolicy, SharedEndpointPolicy, RayConditionedPolicy, perimeter_points
from joint_grouped_routes import read_pairs
from joint_hard_geometry import FullHardGeometry
from joint_layout_proxy import LayoutProxy


def validate(directory, active_pairs=False, cross_depth_weight=0., shared_endpoints=False, spacing_padding=2., ray_conditioned_ports=False):
    rng = np.random.default_rng(711)
    provider = PerimeterRoutes(directory)
    positions = np.array(list(read_pairs(directory/'positions.tsv').values()))
    sizes_map = read_pairs(directory/'nodes.tsv')
    sizes = np.array([sizes_map[k] for k in provider.physical_ids])
    indices = {k:i for i,k in enumerate(provider.physical_ids)}
    edges = np.array([[indices[s], indices[t]] for _,s,t in
                      (line.split('\t') for line in (directory/'edges.tsv').read_text().splitlines())])
    features = np.array([r['features'] for r in json.loads((directory/'joint-observations.json').read_text())['nodes']])
    model_class = RayConditionedPolicy if ray_conditioned_ports else SharedEndpointPolicy if shared_endpoints else JointPortPolicy
    model = model_class(features, positions, sizes, 32., 719, provider, .125)
    action, _ = model.forward(features)
    np.testing.assert_array_equal(action, 0.)
    provider.set_actions(action[len(positions):], quantized=True, positions=positions)
    full_positions = positions[provider.owner]+provider.offsets
    ports = provider.full_provider.forward(full_positions, provider.sizes)[0]
    np.testing.assert_allclose(ports, provider.original_ports, atol=1e-8, rtol=0)
    zero_ports = ports.size//2
    # Check every side and wrapping without depending on the native decoder.
    phases = np.arange(-1., 2., .03125)[:, None]
    boundary, tangent = perimeter_points(phases, np.broadcast_to([120., 80.], phases.shape+(2,)))
    distance = np.minimum(abs(abs(boundary[..., 0])-60), abs(abs(boundary[..., 1])-40))
    assert distance.max() < 1e-10 and abs(boundary[..., 0]).max()<=60 and abs(boundary[..., 1]).max()<=40
    # Route gradients include projection, representative penalties, and offsets.
    actions = rng.normal(0, .013, provider.phase.shape)
    physical = positions+rng.uniform(-3., 3., positions.shape)
    weight = rng.normal(size=(len(edges), 2, 2))
    def route_loss(p, a):
        provider.set_actions(a)
        points, cache, dynamic_edges = provider.forward(p, sizes)
        penalty, gradient = provider.constraint_loss(p)
        provider.backward(gradient, weight, dynamic_edges, cache)
        return float(np.sum(points*weight)+penalty), gradient, provider.action_gradient().copy()
    _, pg, ag = route_loss(physical, actions)
    checked = 0
    for kind, values, grad, step in [('position',physical,pg,1e-4), ('phase',actions,ag,1e-7)]:
        for index in np.argsort(abs(grad).ravel())[-12:]:
            key = np.unravel_index(index, values.shape)
            hi, lo = values.copy(), values.copy();hi[key]+=step;lo[key]-=step
            fn = (lambda value:route_loss(value,actions)[0]) if kind=='position' else (lambda value:route_loss(physical,value)[0])
            np.testing.assert_allclose(grad[key], (fn(hi)-fn(lo))/(2*step), rtol=1e-4, atol=.003)
            checked+=1
    if active_pairs:
        from joint_active_proxy import ActiveLayoutProxy
        visual=ActiveLayoutProxy(positions,sizes,edges,20.,provider,chunk_size=4096)
    else:visual = LayoutProxy(positions, sizes, edges, 32., 20., provider)
    visual.cross_depth_weight=cross_depth_weight
    visual.spacing_padding=spacing_padding
    hard = FullHardGeometry(provider, positions, sizes, edges, 32., 1000.)
    provider.set_actions(np.zeros_like(actions))
    baseline, _ = visual.loss(positions)
    zero_hard, _ = hard.loss(positions)
    assert zero_hard['total'] < 1e-8, zero_hard
    # Nonzero heads exercise hidden node gradients from both heads.
    model.p['wo'] = rng.normal(0, .004, model.p['wo'].shape)
    model.p['bo'] = rng.normal(0, .005, model.p['bo'].shape)
    model.p['ewo'] = rng.normal(0, .008, model.p['ewo'].shape)
    model.p['ebo'] = rng.normal(0, .005, model.p['ebo'].shape)
    if shared_endpoints:
        random_gradient=rng.normal(size=provider.phase.shape)
        pooled=model.pool(random_gradient)
        np.testing.assert_allclose(model.pool(pooled),pooled,atol=1e-15,rtol=1e-15)
        action=model.forward(features)[0][len(positions):].ravel()
        for group in np.flatnonzero(model.endpoint_counts>1):
            members=action[model.endpoint_groups==group]
            np.testing.assert_array_equal(members,members[0])
    def network_loss():
        action, cache = model.forward(features)
        p = positions+action[:len(positions)]
        provider.set_actions(action[len(positions):])
        values, gradient = visual.loss(p, 3.)
        hard_values, hard_gradient = hard.loss(p)
        return values['total']+hard_values['total'], model.backward(cache, gradient+hard_gradient, provider.action_gradient())
    total, gradients = network_loss()
    assert np.isfinite(total)
    weight_checks = 0
    for name in model.keys:
        for flat in np.argsort(abs(gradients[name]).ravel())[-3:]:
            key = np.unravel_index(flat, model.p[name].shape)
            # The actual layout uses coordinates around 10^4; a 10^-7 weight
            # step loses precision when a shared translation cancels large
            # hard-loss terms. A 10^-5 central difference resolves that signal.
            value = model.p[name][key];step=1e-5
            model.p[name][key]=value+step;hi=network_loss()[0]
            model.p[name][key]=value-step;lo=network_loss()[0]
            model.p[name][key]=value
            np.testing.assert_allclose(gradients[name][key], (hi-lo)/(2*step), rtol=5e-4, atol=.003,
                                       err_msg=f'{directory.name} {name}{key} loss={total}')
            weight_checks+=1
    with tempfile.TemporaryDirectory(dir=directory.parent, prefix='port-weight-check-') as tmp:
        checkpoint=Path(tmp)/'model.npz'
        model.save(checkpoint, {'validationOnly':True})
        restored=JointPortPolicy.load(checkpoint)
        assert type(restored) is model_class
        np.testing.assert_array_equal(restored.forward(features)[0],model.forward(features)[0])
    return {'input':str(directory),'activePairLoss':active_pairs,'crossingDepthWeight':cross_depth_weight,'sharedSourceEndpoints':shared_endpoints,
            'rayConditionedPorts':ray_conditioned_ports,
            'spacingPadding':spacing_padding,
            'sharedGroupsPreserved':int(np.count_nonzero(model.endpoint_counts>1)) if shared_endpoints else None,
            'zeroOutputEndpointsPreserved':zero_ports,'routeGradientChecks':checked,
            'networkWeightGradientChecks':weight_checks,'baselineVisual':baseline['binaryCrossings']+baseline['binaryCardHits'],
            'baselineHardPenalty':zero_hard['total'],'frozenRoundtripExact':True,
            'parameterCount':sum(p.size for p in model.p.values()),'allChecksPassed':True}


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,action='append',required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--active-pairs',action='store_true')
    parser.add_argument('--cross-depth-weight',type=float,default=0.)
    parser.add_argument('--shared-endpoints',action='store_true')
    parser.add_argument('--spacing-padding',type=float,default=2.)
    parser.add_argument('--ray-conditioned-ports',action='store_true')
    args=parser.parse_args()
    reports=[]
    for directory in args.directory:
        report=validate(directory,args.active_pairs,args.cross_depth_weight,args.shared_endpoints,args.spacing_padding,args.ray_conditioned_ports);reports.append(report);print(json.dumps(report),flush=True)
    result={'views':reports,'continuousGradientChecks':True,'nativeValidationStillRequired':True,
            'implementationHashes':{name:hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()
            for name in ['validate_joint_neural_ports.py','joint_neural_ports.py','joint_layout_proxy.py','joint_hard_geometry.py','joint_active_proxy.py']}}
    args.out.write_text(json.dumps(result,indent=2)+'\n')
