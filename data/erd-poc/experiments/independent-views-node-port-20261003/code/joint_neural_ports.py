"""Shared node/edge network and differentiable perimeter endpoint decoder.

Only shared network weights are trained. Card frames, owner indices, and source
port phases are immutable decoding data, not learnable coordinate tables.
"""
import json
import numpy as np
from joint_layout_proxy import JointPolicy, propagate_port_gradients
from joint_grouped_routes import GroupedRoutes, anchored_ports, read_pairs


def perimeter_phase(offsets, sizes):
    """Clockwise from top-left; nearest source side with deterministic ties."""
    x, y = offsets[..., 0], offsets[..., 1]
    w, h = sizes[..., 0], sizes[..., 1]
    side = np.argmin(np.stack([abs(y+h/2), abs(x-w/2), abs(y-h/2), abs(x+w/2)], -1), -1)
    lengths = np.stack([x+w/2, w+y+h/2, w+h+w/2-x, 2*w+h+h/2-y], -1)
    return np.take_along_axis(lengths, side[..., None], -1)[..., 0] / (2*(w+h))


def perimeter_points(phase, sizes):
    w, h = sizes[..., 0], sizes[..., 1]
    length = 2*(w+h)
    t = np.mod(phase, 1.)*length
    side = (t >= w).astype(int)+(t >= w+h)+(t >= 2*w+h)
    points = np.stack([np.where(side == 0, t-w/2, np.where(side == 1, w/2,
                       np.where(side == 2, w/2-(t-w-h), -w/2))),
                       np.where(side == 0, -h/2, np.where(side == 1, t-w-h/2,
                       np.where(side == 2, h/2, h/2-(t-2*w-h))))], -1)
    tangent = np.stack([np.where(side == 0, 1., np.where(side == 2, -1., 0.)),
                        np.where(side == 1, 1., np.where(side == 3, -1., 0.))], -1)
    return points, tangent*length[..., None]


class FullPerimeterPorts:
    def __init__(self, grouped, positions):
        self.grouped = grouped
        self.edges = grouped.full_edges
        half = grouped.sizes[self.edges]/2
        self.low = (positions[self.edges]-half).min(1)-.02
        self.high = (positions[self.edges]+half).max(1)+.02

    def envelopes(self, max_step):
        return self.low-max_step, self.high+max_step

    def forward(self, positions, sizes):
        return positions[self.edges]+self.grouped.current_offsets, None, self.edges

    def backward(self, gradient, route_gradient, edges, cache):
        np.add.at(gradient, edges[:, 0], route_gradient[:, 0])
        np.add.at(gradient, edges[:, 1], route_gradient[:, 1])
        self.grouped.port_gradient += route_gradient


class PerimeterRoutes(GroupedRoutes):
    neural_ports = True

    def __init__(self, directory):
        super().__init__(directory, attached=True)
        self.endpoint_sizes = self.sizes[self.full_edges]
        self.phase = perimeter_phase(self.port_offsets, self.endpoint_sizes)
        self.base_boundary = perimeter_points(self.phase, self.endpoint_sizes)[0]
        # Preserve original sub-cent rounding residue, including at zero output.
        # This is a fixed decoder bias, not a post-inference repair.
        assert np.max(abs(self.port_offsets-self.base_boundary)) < .011
        physical = np.array(list(read_pairs(directory/'positions.tsv').values()))
        sizes = np.array(list(read_pairs(directory/'nodes.tsv').values()))
        self.low = np.array([(physical[self.owner_edges[g]]-sizes[self.owner_edges[g]]/2).reshape(-1, 2).min(0) for g in self.groups])-.02
        self.high = np.array([(physical[self.owner_edges[g]]+sizes[self.owner_edges[g]]/2).reshape(-1, 2).max(0) for g in self.groups])+.02
        self.full_provider = FullPerimeterPorts(self, physical[self.owner]+self.offsets)
        self.set_actions(np.zeros(self.phase.shape))

    def set_actions(self, actions, quantized=False, positions=None):
        boundary, self.tangent = perimeter_points(self.phase+actions, self.endpoint_sizes)
        self.current_offsets = self.port_offsets+(boundary-self.base_boundary)
        if quantized:
            assert positions is not None
            centers = (positions[self.owner]+self.offsets)[self.full_edges]
            ports = centers+self.current_offsets
            self.current_offsets = np.copysign(np.floor(abs(ports)*100+.5), ports)/100-centers
        self.port_gradient = np.zeros_like(self.current_offsets)

    def action_gradient(self):
        return np.sum(self.port_gradient*self.tangent, -1)

    def forward(self, positions, sizes):
        full_positions = positions[self.owner]+self.offsets
        full_ports = full_positions[self.full_edges]+self.current_offsets
        rounded = np.copysign(np.floor(abs(full_ports)*100+.5), full_ports)/100
        lengths = np.sqrt(np.sum((rounded[:, 1]-rounded[:, 0])**2, -1))
        selected = np.array([g[np.argmin(lengths[g])] for g in self.groups])
        edges = self.owner_edges[selected]
        offsets = self.offsets[self.full_edges[selected]]+self.current_offsets[selected]
        clip = self.is_leaf[edges]
        ports, cache = anchored_ports(positions, sizes, edges, offsets, clip)
        direction = cache[0]
        half = sizes[edges]/2
        signs = np.array([-1., 1.])[None, :, None]
        ratios = (half+signs*np.sign(direction)[:, None]*offsets)/np.maximum(abs(direction)[:, None], 1e-12)
        axes = np.argmin(ratios, -1)
        return ports, (cache, selected, clip, axes), edges

    def backward(self, gradient, route_gradient, edges, cache):
        base, selected, clip, axes = cache
        direction, parameters, derivatives = base
        source, target = route_gradient[:, 0], route_gradient[:, 1]
        to_target = parameters[0][:, None]*source+derivatives[0]*np.sum(source*direction, 1)[:, None]
        to_source = parameters[1][:, None]*target+derivatives[1]*np.sum(target*direction, 1)[:, None]
        offset_gradient = np.stack([source-to_target+to_source, target-to_source+to_target], 1)
        propagate_port_gradients(gradient, route_gradient, edges, base)
        for endpoint in range(2):
            rows = np.flatnonzero(clip[:, endpoint]); axis = axes[rows, endpoint]
            offset_gradient[rows, endpoint, axis] -= np.sum(route_gradient[rows, endpoint]*direction[rows], 1)/direction[rows, axis]
        np.add.at(self.port_gradient, selected, offset_gradient)

    def constraint_loss(self, positions):
        full_positions = positions[self.owner]+self.offsets
        ports = full_positions[self.full_edges]+self.current_offsets
        direction = ports[:, 1]-ports[:, 0]
        lengths = np.sqrt(np.sum(direction*direction, 1)+1e-12)
        route_gradient = np.zeros_like(ports)
        total = 0.
        for allowed, other in self.competitors:
            good, bad = allowed[np.argmin(lengths[allowed])], other[np.argmin(lengths[other])]
            depth = max(0., lengths[good]+4.-lengths[bad])
            total += 20.*(depth/8.)**2
            if depth:
                for index, sign in [(good, 1.), (bad, -1.)]:
                    g = direction[index]/lengths[index]*(sign*20.*depth/32.)
                    route_gradient[index, 0] -= g
                    route_gradient[index, 1] += g
        gradient = np.zeros_like(positions)
        np.add.at(gradient, self.owner_edges[:, 0], route_gradient[:, 0])
        np.add.at(gradient, self.owner_edges[:, 1], route_gradient[:, 1])
        self.port_gradient += route_gradient
        return float(total), gradient

    def edge_features(self):
        direction = self.original_ports[:, 1]-self.original_ports[:, 0]
        length = np.maximum(1., np.linalg.norm(direction, axis=1))
        return np.concatenate([np.sin(2*np.pi*self.phase), np.cos(2*np.pi*self.phase),
                               np.log1p(self.endpoint_sizes).reshape(-1, 4),
                               direction/length[:, None], np.log1p(length)[:, None]], 1)


class JointPortPolicy(JointPolicy):
    keys = JointPolicy.keys+('ew1', 'eb1', 'ewo', 'ebo')
    buffers = ('mean', 'scale', 'negative', 'positive', 'edge_inputs', 'owner_edges', 'port_span')

    def __init__(self, features, positions, sizes, max_step, seed, provider, port_span):
        super().__init__(features, positions, sizes, max_step, seed)
        edge = provider.edge_features()
        self.edge_inputs = (edge-edge.mean(0))/np.maximum(.2, edge.std(0))
        self.owner_edges = provider.owner_edges.copy()
        self.port_span = np.array(port_span)
        rng = np.random.default_rng(seed+23000)
        width = 64+edge.shape[1]
        self.p.update(ew1=rng.normal(0, 1/np.sqrt(width), (width, 32)), eb1=np.zeros(32),
                      ewo=np.zeros((32, 2)), ebo=np.zeros(2))

    def forward(self, features):
        nodes, base = super().forward(features)
        x = np.concatenate([base[2][self.owner_edges].reshape(-1, 64), self.edge_inputs], 1)
        hidden = np.tanh(x@self.p['ew1']+self.p['eb1'])
        y = np.tanh(hidden@self.p['ewo']+self.p['ebo'])
        return np.concatenate([nodes, self.port_span*y], 0), (base, x, hidden, y)

    def backward(self, cache, gradient, port_gradient):
        base, x, hidden, y = cache
        dy = port_gradient*self.port_span*(1-y*y)
        dh = (dy@self.p['ewo'].T)*(1-hidden*hidden)
        dx = dh@self.p['ew1'].T
        hidden_gradient = np.zeros_like(base[2])
        np.add.at(hidden_gradient, self.owner_edges[:, 0], dx[:, :32])
        np.add.at(hidden_gradient, self.owner_edges[:, 1], dx[:, 32:64])
        result = super().backward(base, gradient, hidden_gradient)
        result.update(ewo=hidden.T@dy, ebo=dy.sum(0), ew1=x.T@dh, eb1=dh.sum(0))
        return result

    def save(self, path, metadata):
        np.savez_compressed(path, **self.p, **{k:getattr(self, k) for k in self.buffers}, metadata=json.dumps(metadata))

    @classmethod
    def load(cls, path):
        model = object.__new__(cls)
        with np.load(path) as data:
            model.p = {k:data[k].copy() for k in cls.keys}
            for k in cls.buffers:setattr(model, k, data[k].copy())
        return model
