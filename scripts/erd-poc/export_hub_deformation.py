"""Prepare a complete layout seed by exchanging two real hubs and their followers.

Other high-degree hubs anchor the displacement field. Every remaining card gets
the average displacement of its graph neighbors; cards in unrelated components
stay fixed. The constrained optimizer must repair and score this seed afterward.
"""
from pathlib import Path
import math


def harmonic_swap(positions, adjacency, first, second, anchor_degree=12):
    if first == second or first not in positions or second not in positions:
        raise ValueError('hub swap requires two distinct original card IDs')
    if anchor_degree < 3:
        raise ValueError('anchor degree must be at least 3')
    fixed = {n for n in positions if len(adjacency[n]) >= anchor_degree} | {first, second}
    values = {n: (1. if n == first else -1. if n == second else 0.) for n in positions}
    free = [n for n in positions if n not in fixed and adjacency[n]]
    residual = 0.
    iterations = 0
    for iterations in range(1, 10001):
        change = 0.
        for n in free:
            value = sum(values[other] for other in adjacency[n]) / len(adjacency[n])
            change = max(change, abs(value - values[n]))
            values[n] = value
        if change < 1e-11:
            break
    for n in free:
        residual = max(residual, abs(values[n] - sum(values[other] for other in adjacency[n]) / len(adjacency[n])))
    if residual > 1e-8:
        raise RuntimeError(f'harmonic displacement did not converge: {residual}')
    delta = tuple(positions[second][i] - positions[first][i] for i in range(2))
    result = {n: tuple(p[i] + values[n] * delta[i] for i in range(2)) for n, p in positions.items()}
    if not all(math.isfinite(v) for p in result.values() for v in p):
        raise ValueError('non-finite hub deformation')
    return result, {'first': first, 'second': second, 'anchorDegree': anchor_degree,
                    'fixedAnchors': len(fixed), 'iterations': iterations, 'residual': residual,
                    'displacedCards': sum(abs(value) > 1e-8 for value in values.values())}


def write_hub_swap(position_path, edge_path, output, first, second, anchor_degree=12):
    positions = {}
    for row in Path(position_path).read_text().splitlines():
        name, x, y = row.split('\t')
        positions[name] = (float(x), float(y))
    adjacency = {n: set() for n in positions}
    for row in Path(edge_path).read_text().splitlines():
        _, source, target, *_ = row.split('\t')
        if source != target:
            adjacency[source].add(target)
            adjacency[target].add(source)
    # Stable iteration order also makes the convergence and exported seed reproducible.
    adjacency = {n: sorted(others) for n, others in adjacency.items()}
    result, stats = harmonic_swap(positions, adjacency, first, second, anchor_degree)
    Path(output).write_text(''.join(f'{n}\t{p[0]:.9f}\t{p[1]:.9f}\n' for n, p in result.items()))
    return stats
