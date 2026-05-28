# v35 Action Scoring Schema

v35 is no longer a coordinate-imitation checkpoint. It is a move-judgment
model path: generate a bounded set of explicit actions, score each action,
then exact-verify only the top-ranked candidates.

## Action Contract

Each v34 counterfactual JSONL row now contains:

- `graph`: graph-level sizing and metric mode.
- `base`: current layout metrics before the candidate.
- `candidate`: scalar summary features for cheap baselines.
- `action`: explicit action identity and moved nodes.
- `result`: exact-measured metrics after the candidate.
- `delta*`: exact result minus base.

The `action` object is the important part for graph-aware learning:

- `type`: stable action type.
- `kind`: original v34 candidate kind.
- `groupKey` / `otherGroupKey`: structural group identity for group moves
  and paired group swaps.
- `sourceGroupKey`: original structural group for semantic re-anchoring.
- `groupIsLouvain` / `otherGroupIsLouvain`: whether the group identity is
  a Louvain community (`_louv...`), including compact component keys that
  contain Louvain groups.
- `semanticScore`, `semanticTokenJaccard`, `semanticSharedTokens`,
  `semanticAppMatch`: name-token evidence for semantic orphan placement.
- `indices` / `modelIds`: moved nodes.
- `otherIndices` / `otherModelIds`: paired nodes for swaps.
- `mode`: `translate` or `set_positions`.
- `dx`, `dy`: shared translation for translate actions.
- `targetDeltas`: per-moved-node target delta for set-position actions.
- `targets`: absolute target centers for reconstruction/debugging.

## Current Action Types

- `node_translate`
- `node_anchor_to_neighbors`
- `node_set_position`
- `node_position_swap`
- `edge_endpoint_translate`
- `edge_translate`
- `crossing_edge_translate`
- `crossing_pair_edge_spread`
- `group_translate`
- `group_anchor_to_neighbors`
- `group_centroid_swap`
- `group_set_positions`
- `component_anchor_to_neighbors`
- `louvain_cluster_translate`
- `louvain_cluster_anchor_to_neighbors`
- `louvain_cluster_centroid_swap`
- `louvain_cluster_set_positions`
- `louvain_component_anchor_to_neighbors`
- `semantic_orphan_to_louvain_cluster`
- `semantic_orphan_to_pseudo_group`
- `semantic_orphan_to_group`
- `overlap_single_push`
- `overlap_component_line`
- `overlap_component_ring`
- `overlap_components_batch_line`
- `edge_node_blocker_precise_clear`
- `edge_node_neighborhood_precise_clear`
- `edge_node_corridor_clear`
- `edge_node_force_relax`

Carrier/badge/alias-style shortcuts are diagnostics only for the current
research direction. The desired path is pure relation-preserving untangling:
the model ranks actions that move real nodes/groups while original direct
relations remain visible.

## Next Scorer Shape

The next useful model should not use only aggregate scalars. It should encode:

- current node features and positions,
- moved-node mask,
- optional other-node mask,
- per-node action delta,
- local incident-edge/crossing context,
- action type embedding.

The current NPZ builder includes pre-move action context scalars such as:

- incident edge counts for the moved nodes,
- current crossing pairs involving those incident edges,
- current overlap pairs touching the moved nodes,
- incident edge crossing density,
- candidate-induced incident-edge length/stretch deltas.

The score target is not final coordinates. It is expected gain:

`gain = base.score - result.score`

At runtime, the model ranks actions. The exact scorer still verifies the
top small subset, so the model can be imperfect without corrupting layout.

## Training Target

The scorer is trained for top-k verification, not exact-best imitation.
For each layout state, candidates with positive gain near the best observed
gain, or among the top positive candidates, are labeled acceptable verifier
candidates. The rank head is optimized to put at least one acceptable
candidate into the verifier shortlist. Exact scoring then chooses the final
move from that shortlist.

The current prototype verifier threshold is `topK=500` for crossing-heavy
states. Smaller shortlists such as `topK=50` work for semantic/overlap repair
but can miss crossing improvements whose rank falls outside the first 50.

Scale checks showed that smaller graphs can still need broad shortlists when
the best action family changes. In 700-node subsets, `node_anchor_to_neighbors`
often carried the best crossing gain but was globally under-ranked, so future
runtime filters should support action-family quotas or family-specific heads.

The 1250-node search added two crossing-pair escape actions after the scorer
hit a local minimum:

- `crossing_edge_translate`: move one edge along the normal of the other edge
  in a currently crossing pair.
- `crossing_pair_edge_spread`: move both crossing edges apart as one
  set-position action.

These are still generic verifier actions, not hand-coded special cases. The
mixed 1250/700 checkpoint has been retrained on them, but exact verification
still remains the quality gate.

The retrained checkpoint uses action-family balanced candidate truncation
(`final-per-action-candidates`) before scoring. This keeps crossing-pair
actions available without eliminating ordinary node/edge/group actions from
the dataset. The trainer also supports repeated `--dataset` arguments and
unions feature names across 1250-node and 700-node datasets, allowing the
scorer to learn scale-dependent action rankings without fixed node-count
coordinates.

Rigid measurement now writes graph TSVs from the active layout before calling
the OGDF binary, so subset layouts such as hot700/random700 can be measured
without accidentally using the full Captain graph.

## v37 Action-Family Prior

The v37 step adds a state-level prior before the concrete action scorer:

- `train_v37_action_family_prior.py` trains from the same exact-verifier NPZ
  data, but aggregates each state into best observed gain per action family.
- Runtime can pass `--family-prior-ckpt` and `--family-prior-top-actions`.
  The prior chooses which action families deserve exact verification before
  the v36 scorer ranks individual moves.
- Features are translation-invariant/log-scaled state features: graph size,
  current crossing/overlap/edge-node counts, bbox, span/std, and aggregate
  node static features. Absolute coordinates are intentionally excluded.

This is still not an end-to-end layout generator, but it shifts responsibility
from brute-force candidate verification toward learned method selection:
first choose likely useful action families, then score concrete moves inside
those families, then exact-verify.
