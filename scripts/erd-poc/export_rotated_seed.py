"""Rotate only layout coordinates; the constrained engine retains actual card sizes."""
from pathlib import Path
import math


def write_rotation(position_path, output, degrees):
    if not math.isfinite(degrees):
        raise ValueError('rotation must be finite')
    rows = [row.split('\t') for row in Path(position_path).read_text().splitlines()]
    points = [(float(x), float(y)) for _, x, y in rows]
    cx = (min(p[0] for p in points) + max(p[0] for p in points)) / 2
    cy = (min(p[1] for p in points) + max(p[1] for p in points)) / 2
    angle = math.radians(degrees)
    cosine, sine = math.cos(angle), math.sin(angle)
    rotated = [(cx + (x - cx) * cosine - (y - cy) * sine,
                cy + (x - cx) * sine + (y - cy) * cosine) for x, y in points]
    Path(output).write_text(''.join(f'{row[0]}\t{x:.9f}\t{y:.9f}\n' for row, (x, y) in zip(rows, rotated)))
    return {'kind': 'rotation', 'degrees': degrees, 'cardCount': len(rows),
            'note': 'Only an input seed. Actual card spacing and canvas bounds require repair and full scoring.'}
