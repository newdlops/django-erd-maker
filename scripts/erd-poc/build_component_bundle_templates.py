#!/usr/bin/env python3
"""Build planar local templates for tolerant non-core node components."""

from __future__ import annotations

import argparse
import resource
import sys
from collections import Counter, defaultdict
from pathlib import Path

import networkx as nx

from expand_anchor_node_bundles import (
    bounds,
    local_planar_positions,
    read_edges,
    read_nodes,
    read_positions,
    scaled_without_card_overlap,
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--edges", type=Path, required=True)
    parser.add_argument("--core-nodes", type=Path, required=True)
    parser.add_argument("--core-positions", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--promoted-dir", type=Path)
    parser.add_argument("--card-gap", type=float, default=24.0)
    parser.add_argument("--detached-gap", type=float, default=500.0)
    args = parser.parse_args()

    node_order, sizes = read_nodes(args.nodes)
    edges = read_edges(args.edges)
    core = {
        raw.split("\t", 1)[0]
        for raw in args.core_nodes.read_text(encoding="utf-8").splitlines()
        if raw
    }
    core_positions = read_positions(args.core_positions)
    graph = nx.Graph()
    graph.add_nodes_from(node_order)
    graph.add_edges_from(edges)
    shell = set(node_order) - core
    components = [set(value) for value in nx.connected_components(graph.subgraph(shell))]
    records: list[tuple[set[str], set[str]]] = []
    for component in components:
        anchors = {
            neighbor
            for node in component
            for neighbor in graph.neighbors(node)
            if neighbor in core
        }
        records.append((component, anchors))
    records.sort(key=lambda item: (-len(item[1]), -len(item[0]), min(item[0])))
    anchor_histogram = Counter(len(anchors) for _component, anchors in records)
    promoted_member_count = sum(
        len(component) for component, anchors in records if len(anchors) > 2
    )

    result = dict(core_positions)
    anchored_templates: list[tuple[dict[str, tuple[float, float]], tuple[float, float]]] = []
    detached_templates: list[dict[str, tuple[float, float]]] = []
    nonplanar = 0
    for component, anchors in records:
        component_graph = graph.subgraph(component).copy()
        planar, _ = nx.check_planarity(component_graph, counterexample=False)
        if not planar:
            nonplanar += 1
        local_anchor = min(component)
        local = scaled_without_card_overlap(
            local_planar_positions(component_graph, local_anchor),
            sizes,
            args.card_gap,
        )
        box = bounds(local, sizes)
        local_center = ((box[0] + box[2]) * 0.5, (box[1] + box[3]) * 0.5)
        local = {
            node: (point[0] - local_center[0], point[1] - local_center[1])
            for node, point in local.items()
        }
        if anchors:
            center = (
                sum(core_positions[anchor][0] for anchor in anchors) / len(anchors),
                sum(core_positions[anchor][1] for anchor in anchors) / len(anchors),
            )
            anchored_templates.append((local, center))
        else:
            detached_templates.append(local)

    for local, center in anchored_templates:
        for node, point in local.items():
            result[node] = (center[0] + point[0], center[1] + point[1])

    core_box = bounds({node: result[node] for node in core}, sizes)
    cursor_x = core_box[2] + args.detached_gap * 2.0
    cursor_y = core_box[1]
    column_width = 0.0
    column_limit = max(4000.0, core_box[3] - core_box[1])
    for local in detached_templates:
        box = bounds(local, sizes)
        width = box[2] - box[0]
        height = box[3] - box[1]
        if cursor_y > core_box[1] and cursor_y + height > core_box[1] + column_limit:
            cursor_x += column_width + args.detached_gap
            cursor_y = core_box[1]
            column_width = 0.0
        for node, point in local.items():
            result[node] = (cursor_x + point[0] - box[0], cursor_y + point[1] - box[1])
        cursor_y += height + args.detached_gap
        column_width = max(column_width, width)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(f"{node}\t{result[node][0]:.9f}\t{result[node][1]:.9f}\n" for node in node_order),
        encoding="utf-8",
    )
    if args.promoted_dir is not None:
        promoted = set(core)
        for component, anchors in records:
            if len(anchors) > 2:
                promoted.update(component)
        args.promoted_dir.mkdir(parents=True, exist_ok=True)
        (args.promoted_dir / "nodes.tsv").write_text(
            "".join(
                f"{node}\t{sizes[node][0]:.9f}\t{sizes[node][1]:.9f}\n"
                for node in node_order
                if node in promoted
            ),
            encoding="utf-8",
        )
        (args.promoted_dir / "edges.tsv").write_text(
            "".join(
                f"promoted-edge:{index}\t{source}\t{target}\tpromoted\n"
                for index, (source, target) in enumerate(
                    (edge for edge in edges if edge[0] in promoted and edge[1] in promoted)
                )
            ),
            encoding="utf-8",
        )
        (args.promoted_dir / "positions.tsv").write_text(
            "".join(
                f"{node}\t{result[node][0]:.9f}\t{result[node][1]:.9f}\n"
                for node in node_order
                if node in promoted
            ),
            encoding="utf-8",
        )
        (args.promoted_dir / "native-nodes.tsv").write_text(
            "".join(
                f"{node}\t{sizes[node][0]:.9f}\t{sizes[node][1]:.9f}"
                f"\t{result[node][0]:.9f}\t{result[node][1]:.9f}\n"
                for node in node_order
                if node in promoted
            ),
            encoding="utf-8",
        )
        skeleton_weights: dict[tuple[str, str], int] = defaultdict(int)
        for source, target in edges:
            if source in promoted and target in promoted:
                skeleton_weights[tuple(sorted((source, target)))] += 1
        residual = set(node_order) - promoted
        residual_records: list[tuple[set[str], set[str]]] = []
        for component_raw in nx.connected_components(graph.subgraph(residual)):
            component = set(component_raw)
            anchors = {
                neighbor
                for node in component
                for neighbor in graph.neighbors(node)
                if neighbor in promoted
            }
            residual_records.append((component, anchors))
            if len(anchors) == 2:
                skeleton_weights[tuple(sorted(anchors))] += 1
        (args.promoted_dir / "skeleton-edges.tsv").write_text(
            "".join(
                f"skeleton-edge:{index}\t{source}\t{target}\tskeleton\t{weight}\n"
                for index, ((source, target), weight) in enumerate(
                    sorted(skeleton_weights.items())
                )
            ),
            encoding="utf-8",
        )
        (args.promoted_dir / "skeleton-edges-ogdf.tsv").write_text(
            "".join(
                f"skeleton-edge:{index}\t{source}\t{target}\tskeleton"
                f"\tstructural\t{weight}\n"
                for index, ((source, target), weight) in enumerate(
                    sorted(skeleton_weights.items())
                )
            ),
            encoding="utf-8",
        )
        (args.promoted_dir / "native-skeleton-edges.tsv").write_text(
            "".join(
                f"skeleton-edge:{index}\t{source}\t{target}\tskeleton\tstructural\n"
                for index, (source, target) in enumerate(sorted(skeleton_weights))
            ),
            encoding="utf-8",
        )
        (args.promoted_dir / "residual-components.tsv").write_text(
            "".join(
                f"{component_index}\t{node}\t{','.join(sorted(anchors))}\n"
                for component_index, (component, anchors) in enumerate(residual_records)
                for node in sorted(component)
            ),
            encoding="utf-8",
        )
        template_rows: list[str] = []
        template_group_rows: list[str] = []
        anchor_order_rows: list[str] = []
        nonplanar_anchor_templates = 0
        fully_cofacial_templates = 0
        template_index = 0
        for component, anchors in records:
            if len(anchors) <= 2:
                continue
            template_graph = nx.Graph()
            template_nodes = component | anchors
            template_graph.add_nodes_from(template_nodes)
            template_graph.add_edges_from(
                (source, target)
                for source, target in edges
                if source in template_nodes
                and target in template_nodes
                and (source in component or target in component)
            )
            planar, embedding = nx.check_planarity(template_graph, counterexample=False)
            if not planar:
                nonplanar_anchor_templates += 1
            else:
                marked_half_edges: set[tuple[str, str]] = set()
                best_face_anchors: list[str] = []
                for source, target in embedding.edges():
                    if (source, target) in marked_half_edges:
                        continue
                    face = embedding.traverse_face(
                        source,
                        target,
                        mark_half_edges=marked_half_edges,
                    )
                    ordered_anchors: list[str] = []
                    for node in face:
                        if node in anchors and node not in ordered_anchors:
                            ordered_anchors.append(node)
                    if len(ordered_anchors) > len(best_face_anchors):
                        best_face_anchors = ordered_anchors
                if len(best_face_anchors) == len(anchors):
                    fully_cofacial_templates += 1
                anchor_order_rows.append(
                    f"{template_index}\t{len(anchors)}\t{len(best_face_anchors)}\t"
                    f"{','.join(best_face_anchors)}\n"
                )
            template_anchor = min(component)
            template_positions = scaled_without_card_overlap(
                local_planar_positions(template_graph, template_anchor),
                sizes,
                args.card_gap,
            )
            for node in sorted(template_nodes):
                point = template_positions[node]
                template_rows.append(
                    f"{template_index}\t{node}\t{point[0]:.9f}\t{point[1]:.9f}\t"
                    f"{1 if node in anchors else 0}\n"
                )
                template_group_rows.append(f"{template_index}\t{node}\n")
            template_index += 1
        (args.promoted_dir / "multi-anchor-templates.tsv").write_text(
            "".join(template_rows),
            encoding="utf-8",
        )
        (args.promoted_dir / "multi-anchor-groups.tsv").write_text(
            "".join(template_group_rows),
            encoding="utf-8",
        )
        (args.promoted_dir / "multi-anchor-orders.tsv").write_text(
            "".join(anchor_order_rows),
            encoding="utf-8",
        )
        residual_histogram = Counter(len(anchors) for _component, anchors in residual_records)
        print(
            f"promoted={len(promoted)} promotedEdges="
            f"{sum(1 for source, target in edges if source in promoted and target in promoted)} "
            f"skeletonEdges={len(skeleton_weights)} "
            f"skeletonWeight={sum(skeleton_weights.values())} "
            f"residualComponents={len(residual_records)} "
            f"residualAnchorHistogram={dict(sorted(residual_histogram.items()))} "
            f"anchorTemplates={template_index} "
            f"nonplanarAnchorTemplates={nonplanar_anchor_templates} "
            f"fullyCofacialTemplates={fully_cofacial_templates}",
        )
    peak_raw = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    peak_mib = peak_raw / (1024.0 * 1024.0) if sys.platform == "darwin" else peak_raw / 1024.0
    print(
        f"components={len(records)} anchored={len(anchored_templates)} "
        f"detached={len(detached_templates)} nonplanar={nonplanar} "
        f"anchorHistogram={dict(sorted(anchor_histogram.items()))} "
        f"multiAnchorMembers={promoted_member_count} "
        f"peakMiB={peak_mib:.1f} out={args.out}"
    )


if __name__ == "__main__":
    main()
