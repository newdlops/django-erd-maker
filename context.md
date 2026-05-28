# Django ERD Maker — ML Layout Research Context

**Updated:** 2026-05-16 (after v35 action-scoring and Louvain-action schema work).
**Repo root:** `/Users/lky/project/django-erd-maker`
**Branch:** `multi-view`
**Test ERD:** Captain (a Django project at `/Users/lky/project/captain`)

## 2026-05-22 Update: Generic BBox-Compression Acceptance Metrics

The user rejected absolute intermediate gates such as fixed visual-crossing
or node-spacing counts because those values overfit the current Captain graph.
The bbox-target acceptance gate in
`src/extension/services/layout/runOgdfLayout.ts` now defaults to normalized
tradeoff metrics instead:

- `bboxGain`: relative node-bbox area reduction from the current stage input.
- `visualDebt`: positive visual-crossing regression divided by the previous
  visual-crossing count.
- `edgeNodeDebt`: positive edge-node regression divided by routed edge count.
- `qualityDebtPerGain`: `(visualDebt + edgeNodeWeight * edgeNodeDebt) /
  bboxGain`.
- `spacingDebtPerGain`: positive node-spacing regression divided by node count
  and then by `bboxGain`.

Default absolute caps for `DJERD_OPTIMIZED_BBOX_TARGET_MAX_VISUAL` and
`DJERD_OPTIMIZED_BBOX_TARGET_MAX_NODE_SPACING` are now `auto`; they only apply
when explicitly set. Hard safety remains: node overlaps must be zero and
bundle-node overlaps must stay within the configured cap.

Latest log implication before this patch:

- The 5.01B candidate had strong bbox reduction but was rejected by fixed
  `visual<=900` and `nodeSpacing<=128` gates.
- With generic metrics it is evaluated by the cost of added visual/spacing
  debt per bbox gain, so the decision should transfer better to projects with
  different node and edge counts.

Follow-up edge-node polish:

- Latest `log.txt` accepted staged compression to about `3.47B`:
  `visualCrossings=887`, `edgeNodeIntersections=365`,
  `bundleNodeOverlaps=0`, `nodeOverlaps=0`.
- Added an `edge-node polish` reroute after staged bbox acceptance in
  `runOgdfLayout.ts`. It starts from the accepted layout, runs stronger
  node-edge relief with endpoint shifts, then accepts only by normalized
  improvement:
  edge-node improvement ratio, visual debt per edge-node gain, spacing debt
  per edge-node gain, and relative bbox growth. Hard node overlap remains 0
  and bundle-node overlap cap defaults to 0.
- Native leaf-bundle clear now retries node-overlap repair after reroute and
  allows edge-node tradeoff when overall visual crossings improve. This is
  needed because endpoint relief can reduce edge-node crossings but leave a
  small residual rendered bundle-box clash.
- Edge polish defaults also enable the existing
  `leaf-bundle-node-clear-after-relocate-final` micro-clear so residual
  bundle-node contacts can be removed without relation-breaking carriers or
  polyline detours.
- Probe on the latest preserved Captain input produced a viable candidate
  from the accepted 3.5B stage shape: roughly `edgeNode 365 -> 330`,
  `visual 887 -> 857`, `bundleNode=0`, `nodeOverlaps=0`, `bbox≈3.65B`.
  This should be accepted by the new normalized polish gate on the next app
  run if the same local optimum is reached.

## 2026-05-20 Update: Pure Relation-Preserving Untangle

The user rejected carrier/badge/alias approaches because they obscure or break
the visible relationship. Do not continue with those as solutions unless the
user explicitly reopens that direction. Current direction:

- Keep real nodes and real direct relation edges visible.
- Do not replace relationships with badges, carriers, aliases, or collapsed
  proxy nodes.
- Polyline detour remains last-resort only.
- Treat edge-node collision as a crossing class.
- Bundle render boxes may move, but relations must remain directly connected.

Current best pure/direct visual artifact:

- HTML: `/tmp/v35-space-cross-r35-force-relax-ports.html`
- JSON: `/tmp/v35-space-cross-r35-force-relax-ports.json`
- PNG: `/tmp/v35-space-cross-r35-force-relax-ports.png`
- Position TSV: `/tmp/v35-space-cross-r35-force-relax.tsv`
- HTML metrics: `edgeCross=767`, `edgeRect=127`, `overlaps=1`,
  `visualCross=895`.

Scripts updated for the pure path:

- `scripts/erd-poc/v35_exact_relation_search.py`
  - Added group-swap candidate integration.
  - Added precise edge-rect blocker clearing.
  - Added edge corridor clearing.
  - Added blocker-neighborhood clearing.
  - Added edge-rect force-relax candidate.
- `scripts/erd-poc/v35_port_assignment_view.py`
  - Initial rendered ports are now inserted as exact candidates before
    nearest-option matching, so warm-start ports are preserved.
- `scripts/erd-poc/v34_move_search.py`
  - Added general action candidates for answer-producing models:
    `edge_node_blocker_precise_clear`,
    `edge_node_neighborhood_precise_clear`,
    `edge_node_corridor_clear`, and `edge_node_force_relax`.
- `scripts/erd-poc/build_v35_action_dataset.py` and
  `scripts/erd-poc/eval_v35_scorer_filter.py`
  - Added CLI support for the new pure edge-node actions so they can become
    training examples and runtime scorer candidates.
- `scripts/erd-poc/run_v36_pure_action_scorer.sh`
  - New training entrypoint for a pure relation-preserving action scorer.
    It disables carrier-pair training by default and trains a separate
    checkpoint at `data/erd-poc/checkpoints/v36-pure-action-scorer.pt`.

v36 completed:

- Checkpoint: `data/erd-poc/checkpoints/v36-pure-action-scorer.pt`
- Training data:
  - `data/erd-poc/v36-pure-action-scorer/full1250.npz`
  - `data/erd-poc/v36-pure-action-scorer/hot700.npz`
  - `data/erd-poc/v36-pure-action-scorer/random700.npz`
- Train/val log: `/tmp/v36-pure-action-scorer.log`
- Runtime default in `src/extension/services/layout/runOgdfLayout.ts` now points
  at the v36 checkpoint, keeps carrier candidates disabled, and enables the
  pure edge-node actions used during training.
- Captain r35 eval artifacts:
  - Positions: `/tmp/v36-pure-r35-eval.tsv`
  - Zoomable HTML: `/tmp/v36-pure-r35-eval-ports.html`
  - JSON: `/tmp/v36-pure-r35-eval-ports.json`
  - Port-view metrics: `edgeCross=762`, `edgeRect=116`, `overlaps=1`,
    `visualCross=879`.
- Subset evals:
  - hot700: `visualCross 1699 -> 1595`, `edgeNode 276 -> 182`,
    `overlaps 6 -> 4`.
  - random700: `visualCross 297 -> 244`, `edgeNode 88 -> 49`,
    `overlaps 4 -> 2`.
- Runtime performance note from `log.txt`:
  - Previous extension defaults ran `rounds=8`, `final-max-candidates=5000`,
    and selected roughly 1000 candidates per round.
  - In `log.txt`, Python v36 scorer took `710005ms`; cluster baseline took
    about 4.5s and rigid reroute about 3.1s, so Python shortlist verification
    was the bottleneck.
  - Fast tested profile (`rounds=1`, `final-max-candidates=800`) took 10.5s
    for Python and about 13s including reroute on the same preserved Captain
    input.
  - Actual rendered-carrier reroute quality moved from the long profile's
    `visualCrossings=840` to the fast profile's `visualCrossings=885`, still
    below the 1000 target. Runtime defaults now use this fast profile.
- 2026-05-21 v37 action-family prior:
  - Added `scripts/erd-poc/train_v37_action_family_prior.py`.
  - Checkpoint: `data/erd-poc/checkpoints/v37-action-family-prior.pt`.
  - Purpose: predict which action families are worth checking before v36
    scores concrete candidate moves. This moves the system one step closer to
    "knowing the method" instead of blindly verifying every generated family.
  - Runtime now passes the prior into `eval_v35_scorer_filter.py` with
    `DJERD_V37_FAMILY_PRIOR_TOP_ACTIONS=8` by default.
  - On the preserved Captain runtime input, prior + `final-max-candidates=400`
    took `6.9s` in Python and kept the same rendered reroute quality as the
    previous ultrafast profile: `visualCrossings=885`.

Rejected artifacts from carrier/badge experiments should be treated as
diagnostics only, not as accepted solutions.

This document captures the multi-week research effort to replace the C++
heuristic ERD layout pipeline with a pure-ML solution. Use it to onboard
quickly without re-reading the full session transcript.

---

## 1. Goal

Make Captain ERD (~1,250 nodes, ~1,500 edges) visually readable. The
user's explicit visual threshold is:

- **edgeCrossings < 1000** (current best at runtime: 3,612)
- **bbox 3–4 B** (in target, currently ~3.9 B)
- **nodeOverlaps = 0** (current: 104, was 295 before overlap loss)
- **ML solves it alone** — no C++ post-processing (§13 cluster-swap
  heuristic removed since v29)

Live extension pipeline budget: ≤ 30 s per ML layout (currently 22 s).

---

## 2. Stack & Critical Paths

### Repo layout
- `analyzer/` — Rust analyzer extracting Django models → structural graph
- `bin/ogdf/darwin-arm64/django-erd-ogdf-layout` — built C++ binary
  (OGDF + custom cluster_graph + post-passes §13–§15)
- `native/ogdf-layout/src/` — C++ source (see `clusterGraph.cpp`,
  `main.cpp`, `io.cpp`, `types.h`)
- `src/extension/services/layout/runOgdfLayout.ts` — TS that drives the
  ML pipeline (spawns Python, then C++ binary)
- `scripts/erd-poc/` — Python research code (training, sampling)
- `data/erd-poc/`
  - `graphs/real-main/{graph.json,nodes.tsv,edges.tsv}` — Captain graph
  - `layouts/real-main.json` — Captain pre-§13 baseline layout
  - `expert-strong/real-main.json` — manual best Captain layout
    (cross=1032) + 551 synthetic layouts
  - `captain-corpus*/` — self-improvement corpora (see iterations)
  - `checkpoints/v{N}*.pt` — trained checkpoints

### Python env
- `/Users/lky/project/django-erd-maker/.venv-ml/bin/python` (Python 3.11)
  with torch, torch_geometric, onnx, onnxruntime
- **MPS is non-deterministic on save/load** — *always train on CPU*
  (per memory rule)
- ML inference runs on CPU at runtime via extension

### Runtime captain graph has grown
- Training data: **1,250 nodes, 1,484 edges** (snapshot in `data/`)
- Current runtime: **1,287 nodes, 1,545 edges** (live analyzer output)
- This distribution shift is the main source of offline→runtime cross
  degradation.

---

## 3. ML Architecture Evolution

### Phase A: Imitation Learning (v10–v12, FAILED to break ceiling)
- `train_distill.py` — GATv2Conv encoder + residual position head
- Train to imitate `expert-strong/` layouts (MSE on positions)
- Best: v12-general-best (val MSE 0.36)
- Runtime ceiling: cross ≈ 2,900 (matches C++ §13 baseline)

### Phase B: RL on Cluster Moves (v18–v22, ALL FAILED at ~21k rigid)
Tried four formulations, all plateaued at cross ≈ 21,000–25,000 (rigid):
- **v18** REINFORCE iterative cluster moves
- **v19** PPO + GAE actor-critic (continuous cluster delta)
- **v20** REINFORCE discrete-action (cluster × dir × stride)
- **v21** v20 with fast in-Python `FastCrossEval` (50× faster eval)
- **v22** AlphaZero-style MCTS + policy/value network (worse than RL)

Root cause: 17,280-action discrete space + sparse reward → policy
gradient can't out-search §13's 1,900-swap greedy heuristic.

### Phase C: Diffusion (v23–v32, ACTIVE & WORKING)

DDPM-style diffusion over node positions, conditioned on graph structure.

- `train_diffusion.py` — GraphDenoiser (GAT + FiLM time conditioning)
- `sample_diffusion.py` — DDPM sampling + cross/bbox guidance
- `fast_cross_eval.py` — vectorized numpy crossing counter (60 ms)
- `generate_captain_corpus.py` — sample → §13 → save (corpus building)

#### Iteration history (offline measurements on 1,250-node Captain)

| Ckpt | Notes | Rigid cross | Full §13 cross | Rigid bbox |
|------|-------|-------------|----------------|------------|
| v23 | 5 min, hidden 128 | 17,118 | 3,473 | — |
| v24 | hidden 256, 200 ep, +guidance | 11,524 | 3,557 | — |
| v25 | + real-main x30 upweight | 7,265 | 2,779 | 12.0 B |
| v26 | corpus=post-§13 (51 layouts) | 8,291 | 2,742 | 14.8 B |
| v27 | corpus filtered <2500, x100 upweight | 8,462 | **1,916** | 7.4 B |
| v28 | self-improve loop (corpus from v27) | — | **1,788** | — |
| **v29** | **+ cross-loss training term** | **2,574** | **1,167** | **3.6 B** |
| v30 | + 5% node-drop augmentation | 2,789 | 1,193 | 3.2 B |
| v31 | + overlap-loss (w=0.1, margin=30) | 2,816 | 1,238 | 3.5 B |
| **v32** | **w_overlap=0.5 margin=50, corpus<1300** | **2,686** | **1,178** | **3.4 B** |

#### Runtime (1,287-node Captain) measurements

| Version | Runtime cross | nodeOverlaps | bbox | ML time |
|---------|---------------|--------------|------|---------|
| v12 + §13 (v1062) | 2,954 | 0 | 2.95 B | 854 s |
| v29 (no §13, v1066) | 3,794 | (likely high) | 4.17 B | 27 s |
| v30 (+nodedrop, v1067) | 3,645 | 295 | 3.43 B | 22 s |
| v31 (+overlap, v1068) | 3,612 | **104** | 3.92 B | 22 s |
| **Target** | **< 1,000** | **0** | **3–4 B** | ≤ 30 s |

---

## 4. Current Pipeline (v0.0.1068, deployed)

`src/extension/services/layout/runOgdfLayout.ts`:

1. **Fast baseline** — C++ binary, `DJERD_SKIP_CG_OPT=1` env
   skips §13/§14/§15 (4 s vs 124 s).
2. **Diffusion sampling** — Python `sample_diffusion.py` with v31 ckpt,
   T=200, guidance c=0.5 b=500, 4 samples → best by FastCrossEval (~20 s).
3. **Rigid reroute** — C++ binary `--rigid-positions 1` (no §13)
   computes routes + crossings on ML positions (~2.5 s).

§13 is **completely removed** from the user-facing pipeline.

---

## 5. Key Scripts

| Script | Role |
|--------|------|
| `train_diffusion.py` | Train GraphDenoiser DDPM (eps + cross + overlap loss; node-drop aug) |
| `sample_diffusion.py` | DDPM sampling with cross/bbox guidance |
| `generate_captain_corpus.py` | Sample N layouts → §13 → save (corpus builder) |
| `fast_cross_eval.py` | Vectorized numpy crossing counter (60 ms on Captain) |
| `train_distill.py` | (legacy) Imitation learning |
| `train_reinforce_cross.py`, `train_ppo_cluster.py`, `train_mcts_cluster.py` | (failed) RL attempts |
| `apply_distill_real.py` | (legacy) Apply distill model |
| `export_onnx.py` | ONNX export PoC (verified working with FNV hash) |
| `test-onnx-inference.mjs` | Node.js + `onnxruntime-node` verification |
| `cluster-pair-target.py` | (used during corpus building) CPT post-pass |

---

## 6. C++ binary changes
- `clusterGraph.cpp` — `DJERD_SKIP_CG_OPT=1` env skips §13/§14/§15
  (set from `runOgdfLayout.ts` for fast baseline).
- `main.cpp` — Reads `DJERD_SKIP_CG_OPT` and sets `setenv` when
  `--rigid-positions 1 + --positions-tsv` are passed together (already
  built into binary at `bin/ogdf/darwin-arm64/django-erd-ogdf-layout`).
- `types.h` / `io.cpp` — Added `--rigid-positions` flag.

---

## 7. Important hyper-params (current v32)

```bash
# v32 (training as of this writing, PID 59474)
python scripts/erd-poc/train_diffusion.py \
  --layouts data/erd-poc/captain-corpus-v32 \      # 28 layouts, cross<1300
  --ckpt data/erd-poc/checkpoints/v32-stronger.pt \
  --T 200 --epochs 400 \
  --hidden 256 --layers 6 --lr 2e-4 \
  --upweight real-main --upweight-factor 150 \
  --w-cross 0.5 --cross-warmup 50 \
  --w-overlap 0.5 --overlap-margin 50 \      # 5× v31, 1.7× margin
  --node-drop-prob 0.05
```

Sample with the v32 ckpt (once done):
```bash
python scripts/erd-poc/sample_diffusion.py \
  --ckpt data/erd-poc/checkpoints/v32-stronger.pt \
  --layout data/erd-poc/layouts/real-main.json \
  --expert data/erd-poc/expert-strong/real-main.json \
  --out-tsv /tmp/v32-best.tsv \
  --T 200 --hidden 256 --layers 6 \
  --n-samples 6 --guidance 0.5 --bbox-guidance 500 \
  --guidance-start-frac 0.5 --measure-rigid
```

---

## 8. Self-improvement loop (proven path)

The Phase C breakthrough came from self-distillation:

1. Train v_N diffusion on a corpus.
2. Generate 50 layouts with v_N + post-§13 → new corpus entries.
3. Filter combined corpus by stricter cross threshold each iteration.
4. Train v_{N+1} on filtered corpus.

Corpus quality history:
- v26 corpus: 51 layouts, mean cross 2,972 (initial)
- v27 corpus: filtered <2500, 13 layouts
- v28 corpus: + v27 outputs filtered <2000, 22 layouts (mean 1,742)
- v31 corpus: + v30 outputs filtered <1700, 56 layouts (mean 1,278)
- **v32 corpus: filtered <1300, 28 layouts (mean 1,242)**

Each iteration drops the corpus mean ~15-30 % and the best sample ~5-10 %.

### Phase D: v34 exact-verified generic move search (current)

v34 is a runtime-oriented prototype that starts from diffusion coordinates,
generates generic candidate moves, and accepts only exact-measured
crossing/overlap/bbox improvements. It is intentionally not a §13 policy
clone; §13 is only used as an oracle/diagnostic because it is a special
solution and has overfit risk.

Current best path on offline Captain:

| Input / pass | Rigid edgeCrossings | nodeOverlaps | bbox |
|--------------|---------------------|--------------|------|
| v32 b=200 raw | 2,686 | 82 | 3.42 B |
| v34 broad edge-aware | 2,480 | 81 | 3.44 B |
| v34 overlap repair | 2,487 | **8** | 3.44 B |
| v34 loose placement | 2,303 | 7 | 3.44 B |
| v34 loose2 | 2,084 | 7 | 3.44 B |
| **v34 loose3** | **1,994** | **7** | 3.44 B |
| §13 final positions, rigid remeasure | 1,476 | 52 | 3.30 B |
| §13 final positions + v34 overlap repair | 1,477 | 19 | 3.30 B |
| v34 group-anchor2 | 1,531 | 7 | 3.95 B |
| v34 carrier/bundle-orbit + C++ skip-CG postpasses | 1,088 | 0 | 4.41 B |
| v34 postpass fixed-point + carrier repair | 994 | 0 | 4.50 B |
| **v34 under-1000 scaled candidate** | **999** | **0** | **3.46 B** |

Observations:
- Overlap <= 10 is solved by generic v34 push-off on the v32-derived path.
- Crossing is now the bottleneck; positions alone can reach at least ~1,476
  in the §13 coordinate basin, but the full §13 JSON reports 1,178, so route
  and post-route effects explain part of the remaining gap.
- The latest direction is global group-anchor search: move hot small
  clusters / no-cluster pseudo groups near their external graph neighborhood,
  including large-radius candidates, with exact verification.
- Under-1000 candidate is saved at `/tmp/v34-under1000.json` with positions
  `/tmp/v34-under1000.tsv`: `edgeCrossings=999`, `nodeOverlaps=0`,
  `bundleNodeOverlaps=12`, `bbox=3.46B`. It is not yet a deployable
  ML-only path: it uses v34 exact search, C++ skip-CG postpasses with
  `DJERD_KNOT_2NDPASS=1`, and an affine scale sweep (`x=0.86`, `y=0.88`)
  after reaching the 994-crossing basin.
- The under-1000 artifact has been promoted into the repo under
  `data/erd-poc/v34-under1000/`. Fast replay:
  `.venv-ml/bin/python scripts/erd-poc/v35_replay_under1000.py --mode quick`
  reproduces `edgeCrossings=999`, `nodeOverlaps=0`, `bbox=3.463B`.
  The final JSON is also copied to
  `data/erd-poc/captain-corpus-v34/real-main-v34-under1000.json` for the
  next training/distillation pass.
- Important classification: `DJERD_SKIP_CG_OPT=1` is **not ML**. It skips
  §13/§14/§15 inside `cluster_graph`, but C++ expert postpasses still run
  afterward (leaf/knot/detour/face/hot-region/carrier logic). Treat the
  under-1000 `skip-CG` result as a teacher/diagnostic, not as a deployable
  pure-ML candidate. Strict candidates should be diffusion and/or generic
  v34 exact primitives measured by rigid/raw output without C++ expert
  postpasses.
- v35 diffusion distillation on `captain-corpus-v35` completed but failed
  to absorb the under-1000 artifact: rigid samples were ~3.7k-4.6k, and
  `skip-CG` postpass on v35 samples still measured 1,472-1,679. Do not
  deploy v35 as-is.
- v35 was reoriented toward a "judgment model" instead of coordinate
  imitation. Added:
  - `scripts/erd-poc/train_v35_move_scorer.py`
  - `scripts/erd-poc/run_v35_move_scorer_data.sh`
  - v34 JSONL logging now records current base metrics, graph size, and
    target-move features for each candidate.
  Generated 96k counterfactual candidates under
  `data/erd-poc/v35-move-scorer/` from three starts:
  under1000 overlap path, groupanchor2 path, and zero-overlap path. MLP
  scorer/ranker checkpoints were saved at
  `data/erd-poc/checkpoints/v35-move-scorer.pt`,
  `v35-move-ranker.pt`, and `v35-move-ranker-v2.pt`.
  Current result: **not deployable**. Held-out round top-1/top-5 hit stayed
  at 0. Aggregate candidate features are insufficient; the next version
  needs richer graph-aware per-candidate state (moved node masks, target
  deltas per node, local edge-pair sketches, and/or per-round layout state),
  not just scalar summaries.
- Follow-up action-taxonomy step: added
  `scripts/erd-poc/v35_action_schema.md`. v34 JSONL now includes an
  explicit `action` object with stable action `type`, moved indices/modelIds,
  optional paired nodes, and either shared translation or per-node
  target deltas. `train_v35_move_scorer.py` now prefers `action.type` over
  raw `candidate.kind` for scoring. Smoke test passed on
  `/tmp/v35-action-schema-smoke.jsonl`.
- Next graph-aware action scoring step added:
  - `scripts/erd-poc/build_v35_action_dataset.py`
  - `scripts/erd-poc/train_v35_graph_action_scorer.py`
  - `scripts/erd-poc/run_v35_graph_action_scorer.sh`
  This builds `data/erd-poc/v35-move-scorer/v35-action-dataset.npz`
  with 24 states, 96k candidate actions, and ragged moved-node/per-node-delta
  arrays. It trains `data/erd-poc/checkpoints/v35-graph-action-scorer.pt`.
  First result: better signal than scalar-only scorer (`corr` reached about
  0.55 on some held-out states; top20 occasionally 1/6 or 2/6 eligible
  states), but **still not deployable** because top1 stayed 0. The next
  needed feature is local crossing/edge-pair context per action, not just
  moved-node static features and deltas.

---

## 9. Known Issues / Open Questions

1. **Cross still ~3.6× over target** (3,612 vs 1,000). Self-improvement
   loop has been slowing — each iteration shaves only ~1 % from runtime
   cross. May need stronger cross guidance or different architecture.

2. **Distribution shift offline → runtime** (1,250 → 1,287 nodes).
   Offline cross ≈ 1,200; runtime ≈ 3,600 (3×). Node-drop augmentation
   partially helps but not fully.

3. **nodeOverlaps still > 0** at runtime (104 with v31). v32 raised
   overlap loss to 0.5 / margin 50, but offline ML-only overlap worsened
   (best sweep: 82 vs v31's 63), so do not deploy v32 as-is.

4. **ONNX deployment infra is ready** (`export_onnx.py` verified PyTorch
   ↔ Python ONNX ↔ Node.js `onnxruntime-node` bit-identical with FNV
   hash) but extension still spawns Python at runtime. Migration to
   `onnxruntime-node` would remove the `.venv-ml` 2 GB dependency for
   marketplace distribution.

5. **Captain `<no-cluster>` group has 453 nodes** (~36 %). These loose
   nodes are the hardest cases — they have no cluster membership for
   structural reasoning. They dominate inter-cluster crossings.

---

## 10. What's NOT Working (don't retry)

- **Soft cross loss alone (no expert imitation)** — diverges (v15 RL,
  also early diffusion guidance with high weight).
- **PPO / REINFORCE on continuous cluster deltas** — plateau at 21 k
  rigid (v19 ran 5 h, MCTS made it worse at 30 k).
- **MCTS** — cold-start prior is uniform → search degenerates (v22).
- **Sampling-time guidance without underlying model improvement** —
  diminishing returns past `bbox=500`.
- **Pure rigid reroute on v25/v26 output** — needed §13 to be usable
  (only fixed in v29 with cross-loss training term).

---

## 11. Memory rules to respect
- **No `Co-Authored-By` trailer** in git commits.
- **CPU only** for ML training (MPS non-deterministic on save/load).
- **Log analysis** — render.frame / drag / zoom counts are arbitrary,
  not quality signals.

---

## 12. Useful commands

### Background process management
```bash
# Train in background, robust to wait-task termination
PY=/Users/lky/project/django-erd-maker/.venv-ml/bin/python
nohup $PY -u scripts/erd-poc/train_diffusion.py ... > /tmp/train.log 2>&1 &
disown
```

### Full-pass measurement (with §13)
```bash
bin/ogdf/darwin-arm64/django-erd-ogdf-layout layout \
  --mode hierarchical_barycenter \
  --nodes-file data/erd-poc/graphs/real-main/nodes.tsv \
  --edges-file data/erd-poc/graphs/real-main/edges.tsv \
  --edge-routing straight --cluster-graph 1 \
  --positions-tsv /tmp/positions.tsv \
  > /tmp/final.json
```

### Rigid reroute (no §13)
Add `--rigid-positions 1` to the above.

### Build OGDF binary after C++ changes
```bash
node scripts/build-ogdf-binary.mjs
```

### Type check extension
```bash
npm run typecheck
```

---

## 13. v32 Evaluation Results

Offline Captain snapshot (1,250 nodes / 1,484 edges), `T=200`,
`n-samples=6`, `guidance=0.5`, `guidance-start-frac=0.5`:

| Checkpoint | bbox guidance | Rigid cross | nodeOverlaps | bbox |
|------------|---------------|-------------|--------------|------|
| v31 | 200 | 2,816 | 63 | 4.05 B |
| v31 | 500 | 2,969 | 68 | 3.55 B |
| v31 | 1000 | 3,372 | 77 | 2.97 B |
| v32 | 200 | 2,686 | 82 | 3.42 B |
| v32 | 500 | 2,819 | 104 | 3.81 B |
| v32 | 1000 | 3,128 | 129 | 3.17 B |

Best v32 ML-only result is `b=200`: crossings improved by 130 vs v31
`b=200`, but overlaps worsened by 19. v32 should **not** replace v31 in
the extension unless a separate runtime test shows a surprising win.

Full §13 pass on `/tmp/v32-b200-best.tsv`:
- `edgeCrossings=1178`
- `nodeOverlaps=0`
- `bbox=3.25 B`

This is good enough as another self-improvement corpus candidate, but it
does not satisfy the ML-only deployment requirement.

## 14. Pending tasks

1. Keep extension on v31 / v0.0.1068 for now.
2. Wait for v33 training to finish, then run the same b=200/500/1000
   offline evaluation sweep against v31/v32.
3. Consider stronger or better-correlated cross objective if runtime cross
   remains around 3,600.

---

## 15. v33 Started

v33 corpus:
- `data/erd-poc/captain-corpus-v33`
- 32 layouts selected from v32 + v32out
- filter: `edgeCrossings < 1300`, `nodeOverlaps == 0`, `bbox <= 4.0 B`
- metrics: min=1032, median=1235, max=1297, mean=1232.5

v33 training command:
```bash
/Users/lky/project/django-erd-maker/.venv-ml/bin/python -u scripts/erd-poc/train_diffusion.py \
  --layouts data/erd-poc/captain-corpus-v33 \
  --ckpt data/erd-poc/checkpoints/v33-subset-overlap.pt \
  --init-from data/erd-poc/checkpoints/v32-stronger.pt \
  --T 200 --epochs 300 \
  --hidden 256 --layers 6 --lr 1e-4 \
  --upweight real-main --upweight-factor 150 \
  --w-cross 0.5 --cross-warmup 0 \
  --w-overlap 0.25 --overlap-margin 60 \
  --overlap-mode subset --overlap-sample-nodes 512 \
  --node-drop-prob 0.05 --save-every 20 \
  > /tmp/v33-train.log 2>&1
```

Training session id: 57536. Completion notifier watcher PID: 6083.

v33 offline evaluation (Captain 1,250 nodes, `n-samples=6`,
`guidance=0.5`, `guidance-start-frac=0.5`):

| Checkpoint | bbox guidance | Rigid cross | nodeOverlaps | bbox |
|------------|---------------|-------------|--------------|------|
| v33 | 0 | 3,296 | 25 | 7.04 B |
| v33 | 200 | 3,338 | 39 | 5.95 B |
| v33 | 500 | 3,360 | 43 | 4.70 B |
| v33 | 1000 | 3,751 | 43 | 3.35 B |

Conclusion: v33's subset overlap loss worked for overlaps but destroyed
compactness/crossing quality. Do not deploy v33. Keep v31 in extension.

## 16. v34 Move-Search Prototype

Rationale: §13 is likely overfit as a policy teacher, so v34 starts with
generic move primitives and exact verification. The script accepts only
measured improvements and can emit counterfactual JSONL for a future ML
candidate scorer.

Script:
- `scripts/erd-poc/v34_move_search.py`

Implemented primitives:
- hot node nudges
- hot cluster translations
- hot node swaps
- hot cluster centroid swaps
- hot node repositioning near neighbor barycenter rings
- hot edge endpoint/edge-pair translations
- overlap push-off repair
- no-cluster pseudo-groups by neighboring cluster signature
- incremental exact crossing evaluation for moved-node candidates

Smoke results on offline Captain:

| Start TSV | Search | Straight cross | Straight overlaps | Rigid cross | Rigid overlaps | bbox |
|-----------|--------|----------------|-------------------|-------------|----------------|------|
| v31 b=200 | 6 rounds, 586 cand/round | 3152 → 3045 | 94 → 82 | 2816 → 2742 | 63 → 63 | 4.07 B |
| v32 b=200 | 4 rounds, 220 cand/round | 3004 → 2904 | 100 → 99 | 2686 → 2625 | 82 → 82 | 3.44 B |
| v32 b=200 | 12 rounds, 320 cand/round | 3004 → 2840 | 100 → 99 | 2686 → 2559 | 82 → 82 | 3.44 B |
| v32 b=200 | + neighbor anchors | 3004 → 2850 | 100 → 99 | 2686 → 2578 | 82 → 82 | 3.44 B |
| v32 b=200 | broad edge-aware search | 3004 → 2744 | 100 → 97 | 2686 → 2480 | 82 → 81 | 3.44 B |
| v32 b=200 | broad + overlap repair | 3004 → 2753 | 100 → 14 | 2686 → 2487 | 82 → 8 | 3.44 B |

Conclusion: exact-verified generic local moves produce real gains, but
the current primitive set plateaus around rigid cross ~2,480, far above
the 1,000 target. The hard overlap target is now reachable (8 overlaps)
without cross regression, but crossing reduction still needs a stronger
global primitive. Next v34 work should either:
- train a scorer from the JSONL to cheaply search many more candidates, or
- add stronger generic primitives (subgraph/cluster ordering, edge-carrier
  detour anchors, and no-overlap projection).

---

## 17. Trajectory summary plot (mental model)

```
Cross @ runtime (1287 nodes), full-pass §13 OR rigid (ML alone):
  v1062 (§13 only)     2,954  ████████████████████ 854 s
  v1066 (v29 no-§13)   3,794  █████████████████████████   27 s
  v1067 (v30 +ndrop)   3,645  ████████████████████████   22 s
  v1068 (v31 +overlap) 3,612  ████████████████████████   22 s  +overlap 104
  target               1,000  ███████                     —     overlap 0
```

---

When picking up this work:
1. Do not deploy v32 over v31 based on current offline results.
2. Continue corpus → train → measure loop until target hit.
3. For v33, preserve ML-only overlap as a gating metric, not just cross.

## 18. v35 Action/Judgment Model Direction

The diffusion-only v35-under1000 attempt did **not** reproduce the
under1000 layout and is not deployable. The current direction is a
generalizing action scorer:

- generate explicit candidate actions from the current graph/layout state,
- score actions with ML,
- exact-verify only the top-ranked subset,
- never train the model to memorize final Captain coordinates.

Current artifacts:
- `scripts/erd-poc/v35_action_schema.md`
- `scripts/erd-poc/build_v35_action_dataset.py`
- `scripts/erd-poc/train_v35_graph_action_scorer.py`
- `scripts/erd-poc/train_v35_move_scorer.py`

The first scalar and graph-action scorers are **not deployable yet**. They
learn some correlation but fail to pick the best move reliably. Next
modeling work needs stronger local crossing/incident-edge context.

As of 2026-05-16, v34 candidate records carry structural group metadata:
`groupKey`, `otherGroupKey`, `groupIsLouvain`, pseudo/component flags, and
Louvain-specific action types such as `louvain_cluster_translate`,
`louvain_cluster_anchor_to_neighbors`, and
`louvain_cluster_centroid_swap`. Regenerate the NPZ/JSONL before retraining
so the model can distinguish Louvain community movement from generic group
or pseudo-group movement.

Also added a semantic orphan-placement primitive:
`semantic_anchor_candidates()`. It uses app/name-token overlap rather than
memorized model ids to propose `semantic_orphan_to_louvain_cluster` actions
for degree-0 or otherwise weakly-linked nodes. This is not expected to lower
crossings by itself; it gives the judgment model a general semantic placement
action for nodes whose graph structure has no useful signal.

Regenerated and retrained after Louvain+semantic action additions:
- Dataset: `data/erd-poc/v35-move-scorer/v35-action-dataset.npz`
- Checkpoint: `data/erd-poc/checkpoints/v35-graph-action-scorer.pt`
- Dataset size: 20 states, 80,000 samples, 117,644 moved-node refs, 9 actions.
- Semantic action is present:
  `semantic_orphan_to_louvain_cluster` = 20,400 samples, 4,240 positive,
  10 best moves.
- Final training: corr ≈ 0.595, but top1/top5/top20 remained 0 on the
  held-out states. Still **not deployable** as a move selector.

Interpretation: semantic orphan placement is now represented and learnable,
but the scorer still lacks enough local crossing/edge-pair context to rank
the exact best move. Next useful step is to add per-action incident-edge and
crossing-pair features rather than only moved-node aggregate features.

Implemented the next feature step in `build_v35_action_dataset.py`: each
sample now includes pre-move action context scalars for incident edges,
current crossing pairs touching those incident edges, current overlaps
touching moved nodes, and candidate-induced incident-edge length/stretch
changes. These are runtime-computable before exact verification and should
give the scorer a more direct signal than moved-node aggregates alone.

Retrained after adding those context features:
- Dataset stayed at 20 states / 80,000 samples / 9 actions.
- Scalar columns increased to 52, including 18 `context*` features.
- Checkpoint overwritten: `data/erd-poc/checkpoints/v35-graph-action-scorer.pt`
- Best observed validation correlation improved to about 0.67
  (previous run about 0.59).
- Regret dropped to single/double digits on several epochs, but
  top1/top5/top20 still reported 0 on the held-out states.

Interpretation: incident-edge/crossing context is the right direction and
materially improves correlation, but the rank metric is still not selecting
the exact best candidate. The next likely fix is training objective/eval:
separate semantic-overlap moves from crossing moves and optimize a grouped
ranking/classification target for "acceptable top-k verifier candidates",
not just scalar gain regression.

Implemented the top-k verifier objective in
`train_v35_graph_action_scorer.py`:
- Builds state-local `acceptable` labels from top positive candidates and
  candidates within a small regret window of the exact best.
- Trains the rank head with BCE on acceptable candidates plus a grouped
  listwise loss over acceptable candidates instead of forcing one exact
  best candidate.
- Evaluation now reports `accept@20`, `accept@50`, `pos@50`,
  `regret@20`, and `regret@50`.

Retrained on the existing context-feature dataset:
- Dataset: 80,000 samples, 20 states, 99 input features.
- Train positives: 3,559; train acceptable labels: 1,511.
- Best observed verifier metrics reached `accept@20=1.0`,
  `accept@50=1.0`, `pos@50=1.0`, with `regret@50≈1.4`.
- Exact-best `best@20` stayed 0, but that is no longer the deploy target:
  the intended runtime behavior is ML top-k filtering followed by exact
  verification of that shortlist.

Interpretation: this is the first v35 scorer result that matches the
runtime use case. It is still not a final layout solver, but it is now a
plausible candidate-filter model for exact verification.

Added `scripts/erd-poc/eval_v35_scorer_filter.py` to attach the trained
scorer to v34 candidate generation:
- Builds the same live feature rows used by training.
- Scores all generated candidates with the rank head.
- Exact-verifies only ML top-k, or optionally also full-verifies all
  candidates for comparison.

Filter evaluation:
- `groupanchor2` start, `topK=50`, 4 rounds:
  captured full exact gain exactly (`5005 / 5005`), ending at
  cross=1659, overlaps=8.
- zero-overlap start, `topK=50`, 4 rounds:
  captured only `16 / 63` gain; crossing moved 1416 → 1400. The missed full
  best ranks were around 73, 72, 113, and 473.
- zero-overlap start, `topK=500`, 4 comparison rounds:
  captured full exact gain exactly (`38 / 38`), crossing 1416 → 1378.
- zero-overlap start, `topK=500`, no full comparison, 8 actual ML-filtered
  rounds:
  crossing 1416 → 1371, overlaps remained 0, matching the earlier full-exact
  trajectory for those rounds. Runtime was about 152 s for 8 rounds in this
  Python prototype.

Interpretation: `topK=50` is enough for overlap/semantic repair but too
small for crossing-only search. `topK=500` is the current practical filter
threshold: it preserves full-search quality on the tested zero-overlap path
while cutting exact verification from 4000 candidates per round to 500.

Added `scripts/erd-poc/build_v35_subset_layout.py` for node-count scale
checks. It can build deterministic smaller layouts from Captain by either
hot Louvain clusters or random node sampling.

700-node scale check:
- `captain-hot700` built from high-crossing Louvain clusters:
  700 nodes, 949 edges, initial cross=1423, overlaps=2, bbox=3.10B.
  This is intentionally hard despite fewer nodes.
- `topK=50`, 4 rounds: captured `4 / 18` full gain; insufficient.
- `topK=500`, 4 rounds: captured `15 / 21` full gain.
- `topK=1200`, 4 rounds: captured `18 / 19` full gain.
- `captain-random700-s0`: 700 nodes, 467 edges, initial cross=209,
  overlaps=0, bbox=3.79B.
- Random700 `topK=50`, 4 rounds: captured `1 / 14` full gain. Full best
  was mostly `node_anchor_to_neighbors`, ranked around global 3800.
- Adding action-family quota `per-action-k=100`: captured `11 / 30`.
- `per-action-k=400`: captured `22 / 22`, but selected about 2400-2600
  candidates, so it is closer to broad exact search than a tight filter.

Interpretation: smaller node count does **not** automatically mean smaller
ML shortlist. The current scorer under-ranks `node_anchor_to_neighbors` on
700-node subsets. The next fix is multi-scale training: include 700-node
subset action datasets and/or add action-family quota or family-specific
heads so node-anchor moves are not globally buried.

1250-node best pass before returning to 700:
- Broad ML-filter from the zero-overlap start with `final-max-candidates=12000`,
  `ml-top-k=2000`, and `per-action-k=700` improved straight crossings
  `1326 -> 1300`; rigid measured `edgeCrossings=1207`, `nodeOverlaps=0`,
  `bbox=3.60B`.
- Raising the family quota to `per-action-k=1500` continued to straight
  `1277`; rigid measured `edgeCrossings=1196`, `nodeOverlaps=0`,
  `bbox=3.60B`. A full 12000-candidate comparison from that state had no
  remaining gain, but it was still worse than the older broad-exact artifact.
- The older broad-exact artifact
  `/tmp/v34-under1000-zero-overlap-cross.tsv` was verified as straight
  `1273`, rigid `edgeCrossings=1172`, `nodeOverlaps=0`, `bbox=3.60B`, and
  current v35 candidates had no one-round full-exact improvement from it.

Added a generic crossing-pair escape action family in
`scripts/erd-poc/v34_move_search.py`, exposed through the v35 dataset/eval
entry points:
- `crossing_edge_translate`: translate one edge in a currently crossing pair
  along the other edge's normal.
- `crossing_pair_edge_spread`: move both crossing edges apart together as a
  four-node set-position action.

This is not a new special-case expert rule; it is an additional generic action
family that still requires exact verification. A wide 1250 exact pass from
the previous best found new improvements:
- Probe from `/tmp/v34-under1000-zero-overlap-cross.tsv` with
  `cross-pair-candidates=500` and `final-max-candidates=50000` found
  `1273 -> 1268`.
- Continuing from the probe for up to 40 rounds accepted 24 more moves and
  stopped at straight `cross=1231`, `overlaps=0`, `bbox=3.58B`.
- Rigid measurement for `/tmp/v35-crosspair-1250-wide.tsv`:
  `edgeCrossings=1141`, `nodeOverlaps=0`, `bbox=3.60B`.

Current 1250 best is therefore `/tmp/v35-crosspair-1250-wide.tsv`
with rigid JSON `/tmp/v35-crosspair-1250-wide.rigid.json`. The next useful
step is to regenerate/retrain the v35 action dataset with the crossing-pair
family included, then repeat the 700-node checks with the retrained scorer.

Regenerated v35 action data with crossing-pair actions:
- Added `final-per-action-candidates` balanced truncation so crossing-pair
  candidates do not crowd out node/edge/group actions.
- 1250 dataset:
  `data/erd-poc/v35-move-scorer/v35-action-dataset.npz`
  = 16 states, 192,000 samples, 11 action types.
- 700 datasets:
  `data/erd-poc/v35-move-scorer/v35-action-dataset-hot700.npz`
  = 4 states, 48,000 samples, 12 actions.
  `data/erd-poc/v35-move-scorer/v35-action-dataset-random700.npz`
  = 4 states, 48,000 samples, 13 actions.
- `train_v35_graph_action_scorer.py` now accepts repeated `--dataset`
  arguments and unions feature names/action one-hots across datasets.
- Mixed checkpoint:
  `data/erd-poc/checkpoints/v35-graph-action-scorer.pt`
  trained on 288,000 samples / 24 states / 103 features.

Post mixed-training verification:
- 1250 probe from `/tmp/v34-under1000-zero-overlap-cross.tsv`:
  full best `crossing_edge_translate` had scorer global rank 2; ML captured
  `1/1` gain with zero regret.
- hot700 with `ml-top-k=500`, `per-action-k=700`,
  `final-max-candidates=12000`, `final-per-action-candidates=2500`:
  captured `21/21` full exact gain over 4 rounds, ending straight
  `cross=1402`, `overlaps=2`.
- random700 with the same settings:
  captured `25/25` full exact gain over 4 rounds, ending straight
  `cross=184`, `overlaps=0`.

Fixed subset rigid measurement:
- `run_rigid_measure()` now writes temporary `nodes.tsv` / `edges.tsv` from
  the supplied layout instead of always using full `real-main` graph files.
- Full Captain regression check preserved the current best:
  `/tmp/v35-crosspair-1250-wide-rigidcheck.rigid.json` =
  `edgeCrossings=1141`, `nodeOverlaps=0`, `bbox=3.60B`.
- hot700 subset rigid measurement:
  `/tmp/v35-mixed-hot700-subset-rigid.rigid.json` =
  `edgeCrossings=1296`, `nodeOverlaps=0`, `bbox=3.12B`.
- random700 subset rigid measurement:
  `/tmp/v35-mixed-random700-subset-rigid.rigid.json` =
  `edgeCrossings=165`, `nodeOverlaps=0`, `bbox=3.81B`.

VSCode optimized-path wiring:
- `src/extension/services/layout/runOgdfLayout.ts` no longer calls the old
  v31 diffusion sampler for optimized layout. It now writes the current
  cluster-graph baseline JSON + center-position TSV, then runs
  `scripts/erd-poc/eval_v35_scorer_filter.py` with
  `data/erd-poc/checkpoints/v35-graph-action-scorer.pt`.
- The v35 path is ML-guided/exact-verified: scorer ranks candidates,
  shortlisted moves are exact-measured, accepted positions are rigid-rerouted
  by the OGDF binary.
- `DJERD_OPTIMIZED_POSITIONS_TSV=/tmp/v35-crosspair-1250-wide.tsv` can be
  used for immediate visual inspection of the current best artifact through
  the same C++ rigid reroute path.
- Smoke checks after wiring:
  - TypeScript typecheck and extension build pass after changing
    `tsconfig.json` `ignoreDeprecations` from invalid `"6.0"` to `"5.0"`.
  - Precomputed v35 TSV through `runOgdfLayout()`:
    `edgeCrossings=1141`, `nodeOverlaps=0`, `bundleNodeOverlaps=47`,
    `bbox=3.60B`, 1250 nodes / 1484 edges.
- Live v35 scorer smoke with intentionally tiny 1-round settings executed
  `eval_v35_scorer_filter.py` and kept the v35 reroute without falling back:
  `edgeCrossings=3461`, `nodeOverlaps=0`, `bbox=14.63B`.

After analyzing `log.txt`, live optimized on the current 1289-node Captain
graph did run v35, not old diffusion:
- v35 scorer time: 531970ms (~8m52s), accepted 8 moves.
- Python verifier ended straight `cross=3829`, `overlaps=1`, `bbox=14.30B`.
- Rigid reroute ended `edgeCrossings=3546`, `nodeOverlaps=1`,
  `edgeNodeIntersections=954`, `bundleNodeOverlaps=335`, `bbox=14.34B`.
- The first accepted move was a scalar-weight artifact:
  `cross=4122 -> 4822` while `overlaps=2 -> 1`; overlap penalty dominated
  crossing regression.

To move away from artifact/TSV overfitting and toward generic behavior
optimization, `eval_v35_scorer_filter.py` now has an exact-measured
admissibility gate before accepting ML-ranked moves:
- reject node-overlap regression (`--max-overlap-regression`, default 0)
- reject crossing regression (`--max-cross-regression`, default 0)
- reject large bbox growth (`--max-bbox-growth`, default 1.05)
- require exact score improvement after those behavior gates

The VSCode optimized path forwards these as:
`DJERD_V35_MAX_CROSS_REGRESSION`, `DJERD_V35_MAX_OVERLAP_REGRESSION`,
`DJERD_V35_MAX_BBOX_GROWTH`.
This is only the first guardrail; the next structural fix is to add cheap
edge-node and bundle-object interaction terms to the Python verifier/training
labels so the scorer learns behavior, not coordinate artifacts.

Next structural fix implemented:
- `v34_move_search.py` now measures `edge_node` as straight edge segment vs.
  visible node rectangle intersections, excluding the edge's own endpoints.
- `Metrics` / `score_metrics()` now carry an `edge_node` term. Default
  weight is still compatible for older callers, while v35 uses
  `--edge-node-weight` default 2.0.
- Candidate evaluation updates edge-node counts with candidate-local impacted
  pairs rather than scanning all edge-node pairs per candidate.
- `build_v35_action_dataset.py` now writes edge-node-aware labels/features:
  `baseEdgeNode`, `contextCurrentEdgeNodePairs`,
  `contextCurrentEdgeNodeFrac`, and `sample_delta_edge_node`.
- `eval_v35_scorer_filter.py` now has an admissibility gate for edge-node
  regression: `--max-edge-node-regression` default 0. With the current
  checkpoint it keeps old feature compatibility; edge-node context features
  are only computed when a future checkpoint asks for them.
- The VSCode optimized path forwards:
  `DJERD_V35_MAX_EDGE_NODE_REGRESSION`,
  `DJERD_V35_EDGE_NODE_MARGIN`, and `DJERD_V35_EDGE_NODE_WEIGHT`.

Verification after edge-node verifier work:
- Python compile passed for `v34_move_search.py`,
  `build_v35_action_dataset.py`, and `eval_v35_scorer_filter.py`.
- `npm run typecheck` passed.
- `npm run build:extension` passed.
- Tiny v35 smoke:
  `initial cross=1365 overlaps=102 edgeNode=554 bbox=3.54B`
  then 1 accepted move to `cross=1365 overlaps=100 edgeNode=554`.
- Incremental edge-node delta smoke matched full re-measurement on sampled
  node moves (`edge-node pairs=1,444,934`).

Remaining next step:
- Retrain a v35 checkpoint on the new edge-node-aware dataset, then run
  Captain live/rigid evaluation and compare crossings, node overlaps,
  edge-node intersections, bundle-node overlaps, bbox, and runtime.

Edge-node-aware pilot training/eval completed:
- `run_v35_graph_action_scorer.sh` now rebuilds 1250, hot700, and random700
  datasets instead of mixing new full data with stale subset NPZs.
- Removed fixed best TSV starts (`v34-best`, `v35-crosspair-best`) from the
  training starts to avoid answer-sheet contamination. The full dataset now
  uses `under1000`, `groupanchor2`, and optional `zero-overlap`.
- Removed `--count-bundle-nodes` from the v35 training data path so Python
  edge-node measurements match the live visible-node verifier.
- Candidate width for the pilot was reduced to `final-max-candidates=2000`
  and `final-per-action-candidates=400`; the earlier 6000/12000 settings
  were too slow with edge-node labels.
- `build_v35_action_dataset.py` now gates `sample_gain` with the same
  non-regression behavior rules as live eval before marking positives:
  no crossing, node-overlap, edge-node, or bbox regression by default.

Pilot training artifacts:
- `data/erd-poc/v35-move-scorer/v35-action-dataset.npz`:
  12 states / 24,000 samples / 102 local features.
- `data/erd-poc/v35-move-scorer/v35-action-dataset-hot700.npz`:
  2 states / 4,000 samples.
- `data/erd-poc/v35-move-scorer/v35-action-dataset-random700.npz`:
  4 states / 8,000 samples.
- New `data/erd-poc/checkpoints/v35-graph-action-scorer.pt` trained on
  36,000 samples / 18 states / 103 merged features.
- Final epoch summary: `valReg=0.4861`, `corr=0.479`,
  `accept@50=1.000`, `regret@50=2.25`.

Pilot eval with the new checkpoint:
- 1250 `under1000` start, 4 rounds, no full compare:
  straight `cross=1365`, `overlaps=102 -> 95`, `edgeNode=554`.
- 1250 `zero-overlap` start, 4 rounds, no full compare:
  straight `cross=1416 -> 1407`, `overlaps=0`, `edgeNode=541 -> 517`.
- hot700, 4 rounds:
  straight `cross=1423 -> 1417`, `edgeNode=266 -> 246`.
- random700, 4 rounds:
  straight `cross=209 -> 207`, `edgeNode=90 -> 67`.
- One-round full exact comparison on 1250 `zero-overlap`:
  `fullGain=19`, `mlGain=19`, `regret=0`, full best rank 173 globally
  and action rank 82 within `edge_endpoint_translate`.

Rigid reroute measurements:
- `zero-overlap` baseline:
  `edgeCrossings=1314`, `nodeOverlaps=0`,
  `edgeNodeIntersections=579`, `bundleNodeOverlaps=48`.
- New pilot `/tmp/v35-edge-node-zero-overlap.tsv`:
  `edgeCrossings=1305`, `nodeOverlaps=0`,
  `edgeNodeIntersections=564`, `bundleNodeOverlaps=48`.
- Fixed reference `/tmp/v35-crosspair-1250-wide.tsv` remains better on
  crossings but is still a fixed 1250-node artifact:
  `edgeCrossings=1141`, `nodeOverlaps=0`,
  `edgeNodeIntersections=555`, `bundleNodeOverlaps=47`.

Interpretation:
- The ML+exact behavior path now improves edge-node and crossing together
  without using fixed answer coordinates.
- It is not yet at the fixed artifact's crossing level. The next useful
  work is to add crossing-reduction actions that are also edge-node safe,
  then widen data/search after profiling.

Crossing-action widening step completed:
- Added generic crossing-derived actions in `v34_move_search.py`:
  `crossing_fan_edge_translate`, `crossing_fan_endpoint_translate`,
  experimental `crossing_endpoint_partner_orbit`, and experimental
  `crossing_pair_endpoint_swap`.
- `crossing_fan_*` is enabled in the training/live path. The partner-orbit
  and endpoint-swap actions remain available as CLI/env knobs but are off by
  default because exact analysis showed unstable positives inside the normal
  candidate budget.
- Live/default v35 candidate budget is now:
  `final-max-candidates=4000`, `final-per-action-candidates=1000`,
  `cross-fan-edges=120`, `edge-node-weight=0.5`.
- `edge-node-weight` was lowered from 2.0 to 0.5. The regression gate still
  forbids edge-node increases, but the score now prioritizes crossing
  reduction over pure edge-node cleanup.

Crossing-action pilot training:
- Rebuilt all three datasets at 4000 candidates/state:
  full 1250 = 48,000 samples, hot700 = 16,000, random700 = 16,000.
- New checkpoint trained on 80,000 samples / 20 states / 98 features:
  `data/erd-poc/checkpoints/v35-graph-action-scorer.pt`.
- Final epoch for the weight-0.5 run:
  `valReg=0.4708`, `corr=0.281`, `accept@50=1.000`, `regret@50=0.40`.

Latest eval, weight 0.5 checkpoint:
- 1250 zero-overlap, 4 rounds, `per-action-k=250`:
  straight `cross=1416 -> 1397`, `edgeNode=541 -> 514`, `overlaps=0`.
- Same result after rigid reroute:
  `edgeCrossings=1290`, `nodeOverlaps=0`,
  `edgeNodeIntersections=558`, `bundleNodeOverlaps=48`.
- 1250 zero-overlap baseline rigid:
  `edgeCrossings=1314`, `edgeNodeIntersections=579`.
- Fixed artifact reference still better on crossings:
  `edgeCrossings=1141`, `edgeNodeIntersections=555`.
- hot700 eval:
  straight `cross=1423 -> 1418`, `edgeNode=266 -> 255`.
- random700 eval:
  straight `cross=209 -> 198`, `edgeNode=90 -> 68`.

Shortlist note:
- `per-action-k=300` captured the one-round full exact best
  (`fullGain=13`, `mlGain=13`, `regret=0`), but its 4-round rigid result had
  worse crossings (`1303`) than `per-action-k=250` (`1290`). Keep the default
  at 250 for now because crossing reduction is the primary target.

Next useful direction:
- The current behavior model is improving crossings generically, but the
  remaining gap to 1000 crossings is still large. The next action family
  should be a group/cluster-level crossing-carrier move: detect a carrier
  bundle or louvain group that participates in many crossings and move it
  as a coherent object while the verifier blocks node/edge-node regressions.

Group crossing-fan step completed:
- Added `cross_group_fan_translate` in `scripts/erd-poc/v34_move_search.py`.
  It ranks louvain/pseudo groups by crossing incident edges, derives move
  directions from external crossing partner edge fans and boundary neighbors,
  and exact-verifies the move like every other v35 action.
- Wired the action through:
  `build_v35_action_dataset.py`, `eval_v35_scorer_filter.py`,
  `run_v35_graph_action_scorer.sh`, and
  `src/extension/services/layout/runOgdfLayout.ts`.
- New default knobs:
  `DJERD_V35_CROSS_GROUP_FAN_GROUPS=160`,
  `DJERD_V35_CROSS_GROUP_FAN_MAX_SIZE=120`,
  `DJERD_V35_CROSS_GROUP_FAN_STEPS=75,150,300,600,1000,1800,3000,5000`.

Group crossing-fan training/eval:
- Smoke with only 5 group-fan groups produced a valid positive:
  straight `cross=1416 -> 1415`, `edgeNode=541 -> 537`.
- One-round full exact comparison on 1250 zero-overlap found the new action
  as the best candidate:
  `louvain_crossing_group_fan_translate`, gain `15.0`,
  `deltaCross=-12`, `deltaEdgeNode=-6`.
- Rebuilt all v35 scorer datasets and retrained
  `data/erd-poc/checkpoints/v35-graph-action-scorer.pt`.
  Latest datasets still use 80,000 samples / 20 states, now with 6 action
  types including `louvain_crossing_group_fan_translate`.
- New 1250 zero-overlap, 8 rounds, no full compare:
  straight `cross=1416 -> 1384`, `edgeNode=541 -> 497`, `overlaps=0`.
- Same 8-round output after rigid reroute
  `/tmp/v35-cross-group-zero-r8.tsv`:
  `edgeCrossings=1286`, `nodeOverlaps=0`,
  `edgeNodeIntersections=539`, `bundleNodeOverlaps=47`,
  `bundleEdgeIntersections=122`.
- hot700, 4 rounds:
  straight `cross=1423 -> 1412`, `edgeNode=266 -> 250`, `overlaps=0`.
- random700, 4 rounds:
  straight `cross=209 -> 194`, `edgeNode=90 -> 77`, `overlaps=0`.

Interpretation after group crossing-fan:
- This is a real generic improvement over the prior behavior path:
  previous rigid 1250 was `edgeCrossings=1290`, `edgeNodeIntersections=558`;
  new 8-round rigid is `edgeCrossings=1286`, `edgeNodeIntersections=539`.
- It is still far from the fixed 1250 artifact's crossing count (`1141`) and
  the user target near `1000`. The next step should not be wider random
  search; it should add an even more structural action, likely a
  carrier-pair separation / corridor action that moves two crossing carrier
  groups apart together instead of moving only one louvain group away from
  its fan.

Visual diagnostics:
- Added `scripts/erd-poc/render_layout_visual_diagnostics.py`, a static HTML
  renderer for layout JSONs. It draws a full overview, crossing-hotspot zooms,
  top crossing edges, and approximate edge-node hit concentration.
- Generated and opened:
  `/tmp/v35-cross-group-visual-diagnostics.html`.
  Screenshot artifact:
  `/tmp/v35-cross-group-visual-diagnostics.png`.
- Latest visual problem concentration for
  `/tmp/v35-cross-group-zero-r8-rigid.json`:
  dense crossing cells around `(27511,16958)`, `(27172,23369)`,
  `(28142,25815)`, `(23003,22927)`, `(31384,26986)`, `(22976,17937)`.
- Top crossing edges are mostly long inter-domain carriers:
  `InvestmentAssociation -> VentureCapital` (43),
  `NewIssueSkipNoticeDocument -> Shareholder` (38),
  `Purchase -> VentureCapital` (30),
  `Stakeholder -> Address` (30),
  `SubscriptionPaymentRequest -> Subscription` (28),
  `CreditCard -> VentureCapital` (27).
- Edge-node visual hits concentrate around document/company satellite nodes,
  e.g. `DirectorsMeetingMinutesDocument`,
  `SealedOptionGrantShareholdersMeetingResolutionDocument`,
  `CompanyRegistrationAssignmentLog`, `RegistrationCaseReportUpdateLog`.
  The script's approximate edge-node total overcounts (`891`) versus the C++
  authority metric (`539`) because it uses a simple segment-rectangle test;
  use it as a visual hotspot locator, not the quality source of truth.

Carrier-pair separation step completed:
- Added `cross_carrier_pair_separate` in `scripts/erd-poc/v34_move_search.py`.
  It finds louvain/pseudo group pairs that co-occur in many current crossing
  edge pairs, then generates coordinated set-position moves that push the two
  carrier groups apart. The exact verifier still gates every accepted move.
- Wired through:
  `build_v35_action_dataset.py`, `eval_v35_scorer_filter.py`,
  `run_v35_graph_action_scorer.sh`, and
  `src/extension/services/layout/runOgdfLayout.ts`.
- New default knobs:
  `DJERD_V35_CROSS_CARRIER_PAIR_CANDIDATES=160`,
  `DJERD_V35_CROSS_CARRIER_PAIR_MAX_SIZE=120`,
  `DJERD_V35_CROSS_CARRIER_PAIR_STEPS=75,150,300,600,1000,1800,3000,5000`.
- Smoke with only carrier-pair candidates:
  495 candidates, 23 positive,
  best `louvain_crossing_carrier_pair_separate`, gain `8.5`,
  `deltaCross=-8`, `deltaEdgeNode=-1`.
- One-round full exact on 1250 zero-overlap:
  best `louvain_crossing_carrier_pair_separate`, gain `15.0`,
  `deltaCross=-15`, `deltaEdgeNode=0`.
- Rebuilt datasets and retrained
  `data/erd-poc/checkpoints/v35-graph-action-scorer.pt`.
  Latest action set has 7 action types including
  `louvain_crossing_carrier_pair_separate`.

Carrier-pair eval:
- 1250 zero-overlap, 8 rounds, no full compare:
  straight `cross=1416 -> 1384`, `edgeNode=541 -> 510`, `overlaps=0`.
  This keeps straight crossing equal to the prior group-fan result but is
  worse on straight edge-node (`497` before).
- Same 8-round output after rigid reroute
  `/tmp/v35-carrier-pair-zero-r8.tsv`:
  `edgeCrossings=1272`, `nodeOverlaps=0`,
  `edgeNodeIntersections=547`, `bundleNodeOverlaps=44`,
  `bundleEdgeIntersections=124`, `visualCrossings=1987`.
  Prior group-fan rigid was `edgeCrossings=1286`, `edgeNodeIntersections=539`,
  `bundleNodeOverlaps=47`, `visualCrossings=1994`.
- hot700, 4 rounds:
  straight `cross=1423 -> 1412`, `edgeNode=266 -> 250`, `overlaps=0`;
  effectively unchanged from group-fan.
- random700, 4 rounds:
  straight `cross=209 -> 199`, `edgeNode=90 -> 71`, `overlaps=0`;
  crossing is worse than group-fan (`194`) but edge-node is better (`77`).

Carrier-pair visual diagnostics:
- Generated and opened:
  `/tmp/v35-carrier-pair-visual-diagnostics.html`.
  Screenshot artifact:
  `/tmp/v35-carrier-pair-visual-diagnostics.png`.
- Top hotspot cells are still centered in the same corridor:
  `(27518,16933)`, `(28156,25640)`, `(27096,23344)`, `(22864,22796)`,
  `(31386,27108)`, `(23003,17871)`.
- Top crossing edges remain long inter-domain carriers:
  `InvestmentAssociation -> VentureCapital` (43),
  `Stakeholder -> Address` (30),
  `CreditCard -> VentureCapital` (29),
  `Purchase -> VentureCapital` (28),
  `SubscriptionPaymentRequest -> Subscription` (28).

Interpretation after carrier-pair:
- The action is useful: it lowered 1250 rigid crossings from `1286` to `1272`
  without node overlaps. That is the best generic v35 rigid crossing so far.
- It did not break the central corridor pattern; hotspots stayed in nearly
  the same places. The next structural action should target corridor routing
  itself: detect the top long carrier edges through a hotspot and create a
  bypass/port-side action that moves endpoints or small endpoint-side groups
  to route around the hotspot, while preserving zero node overlaps.

Hotspot endpoint-bypass step completed:
- Added `cross_hotspot_endpoint_bypass` in
  `scripts/erd-poc/v34_move_search.py`.
  It detects dense crossing cells, ranks high-crossing edges through each
  hotspot, and pivots either one endpoint or the endpoint's small louvain/pseudo
  group along normal/diagonal directions so the straight segment can miss the
  hotspot. Exact verifier still gates crossing, overlap, edge-node, and bbox.
- Wired through:
  `build_v35_action_dataset.py`, `eval_v35_scorer_filter.py`,
  `run_v35_graph_action_scorer.sh`, and
  `src/extension/services/layout/runOgdfLayout.ts`.
- Training script includes the action:
  `--cross-hotspot-bypass-hotspots 8`,
  `--cross-hotspot-bypass-edges 8`,
  `--cross-hotspot-bypass-max-size 80`,
  `--cross-hotspot-bypass-cell-size 5000`,
  `--cross-hotspot-bypass-steps 75,150,300,600,1000,1800,3000,5000`.
- Live/extension default is intentionally off:
  `DJERD_V35_CROSS_HOTSPOT_BYPASS_HOTSPOTS=0`.
  It can be enabled manually for experiments, but the current visual metric is
  better with it disabled.

Hotspot endpoint-bypass eval:
- Smoke with only hotspot bypass candidates:
  1500 candidates, best `crossing_hotspot_endpoint_bypass`, gain `21.5`,
  `deltaCross=-21`, `deltaEdgeNode=-1`.
  Also positive louvain-group endpoint moves existed:
  best `louvain_crossing_hotspot_endpoint_bypass`, gain `18.0`,
  `deltaCross=-16`, `deltaEdgeNode=-4`.
- One-round full exact with all normal candidates:
  best `louvain_crossing_hotspot_endpoint_bypass`, gain `18.0`,
  `deltaCross=-16`, `deltaEdgeNode=-4`.
- Rebuilt datasets and retrained
  `data/erd-poc/checkpoints/v35-graph-action-scorer.pt`.
  Latest datasets are still 80,000 samples / 20 states, now with hotspot
  bypass action types present.
- With hotspot enabled, 1250 zero-overlap 8 rounds:
  straight `cross=1416 -> 1382`, `edgeNode=541 -> 509`, `overlaps=0`;
  rigid `/tmp/v35-hotspot-bypass-zero-r8-rigid.json`:
  `edgeCrossings=1276`, `nodeOverlaps=0`,
  `edgeNodeIntersections=552`, `bundleNodeOverlaps=44`,
  `bundleEdgeIntersections=121`, `visualCrossings=1993`.
- With hotspot disabled using the same new checkpoint, 1250 zero-overlap
  8 rounds:
  straight `cross=1416 -> 1384`, `edgeNode=541 -> 510`, `overlaps=0`;
  rigid `/tmp/v35-hotspot-off-zero-r8-rigid.json`:
  `edgeCrossings=1272`, `nodeOverlaps=0`,
  `edgeNodeIntersections=547`, `bundleNodeOverlaps=44`,
  `bundleEdgeIntersections=124`, `visualCrossings=1987`.
  This matches the carrier-pair best rigid crossing and remains the live
  recommended setting.
- hot700, hotspot disabled:
  straight `cross=1423 -> 1412`, `edgeNode=266 -> 250`, `overlaps=0`.
- random700, hotspot disabled:
  straight `cross=209 -> 202`, `edgeNode=90 -> 65`, `overlaps=0`.

Interpretation after hotspot bypass:
- The new action is genuinely useful in exact local search, but the learned
  live sequence with hotspot enabled gives worse rigid crossings than keeping
  it off (`1276` vs `1272`). It is currently an experimental action, not a
  live default.
- The next useful step is not another candidate family immediately. First
  analyze why exact-positive hotspot moves hurt rigid reroute: compare
  accepted hotspot move records against rigid hotspot cells, then add either a
  rigid-aware acceptance proxy or a feature/gate that penalizes moving a
  port-side group when it increases later reroute edge-node/corridor pressure.

Hotspot pressure-gate probe:
- Added optional runtime diagnostic/gate in
  `scripts/erd-poc/eval_v35_scorer_filter.py`:
  `--cross-hotspot-bypass-pressure-top-cells` and
  `--cross-hotspot-bypass-max-pressure-growth`.
  It computes straight-line crossing density over grid cells and can reject
  `cross_hotspot_endpoint_bypass` candidates that increase top-cell pressure.
- The gate is disabled by default (`max-pressure-growth=-1`) because the first
  probe showed it is not a reliable rigid proxy.
- Probe details on 1250 zero-overlap, first round:
  - hotspot enabled without pressure gate:
    selected `louvain_crossing_hotspot_endpoint_bypass`,
    straight `cross=1400`, `edgeNode=537`;
    rigid `edgeCrossings=1287`, `edgeNodeIntersections=574`.
  - hotspot disabled:
    selected `louvain_crossing_carrier_pair_separate`,
    straight `cross=1401`, `edgeNode=541`;
    rigid `edgeCrossings=1288`, `edgeNodeIntersections=578`.
  - hotspot enabled with strict pressure gate (`max-growth=0`):
    rejected 19 hotspot candidates but still selected another hotspot move,
    straight `cross=1403`, `edgeNode=535`;
    rigid `edgeCrossings=1299`, `edgeNodeIntersections=576`.
- Pressure summaries:
  start top8 cell pressure `963`;
  no-gate hotspot r1 `971`;
  carrier r1 `969`;
  strict-gated hotspot r1 `961`.
  Lower straight hotspot-cell pressure did not imply better rigid crossings,
  so this proxy alone is insufficient.
- Current conclusion:
  leave hotspot bypass as an experimental action and keep live default off.
  To improve beyond `1272`, the next analysis should log accepted action
  records plus C++ rigid deltas for a small set of candidate trajectories,
  then learn or hand-code a proxy that uses endpoint/group identity and reroute
  side effects, not just straight crossing-cell density.

May 21 v36/v37 runtime trust fixes:
- User flagged the fast runtime result as hard to trust because bbox was too
  wide, an edge looked disconnected, and leaf-bundles lacked margin from other
  nodes.
- Fixed straight-edge rendering attachment in
  `native/ogdf-layout/src/main.cpp`: parallel-edge lane offsets and
  obstacle nudges now slide ports along the node rectangle boundary instead of
  floating endpoints off the box. Final route sync now defaults to a 100-unit
  stale-gap threshold and snaps stale endpoints to boundary ports, not centers.
- Added render-aware leaf-bundle margin:
  `DJERD_LEAF_BUNDLE_VISUAL_MARGIN` default is 32 while normal node margin
  remains 8. Measure, bundle relocation, final detour, and relief paths use
  the larger rendered leaf-bundle obstacle.
- Rigid ML reroute now allows narrow visual-integrity passes:
  `DJERD_RIGID_ATTACH_ISOLATED_FINAL=1` moves edge-less nodes near connected
  nodes selected by model-name token overlap, avoiding existing nodes and
  current straight routes. This targets the large bbox caused by isolated
  nodes parked at layout extremes.
- Rigid ML reroute also enables leaf-bundle/node clearance by default in
  `src/extension/services/layout/runOgdfLayout.ts`:
  8 passes, max shift 3600, extra clearance 32.
- Validation on preserved Captain input:
  - previous fixed-margin reroute without isolated attach:
    bbox `14.34B`, visual `903`, bundleNode `2`, endpoint gap max `~0`.
  - with isolated attach and stronger bundle clear:
    bbox `11.38B`, visual `888`, edgeCross `529`, edgeNode `321`,
    bundleEdge `31`, bundleNode `5`, nodeOverlaps `2`.
  - endpoint audit on `/tmp/v37-fixed-attach-clear8.json`:
    `bad=0`, worst endpoint gap `0.006`, so the disconnected-edge issue is
    fixed for straight routes.
- Tried axis whitespace compaction: bbox fell to `1.71B` but visual exploded
  to `12090`; keep it disabled. Uniform bbox scaling is also unsafe unless it
  preserves margin overlaps, so it remains guarded and usually no-ops on this
  input.
- Native binary rebuilt into
  `bin/ogdf/darwin-arm64/django-erd-ogdf-layout`; `npm run build` passed.

May 21 sidecar component + gated Y compaction:
- Component decomposition of the `11.39B` layout showed 36 connected
  components plus 157 isolated nodes. The main component was
  `112550 x 75050` (`8.45B`); the second component (`vcm.*`) sat below it
  at `0..27906 x 75230..100955`, and many degree-0 nodes also extended the
  bottom bbox. This was a layout packing problem, not a route crossing
  problem.
- Added `compactSidecarBBoxComponents`: all non-main connected components
  and isolated nodes outside the main component bbox are packed into a
  right-side vertical sidecar lane. It snapshots positions and accepts only
  if bbox area improves, aspect stays below
  `DJERD_SIDECAR_BBOX_COMPACT_MAX_ASPECT` (default `2.2`), and node overlap
  counts do not regress.
- Added a final metric-gated Y-axis shrink (`DJERD_BBOX_AXIS_SCALE_FINAL`).
  It tests `DJERD_BBOX_Y_SCALE_FINAL_SCALES` (default `0.98,0.95`), reroutes
  and recomputes rendered-carrier metrics for each candidate, and accepts
  only if visualCrossings do not increase, node/bundle overlaps do not
  increase, bbox improves by at least `1.5%`, and aspect remains below `2.1`.
- Replay validation with the same `log.txt` v36 positions:
  edge-less bbox pass `12.52B -> 11.39B`;
  sidecar pass moved `35` connected components and `125` edge-less nodes,
  `11.39B -> 10.56B`;
  Y-scale `0.980` accepted, `10.56B -> 10.35B`,
  `visualCrossings=496`, `edgeCrossings=314`,
  `edgeNodeIntersections=162`, `nodeOverlaps=0`,
  `bundleEdgeIntersections=13`, `bundleNodeOverlaps=7`,
  aspect `1.914`.
- Native binary rebuilt into
  `bin/ogdf/darwin-arm64/django-erd-ogdf-layout`; `npm run build` and
  `git diff --check` passed.

May 21 webview render endpoint fix:
- `log.txt` showed the native optimized run itself was improved:
  `visualCrossings=888`, `edgeCrossings=529`, `edgeNodeIntersections=321`,
  `leafBundles=47`, and final route sync reported no stale route endpoints.
  The suspect visual break was in the webview render layer, not the native
  route JSON.
- The webview scene built `tables=1336` while the native layout had
  `nodes=1289`; leaf bundles and synthetic render tables can change the
  rendered box size/position relative to native route endpoints. Hub-carrier
  static routes can also carry average endpoints that are not attached to the
  representative rendered node.
- Patched `src/webview/interaction/runtime/browserLayoutSource.ts` so static
  and manual-position fallback edge paths are reattached to the current
  rendered source/target table rectangle before drawing. This makes endpoint
  accuracy depend on the actual rendered box, not stale native/static points.
- `npm run build` passed after the patch.

May 21 optimized post-stack reroute:
- Rigid-only relief improved the previous v36 optimized replay from
  `visualCrossings=888` to `619` when tuned with rendered metrics
  (`maxShift=80`, `strength=0.8`, `bundleNodeWeight=0`), but it plateaued
  above the 500 target.
- The better path is to feed v36 positions into the existing C++ post-stack
  without `--rigid-positions`, while disabling the expensive/detour-oriented
  passes for the optimized path:
  `DJERD_SKIP_CG_OPT=1`, `DJERD_FACE_RASTER=0`, `DJERD_HOT_REGION_SA=0`,
  `DJERD_STUCK_LEAF_2D=0`, `DJERD_XINGS_DETOUR=0`, `DJERD_NO_PD_KNOT=1`,
  `DJERD_VISUAL_KNOT=0`, `DJERD_BUNDLE_BOX_RELOCATE_FINAL=0`.
- The default optimized reroute in
  `src/extension/services/layout/runOgdfLayout.ts` now uses this post-stack
  path unless `DJERD_OPTIMIZED_REROUTE_POSTSTACK=0` is set. It keeps straight
  routes and uses final node-edge relief with `passes=4`, `maxShift=80`,
  `strength=0.8`, endpoints disabled.
- Replay validation with the installed bin binary:
  `visualCrossings=493`, `edgeCrossings=313`,
  `edgeNodeIntersections=161`, `nodeOverlaps=0`,
  `bundleEdgeIntersections=13`, `bundleNodeOverlaps=6`,
  `routeSegments=918`, bbox `11.69B`, runtime about `35s`.
- Full post-stack can reach `visualCrossings=389`, but took about `70s` on
  the replay and uses more expensive passes, so it is not the default.
- A constrained bundle relocate (`top=2`, `candidates=16`) reached
  `visualCrossings=484`, but still took about `100s`; keep bundle relocate
  disabled for the optimized default.
- Rebuilt native binary into
  `bin/ogdf/darwin-arm64/django-erd-ogdf-layout`; `npm run build` passed.

May 21 edge-less node clustering fix:
- The optimized post-stack path had regressed edge-less node clustering:
  rigid reroute used name-based isolated attach, but post-stack skipped that
  call and then `isolated-stash` moved all degree-0 nodes into a right-side
  strip.
- `attachIsolatedNodesByName` now also reads
  `DJERD_ATTACH_ISOLATED_BY_NAME_FINAL`. The general post-stack calls it
  before `isolated-stash`; if any isolated node is attached, stash does not
  undo it. Optimized post-stack sets `DJERD_ISOLATED_STASH=0`.
- Isolated placement now prefers the outward direction from the matched
  semantic anchor, so edge-less nodes join the related cluster edge instead of
  being inserted through the graph interior.
- Optimized default uses strong semantic matching only:
  `DJERD_ATTACH_ISOLATED_MIN_SCORE=35`,
  `DJERD_ATTACH_ISOLATED_ROUTE_CHECK=1`.
- Replay validation after this fix:
  `isolated-name-attach-final attached 47/157`,
  no `isolated-stash`, `visualCrossings=499`, `edgeCrossings=314`,
  `edgeNodeIntersections=165`, `nodeOverlaps=0`,
  `bundleEdgeIntersections=13`, `bundleNodeOverlaps=7`,
  bbox `12.52B`, runtime about `32s`.
- Native binary rebuilt into
  `bin/ogdf/darwin-arm64/django-erd-ogdf-layout`; `npm run build` passed.

May 21 bbox reduction from edge-less outliers:
- `log.txt` latest optimized run had stale installed-binary metrics:
  `visualCrossings=789`, `edgeCrossings=563`, `edgeNodeIntersections=209`,
  bbox `123726.4 x 101210.9`. Replaying the same v36 positions with the
  current post-stack binary gives the real baseline:
  `visualCrossings=499`, `edgeCrossings=314`, `edgeNodeIntersections=165`,
  `nodeOverlaps=0`, `bundleEdgeIntersections=13`, `bundleNodeOverlaps=7`,
  bbox area `12.52B`.
- Bbox was node-dominated by degree-0 outliers. Connected-node bbox was
  about `112558 x 100955` (`11.36B`), while the full node bbox was
  `123726 x 101211` (`12.52B`). The right and bottom extremes were mostly
  edge-less nodes, so global scaling was the wrong lever.
- Uniform scale tests reduced bbox but harmed visual quality:
  scale `0.95` -> bbox `11.30B` but `visualCrossings=546`,
  scale `0.90` -> bbox `10.15B` but `visualCrossings=586`,
  scale `0.85` introduced `nodeOverlaps=56`.
- Added `compactIsolatedBBoxOutliers`: degree-0 nodes that expand the
  connected graph bbox are sorted by app/name tokens and packed into a thin
  shelf constrained to the connected graph width. It only accepts if bbox
  area improves by at least `DJERD_ISOLATED_BBOX_COMPACT_MIN_GAIN`
  (default `0.01`), otherwise it restores positions.
- Optimized reroute defaults now enable
  `DJERD_ISOLATED_BBOX_COMPACT_FINAL=1`, with gapX `220`, gapY `46`,
  offsetY `180`. This is not edge detouring and does not move connected
  relationship nodes.
- Replay validation with the same `log.txt` positions:
  `[isolated-name-attach-final] attached 47/157`,
  `[isolated-bbox-compact-final] moved 60/157 edge-less outliers`,
  bbox `12.52B -> 11.39B`, `visualCrossings=499`,
  `edgeCrossings=314`, `edgeNodeIntersections=165`, `nodeOverlaps=0`,
  `bundleEdgeIntersections=13`, `bundleNodeOverlaps=7`.
- Native binary rebuilt into
  `bin/ogdf/darwin-arm64/django-erd-ogdf-layout`; `npm run build` passed.

May 21 leaf-bundle big-node clash fix:
- `log.txt` latest installed run still had rendered leaf-bundle clashes:
  `visualCrossings=898`, `edgeCrossings=681`,
  `edgeNodeIntersections=197`, `bundleEdgeIntersections=14`,
  `bundleNodeOverlaps=6`, bbox about `10.33B` after sidecar compaction.
- Replaying the same v36 positions with the current reconstructed input
  showed the same shape: `visualCrossings=844`, `edgeCrossings=637`,
  `edgeNodeIntersections=189`, `bundleEdgeIntersections=7`,
  `bundleNodeOverlaps=11`, bbox `10.29B`.
- `DJERD_VISUAL_KNOT=1` was tested as a no-detour crossing pass, but it
  worsened the replay to `visualCrossings=938`, so it remains disabled for
  the optimized default.
- Added a final metric-gated leaf-bundle/node clear in
  `native/ogdf-layout/src/main.cpp`. It treats each leaf bundle as the
  rendered synthetic big-node box, moves the bundle leaves as a rigid block,
  reroutes, recomputes rendered-carrier metrics, and accepts only if
  `bundleNodeOverlaps` improves while visual crossings, edge-node
  intersections, hard node overlaps, and bbox stay within gates.
- Optimized post-stack now enables this pass via
  `DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL=1` with defaults:
  `PASSES=8`, `MAX_SHIFT=3600`, `EXTRA=32`, `VISUAL_SLACK=0`,
  `NODE_OVERLAP_SLACK=0`, `EDGE_NODE_SLACK=0`, `BBOX_LIMIT=1.04`.
- Replay validation after the fix:
  `[leaf-bundle-node-clear-final] accepted 10 bundle moves`,
  `bundleNodeOverlaps=11 -> 0`, `edgeNodeIntersections=189 -> 176`,
  `bundleEdgeIntersections=7 -> 6`, `visualCrossings=844 -> 826`,
  `nodeOverlaps=0`, bbox unchanged at about `10.29B`.
- Native binary rebuilt into
  `bin/ogdf/darwin-arm64/django-erd-ogdf-layout`; `npm run build` and
  `git diff --check` passed.

May 21 2B bbox target pass:
- Latest `log.txt` after the leaf-bundle fix reported
  `visualCrossings=884`, `edgeCrossings=685`,
  `edgeNodeIntersections=187`, `bundleEdgeIntersections=12`,
  `bundleNodeOverlaps=0`, bbox `115954.9 x 89054.9` (`10.33B`).
- Replay of the same v36 positions with current binary gave
  `visualCrossings=826`, `edgeCrossings=644`,
  `edgeNodeIntersections=176`, `bundleEdgeIntersections=6`,
  `bundleNodeOverlaps=0`, node bbox `115493.2 x 89054.9` (`10.285B`).
- Component decomposition showed the real blocker: the main connected
  component alone was `87246 x 89055` (`7.77B`). Sidecar/isolated packing
  cannot reach 2B unless the main component is also compressed.
- Added a TypeScript optimized reroute target pass in
  `src/extension/services/layout/runOgdfLayout.ts`: after the first v36
  post-stack result, if node bbox exceeds `DJERD_OPTIMIZED_BBOX_TARGET_B`
  (default `2.0`), write a scaled positions TSV around the current bbox
  center and rerun the native post-stack once more.
- Default target safety is `0.99`; target reroute uses stronger but still
  non-detour relief defaults:
  `DJERD_OPTIMIZED_BBOX_TARGET_RELIEF_PASSES=8`,
  `MAX_SHIFT=160`, `STRENGTH=0.9`, and bundle clear edge-node slack `2`.
  Candidate acceptance requires `nodeOverlaps=0`, bbox within target
  tolerance (`1.02` by default), and visualCrossings below
  `DJERD_OPTIMIZED_BBOX_TARGET_MAX_VISUAL` (`1600`).
- Replay validation for the integrated scale (`0.438758`) produced
  node bbox `49494.9 x 39537.8` (`1.957B`), `visualCrossings=784`,
  `edgeCrossings=400`, `edgeNodeIntersections=362`,
  `bundleEdgeIntersections=19`, `bundleNodeOverlaps=3`,
  `nodeOverlaps=0`. This meets the 2B goal and is lower visualCrossing than
  the uncompressed replay, though edge-node contacts remain higher.
- `npm run build` passed.

May 21 bbox empty-space tightening:
- Latest `log.txt` showed the target pass was attempted but rejected:
  first post-stack `bbox=7.03B`, `visualCrossings=663`,
  `edgeNodeIntersections=169`, `bundleNodeOverlaps=1`; bbox target scaled
  by `0.531` but candidate ended at `bbox=2.30B`, `visualCrossings=980`,
  `edgeNodeIntersections=320`, `nodeOverlaps=0`, so it missed the
  `2.0B * 1.02` area gate and the final output stayed at the large bbox.
- Replayed the preserved app input
  `/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-fmmm-rnXECP`
  with the current binary. Baseline post-stack now gives `bbox=6.750B`,
  `visualCrossings=624`, `edgeCrossings=466`,
  `edgeNodeIntersections=144`, `bundleEdgeIntersections=12`,
  `bundleNodeOverlaps=2`, `nodeOverlaps=0`.
- The useful compression point is safety `0.88`: with target-stage
  leaf-bundle slack it gives `bbox=1.781B`, `visualCrossings=833`,
  `edgeCrossings=481`, `edgeNodeIntersections=331`,
  `bundleEdgeIntersections=17`, `bundleNodeOverlaps=4`,
  `nodeOverlaps=0`, `nodeSpacingOverlaps=7`. This removes most empty space
  while staying under a 900 visual-cross gate and keeping bundle-node clashes
  bounded.
- Updated `src/extension/services/layout/runOgdfLayout.ts` so bbox target
  tries safety values in order, defaulting to `0.88,0.99` (or a user-provided
  `DJERD_OPTIMIZED_BBOX_TARGET_SAFETIES`; singular
  `DJERD_OPTIMIZED_BBOX_TARGET_SAFETY` still forces one value). Candidate
  acceptance now also gates `bundleNodeOverlaps`
  (`DJERD_OPTIMIZED_BBOX_TARGET_MAX_BUNDLE_NODE`, default `4`) and tightens
  default visual acceptance to `900`.
- Target-stage leaf-bundle clearing now defaults to a small amount of extra
  slack: `DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_EDGE_NODE_SLACK=16` and
  `DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_VISUAL_SLACK=50`, so compressed
  candidates can clear bundle boxes instead of keeping bundle-node clashes.
- `npm run build` passed. `git diff --check -- src/extension/services/layout/runOgdfLayout.ts`
  passed; repository-wide `git diff --check` still reports an unrelated
  pre-existing blank line at EOF in `analyzer/src/resolve/graph_builder.rs`.

May 21 bbox target below 1.5B:
- Re-reading `log.txt` showed it still contains the previous app run, not
  the new safety-list patch: the visible result was the rejected one-shot
  target and final `bbox=7.03B`.
- Additional replay on the preserved 1251-node input found that pure scaling
  can go lower than `0.88`: `safety=0.70` gives `bbox=1.396B`,
  `visualCrossings=893`, `edgeCrossings=534`,
  `edgeNodeIntersections=312`, `bundleEdgeIntersections=37`,
  `bundleNodeOverlaps=10`, `nodeOverlaps=0`. The only blocker is the bundle
  render box placement.
- Enabling the existing bundle-box relocate pass only for bbox-target reruns
  clears that blocker. Fast settings (`PASSES=1`, `TOP=10`,
  `MAX_CANDIDATES=32`, `SHORTLIST=4`, `FULL_SCAN=0`) produced
  `bbox=1.396B`, `visualCrossings=880`, `edgeCrossings=539`,
  `edgeNodeIntersections=316`, `bundleEdgeIntersections=25`,
  `bundleNodeOverlaps=0`, `nodeOverlaps=0`.
- Lower targets were tested but are past the current quality frontier:
  `safety=0.66` with fast relocate reached `bbox=1.319B` but worsened to
  `visualCrossings=1018`; `safety=0.62` reached `bbox=1.241B` but
  `visualCrossings=1028` and `bundleNodeOverlaps=7`.
- Updated `src/extension/services/layout/runOgdfLayout.ts` defaults:
  bbox target safeties are now `0.70,0.88,0.99`, target-stage bundle-box
  relocate is enabled with the fast settings above, and target acceptance
  now defaults to `bundleNodeOverlaps=0` plus `visualCrossings<=900` and
  `nodeOverlaps=0`.
- `npm run build` passed. `git diff --check -- src/extension/services/layout/runOgdfLayout.ts context.md`
  passed.

May 21 bbox target to ~1.26B:
- Latest `log.txt` still does not include the new bbox-target safety-list
  run. It remains the old one-shot target (`bbox=2.30B`, rejected) with
  final output `bbox=7.03B`; no `[bbox target] safety=0.700` or
  `[bundle-box-relocate-final]` line appears in the app log.
- Further replay showed the practical lower bound can move below 1.40B.
  With stronger target-only relief and fast bundle-box relocate,
  `safety=0.62` produced `bbox=1.266B`,
  `visualCrossings=925`, `edgeCrossings=371`,
  `edgeNodeIntersections=533`, `bundleEdgeIntersections=21`,
  `bundleNodeOverlaps=0`, `nodeOverlaps=0`.
- Compared candidates:
  `safety=0.70` remains cleaner (`bbox=1.396B`, `visualCrossings=880`,
  `edgeNodeIntersections=316`), while `safety=0.62` trades more edge-node
  contacts for a much smaller bbox. `safety=0.66` is worse than 0.62 here
  (`bbox=1.319B`, `visualCrossings=973`, `bundleNodeOverlaps=1`), so it is
  not in the default list.
- Updated `src/extension/services/layout/runOgdfLayout.ts` defaults:
  target safeties are now `0.62,0.70,0.88,0.99`; target accept visual gate
  is `950`; target-only relief is `PASSES=10`, `MAX_SHIFT=220`,
  `STRENGTH=0.95`; target leaf-bundle clear slack is edge-node `24` and
  visual `120`; bundle relocate max move is `6200`.
- `npm run build` passed. `git diff --check -- src/extension/services/layout/runOgdfLayout.ts context.md`
  passed.

May 21 latest 1291-node bbox target:
- `log.txt` is currently not an ERD layout log. It contains
  `intellij-styled-search`/codeidx extension logs, so no `[bbox target]`,
  `visualCrossings`, or OGDF completion lines can be evaluated from it.
- Used the preserved latest ERD input instead:
  `/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-fmmm-TnM29s`.
  The latest ML baseline was `nodes=1291`, `routedEdges=1554`,
  `bbox=12.954B`, `visualCrossings=1324`, `edgeCrossings=902`,
  `edgeNodeIntersections=383`, `bundleEdgeIntersections=35`,
  `bundleNodeOverlaps=3`, `nodeOverlaps=1`.
- The app-created bbox-target candidates showed the empty-space problem:
  compact candidates existed but were rejected by strict gates. Before this
  patch, `safety=0.70` reached `bbox=1.792B`, `visualCrossings=1005`,
  `bundleNodeOverlaps=0`, but `nodeOverlaps=3`; `safety=0.99` reached
  `bbox=1.943B`, `visualCrossings=1013`, `bundleNodeOverlaps=0`, but
  `nodeOverlaps=1`.
- Added a target-stage final node visual-overlap clear pass in
  `native/ogdf-layout/src/main.cpp`. It only moves margin-overlapping
  non-bundle nodes by the minimum local separation, reroutes, and accepts only
  when `nodeOverlaps` improves without material bbox/visual/edge-node
  regression.
- Enabled that pass from `ogdfOptimizedBboxTargetEnv()` and added the knobs to
  the layout cache key. Target defaults:
  `DJERD_NODE_OVERLAP_CLEAR_FINAL=1`, passes `8`, max shift `260`, extra `12`,
  visual slack `80`, edge-node slack `80`, bbox limit `1.03`.
- Rebuilt the native binary and replayed `safety=0.99`: accepted candidate
  now gives `bbox=1.943B`, `visualCrossings=1013`, `edgeCrossings=432`,
  `edgeNodeIntersections=568`, `bundleEdgeIntersections=13`,
  `bundleNodeOverlaps=0`, `nodeOverlaps=0`, `aspectRatio=1.249`.
- Updated bbox target visual accept default from `950` to `1020`, so this
  1.94B candidate is accepted while `0.70` is still rejected because it leaves
  `bundleNodeOverlaps=1` after the new pass.
- `node scripts/build-ogdf-binary.mjs`, `npm run build`, and
  `git diff --check -- src/extension/services/layout/runOgdfLayout.ts native/ogdf-layout/src/main.cpp context.md`
  passed.

May 21 bbox target to ~1.79B with rendered bundle clearance:
- Re-analysed the proper ERD `log.txt` for the 1291-node captain graph.
  The final fallback was visually clean but huge:
  `bbox=10.33B`, `visualCrossings=884`, `edgeNodeIntersections=187`,
  `bundleNodeOverlaps=0`, `nodeOverlaps=0`.
- The rejected bbox candidates showed that 0.70 was the useful compression
  target, but it was blocked by residual rendered bundle-node clashes after
  bundle relocation.
- Added a target-stage after-relocate bundle-node clearance pass in
  `native/ogdf-layout/src/main.cpp`. It now:
  - measures the base with rendered carrier metrics,
  - screens candidate bundle offsets with quick geometry,
  - runs precise rendered metrics only for a small shortlist,
  - repeats up to 4 passes so multiple tiny residual bundle-node contacts can
    be cleared sequentially.
- Bbox-target defaults in `src/extension/services/layout/runOgdfLayout.ts`
  now include after-relocate clear passes/cache keys and lower target relief
  to 1 pass; the stronger 10-pass relief was moving many nodes and worsening
  visual crossings for compressed candidates.
- Replayed `safety=0.70` with the app-equivalent clean env:
  `bbox=1.792B`, `visualCrossings=1160`, `edgeCrossings=571`,
  `edgeNodeIntersections=571`, `bundleEdgeIntersections=18`,
  `bundleNodeOverlaps=0`, `nodeOverlaps=0`, `aspectRatio=1.63`.
  This is ~83% smaller than the 10.33B fallback, trading +276 visual
  crossings for the much smaller bbox.
- Replayed `safety=0.62`: `bbox=1.444B`, but `visualCrossings=1368` and
  `bundleNodeOverlaps=2`, so it remains rejected. The default safety order
  still tries 0.62 first, then accepts 0.70 under the new gate.
- Bbox-target visual accept default is now `1200` so the 0.70 candidate is
  accepted; this keeps the 0.62 candidate rejected.
- `node scripts/build-ogdf-binary.mjs`, `npm run build`, and
  `git diff --check -- src/extension/services/layout/runOgdfLayout.ts native/ogdf-layout/src/main.cpp`
  passed.

May 21 follow-up: simultaneous bbox + visual improvement:
- Important replay correction: use the preserved 1291-node / 1554-edge input
  at `/private/tmp/djerd-v36-latest-replay/{nodes,edges}.tsv`, not
  `data/erd-poc/graphs/real-main` (1250/1484). The latter produces misleading
  bbox/visual numbers for this run.
- With correct `edge-routing=straight`, the previous target result was
  `safety=0.70`, `bbox=1.773B`, `visualCrossings=1088`,
  `edgeCrossings=513`, `edgeNodeIntersections=553`,
  `bundleEdgeIntersections=22`, `bundleNodeOverlaps=0`,
  `nodeOverlaps=0`, `aspectRatio=1.613`.
- Tightening only the sidecar X gap from `220` to `160` kept visual unchanged
  and reduced bbox slightly to `1.769B`.
- The after-relocate bundle-node clear pass was missing the better `-120px`
  clearance candidate because quick shortlist ties preferred smaller moves.
  Changed that tie-break to prefer larger clearance moves when quick metrics
  are otherwise equal. This kept the shortlist small but selected
  `offset=(-120,0)` instead of `(-64,0)`.
- New replay after rebuild:
  `bbox=1.769B`, `visualCrossings=1086`, `edgeCrossings=512`,
  `edgeNodeIntersections=552`, `bundleEdgeIntersections=22`,
  `bundleNodeOverlaps=0`, `nodeOverlaps=0`, `aspectRatio=1.609`.
- Updated extension defaults/cache keys:
  isolated compact gap `180/36`, offsetY `140`; sidecar gap `160/120`;
  bbox-target accept visual gate `1100`.
- Widening target bundle-box relocate to `PASSES=2`, `TOP=20`,
  `MAX_CANDIDATES=64`, `SHORTLIST=8` is a larger win: replayed 0.70 gives
  `bbox=1.769B`, `visualCrossings=997`, `edgeCrossings=457`,
  `edgeNodeIntersections=526`, `bundleEdgeIntersections=14`,
  `bundleNodeOverlaps=0`, `nodeOverlaps=0`, `routeSegments=897`.
  The lower 0.66 interpolation remains rejected (`bbox=1.346B`,
  `visualCrossings=1200`), so the 0.70 candidate stays the useful default
  under the `1100` gate.
- `node scripts/build-ogdf-binary.mjs` and `npm run build` passed.

May 22 density/spacing follow-up:
- Analysed the latest proper ERD `log.txt`. The accepted app candidate was
  `safety=0.880`, `bbox=1.90B`, `visualCrossings=997`,
  `edgeNodeIntersections=456`, `bundleNodeOverlaps=0`,
  `nodeOverlaps=0`, but `nodeSpacingOverlaps=30`. Rejected denser
  candidates (`0.62`, `0.70`) had smaller bboxes but too many visual/node
  contacts, confirming that the pipeline had no local density/cluster-margin
  objective.
- Added rendered-density diagnostics/helpers and an opt-in
  `DJERD_DENSITY_BALANCE_FINAL` cluster expansion pass. Replay showed this
  early expansion runs before bundle relocation creates the final dense
  pockets, so it is disabled by default (`0`) and kept as a knob.
- Added a late `node-spacing-clear-final` pass after bundle relocation and
  after-relocate bundle clearance. It separates spacing-buffer overlaps on
  non-bundle rendered nodes, reroutes, and accepts only when spacing improves
  without hard node/bundle overlap, bbox, visual, or edge-node regression.
  Defaults: enabled, 6 passes, max shift 220, extra 8, visual slack 80,
  edge-node slack 80, bbox limit 1.025.
- Replayed the recent `safety=0.880` position file with a temporary bundle
  relocate cap of 768 candidates. The new spacing pass accepted:
  `nodeSpacingOverlaps 349 -> 149`, `visual 1188 -> 1187`,
  `edgeNode 554 -> 551`, bbox unchanged. However the cap left
  `bundleNodeOverlaps=2`, so the cap is not a safe quality default.
- Kept the bundle-relocate total-limit knob but defaulted it to `2560` to
  preserve the prior bundle-node clearing behavior. Increased after-relocate
  clear breadth to `TOP=6`, `MAX_CANDIDATES=24`, `SHORTLIST=5` so residual
  bundle-node contacts have a better chance to clear after capped experiments.
- Rebuilt `bin/ogdf/darwin-arm64/django-erd-ogdf-layout`; `npm run build`
  and `git diff --check -- native/ogdf-layout/src/main.cpp
  src/extension/services/layout/runOgdfLayout.ts context.md` passed.

May 22 bbox-target timeout/fallback follow-up:
- Re-analysed the proper `log.txt`: the v36 post-stack cluster result reached
  `bbox=9.78B`, `visualCrossings=833`, `edgeNodeIntersections=196`, but the
  first bbox target candidate was `safety=0.620`. It consumed the full 600s
  timeout and escaped the bbox loop, causing the extension to discard the
  cluster/bundle result and fall back to exact FMMM (`leafBundles=0`,
  `visualCrossings=43284`).
- Changed bbox-target default safety order to least-aggressive first:
  `0.99,0.88,0.70,0.62`.
- Added `DJERD_OPTIMIZED_BBOX_TARGET_TIMEOUT_MS` with a 180s default and
  candidate-local failure handling. A timed-out or invalid candidate now logs
  the failure and tries the next safety instead of aborting the whole ML
  reroute. If no bbox candidate is accepted, the successful v36 post-stack
  reroute is kept rather than falling back to exact/FMMM.
- `npm run build` passed after the TypeScript change.

May 22 density-pack / sparse-band compaction follow-up:
- Re-analysed the latest `log.txt`: final visual quality was acceptable
  (`visualCrossings=884`, `edgeCrossings=685`, `edgeNode=187`,
  `bundleEdge=12`, `bundleNode=0`, `nodeOverlaps=0`) but bbox was still large
  (`~10.31B`) and viewport frame density showed both empty areas and dense
  pockets. The existing spacing metric only catches box overlaps, not visual
  density balance.
- First attempted center-pull group packing. Even when bundle/cluster groups
  were moved rigidly, it caused node overlaps and visual regression, so it is
  not a safe default.
- Reworked late `density-pack-final` to fold only globally empty rendered
  X/Y bands. It builds rigid pack groups from leaf-bundles, cluster IDs, and
  singleton nodes, scores with rendered node/bundle boxes, and does not break
  relationships or route edges through artificial carriers.
- Defaulted the pass to safe sparse-band mode: `DJERD_DENSITY_PACK_TOP=0`,
  `DJERD_DENSITY_PACK_CLEANUP=0`, `DJERD_DENSITY_PACK_EMPTY_BAND_KEEP=720`,
  scales `0.94,0.90,0.86,0.82,0.78,0.72`. Dense expansion and cleanup remain
  available only as explicit knobs because they increased overlaps in replay.
- Added `DJERD_SKIP_CG_OPT=1` to optimized post-stack reroute env. Since
  v36 positions overwrite the cluster-graph coordinates, this skips wasted
  §13/§14/§15 position passes and reduced the replay reroute from ~2.5min+
  to ~17-18s without changing the post-position pass stack.
- Extension-env replay with final rendered-carrier metrics:
  density-pack OFF `bbox=10.27B`, `visual=826`, `edgeCross=644`,
  `edgeNode=176`, `bundleEdge=6`, `bundleNode=0`, `nodeOverlaps=0`,
  `nodeSpacing=16`.
  density-pack ON accepted scale `0.72`: `bbox=8.55B`, `visual=837`,
  `edgeCross=652`, `edgeNode=178`, `bundleEdge=7`, `bundleNode=0`,
  `nodeOverlaps=0`, `nodeSpacing=16`. Tradeoff: ~16.8% bbox reduction for
  +11 visual crossings, still below the earlier `log.txt` visual 884.
- `node scripts/build-ogdf-binary.mjs`, `npm run build`, and
  `git diff --check -- native/ogdf-layout/src/main.cpp
  src/extension/services/layout/runOgdfLayout.ts context.md` passed.

May 22 bbox-target residual clash / soft accept follow-up:
- Re-analysed the latest proper `log.txt`. The v36 post-stack result was
  usable but still wide: `bbox=5.37B`, `visualCrossings=549`,
  `edgeNodeIntersections=161`, `bundleNodeOverlaps=0`,
  `nodeSpacingOverlaps=3`. The bbox-target loop then spent four 60s
  timeouts and rejected all candidates, so the app kept the wide result.
- The slow part was not the core reroute. Replaying bbox target candidates
  with the extension bbox env and expensive bundle relocation disabled runs
  in about 4.5s per candidate. The previous timeout came from
  bbox-target-only bundle relocation / after-relocate clearance.
- Added final rendered leaf-bundle external-node push:
  `clearLeafBundleExternalNodeMargins`. It pushes globally non-absorbed nodes
  out of rendered leaf-bundle boxes, then runs node-overlap repair before
  measuring. This fixes the observed residual `db.Hrm` bundle clash with
  `db.EmployeeDepartmentCodeRelation` without moving relationships into
  carriers or badges.
- Extension defaults now keep bbox-target bundle relocate and after-relocate
  clear disabled (`DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE=0`,
  `DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE=0`) and enable
  the cheap final node push (`DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODES=1`).
- Added bbox-target soft acceptance in `runOgdfLayout.ts`: the hard 2.0B
  target remains, but a candidate can now be accepted when it is under
  `DJERD_OPTIMIZED_BBOX_TARGET_SOFT_B=2.5` and improves area by at least
  `DJERD_OPTIMIZED_BBOX_TARGET_ACCEPT_MIN_GAIN=0.5`. This prevents the app
  from discarding a large safe bbox reduction just because it missed the
  strict 2.04B hard cap.
- Added `DJERD_OPTIMIZED_BBOX_TARGET_MAX_NODE_SPACING=80` to the bbox-target
  accept gate. This rejects visually cramped candidates such as safety 0.99
  even if their bbox is below 2B.
- Correct replay condition note: include the extension-off switches
  `DJERD_FACE_RASTER=0`, `DJERD_HOT_REGION_SA=0`,
  `DJERD_STUCK_LEAF_2D=0`, `DJERD_XINGS_DETOUR=0`,
  `DJERD_LEAF_PASSES=1`, and `DJERD_LEAF_PASSES_2=0`; otherwise direct
  binary replay runs old experimental passes and gives misleading bbox.
- Current replay results on preserved inputs:
  safety 0.99 -> `bbox=1.95B`, `visualCrossings=812`,
  `bundleNodeOverlaps=1`, `nodeSpacingOverlaps=464`, reject.
  safety 0.88 -> `bbox=2.38B`, `visualCrossings=681`,
  `edgeCrossings=300`, `edgeNodeIntersections=360`,
  `bundleEdgeIntersections=21`, `bundleNodeOverlaps=0`,
  `nodeOverlaps=0`, `nodeSpacingOverlaps=14`, accept by soft bbox gate.
- `node scripts/build-ogdf-binary.mjs`, `npm run build`, and
  `git diff --check -- src/extension/services/layout/runOgdfLayout.ts
  native/ogdf-layout/src/main.cpp context.md` passed.

May 22 bbox-target 2B-under follow-up:
- User correctly noted there was still room to improve. The previous change
  accepted safety 0.88 (`bbox=2.38B`, `visualCrossings=681`,
  `bundleNodeOverlaps=0`, `nodeSpacingOverlaps=14`), but that still missed
  the 2B bbox goal.
- Replayed actual current post-stack output (`bbox=5.374B`,
  `visualCrossings=534`, `bundleNodeOverlaps=0`, `nodeSpacingOverlaps=3`)
  and generated bbox-target positions using the same extension scaling
  formula.
- Important replay results:
  safety 0.99 -> `bbox=1.95B`, `visualCrossings=812`,
  `bundleNodeOverlaps=1`, `nodeSpacingOverlaps=464`, reject.
  safety 0.82 -> `bbox=1.72B`, `visualCrossings=769`,
  `edgeCrossings=376`, `edgeNodeIntersections=367`,
  `bundleEdgeIntersections=26`, `bundleNodeOverlaps=0`,
  `nodeOverlaps=0`, `nodeSpacingOverlaps=114`, accept.
  safety 0.78 -> `bbox=1.63B`, `visualCrossings=951`, reject.
  safety 0.74 -> `bbox=1.95B`, `visualCrossings=850`, reject.
- Stronger node-spacing clear on the 0.99 candidate reduced spacing only to
  188 and increased bundle-node overlaps (`1 -> 3`) while taking ~33s, so it
  is not a good default direction.
- Historical bbox-target experiment:
  default safeties were changed to `0.99,0.82,0.88,0.70,0.62`;
  `DJERD_OPTIMIZED_BBOX_TARGET_MAX_VISUAL=800` and
  `DJERD_OPTIMIZED_BBOX_TARGET_MAX_NODE_SPACING=128` were tried as fixed caps.
  Those fixed caps were later removed from defaults because they overfit the
  current Captain graph scale.
- Added `bundleNode` and `nodeOverlaps` to node-spacing-clear accept/reject
  logs, so future rejects explain which hard visual constraint failed.
- `node scripts/build-ogdf-binary.mjs`, `npm run build`, and
  `git diff --check -- src/extension/services/layout/runOgdfLayout.ts
  native/ogdf-layout/src/main.cpp context.md` passed after these changes.

May 22 staged bbox-target follow-up:
- Latest `log.txt` showed the previous single-shot bbox-target path applied
  the new safeties but still rejected every candidate. Starting from
  `bbox=8.17B`, direct compression to about 2B produced:
  `0.99 -> bbox=2.23B visual=1128 spacing=142`,
  `0.82 -> bbox=1.81B visual=1423 spacing=324`,
  `0.88 -> bbox=1.96B visual=1134 bundleNode=3`,
  `0.70 -> bbox=1.84B visual=1292`,
  `0.62 -> bbox=1.43B visual=1323 bundleNode=3`.
  None passed the visual/bundle/spacing gates, so the app kept the 8.17B
  post-stack result.
- Implemented staged bbox-target compression in `runOgdfLayout.ts`.
  Defaults:
  `DJERD_OPTIMIZED_BBOX_TARGET_STAGES_B=5.0,3.5,2.5,2.0`,
  `DJERD_OPTIMIZED_BBOX_TARGET_SAFETIES=0.99,0.94,0.90`.
- Each stage scales from the last accepted layout, not from the original
  post-stack layout. If a stage has no accepted candidate, staged compression
  stops and keeps the last accepted stage rather than trying a more aggressive
  target.
- Absolute visual/spacing defaults were later replaced by normalized debt
  metrics: stage/final quality debt per bbox gain and stage/final spacing debt
  per bbox gain. `DJERD_OPTIMIZED_BBOX_TARGET_MAX_VISUAL` and
  `DJERD_OPTIMIZED_BBOX_TARGET_MAX_NODE_SPACING` are now optional explicit caps
  rather than default gates.
- Cache-key parts now include staged target/safety/debt defaults. Build and
  whitespace checks passed: `npm run build` and
  `git diff --check -- src/extension/services/layout/runOgdfLayout.ts
  native/ogdf-layout/src/main.cpp context.md`.

May 22 edge-node polish variant split:
- Latest `log.txt` showed staged bbox compression worked and kept
  `bbox=3.47B`, `visualCrossings=887`, `edgeNodeIntersections=365`,
  `bundleNodeOverlaps=0`, `nodeOverlaps=0`, but the old edge-node polish
  worsened to `edgeNode=398`, `visual=991` and was correctly rejected.
- Root cause: polish reused the bbox-target post-stack env, so it reran
  broad actions (`leaf-untangle`, isolated attach, sidecar compaction, etc.)
  against an already accepted layout.
- Implemented polish variants instead of globally removing possibilities:
  `DJERD_OPTIMIZED_EDGE_NODE_POLISH_VARIANTS=local,holistic` by default.
  `local` is a narrow edge-node/bundle/node-overlap cleanup candidate;
  `holistic` keeps the wider post-stack path as a separate candidate.
  Both are measured and accepted only by normalized gain/debt gates.
- Added per-variant logging:
  `[edge-node polish:local] ...` and `[edge-node polish:holistic] ...`.
- Local polish explicitly disables broad/experimental native defaults such as
  isolated stash, face raster/untangle, hot-region SA, stuck-leaf 2D,
  xings detour, visual-knot, density pack/balance, sidecar/isolated compaction,
  and leaf untangle.
- Fixed native `DJERD_LEAF_PASSES=0`: C++ previously forced at least one
  leaf-untangle pass with `max(1, value)`. It now allows `0` and skips the
  pass. This makes the local polish variant actually local.
- Manual native replay on preserved inputs confirmed the local env no longer
  runs leaf-untangle/detour/face/isolated-stash paths. The replay started from
  an intermediate TSV, not the exact final accepted JSON, so its quality
  numbers are only an env sanity check, not a final quality benchmark.
- Rebuilt bundled native binary with `node scripts/build-ogdf-binary.mjs`.
  Verification passed: `npm run build` and
  `git diff --check -- src/extension/services/layout/runOgdfLayout.ts
  native/ogdf-layout/src/main.cpp`.

May 22 relative bbox-target stages:
- Latest `log.txt` after polish variant split produced a better quality
  layout but with larger bbox:
  `visualCrossings=767`, `edgeNodeIntersections=193`,
  `bundleNodeOverlaps=0`, `nodeOverlaps=0`, `bbox≈7.75B`.
- Fixed-stage bbox compression tried `5.00B` immediately and rejected all
  safeties because quality debt was too high:
  `5.35B visual=934 edgeNode=326`,
  `5.19B visual=909 edgeNode=321`,
  `4.87B visual=935 edgeNode=345`.
- Changed default bbox stages from fixed `5.0,3.5,2.5,2.0` to relative
  stages based on the current accepted bbox. Default ratios:
  `DJERD_OPTIMIZED_BBOX_TARGET_STAGE_RATIOS=0.86,0.74,0.64,0.55,0.45,0.36,0.28`.
  For a `7.75B` base this starts around `6.67B`, then `5.74B`, then
  `4.96B`, avoiding a first-step cliff to `5B`.
- Explicit `DJERD_OPTIMIZED_BBOX_TARGET_STAGES_B` still overrides the
  relative schedule, so experiments can force absolute stages when needed.
- Added a stage-plan log line:
  `[bbox target] stages=... · current=... · final=...`.
- Verification passed: `npm run build` and
  `git diff --check -- src/extension/services/layout/runOgdfLayout.ts`.

May 22 bbox-target variant split:
- The relative stage schedule still rejected the first stage because the only
  bbox-target candidate reused the broad post-stack env. The first `6.67B`
  stage improved bbox but increased visual/edge-node debt too much, so no
  candidate was accepted and the layout stayed at `bbox≈7.75B`.
- Split bbox-target candidates into variants:
  `DJERD_OPTIMIZED_BBOX_TARGET_VARIANTS=local,holistic` by default.
- `local` scales the accepted positions and keeps only route sync plus
  collision cleanup families: node-edge relief, leaf-bundle/node clear,
  node-overlap clear, and node-spacing clear. It disables broad/shape-changing
  actions such as leaf untangle, isolated attach/compaction, sidecar compact,
  density pack/balance, bundle relocate, rigid compaction, face/SA/stuck-leaf,
  visual knot, and detour.
- `holistic` preserves the previous broad bbox-target post-stack as a fallback,
  so we do not close off that possibility globally.
- Each stage/safety now writes the scaled TSV once, then evaluates variants in
  order. Logs are variant-tagged as `[bbox target:local]` and
  `[bbox target:holistic]`. The first accepted variant advances the stage.
- Cache key now includes `optimizedBboxTargetVariants`.
- Verification passed: `npm run build` and
  `git diff --check -- src/extension/services/layout/runOgdfLayout.ts`.

May 22 bbox-target partial accept:
- Latest `log.txt` confirmed `DJERD_OPTIMIZED_BBOX_TARGET_VARIANTS=local,holistic`
  was active, but every first-stage candidate still rejected. `local` behaved
  better than `holistic` but missed the hard `6.67B` target:
  `0.990 -> bbox=7.48B visual=787 edgeNode=226`,
  `0.940 -> bbox=7.19B visual=823 edgeNode=256`,
  `0.900 -> bbox=6.88B visual=830 edgeNode=261`.
- Added partial accept for bbox-target:
  `DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_ACCEPT=1` by default.
- A candidate can now be accepted as `bboxOk=partial` when it does not reach
  the current stage target but still reduces bbox by at least the normalized
  gain threshold and keeps absolute normalized quality/spacing debt below
  limits.
- Defaults:
  `DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_MIN_GAIN=0.025`,
  `DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_MAX_QUALITY_DEBT=0.04`,
  `DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_MAX_SPACING_DEBT=0.01`.
- This is intended to accept the previous `local 0.990` style small step while
  still rejecting the more damaging `local 0.940/0.900` and holistic candidates.
- Candidate logs now include `qualityDebt`, `spacingDebt`, `partialOk`, and
  `bboxOk=partial` when that path accepts.
- Cache key now includes the partial-accept knobs.
- Verification passed: `npm run build` and
  `git diff --check -- src/extension/services/layout/runOgdfLayout.ts context.md`.

May 22 bbox-target gap-compression strategy:
- Latest `log.txt` showed partial accept did not fire. The first `local 0.990`
  candidate only reached `bbox=8.13B` from an `8.17B` base, with
  `visual=869`, `edgeNode=241`, `qualityDebt=0.070`, and `bboxGain=0.005`.
  Uniform scaling was being mostly undone by cleanup/reroute.
- Added bbox position-generation strategies:
  `DJERD_OPTIMIZED_BBOX_TARGET_POSITION_STRATEGIES=gap,scale` by default.
- `gap` closes large empty x/y bands between occupied node intervals while
  preserving node ordering and internal cluster distances. It estimates the
  requested target dimensions, reduces only reducible empty gaps, and keeps a
  minimum gap derived from median node size:
  `DJERD_OPTIMIZED_BBOX_TARGET_GAP_MIN_FACTOR=0.85`.
- Optional absolute overrides exist for experiments:
  `DJERD_OPTIMIZED_BBOX_TARGET_GAP_MIN_X` and
  `DJERD_OPTIMIZED_BBOX_TARGET_GAP_MIN_Y`; default is `auto`.
- `scale` remains as fallback, so the old uniform-scale candidate is still
  available after `gap`.
- Candidate logs now include `strategy=gap|scale`, and the pre-reroute log for
  `gap` includes estimated bbox, requested bbox, x/y reduction ratios, and gap
  counts.
- Cache key now includes the position strategy and gap-min knobs.
- Verification passed: `npm run build` and
  `git diff --check -- src/extension/services/layout/runOgdfLayout.ts`.

May 22 gap-compression log result and next step:
- Latest `log.txt` confirmed `gap` strategy is effective.
- Final accepted layout:
  `bbox≈3.65B`, `visualCrossings=873`, `edgeNodeIntersections=294`,
  `nodeSpacingOverlaps=12`, `bundleNodeOverlaps=0`, `nodeOverlaps=0`.
- Runtime was high: `OGDF layout completed in 243155ms`.
- Accepted bbox-target progression:
  `8.17B -> 7.69B` via `gap` partial accept
  (`visual=819`, `edgeNode=215`),
  `7.69B -> 6.17B` via `gap` hard accept
  (`visual=867`, `edgeNode=207`),
  `6.17B -> 5.44B` via `gap` partial accept
  (`visual=859`, `edgeNode=226`),
  `5.44B -> 4.06B` via `scale` hard accept
  (`visual=917`, `edgeNode=285`),
  `4.06B -> 3.65B` via `scale` hard accept
  (`visual=873`, `edgeNode=294`).
- The `2.94B` stage rejected all candidates. Candidates that reached
  `bbox≈2.66B-3.05B` had too much quality debt, e.g. `visual≈1075`,
  `edgeNode≈442`, or high spacing debt. Current normalized gates therefore
  place the stable limit around `3.65B` for this run.
- Important interpretation:
  `gap` works well down to about `5.44B` while preserving visual quality.
  After that, x-axis gaps are exhausted (`xReduce=0`) and gap compression only
  squeezes y bands. Below about `4.5B`, uniform `scale` becomes the accepted
  route, but it increases edge-node intersections (`226 -> 294`).
- The next useful step is not more bbox pressure. It is a post-compression
  local repair stage that starts from the accepted `3.65B` layout and tries to
  reduce edge-node intersections back toward `230-250` without allowing much
  bbox growth and without relation-breaking tricks.
- Runtime issue:
  The failed `2.94B` stage evaluated many expensive candidates, and both
  edge-node polish variants later rejected after spending roughly another
  47 seconds. Add an early-stop / candidate-budget policy after a stage shows
  repeated high quality debt, and consider limiting polish after aggressive
  bbox compression unless a cheap local candidate passes first.

May 23 bbox-target stage bail-out + cheap polish variant:
- Added per-stage bail-out in `src/extension/services/layout/runOgdfLayout.ts`.
  After N consecutive candidates whose `qualityDebt` is well above the partial
  acceptance floor (default `3x`), the staged compression breaks out of the
  current stage instead of finishing all safety×strategy×variant combinations.
  Defaults:
  `DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_AFTER=3`,
  `DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_QUALITY_RATIO=3.0`,
  `DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_SPACING_RATIO=3.0`,
  `DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_NEAR_RATIO=1.5`.
  Reset on any "near acceptance" candidate (debt within `1.5x` of floor,
  no node/bundle overlap regression).
- Added `cheap` polish variant ahead of `local`/`holistic`. Defaults:
  `DJERD_OPTIMIZED_EDGE_NODE_POLISH_VARIANTS=cheap,local,holistic`.
  The `cheap` variant disables bundle relocate, after-relocate clear, overlap
  clear, leaf passes, isolated stash/attach/compact, density/sidecar/axis
  passes, knot/detour. It runs only 1 narrow edge-node relief pass with
  `STRENGTH=0.6`, `MAX_SHIFT=80`, `ENDPOINTS=0`. Knobs override:
  `DJERD_OPTIMIZED_EDGE_NODE_POLISH_CHEAP_RELIEF_PASSES`,
  `..._RELIEF_MAX_SHIFT`, `..._RELIEF_STRENGTH`, `..._ENDPOINTS`.
- Added polish variant skip-on-visual-blowup: if a rejected candidate's
  `visualCrossings >= base * 1.15`, skip subsequent broader variants. Knob:
  `DJERD_OPTIMIZED_EDGE_NODE_POLISH_SKIP_ON_VISUAL_RATIO=1.15`.
- Cache key now includes the new bail-out and polish variant knobs.
- `npm run build` passed. `git diff --check
  -- src/extension/services/layout/runOgdfLayout.ts context.md` passed.
- Expected savings on the May 22 log shape:
  ~10 candidates × ~4s on the 2.94B stage = ~40s recovered by bail-out
  after 3 bad candidates.
  Polish stage: 22s+25s with both variants → 1 cheap candidate (~5-8s) +
  skip; if cheap also blows up visual, total is ~5-8s instead of ~47s.

May 23 bail-out policy correction:
- First May 23 log showed bail-out was too aggressive: stage 4.50B bailed at
  candidate 4 even though candidate 3 (local scale safety=0.990) achieved
  `bbox=4.47B` hard hit with `qualityDebtPerGain=0.748` (only 1.87x the stage
  limit 0.40). The old absolute-floor metric flagged it as `3.35x` and
  contributed to the "far" streak. Runtime dropped from 243s to 129s, but
  the final layout regressed to `bbox=5.44B`, missing the previously best
  `3.65B`. Other quality metrics improved slightly (visual `873 -> 859`,
  edgeNode `294 -> 226`).
- Switched bail criterion to per-gain ratios against the stage's accept
  limit (`bboxQualityDebtPerGain / bboxQualityDebtLimit` and
  `bboxSpacingDebtPerGain / bboxSpacingDebtLimit`), not absolute partial
  floors. Candidates that hit bbox-hard with moderate per-gain debt are no
  longer counted as "far".
- Raised defaults to be conservative against this case:
  `STAGE_BAIL_AFTER=6` (was 3),
  `STAGE_BAIL_QUALITY_RATIO=5.0` (was 3.0),
  `STAGE_BAIL_SPACING_RATIO=10.0` (was 3.0, spacing peaks when bboxGain≈0
  so it skewed the old threshold),
  `STAGE_BAIL_NEAR_RATIO=2.0` (was 1.5).
- Replay trace on the May 23 4.50B stage with new policy: bail-out never
  fires; the search would continue through all safeties and pass at
  safety=0.900 scale, the same pattern as May 22.
- Replay trace on the May 22 2.94B stage (all-reject stage): bail-out fires
  after candidate 9 instead of 12, saving ~3 candidates × ~4s.
- Polish cheap variant now keeps `DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL=1`.
  The cheap variant produced `bundleNode 0 -> 6` regression in the May 23
  log because relief moved nodes into rendered bundle boxes without
  clearance. The bundle clear pass is metric-gated and cheap, so enabling
  it preserves bundleNode without touching anything else.
- Polish skip-on-blowup gate now also triggers when the candidate shows both
  bundleNode regression (over `polishMaxBundleNode`) AND edge-node
  regression — a clear sign the variant is making things worse.
- `npm run build` passed. `git diff --check -- src/extension/services/layout/runOgdfLayout.ts` passed.

May 23 verification on live app log after policy correction:
- Stage 4.50B now correctly runs all 6 safety×strategy combinations and
  accepts at `safety=0.900 strategy=scale`, then 3.68B accepts at
  `safety=0.990 strategy=scale` and reaches the previously-best `bbox=3.65B`.
- Stage 2.94B (all-reject stage) bails out after 6 bad candidates instead
  of attempting all 12, saving ~24s on this stage.
- Polish: `cheap` ran in 2.9s with `bundleNode=0` (no regression thanks to
  re-enabled bundle clear). `local` blew up visual `873 -> 1108` (ratio
  1.27 above the 1.15 skip threshold), so `holistic` was skipped.
- Final state matches May 22 quality: `bbox=3.65B`, `visualCrossings=873`,
  `edgeCrossings=555`, `edgeNodeIntersections=294`, `nodeOverlaps=0`,
  `bundleNodeOverlaps=0`, `nodeSpacingOverlaps=12`.
- Runtime: **162s** vs May 22's **243s** (-33%, ~81s saved) for identical
  final layout quality. Savings sources:
  bail-out on 2.94B (~24s), polish holistic skip (~22s), and the cheap
  polish replacing one heavy local-style run (~3s vs ~22s).

May 23 density-aware non-uniform scale + 1B target:
- User flagged remaining gaps: hotspot vs sparse density still uneven, and
  ~1B bbox should be achievable. Explicit constraint: stay generic so the
  changes apply to any ERD project, not just Captain.
- Added a new bbox-target position strategy `density-scale`. It is generic
  by construction:
  - Cell size = `median(node size) × DENSITY_CELL_FACTOR` (default 4×); the
    bin mesh adapts to any graph automatically.
  - Sparse/dense bins are decided by a fraction of the bin-density median
    (`DENSITY_SPARSE_RATIO`, default 0.5), not by an absolute node count.
  - Bias (`DENSITY_BIAS`, default 0.7) controls how aggressively sparse
    bins shrink relative to dense bins. 0 falls back to uniform scaling;
    1 lets sparse bins compress fully toward the target while dense bins
    stay near 1.
  - No cluster IDs, model names, or absolute coordinates referenced. The
    algorithm uses only the relative distribution of node centers from the
    input layout.
- Default position-strategy order is now `gap,density-scale,scale` so the
  density-aware path is tried before falling back to uniform scale.
- Bbox final target lowered from `2.0B` to `1.0B`
  (`DJERD_OPTIMIZED_BBOX_TARGET_B`) and stage ratios extended to
  `0.86,0.74,0.64,0.55,0.45,0.36,0.28,0.22,0.17,0.13`. Existing per-gain
  bail-out and normalized debt gates protect against runaway compression,
  so adding deeper stages is safe — anything that breaks quality is
  rejected by the same gates that already work on the higher stages.
- New env knobs:
  `DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_CELL_FACTOR=4`,
  `DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_SPARSE_RATIO=0.5`,
  `DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS=0.7`.
- Cache key now includes the strategy list and density knobs.
- Generalization check: searched the new function region for project
  references (`modelId`, `captain`, `louvain`, `cluster`, specific node
  names) — only one comment uses the descriptive word "neighborhood-scale"
  to explain the cell factor; no hard dependency on any clustering output,
  no hardcoded coordinates, no per-graph constants. Same code runs on a
  100-node ERD or a 5000-node ERD with the same logic, just different bin
  counts.
- `npm run build` passed. `git diff --check
  -- src/extension/services/layout/runOgdfLayout.ts context.md` passed.
- Next step: run the app and read `log.txt` to verify the density-scale
  candidates appear, see whether deeper stages (below 2B) now accept, and
  measure runtime/visual/edge-node/bundle. If density-scale dominates and
  uniform scale stays useless, prune the strategy list later.

May 23 density-scale renorm bug + 14-minute hang follow-up:
- First live run showed two regressions:
  - Runtime exploded to 984836ms (~16.4 min) because one bbox-target
    candidate at stage 2.94B / safety=0.990 / gap / local hung for 819s.
    The 60s timeout sent SIGTERM but the native binary kept running.
  - No `density-scale` candidate appeared in any stage log — they were
    silently skipped because the function returned `undefined`.
- Root cause of the missing density-scale:
  the renorm formula was off by a factor of `realCellSize`. With
  `renorm = targetLength / totalFactor`, the computed `newLength` came
  out as `realCellSize × targetLength`, larger than the original length,
  so `reduction=0` and the caller treated the candidate as a no-op and
  returned `undefined`.
- Fixed: `renorm = targetLength / (realCellSize × totalFactor)`. Now
  `sum(scaled[i] × realCellSize) = targetLength` as intended.
- Root cause of the hang:
  Node's `execFile` timeout sends `SIGTERM` (default) and resolves only
  when the child's stdio drains. A native binary that ignores SIGTERM
  keeps generating output and the parent waits indefinitely.
- Fix: `execFileAsync` now (1) accepts a `killSignal` option, and the
  bbox-target / polish call sites pass `"SIGKILL"`, and (2) wires a
  hard wall-clock watchdog: `setTimeout(timeout + 5s)` that calls
  `child.kill("SIGKILL")` independently of execFile's internal timer.
  The watchdog uses `.unref()` so it doesn't keep the event loop alive.
- `npm run build` passed. The next live run should show `density-scale`
  candidates in the bbox-target log lines, and no candidate should ever
  exceed `timeout + ~5s` of wall-clock.

May 23 density-scale auto-bias + uniform-distribution fallback:
- Before the next live verification, hardened density-scale to behave
  sensibly on arbitrary ERD graphs, not just Captain. Two additions:
  - Auto bias (default `DENSITY_BIAS=auto`): density-scale measures the
    bin coefficient of variation (CV) on each axis, takes the max, and
    interpolates bias between `BIAS_AUTO_MIN` (default 0.4) and
    `BIAS_AUTO_MAX` (0.85) across `BIAS_CV_MIN..BIAS_CV_MAX` (0.25..1.5).
    Highly heterogeneous graphs get aggressive sparse compression;
    near-uniform graphs get a gentle setting.
  - Uniform-distribution short-circuit: when `bias=auto` and the
    cross-axis max CV is below `BIAS_CV_MIN`, the candidate is declined
    with `undefined`. This avoids wasting candidate slots on graphs where
    density-scale would degenerate to uniform scaling anyway.
- Generalization guarantees: CV is dimensionless, bias and CV thresholds
  are normalized scalars in [0, 1] or comparable ratios. No absolute
  coordinate, no node count, no cluster reference. Same defaults apply
  to a 100-node ERD and a 5000-node ERD.
- Per-candidate log line now ends with `cv=Xx.xx/Yy.yy bias=Bb.bb auto`
  (or fixed) for visibility into what auto-bias decided on the live
  graph.
- Env knobs added:
  `DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS=auto` (was 0.7),
  `..._DENSITY_BIAS_CV_MIN=0.25`,
  `..._DENSITY_BIAS_CV_MAX=1.5`,
  `..._DENSITY_BIAS_AUTO_MIN=0.4`,
  `..._DENSITY_BIAS_AUTO_MAX=0.85`.
- Cache key updated for the new knobs.
- `npm run build` and `git diff --check
  -- src/extension/services/layout/runOgdfLayout.ts` passed.

May 23 live verification of density-scale + auto-bias + SIGKILL:
- Live app run reached final layout
  `bbox=1.95B`, `visualCrossings=754`, `edgeCrossings=551`,
  `edgeNodeIntersections=196`, `bundleNodeOverlaps=0`, `nodeOverlaps=0`,
  `nodeSpacingOverlaps=12` in 210s.
- Compared to the May 22 best (`bbox=3.65B`, `visual=873`, `edgeNode=294`,
  243s), this is `-47%` bbox, `-14%` visual, `-33%` edge-node intersections.
- Density-scale candidates were the deciding accepts at stages 6.05B
  (`bbox=5.99B`, `visual=723`, `edgeNode=118`) and 2.29B (`bbox=2.22B`,
  `visual=818`, `edgeNode=205`). Other stages still went via gap or scale.
- The previous 14-minute hang did not recur. All candidates finished
  within ~5s, confirming SIGKILL + watchdog kept misbehaving native runs
  bounded.
- 1.0B target was not reached: stage 1.39B rejected every candidate.
  Closest density-scale candidate (`holistic safety=0.900`) hit
  `bbox=1.29B` but `qualityDebtPerGain=1.052` (cap 0.40) and `visual=1005`
  (754 → 1005), so the gate correctly rejected it.
- The CV/bias signal worked as designed: highly heterogeneous stages
  reported `cv=1.5-2.8` and got `bias=0.85`; near-uniform stages reported
  `cv≈0.4` and got `bias≈0.56-0.61`.
- Polish: cheap ran in 2.9s, local triggered visual blow-up
  (754→877, ratio 1.16 ≥ 1.15) and holistic was correctly skipped.

May 23 density-scale pre-balancing pass:
- User chose pre-balancing as the next direction to break past `bbox=1.95B`
  toward the user's stated 1B aspiration. The premise: stage 1.39B failed
  because density-scale could only compress sparse bins, while hotspot
  bins were still dense enough to drive visual debt past the gate.
  Spreading hotspots into nearby empty space *before* compression should
  let density-scale shave the next layer without breaking quality.
- Added optional 1D rank-uniform blend that runs inside
  `writeDensityScaledPositionsTsvForBBoxTarget` before density measurement:
  - For each axis independently, nodes are sorted by their input center
    coordinate and assigned a rank-uniform position spanning the layout
    bounding box.
  - Each node's new center is `original*(1-blend) + uniform*blend`.
  - `blend=0` keeps the layout untouched; `blend=1` produces fully
    equalized positions; the default is `0.25` to relax hotspots without
    breaking cluster shape too much.
- Pre-balancing is applied per density-scale candidate, not cumulatively
  across stages, so each candidate operates from the current accepted
  layout. CV is recomputed on the balanced positions, so auto-bias adapts.
- Generalization: pure rank-based, no model/cluster reference, axes
  independent. Same 0.25 default works on any ERD; users can disable
  with `DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_PRE_BALANCE=0` if balance
  hurts a specific graph.
- Per-candidate log now appends `preBalance=0.XX` when the pass is on.
- `npm run build` and `git diff --check` passed.
