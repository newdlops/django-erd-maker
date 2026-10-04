#!/usr/bin/env python3
"""Small conditional mixture policy: synthetic training, frozen ML-only proposals.

Uses NumPy only, under run_memory_bounded.py. Model inputs contain local
relative geometry; node names, absolute positions and Captain targets are
excluded. The native environment only observes, validates and applies the
supplied action. It never proposes an alternative or repairs an action.

Mixture-density reference: Bishop, NCRG/94/004 (1994),
https://www.microsoft.com/en-us/research/publication/mixture-density-networks/
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

SCHEMA = "relative-card-context-v1-58"
COMPONENT_SCHEMA = "relative-component-context-v2-64"
JOINT_SCHEMA = "relative-joint-context-v3-64"
PORT_SCHEMA = "relative-boundary-port-context-v4-64"
GLOBAL_SCHEMA = "relative-global-layout-context-v5-64"
NEIGHBOR_SCHEMA = "relative-neighbor-pair-context-v6-64"
SCHEMAS = {SCHEMA:58, COMPONENT_SCHEMA:64, JOINT_SCHEMA:64, PORT_SCHEMA:64, GLOBAL_SCHEMA:64, NEIGHBOR_SCHEMA:64}
KEYS = ("w1", "b1", "w2", "b2", "wo", "bo")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def logsumexp(x, axis=-1, keepdims=False):
    maximum = np.max(x, axis=axis, keepdims=True)
    result = maximum + np.log(np.exp(x - maximum).sum(axis=axis, keepdims=True))
    return result if keepdims else np.squeeze(result, axis=axis)


def sigmoid(x):
    return np.exp(-np.logaddexp(0, -x))


class Policy:
    def __init__(self, seed=37, features=58, hidden=64, mixtures=4):
        self.k = mixtures
        self.gain_cap = 8
        self.min_log_sigma = -3.5
        rng = np.random.default_rng(seed)
        self.p = {
            "w1": rng.normal(0, 1 / math.sqrt(features), (features, hidden)), "b1": np.zeros(hidden),
            "w2": rng.normal(0, 1 / math.sqrt(hidden), (hidden, hidden)), "b2": np.zeros(hidden),
            "wo": rng.normal(0, .02, (hidden, 5 * mixtures + 1)), "bo": np.zeros(5 * mixtures + 1),
        }
        self.p["bo"][mixtures:3*mixtures] = rng.normal(0, .15, 2*mixtures)
        self.p["bo"][3*mixtures:5*mixtures] = math.log(.25)
        self.p["bo"][-1] = -1
        self.mean, self.scale = np.zeros(features), np.ones(features)

    def forward(self, features):
        x = np.clip((features-self.mean)/self.scale, -8, 8)
        h1 = np.tanh(x @ self.p["w1"] + self.p["b1"])
        h2 = np.tanh(h1 @ self.p["w2"] + self.p["b2"])
        return h2 @ self.p["wo"] + self.p["bo"], (x, h1, h2)

    def distribution(self, features):
        z, _ = self.forward(features)
        pi = np.exp(z[:, :self.k] - logsumexp(z[:, :self.k], keepdims=True))
        mu = np.tanh(z[:, self.k:3*self.k].reshape(-1, self.k, 2))
        logs = np.clip(z[:, 3*self.k:5*self.k].reshape(-1, self.k, 2), self.min_log_sigma, -.1)
        return pi, mu, np.exp(logs), sigmoid(z[:, -1])

    def loss(self, features, actions, gain, gradients=False):
        z, (x, h1, h2) = self.forward(features)
        k = self.k
        logpi = z[:, :k] - logsumexp(z[:, :k], keepdims=True)
        pi = np.exp(logpi)
        mu = np.tanh(z[:, k:3*k].reshape(-1, k, 2))
        rawlogs = z[:, 3*k:5*k].reshape(-1, k, 2)
        logs = np.clip(rawlogs, self.min_log_sigma, -.1)
        variance = np.exp(2*logs)
        residual = mu-actions[:, None, :]
        logp = logpi - (logs + .5*residual**2/variance).sum(axis=2) - math.log(2*math.pi)
        loglike = logsumexp(logp)
        responsibility = np.exp(logp-loglike[:, None])
        weight = np.minimum(gain, self.gain_cap)
        weight = weight / max(1., weight.sum())
        positive = (gain > 0).astype(float)
        auxiliary = np.mean(np.logaddexp(0, z[:, -1])-positive*z[:, -1])
        nll = -np.sum(weight*loglike)
        loss = nll + .25*auxiliary
        if not gradients:
            return float(loss), float(nll)
        gz = np.zeros_like(z)
        gz[:, :k] = (pi-responsibility)*weight[:, None]
        shared = responsibility[:, :, None]*weight[:, None, None]
        gz[:, k:3*k] = (shared*residual/variance*(1-mu**2)).reshape(-1, 2*k)
        gz[:, 3*k:5*k] = (shared*(1-residual**2/variance)
            *((rawlogs > self.min_log_sigma)&(rawlogs < -.1))).reshape(-1, 2*k)
        gz[:, -1] = .25*(sigmoid(z[:, -1])-positive)/len(z)
        g2 = (gz @ self.p["wo"].T)*(1-h2**2)
        g1 = (g2 @ self.p["w2"].T)*(1-h1**2)
        grads = {"wo":h2.T @ gz, "bo":gz.sum(0), "w2":h1.T @ g2, "b2":g2.sum(0),
                 "w1":x.T @ g1, "b1":g1.sum(0)}
        return float(loss), grads


def self_test():
    # Integer JSON rewards must support fractional adaptation weights with the
    # CLI's default gain exponent, which argparse may retain as the integer 1.
    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        dataset = Path(directory)/'integer-rewards.jsonl'
        dataset.write_text(json.dumps({'graph':100000,'features':[0], 'actions':[[0,0,2]]})+'\n')
        _,_,_,rewards,_ = load_data(dataset,1)
        rewards = rewards**1
        rewards *= .25
        np.testing.assert_array_equal(rewards,[.5])
    rng = np.random.default_rng(99)
    model = Policy(features=5, hidden=6, mixtures=2)
    x, y, gain = rng.normal(size=(9, 5)), rng.normal(0, .2, (9, 2)), np.array([0, 1, 2, 0, 3, 0, 1, 2, 0])
    _, gradients = model.loss(x, y, gain, True)
    checked, largest = 0, 0.
    for name in KEYS:
        for flat in rng.choice(model.p[name].size, min(8, model.p[name].size), replace=False):
            index = np.unravel_index(flat, model.p[name].shape)
            original = model.p[name][index]
            model.p[name][index] = original+1e-5
            hi = model.loss(x, y, gain)[0]
            model.p[name][index] = original-1e-5
            lo = model.loss(x, y, gain)[0]
            model.p[name][index] = original
            expected = (hi-lo)/2e-5
            error = abs(expected-gradients[name][index])
            assert error < 2e-6, (name, index, expected, gradients[name][index])
            checked += 1; largest = max(largest, error)
    print(json.dumps({"gradientChecks":checked, "maxAbsoluteError":largest, "status":"pass"}))


def load_data(path, feature_count=58):
    states, graphs, actions, gains, indices = [], [], [], [], []
    with path.open() as file:
        for line in file:
            row = json.loads(line)
            assert len(row["features"]) == feature_count
            state_id = len(states)
            states.append(row["features"]); graphs.append(row["graph"])
            for dx, dy, gain in row["actions"]:
                actions.append((dx,dy)); gains.append(gain); indices.append(state_id)
    return np.array(states), np.array(graphs), np.array(actions), np.array(gains,dtype=np.float64), np.array(indices)


def train(args):
    schema = args.schema
    assert (schema == PORT_SCHEMA) == (args.decoder in ('ports','ports-residual')), 'port checkpoint schema/decoder mismatch'
    assert (schema == GLOBAL_SCHEMA) == (args.decoder in ('global-scale','global-scale-attached')), 'global checkpoint schema/decoder mismatch'
    assert schema != NEIGHBOR_SCHEMA or args.decoder == 'attached-all'
    feature_count = SCHEMAS[schema]
    states, groups, actions, gains, indices = load_data(args.dataset, feature_count)
    gains = gains**args.gain_power
    graph_ids = np.unique(groups)
    if args.adapt_graph is not None:
        assert args.adapt_graph in graph_ids and args.adapt_source and args.adapt_source.is_file()
        graph_ids=graph_ids[graph_ids != args.adapt_graph]
        gains[groups[indices] == args.adapt_graph] *= args.adapt_weight
    assert len(graph_ids) >= 8
    split = max(1, int(.8*len(graph_ids)))
    training_graphs, validation_graphs = graph_ids[:split], graph_ids[split:]
    if args.adapt_graph is not None:
        training_graphs=np.append(training_graphs,args.adapt_graph)
    state_mask = np.isin(groups, training_graphs)
    train_rows = np.flatnonzero(state_mask[indices]); val_rows = np.flatnonzero(~state_mask[indices])
    assert gains[train_rows].sum() > 0 and gains[val_rows].sum() > 0
    model = Policy(args.seed, features=feature_count)
    model.gain_cap = args.gain_cap
    model.min_log_sigma = args.min_log_sigma
    if args.min_log_sigma < -3.5:
        # One learned component starts at the fine movement scale. Both the
        # trained and untrained ablations retain this identical initial prior.
        k=model.k
        model.p['wo'][:,3*k-2:3*k]=0
        model.p['bo'][3*k-2:3*k]=0
        model.p['bo'][5*k-2:5*k]=math.log(.005)
    model.mean = states[state_mask].mean(0); model.scale = np.maximum(.2, states[state_mask].std(0))
    initial = {key:value.copy() for key,value in model.p.items()}
    initial_validation = model.loss(states[indices[val_rows]], actions[val_rows], gains[val_rows])
    first = {key:np.zeros_like(value) for key,value in model.p.items()}
    second = {key:np.zeros_like(value) for key,value in model.p.items()}
    rng = np.random.default_rng(args.seed)
    start, updates, history, best_nll, best_epoch, best = time.monotonic(), 0, [], float("inf"), 0, None
    for epoch in range(args.epochs):
        order = rng.permutation(train_rows)
        for offset in range(0, len(order), args.batch):
            batch = order[offset:offset+args.batch]
            loss, grads = model.loss(states[indices[batch]], actions[batch], gains[batch], True)
            assert np.isfinite(loss)
            norm = math.sqrt(sum(np.sum(g*g) for g in grads.values()))
            factor = min(1., 5/max(1e-12,norm)); updates += 1
            for key in KEYS:
                gradient = grads[key]*factor
                first[key] = .9*first[key]+.1*gradient; second[key] = .999*second[key]+.001*gradient**2
                model.p[key] -= args.lr*(first[key]/(1-.9**updates))/(np.sqrt(second[key]/(1-.999**updates))+1e-8)
            if time.monotonic()-start >= args.seconds:
                break
        validation_loss, nll = model.loss(states[indices[val_rows]], actions[val_rows], gains[val_rows])
        record = {"epoch":epoch+1, "updates":updates, "validationNll":nll, "seconds":time.monotonic()-start}
        history.append(record)
        if nll < best_nll:
            best_nll, best_epoch, best = nll, epoch+1, {key:value.copy() for key,value in model.p.items()}
        if epoch%5 == 0:
            print(json.dumps(record), flush=True)
        if time.monotonic()-start >= args.seconds:
            break
    assert best is not None and updates > 0
    metadata = {"kind":"conditional-mixture-card-policy-v1" if schema == SCHEMA else "conditional-mixture-component-policy-v2",
        "schema":schema, "features":feature_count, "mixtures":4,
        "actionScale":384 if schema == SCHEMA else 1, "perObservationScale":schema != SCHEMA,
        "jointMoves":schema == JOINT_SCHEMA,
        "neighborMoves":schema == NEIGHBOR_SCHEMA,
        "gainPower":args.gain_power, "gainCap":args.gain_cap, "environmentDecoder":args.decoder,
        "minLogSigma":args.min_log_sigma,
        "trainedUpdates":updates, "bestEpoch":best_epoch, "seed":args.seed,
        "trainingGraphIds":training_graphs.tolist(), "validationGraphIds":validation_graphs.tolist(),
        "datasetSha256":digest(args.dataset),
        "trainingData":"procedural synthetic graphs only" if args.adapt_graph is None else "synthetic graphs plus Captain reward observations",
        "captainTrainingExamples":0 if args.adapt_graph is None else int(np.count_nonzero(groups[indices] == args.adapt_graph)),
        "adaptationSourceSha256":None if args.adapt_graph is None else digest(args.adapt_source),
        "adaptationWeight":args.adapt_weight if args.adapt_graph is not None else None,
        "validationScope":"held-out synthetic graphs; Captain adaptation is not an unseen-graph evaluation" if args.adapt_graph is not None else "held-out synthetic graphs",
        "absoluteCoordinatesAsInput":False, "modelNamesAsInput":False}
    args.checkpoint.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(args.checkpoint, **best, **{"initial__"+k:v for k,v in initial.items()},
                        mean=model.mean, scale=model.scale, metadata=json.dumps(metadata))
    report = {**metadata, "checkpoint":str(args.checkpoint), "checkpointSha256":digest(args.checkpoint),
        "states":len(states), "trainingActions":len(train_rows), "validationActions":len(val_rows),
        "positiveTrainingActions":int(np.count_nonzero(gains[train_rows])),
        "initialValidationNll":initial_validation[1], "bestValidationNll":best_nll,
        "parameterCount":sum(value.size for value in best.values()), "seconds":time.monotonic()-start, "history":history}
    args.checkpoint.with_suffix(".training.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps({key:value for key,value in report.items() if key != "history"}), flush=True)


def load_policy(path, untrained=False):
    with np.load(path, allow_pickle=False) as file:
        metadata = json.loads(str(file["metadata"]))
        assert metadata["schema"] in SCHEMAS and metadata["trainedUpdates"] > 0
        model = Policy(features=SCHEMAS[metadata["schema"]])
        model.min_log_sigma = metadata.get("minLogSigma",-3.5)
        model.p = {key:file[("initial__" if untrained else "")+key].copy() for key in KEYS}
        model.mean, model.scale = file["mean"].copy(), file["scale"].copy()
    return model, metadata


def infer(args):
    model, metadata = load_policy(args.checkpoint, args.untrained)
    if metadata.get('environmentDecoder') in ('swap','ports','ports-residual','global-scale','global-scale-attached'):
        assert args.directory, 'geometry decoder replay requires an exported input directory'
    rng = np.random.default_rng(args.seed)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    source_args = ["--directory",str(args.directory)] if args.directory else ["--synthetic-seed",str(args.synthetic_seed)]
    if metadata.get("environmentDecoder"):
        source_args += ["--decoder",metadata["environmentDecoder"]]
    if metadata.get("jointMoves"):
        source_args += ["--joint","1"]
    if metadata.get('neighborMoves'):
        assert args.directory
        source_args += ['--neighbors','1']
    if args.overview_only:
        assert args.directory and args.out.resolve().is_relative_to(Path(__file__).resolve().parents[2]/'.tmp')
        source_args += ["--overview-only","1"]
    if args.canonical_start:
        assert metadata["schema"] == COMPONENT_SCHEMA and metadata.get("environmentDecoder") in (None,"ray")
        source_args += ["--canonical-start","1"]
    child = subprocess.Popen([str(args.environment), *source_args, "--out", str(args.out)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=sys.stderr, text=True, bufsize=1)
    def receive():
        line = child.stdout.readline()
        if not line:
            raise RuntimeError(f"native environment exited: {child.poll()}")
        return json.loads(line)
    def request(command):
        child.stdin.write(command+"\n"); child.stdin.flush(); return receive()
    start, attempts, accepted = time.monotonic(), 0, 0
    try:
        initial = receive(); assert initial["ready"]
        with Path(str(args.out)+".observations.jsonl").open("w") as observations, Path(str(args.out)+".actions.jsonl").open("w") as trace:
            for round_id in range(args.rounds):
                state = request(f"OBS {args.observations}")
                rows = state["nodes"]
                if not rows:
                    break
                observations.write(json.dumps({"round":round_id, **state})+"\n"); observations.flush()
                features = np.array([row["features"] for row in rows])
                pi, mu, sigma, confidence = model.distribution(features)
                order = np.argsort(-confidence, kind="stable")
                for row_index in order:
                    node = rows[row_index]["id"]
                    components = list(np.argsort(-pi[row_index], kind="stable"))
                    proposals = [(int(k),np.zeros(2),"mean") for k in components]
                    for _ in range(args.samples):
                        k = int(rng.choice(model.k, p=pi[row_index]))
                        proposals.append((k,rng.normal(size=2),"sample"))
                    for k,latent,kind in proposals:
                        if attempts >= args.budget or time.monotonic()-start >= args.seconds:
                            break
                        action_scale = rows[row_index].get("actionScale",metadata["actionScale"])
                        action = action_scale*(mu[row_index,k]+sigma[row_index,k]*latent)
                        # Only the predicted action is sent. Rejections do not
                        # trigger a native search, projection, or repair.
                        result = request(f"TRY {node} {action[0]:.12g} {action[1]:.12g}")
                        attempts += 1; accepted += int(result["accepted"])
                        trace.write(json.dumps({"round":round_id,"node":node,"component":k,"latent":latent.tolist(),
                            "kind":kind,"action":action.tolist(),"result":result})+"\n")
                        if result["accepted"]:
                            break
                    if attempts >= args.budget or time.monotonic()-start >= args.seconds:
                        break
                print(json.dumps({"round":round_id,"attempts":attempts,"accepted":accepted,"seconds":time.monotonic()-start}),flush=True)
                if attempts >= args.budget or time.monotonic()-start >= args.seconds:
                    break
        assert request("SAVE")["saved"]
        child.stdin.write("QUIT\n");child.stdin.flush(); assert child.wait(timeout=5) == 0
    finally:
        if child.poll() is None:
            child.kill();child.wait()
    stats = json.loads(Path(str(args.out)+".stats.json").read_text())
    assert stats.get("overviewOnly",False) == args.overview_only, 'environment objective mismatch'
    assert stats.get("swapSlots",False) == (metadata.get("environmentDecoder") == "swap"), 'environment decoder mismatch'
    assert stats.get("portActions",False) == (metadata.get("environmentDecoder") in ('ports','ports-residual')), 'environment port decoder mismatch'
    assert stats.get("residualPorts",False) == (metadata.get("environmentDecoder") == 'ports-residual'), 'environment residual decoder mismatch'
    assert stats.get('globalScale',False) == (metadata.get('environmentDecoder') in ('global-scale','global-scale-attached')), 'environment global decoder mismatch'
    assert stats.get('globalAttachedPorts',False) == (metadata.get('environmentDecoder') == 'global-scale-attached'), 'environment global ports mismatch'
    assert stats.get('neighborMoves',False) == metadata.get('neighborMoves',False), 'environment neighbor decoder mismatch'
    assert stats["policyActionsEvaluated"] == attempts and stats["acceptedActions"] == accepted
    report = {"checkpoint":str(args.checkpoint),"checkpointSha256":digest(args.checkpoint),"untrainedControl":args.untrained,
        "sourceDirectory":str(args.directory) if args.directory else None,"syntheticSeed":args.synthetic_seed,
        "policyProposalSource":"conditional mixture neural network", "heuristicSearchCalls":0,
        "overviewOnly":args.overview_only,
        "initial":initial,"final":stats,"budget":args.budget,"seed":args.seed,"seconds":time.monotonic()-start,
        "observationsSha256":digest(str(args.out)+".observations.jsonl"),"actionsSha256":digest(str(args.out)+".actions.jsonl")}
    if metadata.get("environmentDecoder") == "swap":
        report["swapInputHashes"] = {name:digest(args.directory/name) for name in ['nodes.tsv','positions.tsv']}
    if metadata.get("environmentDecoder") in ('ports','ports-residual'):
        report["portInputHashes"] = {name:digest(args.directory/name) for name in
            ['individual.nodes.tsv','individual.edges.tsv','individual.positions.tsv','individual.routes.tsv']}
    if metadata.get('environmentDecoder') in ('global-scale','global-scale-attached'):
        from learned_global_replay import INPUT_FILES
        report['globalInputHashes'] = {name:digest(args.directory/name) for name in INPUT_FILES}
    if metadata.get('neighborMoves'):
        from learned_global_replay import INPUT_FILES
        report['neighborInputHashes'] = {name:digest(args.directory/name) for name in INPUT_FILES}
    Path(str(args.out)+".policy.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(report),flush=True)


def replay(args):
    report = json.loads(Path(str(args.out)+".policy.json").read_text())
    assert report["checkpointSha256"] == digest(args.checkpoint)
    model, metadata = load_policy(args.checkpoint, report["untrainedControl"])
    def read_positions(path):
        result = {}
        for line in path.read_text().splitlines():
            node,x,y = line.split('\t')
            result[node] = [float(x),float(y)]
        return result
    swap_positions, swap_ids = None, None
    port_nodes,port_edges,port_routes,port_sides = None,None,None,None
    global_replay = None
    neighbor_replay = None
    if metadata.get('neighborMoves'):
        from learned_global_replay import NeighborReplay
        directory = Path(report['sourceDirectory'])
        for name,sha in report['neighborInputHashes'].items(): assert digest(directory/name) == sha
        neighbor_replay = NeighborReplay(directory)
    if metadata.get('environmentDecoder') in ('global-scale','global-scale-attached'):
        from learned_global_replay import GlobalReplay
        directory = Path(report['sourceDirectory'])
        for name,sha in report['globalInputHashes'].items(): assert digest(directory/name) == sha
        global_replay = GlobalReplay(directory, report['final']['bboxLimit'], metadata.get('environmentDecoder') == 'global-scale-attached')
    def read_routes(path):
        result = {}
        for line in path.read_text().splitlines():
            key,text = line.split('\t')
            result[key] = [[float(value) for value in point.split(',')] for point in text.split()]
        return result
    def round2(value):
        return math.copysign(math.floor(abs(value)*100+.5)/100,value)
    if metadata.get("environmentDecoder") == "swap":
        directory = Path(report['sourceDirectory'])
        for name, sha in report['swapInputHashes'].items():
            assert digest(directory/name) == sha
        swap_ids = [line.split('\t')[0] for line in (directory/'nodes.tsv').read_text().splitlines()]
        by_id = read_positions(directory/'positions.tsv')
        swap_positions = np.array([by_id[node] for node in swap_ids])
    if metadata.get('environmentDecoder') in ('ports','ports-residual'):
        directory = Path(report['sourceDirectory'])
        for name,sha in report['portInputHashes'].items(): assert digest(directory/name)==sha
        port_positions = read_positions(directory/'individual.positions.tsv')
        port_sizes = read_positions(directory/'individual.nodes.tsv')
        port_nodes = {node:(*port_positions[node],*port_sizes[node]) for node in port_positions}
        port_edges = [line.split('\t') for line in (directory/'individual.edges.tsv').read_text().splitlines()]
        port_routes = read_routes(directory/'individual.routes.tsv')
        port_sides = []
        for key,source,target in port_edges:
            sides = []
            for p,node in zip(port_routes[key],[source,target]):
                x,y,w,h=port_nodes[node]
                distances=[abs(p[0]-x+w/2),abs(p[0]-x-w/2),abs(p[1]-y+h/2),abs(p[1]-y-h/2)]
                sides.append(int(np.argmin(distances)))
            port_sides.append(sides)
    observations = {}
    with Path(str(args.out)+".observations.jsonl").open() as file:
        for line in file:
            record = json.loads(line)
            for row in record["nodes"]:
                observations[(record["round"],row["id"])] = (row["features"],row.get("actionScale",metadata["actionScale"]))
    checked = 0
    with Path(str(args.out)+".actions.jsonl").open() as file:
        for line in file:
            action = json.loads(line)
            features,action_scale = observations[(action["round"],action["node"])]
            _,mu,sigma,_ = model.distribution(np.array([features]))
            k = action["component"]
            expected = action_scale*(mu[0,k]+sigma[0,k]*np.array(action["latent"]))
            np.testing.assert_allclose(expected, action["action"], rtol=1e-10, atol=1e-8)
            if global_replay is not None and action['result']['accepted']:
                global_replay.apply(action['action'])
            if neighbor_replay is not None:
                neighbor_replay.apply_record(action)
            if swap_positions is not None:
                # Replay the fixed decoder as well as the neural intent, with
                # no crossing score or alternative-slot selection.
                sent = np.array([float(f'{value:.12g}') for value in action['action']])
                delta = np.copysign(np.floor(np.abs(sent)*100+.5),sent)/100
                node = action['node']
                distances = np.hypot(*(swap_positions-(swap_positions[node]+delta)).T)
                target = int(np.argmin(np.floor(distances*1e6+.5)))
                assert target == action['result']['decodedTarget'], (checked,node,target)
                if action['result']['accepted']:
                    offset = swap_positions[target]-swap_positions[node]
                    swap_positions[node] += offset
                    swap_positions[target] -= offset
            if port_nodes is not None and action['result']['accepted']:
                edge_index=action['node'];key,source,target=port_edges[edge_index];points=[]
                for k,(value,node,side) in enumerate(zip(action['action'],[source,target],port_sides[edge_index])):
                    q=float(f'{value:.12g}');x,y,w,h=port_nodes[node]
                    if metadata.get('environmentDecoder')=='ports-residual':
                        old=port_routes[key][k];q+=2*(old[1]-y)/h if side<2 else 2*(old[0]-x)/w
                    q=max(-1.,min(1.,q))
                    points.append([round2(x+(-1 if side==0 else 1)*w/2),round2(y+q*h/2)] if side<2
                        else [round2(x+q*w/2),round2(y+(-1 if side==2 else 1)*h/2)])
                port_routes[key]=points
            checked += 1
    assert checked == report["final"]["policyActionsEvaluated"]
    if swap_positions is not None:
        actual = read_positions(args.out)
        np.testing.assert_allclose(swap_positions,np.array([actual[node] for node in swap_ids]),rtol=0,atol=1e-6)
    if port_nodes is not None:
        actual=read_routes(Path(str(args.out)+'.individual.routes.tsv'))
        assert actual.keys()==port_routes.keys()
        np.testing.assert_allclose([actual[key] for key in actual],[port_routes[key] for key in actual],rtol=0,atol=1e-8)
        actual_positions=read_positions(Path(str(args.out)+'.individual'))
        assert actual_positions.keys()==port_positions.keys()
        np.testing.assert_allclose([actual_positions[key] for key in port_positions],list(port_positions.values()),rtol=0,atol=1e-8)
    if global_replay is not None:
        global_replay.verify(args.out)
    if neighbor_replay is not None:
        neighbor_replay.verify(args.out)
    print(json.dumps({"frozenCheckpointActionReplay":"pass","actions":checked,"untrainedControl":report["untrainedControl"],
        "swapTargetAndFinalPositionReplay":swap_positions is not None,'portParametersAndFinalRoutesReplay':port_nodes is not None,
        'globalPositionsAndCanonicalRoutesReplay':global_replay is not None,
        'neighborPairPositionsAndRoutesReplay':neighbor_replay is not None}))


def main():
    parser = argparse.ArgumentParser()
    subs = parser.add_subparsers(dest="command", required=True)
    subs.add_parser("self-test")
    p = subs.add_parser("train")
    p.add_argument("--dataset",type=Path,required=True);p.add_argument("--checkpoint",type=Path,required=True)
    p.add_argument("--schema",choices=list(SCHEMAS),default=SCHEMA)
    p.add_argument("--gain-power",type=float,default=1);p.add_argument("--gain-cap",type=float,default=8)
    p.add_argument("--min-log-sigma",type=float,default=-3.5)
    p.add_argument("--adapt-graph",type=int);p.add_argument("--adapt-source",type=Path);p.add_argument("--adapt-weight",type=float,default=5)
    p.add_argument("--decoder",choices=["ray","attached","attached-all","swap","ports","ports-residual","global-scale","global-scale-attached"])
    p.add_argument("--epochs",type=int,default=60);p.add_argument("--seconds",type=float,default=40)
    p.add_argument("--batch",type=int,default=256);p.add_argument("--lr",type=float,default=.001);p.add_argument("--seed",type=int,default=37)
    p = subs.add_parser("infer")
    p.add_argument("--checkpoint",type=Path,required=True);p.add_argument("--environment",type=Path,required=True)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument("--directory",type=Path);source.add_argument("--synthetic-seed",type=int)
    p.add_argument("--out",type=Path,required=True)
    p.add_argument("--budget",type=int,default=2048);p.add_argument("--rounds",type=int,default=4)
    p.add_argument("--samples",type=int,default=12);p.add_argument("--seconds",type=float,default=20)
    p.add_argument("--observations",type=int,default=192)
    p.add_argument("--canonical-start",action="store_true")
    p.add_argument("--overview-only",action="store_true",help="isolated .tmp experiment; individual visual may regress")
    p.add_argument("--seed",type=int,default=123);p.add_argument("--untrained",action="store_true")
    p = subs.add_parser("replay")
    p.add_argument("--checkpoint",type=Path,required=True);p.add_argument("--out",type=Path,required=True)
    args = parser.parse_args()
    if args.command == "self-test":self_test()
    elif args.command == "train":train(args)
    elif args.command == "infer":infer(args)
    else:replay(args)


if __name__ == "__main__":
    main()
