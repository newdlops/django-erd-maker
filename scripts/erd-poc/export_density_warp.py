"""Spread dense coordinate intervals without changing cards or graph membership.

This exports a seed only. The actual-card optimizer must repair spacing and
score all original relationships before the seed is eligible for selection.
"""
import math
from pathlib import Path


def spread_axis(values, mix, bandwidth):
    if not 0 < mix <= 1 or not math.isfinite(bandwidth) or bandwidth <= 0:
        raise ValueError('invalid density warp limits')
    low, high = min(values), max(values)
    if high == low:
        return list(values)
    scale = math.sqrt(2) * bandwidth
    def cdf(value):
        return sum(math.erf((value - point) / scale) for point in values)
    first, last = cdf(low), cdf(high)
    return [(1 - mix) * value + mix * (low + (high - low) * (cdf(value) - first) / (last - first))
            for value in values]


def write_density_warp(source, output, mix, bandwidth):
    rows = [row.split('\t') for row in Path(source).read_text().splitlines()]
    if not rows or any(len(row) != 3 for row in rows) or len({row[0] for row in rows}) != len(rows):
        raise ValueError('invalid seed rows')
    coordinates = [[float(row[axis]) for row in rows] for axis in (1, 2)]
    if not all(math.isfinite(value) for axis in coordinates for value in axis):
        raise ValueError('nonfinite seed')
    warped = [spread_axis(axis, mix, bandwidth) for axis in coordinates]
    Path(output).write_text(''.join(f'{row[0]}\t{warped[0][i]:.9f}\t{warped[1][i]:.9f}\n'
                                  for i, row in enumerate(rows)))
    return {'kind': 'smoothed-marginal-density', 'mix': mix, 'bandwidth': bandwidth, 'nodes': len(rows)}
