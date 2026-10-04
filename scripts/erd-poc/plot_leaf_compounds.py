#!/usr/bin/env python3
"""Geometry QA of the actual product scene and its layout-only leaf actors."""
import argparse
import json
import os
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--scene', type=Path, required=True)
parser.add_argument('--graph', type=Path, required=True)
parser.add_argument('--positions', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
os.environ['MPLCONFIGDIR'] = str(args.output.parent / 'mpl')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection, PatchCollection
from matplotlib.patches import Rectangle

scene = json.loads(args.scene.read_text())
graph = json.loads(args.graph.read_text())
positions = {mid: (float(x), float(y)) for mid, x, y in (line.split('\t') for line in args.positions.read_text().splitlines())}
compounds = [actor for actor in graph['actors'] if 'members' in actor]
tables = scene['tables']
table_by_id = {node['modelId']: node for node in tables}
if compounds:
    largest = max(compounds, key=lambda actor: len(actor['members']))
    cx, cy = positions[largest['modelId']]
    parent = table_by_id[largest['parentModelId']]
    px, py = parent['position']['x'], parent['position']['y']
    limits = [min(cx - largest['size']['width'] / 2, px) - 500,
              max(cx + largest['size']['width'] / 2, px + parent['size']['width']) + 500,
              min(cy - largest['size']['height'] / 2, py) - 500,
              max(cy + largest['size']['height'] / 2, py + parent['size']['height']) + 500]
else:
    parent = table_by_id['db.Company']
    px, py = parent['position']['x'], parent['position']['y']
    limits = [px - 1900, px + 2700, py - 2600, py + 2000]


def draw(ax, zoom=None):
    ax.set_facecolor('#09121a')
    for bundled in (False, True):
        lines = [[tuple(map(float, point.split(','))) for point in edge['points'].split()]
                 for edge in scene['edges'] if (len(edge.get('memberEdgeIds', [])) > 1) == bundled]
        ax.add_collection(LineCollection(lines, colors='#ffc075' if bundled else '#80a5a1',
                                        alpha=.85 if bundled else .48, linewidths=1 if bundled else .5))
    rectangles = [Rectangle((n['position']['x'], n['position']['y']), n['size']['width'], n['size']['height']) for n in tables]
    ax.add_collection(PatchCollection(rectangles, facecolors='#173448', edgecolors='#67bda7', linewidths=.45, zorder=4))
    for actor in compounds:
        x, y = positions[actor['modelId']]
        width, height = actor['size']['width'], actor['size']['height']
        ax.add_patch(Rectangle((x - width / 2, y - height / 2), width, height, facecolor='none', edgecolor='#ffb957', linewidth=1.1, zorder=5))
        if zoom and zoom[0] < x < zoom[1] and zoom[2] < y < zoom[3]:
            ax.text(x - width / 2, y - height / 2 - 35, f"{actor['modelId']}  |  {len(actor['members'])} leaves  |  {width:.0f} x {height:.0f}", color='#ffca87', fontsize=7)
    if zoom:
        for node in tables:
            x, y = node['position']['x'], node['position']['y']
            if zoom[0] < x < zoom[1] and zoom[2] < y < zoom[3]:
                ax.text(x + node['size']['width'] / 2, y + node['size']['height'] / 2,
                        node['modelName'], ha='center', va='center', fontsize=4.7, color='#e3eee9', zorder=6, clip_on=True)
    else:
        zoom = [min(n['position']['x'] for n in tables) - 300,
                max(n['position']['x'] + n['size']['width'] for n in tables) + 300,
                min(n['position']['y'] for n in tables) - 300,
                max(n['position']['y'] + n['size']['height'] for n in tables) + 300]
    ax.set(xlim=zoom[:2], ylim=zoom[2:][::-1], aspect='equal')
    ax.tick_params(colors='#6f828a', labelsize=7)


fig, axes = plt.subplots(1, 2, figsize=(16, 8.5), dpi=145)
draw(axes[0]); draw(axes[1], limits)
axes[0].set_title('Product geometry, with computation rectangles outlined' if compounds else 'Product geometry: no compound actors')
axes[1].set_title('Largest leaf compound and its actual parent' if compounds else 'Company neighborhood')
fig.suptitle(f"{graph['leafCount']} leaf cards packed into {len(compounds)} rigid, full-size layout actors.\nAmber outlines show computational footprints; they are annotations, not browser UI. All displayed paths are straight.", fontsize=10)
fig.tight_layout(rect=(0, 0, 1, .93))
fig.savefig(args.output)
plt.close(fig)
print(args.output)
