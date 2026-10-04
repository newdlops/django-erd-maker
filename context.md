# Django ERD Maker — ML Layout Research Context

**Updated:** 2026-10-04 (extension 0.0.1069 includes latest trained checkpoint outputs 285 / 1,963; promoted best remains 285 / 1,963; active targets 150 / 750).
**Repo root:** `/Users/lky/project/django-erd-maker`
**Branch:** `multi-view`
**Test ERD:** Captain (a Django project at `/Users/lky/project/captain`)

## 2026-10-03: ML must generate the changes

### Active target: overview 150 / individual 750

The active goal is **“개요 150/ 개별보기 750을 달성해보자.”** The previous
300 / under-2,000 goal is complete; its `thresholdsMet` and `targetMet` fields
do not prove this new goal. The promoted best is now **285 / 1,963**, still well
above the new targets. Combined candidate SHA is
`4e741de39ca655c7ff042422b9407206427f89e02ecd56b7f4a9f36f6a5cd02e`.
Current sources are `.tmp/visualcross-ml-150-750-20261004/single-owner-global-walk1/overview-product1`
and `.tmp/visualcross-ml-150-750-20261004/single-owner1/individual-product1`.
Further work must retain model-generated proposals,
complete cards and relationships, valid straight boundary routes, zero spacing
violations, and the 1.5e9 area limit. Keep numerical jobs serialized, one math
thread, nice +10, and the 128 MiB process-group sampled RSS guard. Earlier
256 MiB runs below are historical; do not raise the current limit merely for
record verification. This is not a hard CPU-percentage quota.

### Current user steering: version, install, commit and push the intermediate result

The user requested updating the model and extension, followed by committing,
pushing and a version bump. Extension **0.0.1069** includes the actual final
radial NN heads: overview update **18** and source-conflict-gated individual
update **21**. Their decoded outputs are **285 / 1,963**, identical to the
preserved best geometry. This is no additional reduction in the active goal.
Both heads, fixed encoders and source decoder buffers are retained at
`data/erd-poc/checkpoints/radial-preview-20261004/`. Every one of these **39 Adam
updates and 156 probe wires** was replayed. Full Native remeasurement covered
all nine trained TRY outputs, first/last teacher updates, source controls and
the final heads: **29 measurements**. The other **140 teacher labels** were
record-replayed, not independently full-remeasured.

The latest preview pointer is now
`data/erd-poc/candidates/captain-ml-latest-checkpoints.layout.json`, SHA
`ab08dfaed8f75354f99f7f5e00697fbecbb761c04e0f806d4242169a841ea3ae`.
The earlier **287 / 1,963** review files remain under
`data/erd-poc/checkpoints/source-port-preview-20261004/previous-preview/`.
Canonical best SHA `4e741de39ca655c7ff042422b9407206427f89e02ecd56b7f4a9f36f6a5cd02e`
is unchanged. No strict-best SAVE fallback replaced the final NN output.

The installed extension includes `media/ml-preview/` with the audited layout,
both model checkpoints and a graph/dimension signature. Default FMMM straight
layout loads it only when all original models, structural relations and card
dimensions match. Explicit file overrides and other layout choices retain their
existing behavior. Normal loading from the original payload, without developer
environment variables, passed with **zero native layout calls**, both checkpoint
hashes matched, all **1,244 models / 1,727 canonical relations** retained, zero
spacing violations and both areas below **1.5e9**. Complete product-file loading,
original ports and overview→individual→overview route/metric parity also passed.
The source-conflict gate and release-loader tests passed, and the new loader's
limited typecheck passed. All 69 current TS implementation files were freshly
transformed with cached esbuild. A full project typecheck and real browser/VS Code
visual inspection remain unperformed under the previously documented limits.
Maximum completed sampled RSS was **121.0MiB**, under the unchanged **128MiB** guard.

The radial family addresses the former 0.05px adjacent-plane bottleneck: fixed
bias controls moved cards hundreds of pixels legally, but neither broad nor
source-conflict-gated learning improved the source. Of 432 eligible individual
owners, only 30 have original cross/hit pressure; in overview, 3 of 289 do.
The shared original-card phase controls preserved ties and allowed about 1.5px
of legal endpoint response, but did not improve either source. Further ML work
must use these measured limitations instead of raising resources or repeating
unchanged budgets. **The 150 / 750 goal remains active and unmet.**

### Previous app review: source-port final checkpoints

The user requested **“우선 중간 결과를 판단할수 있게 앱을 현재 최신 모델로 업데이트하자.”**
No additional training was launched for this request. The app preview now uses
the actual final trained outputs of `source-port-patch1/overview-learning1`
and `source-port-patch1/individual-feasible16`, both step 64. Native's strict-best
SAVE files still contain the sources; those files were deliberately not used
as the latest-checkpoint preview. The frozen forward outputs were independently
full-measured, matched to their recorded wires, then projected without coordinate
search or repair. The overview is **287** (190 crossings + 92 card hits + 5 bundle hits)
and the independent individual view is **1,963**. These outputs do not improve
the preserved best **285 / 1,963**.

New preview: `data/erd-poc/candidates/captain-ml-latest-checkpoints.layout.json`,
SHA `f2c090d646986401a8827c3d82f14b568fe6c3cc512761b769f502749603285b`.
Overview checkpoint SHA is `6a8e610f2f3d75c276a563897dc98c97da4cc242fbd5170bf26e8647ebd219f1`;
individual is `7e7e3f0270444cc1d983118fa4c7f9bbf1424dec56b158f2ca23a0a327a94b21`.
Both checkpoints, observations, source TSVs, frozen inference code and validation
logs are retained in `data/erd-poc/checkpoints/source-port-preview-20261004/`.
The adjacent candidate `.audit.json` and `.provenance.json` bind all artifact hashes.
New tools export one final checkpoint and materialize it without a best-state fallback:
`export_latest_patch_checkpoint.py`, `materialize_latest_patch_preview.cjs`.

F5 **Run Captain (ML latest checkpoints)** loads the preview; **Run Captain
(ML best 285-1963)** loads the unchanged best. Then run **Django ERD: Open Diagram**.
Both switches **June bundles** and **Leaf cards** off activates individual geometry.
The F5 `verify:ml-preview` task checks the frozen files and fresh app build hashes
without compiling or running ML. It passed at sampled 54.0MiB RSS.

Actual product-file loading, all geometry/relationship endpoints and full
overview→individual→overview route/metric parity passed with 1,244 models,
1,727 relationships, zero spacing violations, unchanged dimensions and the
1.5e9 area budget. The 3 existing independent-view tests passed. All 68 current
TS implementation files were rebuilt with the already cached native esbuild
0.28.2 compiler at sampled 39.6MiB RSS; no dependency was installed. This is a
transformation-only build. Full TypeScript checking repeatedly exceeded the
128MiB guard or its reduced heap; failure logs are retained and no full typecheck
success is claimed. The largest completed job sampled 112.4MiB RSS; stopped
compiler attempts sampled up to 135.0MiB before the guard killed them. The limit
was not raised. Browser setup still fails at the unsupported `node:process`
import, before opening any page, so actual viewport/interaction visual QA remains
unperformed. Targets **150 / 750 remain unmet**. The review preview remains frozen
while the active goal continues in separate research runs. The earlier source-port
research root is still not fully replayed or sealed.

### Goal continuation: complete stars and a constrained movement bottleneck

Research root: `.tmp/visualcross-ml-150-750-20261004/common-slot-anchor1/`.
The app review snapshot stays **287 / 1,963**, and the promoted best remains
**285 / 1,963**. No experimental output below replaced either artifact.

Seven terminal runs completed **195 NN updates, 780 reward probes and 47 trained
TRY outputs**. All updates, probe wires, checkpoint parameters, fixed encoders,
decoder arrays, source inputs and original source features were independently
replayed. Full Native verification covered every trained TRY plus first/last
reward updates and each zero-head control: **110 explicit measurements**.
The other **724 reward labels** were replayed from their records, not all
independently remeasured. Six runs generated 524 full Native teacher probes;
the wider original-port run generated 256 sparse teacher probes. Its eight
reward controls and all sixteen outputs matched full Native measurements.

Runs: whole-scene common-anchor slots 20 updates / 5 TRY; ordinal slots 19 / 4;
individual nearby swaps 22 / 5; overview nearby swaps 32 / 8; neighborhood
patches 21 / 5; closed-component patches with separate body and anchor heads
17 / 4; wider original-port feasible patches 64 / 16. **Accepted: zero.**
All 41 legal trained outputs still failed to improve the preserved source;
the remaining six outputs failed hard conditions. NN slot output values were
not substituted with Native's unchanged best SAVE geometry.

Read-only diagnosis matched the recorded first/last neighborhood outputs:
each had one inward endpoint, one own-card reentry and three adjacent proper
crossings (hard 8). These occurred at the cut between changed and unchanged
stars. A fixed source connected-component mask removes that cut; its anchors
use source-line reconstruction plus NN residuals, with no future-cost inputs.
Full Native regression reproduced hard 8 and measured hard 0 with the exact
same trained body translations under the component decoder. However, its
zero-head control was 2,223, and the four trained outputs were 2,243, 2,227, 2,220 and 2,217,
so the improved hard behavior did not improve the best visual score. This is
evidence for the measured outputs, not a general guarantee for cent-rounded rays.
The component regression added three full measurements. Five final frozen toy
checks reproduced both old hard-2 failures and all three new hard-0 outputs.
Their source/code bindings are retained; earlier pre-final fixture logs are
historical and are not claimed to match the final fixture's exact bytes.

The wider feasible patch revealed a stronger bottleneck: the NN body predicted
up to **298.7 pixels**, but immutable source adjacent-segment planes contracted
all sixteen trained outputs to at most **0.05 pixels** (scale 0.0001616–0.0021453).
Increasing the same model's displacement cap from 256 to 2,048 therefore did
not create a useful movement range. Do not repeat this run or simply raise its
training budget. Next investigate a source-preserving endpoint field with
shared original-card phase offsets and coincident-endpoint ties. Establish a
nontrivial legal geometric response before launching longer learning; keep
zero-head geometry identical to the source and keep Native as teacher/validator.

All numerical jobs remained serialized, one thread, nice +10, with the unchanged
128MiB sampled process-group RSS guard. Largest listed completed job sampled
**96.0MiB** (diagnostic classification); neural runs sampled at most 54.4MiB.
The app's existing F5 hash verification was rerun and passed at 54.4MiB. No
additional app build, full typecheck or real visual/browser check was performed.
Closed records: `data/erd-poc/experiments/independent-views-common-star-fields-20261004/manifest.json`.
The active **150 / 750 goal remains unmet**.

### Previous continuation: calibrated ranking and compact NN fields; 285 / 1963 preserved

Research root is `.tmp/visualcross-ml-150-750-20261004/geometry-calibration1/`.
The promoted product is unchanged; **150 / 750 remains active and unmet**.
The 14-weight temperature mixture was trained on 493 previously full-measured
legal actions, with 396 training and 97 validation rows. All **2,100 updates**,
features, split keys and selected weights (70 updates) were exactly replayed.
Validation MSE decreased from 12.73219 to 0.648059; this is a within-source
action holdout, not proof on an unseen Captain scene. Checkpoint SHA is
`da3b7bf93df788de65956534b7f228cda5f0ade10a762c59a100282e358d1ae0`.

Calibrated trajectories replayed **1,952 ranks**, every attempted action
(48 full measurements plus two sources), and 11 complete input observations.
There were no strict improvements. The static predecessor stopped after eight
TRY calls when its ninth candidate repeated a previous wire; that cause is
proved, but its 1,441 ranks and eight outcomes were not fully remeasured.

Compact actors learn **18 shared output weights** with an immutable source
separation DAG, eight active owners, original card sizes and frame. Their
235 updates, 940 reward probes, 58 trained outputs, source features and all
checkpoint arrays were fully replayed and full Native remeasured (1,000
measurements including sources). Individual unpooled outputs were all hard
illegal; two diagnosed outputs had only adjacent-edge crossings, with no
inward endpoints or own-card reentries. A red/green real-source fixture proved
that three originally coincident endpoint groups split, then remained tied
with fixed pooling in every NN forward before any Native feedback.

The tied actors added **95 updates, 380 reward probes and 23 trained outputs**.
All updates, probe wires and weights were replayed. Full Native checks covered
all 23 outputs, first/last-update probe controls (16), and two source controls;
the other 364 reward labels were replayed from recorded Native results without
independent full remeasurement. Individual tied outputs remained hard illegal;
all 16 overview outputs were legal but yielded no gain. Coincident endpoint
tying alone does not establish legality of every adjacent-edge pair.

The record is
`data/erd-poc/experiments/independent-views-calibrated-geometry-fields-20261004/manifest.json`.
New learning totals **2,430 updates**, actual TRY calls 137, independently
full-remeasured TRY outcomes 129, and full verification measurements 1,091.
Numerical work remained serialized, one math thread, nice +10, sampled
128MiB process-group RSS guard. The highest measured peak before archive work
was 83.7MiB (diagnosis); the tied runs peaked at 53.0 and 51.8MiB. Native
coordinate search and post-rejection repairs remain zero. No product code,
F5 behavior or promoted geometry changed; actual browser QA remains incomplete.
The next continuous actor needs validity for distinct adjacent endpoints as
well as coincident endpoint ties. Do not rerun the closed full-probe checks.

### Previous continuation: learned geometric event model; 285 / 1963 preserved

The CLI follow-up confirmed the exact response-body decoding error in the
current thread at 2026-10-04 07:40:20 KST, retry 1/5. Approved outside-sandbox
Doctor passed HTTP and WebSocket checks. Root cause and a memory cause remain
unproven; configuration/auth/security settings were not changed. That is closed
diagnostic evidence, not a reason to rerun scans on each goal continuation.

Research root is `.tmp/visualcross-ml-150-750-20261004/geometry-world1/`.
The combined product remains **285 / 1,963**, SHA `4e741de39ca655c7ff042422b9407206427f89e02ecd56b7f4a9f36f6a5cd02e`.
**150 / 750 is active and unmet.** No candidate was promoted in this continuation.
Source TSVs bind every physical/full center to the verified product.

New event classifiers have **1,538 parameters**, two 24-wide hidden layers per
head, float32 features and float64 weights/arithmetic. Continuous orientation
features predict line crossings; per-axis segment intervals predict card hits.
Exact event predicates are teacher labels only. Candidate ranks use NN outputs;
Native only decodes, measures and accepts/rejects. Checkpoint `training2/model.npz`
SHA is `efec2a7e722662715799ecfcc75f162d32952485d8e024ef41e8fb0037f04a3c`.
Training executes **29,920 Adam updates**: cross 19,600, hit 10,320. Selected
weights are at **10,780 / 9,030 updates**, respectively. Cross data has 58,933
rows / 47,100 training; hit 41,303 / 33,005 training. Within-source held-out
pairs have 0 classification errors (3,033 crossing / 2,332 hit rows); this is
**not an unseen Captain scene**. Synthetic held-out graphs [104,109,114,119]
have 3 crossing / 11 hit errors. All rows, folds, normalizers, initial weights,
29,920 updates, selected weights and reported validation metrics are replayed.

Seven terminal inference stages score **8,040 NN proposals** and actually TRY
**736 moves**, with **0 strict improvements**: both-view single-owner shortlists,
both-view global same-size swaps, individual local same-size swaps and both-view
same-face port policies. Port candidates also test NN-selected joint prefixes.
All scored ranks are replayed; all 736 attempted moves are independently full
Native measured, plus 7 full source controls. Original full decoder checks the
256 attempted fast port geometries; other families already use that decoder.
The maximum learned delta error among legal evaluated actions is **59.2743**,
so source classification accuracy does not establish accurate future scene
counts. No matched untrained Captain inference control was run. Learning caused
a Captain gain is not established. No global geometry minimum is established.

Successful inference peaks **54.9–111.8 MiB**, training **74.7 MiB**, optimizer
replay **81.1 MiB**, policy verification **109.8 MiB** under the unchanged
128 MiB sampled guard. The first teacher preparation expanded all pairs and was
killed at **209.9 MiB**; it produced no checkpoint. Broad-phase construction
was changed to 32-edge chunks before the successful training2. Do not repeat
that failed version or raise the guard limit. All mathematical jobs are terminal.
No product TS/CJS or C++ build changed. Actual rendered browser is still unchecked;
the previously exposed browser runtime failed initialization.

Next use the full-measured outcomes to correct/calibrate the event model, and
broaden to coordinated multi-card proposals. Do not rerun these closed schedules
or optimizer updates as new work. Archive and restore proofs are recorded in
`data/erd-poc/experiments/independent-views-geometry-world-20261004/`.

### Previous continuation: global trajectories, verified outcome learning and 285 / 1963

Previous goal turn was **progress**: 286/1963 was promoted and independently
restored. This continuation promotes **285 / 1,963**; **150 / 750 remains
active and unmet**. Root is
`.tmp/visualcross-ml-150-750-20261004/single-owner-global-walk1/`.
All six proposal stages, training and verification jobs are terminal.
Do not repeat completed schedules, optimizer updates or full reward scans.
Native only decodes, observes, measures and accepts/rejects. All coordinates
remain model-selected. No new C++ compilation or product TS changes.

New sources bind every overview/full center to the previous 286/1963 product.
Cached walk v4 uses global all-size words at each admitted pose and corrects
the sparse baseline measurement's descriptive key. It preserves critic
ef9260…, 66 amplitudes, integer-cent cumulative states and the exact observer.
Each view has a 30-second limit, 128 words, temperature 4, 32 proposals per
round and 512 maximum attempts. Individual seed **100903**: **124 proposals
/ 36 neutral states**, 106.3 MiB. Overview **101309**: **129 / 47**,
101.1 MiB. Neither improves. Independent replay checks all **253 actions**,
**85 full Native feature controls**, and **255 full Native measurements**
including final poses. All model inputs and rewards match.

Before learning, read-only Native remeasures all **2,259 inherited outcomes**:
2,048 previous global actions + 211 previous ordinary/cached walk actions.
Current-pose reward baselines match exactly. These are not new TRY commands.
Reward-control peak **53.5 MiB**.

Trainer v3 adds these 2,259 outcomes + 253 fresh trajectories to the verified
23,552-row parent dataset: **26,064 float32 3x141 rows**. Added categories
are **captain_global 2,048 / captain_walk 464**. All six old validation parts
stay identical; new whole-word holdouts contain **438 global / 83 walk rows**.
Parent-seen training words are excluded from new validation, and all Captain
training words are disjoint from all four Captain validation parts. Synthetic
holdout graphs remain **[129,132,136,138,148,152,154,155]**.
Training has **20,407 rows / 1,991 added Captain rows / 6,678 single-owner
rows**. Improving added global training rows **1**, added walk **0**;
total improving pool **1,885**. All training rows enter sampling once,
single-owner rows twice additionally, improving rows seven times additionally.
The write feature map is released before the training map is opened.

Training executes **23,488 Adam updates**, selects **epoch 1 / 734 adaptation
updates**, takes **8.274695 s**, peaks **95.9 MiB**, and changes all ten
parameter groups. Critic `training1/model.npz` SHA:
`7831d4b8679769a22e4eab8a344f846e892e5ebd52b89b68afafb8ef92c777f9`.
The raw checkpoint marker **14,195 = immediate parent 13,461 + current 734**
is not an all-ancestor lifetime count. Latest provenance separately records
734. Mean eight-part loss **2.272600→2.253497**. Captain
**2.359872→2.403754**, positive **1.611918→1.640673**, negative
**2.799006→2.731428**, mixed **2.494988→2.497006**, Captain single
**1.001306→1.040459**, synthetic single **1.158915→1.184066**, global
**3.861310→3.706799**, walk **2.893481→2.823794**. Five old parts regress.
Holdouts are words/graphs, not an independent Captain scene. Independent
verification reconstructs all rows/folds/sampling and exactly replays all
23,488 updates and saved weights, peak **96.1 MiB**. Normalization stays fixed.

Four matched global policies at **seed 102503 / 1,024 proposals each** use
identical source features, words, amplitudes, gates, Gumbel and budgets within
each view. Both individual policies stay **1963**. Both overview policies
produce **286→285** with the same wire:
`eb4748e276c370556c82d8e88367ac549a662d8c1eaa4a36b3b9bd62b179a7ed`.
Cycle **[516,852,795]**, amplitudes **[0,0,.02]**; frozen rank **610** /
score **−0.6861932938978685**, trained rank **657** /
score **0.2804742482124722**. Learning caused this gain is **not established**.
The trained winner supplies the overview; the earlier 915a… critic's
individual view remains identical. All 4,096 NN scores/schedules/actions
are replayed, with **66 full Native samples**, including both winners.
Other matched action scores are not all recomputed. Current totals:
**4,349 new proposals / 6 completed stages / 2,580 full read-only
measurements** (2,259 + 255 + 66). Matched inference peaks **94.6 MiB**.

Executed static policy v3 defaults its dtype report to float64 when new
checkpoint metadata omits that field; the trained-kind loader actually
constructs Float32InputCritic. Both raw trained reports remain unchanged.
`verification/matched-control.audit.json` reconciles effective **float32
input / float64 inner arithmetic and geometry** from the executed class and
exact replay. Future static policy v4 fixes this reporting fallback only;
**no inference was run with it** and no score/coordinate changes are claimed.

Only `db.ShareholdersMeetingDirectorAttendance` moves; three full routes
change. Overview **285 = 188 crossings + 92 card hits + 5 bundle hits**.
Preserve original 1,244 sizes, 1,727 canonical relationships, 1,035 physical
overview cards, 49 Leaf cards, 1,039 displayed lines and 2,078 valid outward
endpoints. Hard/overlap/spacing are zero; overview area stays
**1,348,777,883.9906998**. Full companion geometry stays 4445. Separately
preserved individual **1963 = 1483 + 480**, area **1,443,630,960.9896998**.
Product apply/audit peak **100.6 / 103.7 MiB**; actual file loading and
complete view round-trip parity peak **111.6 MiB**. Numerical jobs stay
serialized, one math thread, nice +10 and sampled RSS guard **128 MiB**.
No hard CPU-percentage quota is claimed. No new browser attempt or rendered
visual QA; previously supported bootstrap fails before browser initialization.
No CLI settings/authentication or transport diagnostics changed.

Archive `data/erd-poc/experiments/independent-views-global-owner-learning-20261004/manifest.json`
SHA **b17325cd48e8e977b6e7c58211491b84cd52dc2eedce1345de58c47a92575ffb**:
**186 new compressed objects / 55,490,381 bytes**, 200 new + 274 referenced
mappings. Independent restore matches **474 mappings / 369 objects / six
prerequisite manifests**. Seal/restore peak **31.8 / 18.5 MiB**.
`promotion-before/` preserves 286/1963; final `promotion/` supplement
pins metadata, documents and guard receipts independently of the main manifest.

Next work must explicitly bind **285 / 1963** before inference, use static
policy v4 for truthful dtype reporting, and preserve model authority and all
geometry/resource constraints. Closed sources use 286/1963. Sparse positive
rewards and tiny gains suggest a small learned geometry model that predicts
line crossings/card hits of decoded candidates and scores a bounded
NN-selected shortlist. This is a proposed direction, not implemented or
verified. Native must remain a teacher and accept/reject gate; do not feed
the exact future visual count into the selector or let Native choose
coordinates. Lower-temperature global trajectories and protected outcome
learning remain options. A global geometry floor or impossibility is unproved.

### Previous continuation: trained single-owner policies, exact observer and verified 286 / 1963

Promoted **286 / 1,963**; **150 / 750 remains active and unmet**. Root is
`.tmp/visualcross-ml-150-750-20261004/single-owner-trained-walk1/`. All
eight stages are terminal. Do not repeat completed unchanged schedules or the
failed observer probe. Coordinates remain exclusively NN-selected; Native
only decodes, observes, measures and accepts/rejects. Reuse frozen Native
binary `b5c355b2675d225114279549051ef201d84d6e56dbe8658810cc9705004f5514`;
no new C++ compilation or optimizer update occurred in this continuation.
Trained critic `ef92608e90a15f9d8b467f3a7ee964b900ea57d740da4503f83199d57e107df1`
comes from the previous single-owner-learning archive (20,512 executed
updates; selected checkpoint after 13,461). Preserve the separate
`915a84051eedea56c9695a479737706772eef6213ac8b986118ce952c37a7a10`
lineage for the retained individual view. Those training counts are inherited,
not new work or a current matched-parent experiment.

`source-binding.json` binds every source center to the prior promoted
288/1963 product. Versioned walk v2 keeps cumulative movement in integer
cents to prevent floating visited-state jitter; it changes no geometry by
repair. Ordinary local-support NN walks at temperature 1 run for 30 seconds
per view: individual seed **97409**, **33 proposals / 23 neutral states**;
overview seed **97713**, **49 / 31**. Neither improves. Saved best remains
the immutable source; admitted neutral states are exploration only.

The first cached-observer probe fails on two of 64 feature values, maximum
absolute difference **9.999778782798785e-13**, because CPython hypot differs
from Native Darwin hypot. Preserve the failed probe and its one read-only
MEASURE/OBS pair. Observer v2 calls libSystem hypot; its successful six-state
probe exactly compares **6,837 rows / 437,568 values** and adds six read-only
MEASURE/OBS pairs. It reuses a pose's exact crossing/hit matrices while
rebuilding Native feature meanings; it generates no coordinates or proposals.

Cached walk v3 uses that observer only for Native-proven legal states, checks
the initial cache against READY, raises exploration temperature to 4, and
keeps the 30-second limit. Individual seed **98303**: **62 proposals /
34 neutral states**, 110.6 MiB. Overview seed **98609**: **67 / 45**,
106.8 MiB. No strict improvement. Seeds and temperature also change, so these
runs do not establish a causal observer speedup.

NN batch inference samples 2/4/8/16 distinct owners from saved single-owner
scores and freezes 256 batches per view before measurement. Individual
seed **99103**: 78 legal / 26 ties; overview **99529**: 94 / 48.
Neither improves. There is **no learned joint-interaction critic**; the
existing single-owner model selects each owner's movement.

Global single-owner support removes local-neighbor and size restrictions,
retaining isolated cards with direct conflict because they can obstruct
relations. Each immutable schedule has 1,024 model-selected actions with
both directions and 66 amplitudes, temperature 4. Individual seed
**100103**: 606 legal / 303 ties, stays 1963. Overview **100529**:
625 legal / 357 ties, one strict gain **288→286**. Winner rank **70**,
cycle **[174,537,559]**, amplitudes **[0,.1,0]**, wire SHA
`2fa3bd2dc819e5825646885956d9f1f5c07f5e7fcb6c05c11e634065a67e1f22`.
Its critic score is **−1.5750080336494925**; it was sampled through
NN-weighted Gumbel exploration. Learning caused this gain is not established.
Total new proposals: **2,771 = 82 ordinary + 129 cached + 512 batch + 2,048 global**.

Independent verifier `.tmp/verify_trained_single_owner_policies.py` replays
all NN scores, schedules, selected movements and decoder geometry. It checks
all cached inputs used by the model, including final poses: **82 cached
state controls**, **88 total feature controls** including READY baselines.
It performs **133 read-only full Native measurements** of sampled actions,
including the winning action, in addition to the seven observer probes.
It does not independently recompute every new proposal's Native score.
Four archived walk-label NPZ files are not used for training yet.
Walk/static verification peaks at **99.8 / 98.6 MiB**.

Only `db.MeetingDraftAgendaDirectorCompensationItem` moves in the
overview product; two full relationship routes change. Product overview
**286 = 188 edge crossings + 93 edge/card hits + 5 bundle/edge hits**.
Keep 1,244 original model sizes, 1,727 canonical relationships, 1,035
physical overview cards, 49 leaf cards, 1,039 displayed lines and 2,078
valid outward boundary endpoints. Hard/overlap/spacing violations remain
zero. Overview area is **1,348,777,883.9906998**; individual is
**1,443,630,960.9896998**, with 1963 = 1483 crossings + 480 card hits.
The complete individual view is structurally identical to the prior JSON view.
Product loading and full overview→individual→overview route/metric parity
pass with peak **114.1 MiB**. Numerical jobs remain serialized, one math
thread, nice +10 and sampled process-group RSS guard **128 MiB**; no hard
CPU percentage is claimed. Product TypeScript and previous builds are unchanged.

Archive `data/erd-poc/experiments/independent-views-trained-owner-policies-20261004/manifest.json`
SHA **5a24ded20b6c2feec46b7d11f2911d9673052dece55c59762e8162e487e32f16**
contains **234 new compressed objects / 68,376,322 bytes**, 244 new +
222 referenced mappings. Independent restoration matches all **466 mappings,
368 stored objects and five prerequisite manifests**. Seal/restore peaks
at **33.4 / 18.2 MiB**. `promotion-before/` preserves previous
288/1963 layout/audit/provenance; `promotion/` pins final promoted state,
metadata reconciliation, documentation, receipts and Browser attempt.

Browser tooling is now exposed, but the supported runtime initialization
fails with `Importing module "node:process" is not allowed in node_repl`.
No browser selection, rendered viewport, screenshot or interaction check
occurred; **browserVerified=false**. Do not substitute another automation
surface or claim the earlier absence of a callable Browser tool persists.
The user's CLI-location reply is **terminal Codex CLI**. Reuse existing
redacted diagnostics; the captured current-thread event is SSE idle timeout,
not an exact match for the reported response-body decoding error. No new
log scan, CLI session, auth/config change or established memory cause.
Official OpenAI diagnostic-log and resume sections were fetched for guidance.

Next work must bind the verified **286 / 1963** geometry before new proposals.
The eight closed source schedules use 288/1963; do not silently reuse their
bindings for the new product. Consider fresh global cumulative NN trajectories
and outcome learning with the protected validation words/graphs. The global
geometry floor is unproved, and the joint batches need their own learned
interaction schema before claiming joint-policy training.

### Previous continuation: single-owner learning, matched controls and verified 288 / 1963

Promoted **288 / 1,963**; **150 / 750 remains active and unmet**. Root is
`.tmp/visualcross-ml-150-750-20261004/single-owner-learning1/`. All jobs below
are terminal. Do not rerun completed unchanged schedules, failed versions or
the optimizer trace. Coordinates come exclusively from model selection;
Native only decodes, observes, measures and accepts/rejects. The existing
single-owner action support and its 3x141 feature meanings are unchanged.

Fresh all-size fixture graphs **128..159**, seeded **93101+graph**, produce
**64 completed stages / 4,096 model-selected actions** across both views.
Source geometry is created before scoring; individual flattening moves no
cards. Frozen parent `915a84051eedea56c9695a479737706772eef6213ac8b986118ce952c37a7a10`
ranks both cycle directions and 66 amplitudes; vocabulary seed **94083+graph**,
Gumbel seed **95131+graph**, temperature **4**, 64 unique one-owner moves per
stage fixed before Native measurement. All 32 graphs yield improving legal
actions, **509 total**. These are synthetic gains, not Captain gains.
`synthetic_single_owner_curriculum.py` failed after one Native TRY before
logging it because a vocabulary variable shadowed the label list. It has no
completed stage. The versioned v2 collector fixes only that issue. Its first
proposal repeats the failed first geometry; all-history uniqueness is not
claimed. Preserve both versions and failure evidence.

Fine-tuning data are **23,552 float32 feature rows**: 17,408 parent replay,
2,048 previous actual Captain single-owner outcomes, and 4,096 fresh synthetic
outcomes. The original 607 Captain validation words and three 1,024-row
synthetic graph folds remain excluded. Additional Captain validation contains
**433 whole-word rows**, with parent-seen training words excluded from new
validation. Additional synthetic validation contains both views of graphs
**[129,132,136,138,148,152,154,155]**, **1,024 rows**. All Captain training words
are disjoint from both Captain validation parts. There are **18,416 training
rows**, including **4,687 single-owner rows** (1,615 Captain + 3,072 synthetic).
The improving training pool is **1,884**: old positive 804, negative 255,
mixed 460, Captain single **1**, fresh synthetic single **364**. All training
rows enter sampling once, single-owner rows get two additional copies, and
improving rows get seven additional copies. Validation remains unoversampled.

The first trainer stopped at the **128.8 MiB / 128 MiB RSS guard** after writing
the complete NPY dataset and its 18-file hash ledger. Optimizer updates before
that failure are not established. Do not claim zero. Trainer v2 reuses those
verified completed files read-only, memory maps the 39,850,112-byte feature
NPY, and hashes it in 64 KiB blocks. It does not recollect outcomes or rebuild
the dataset. Parent normalization, float64 weights and inner arithmetic stay
unchanged; feature inputs are float32. The selected model has **17,474 params**
and all ten parameter groups change.

Successful training executes **20,512 Adam updates**, selects epoch **21** /
**13,461 updates**, takes **7.46 s**, peaks at **89.3 MiB**, and saves critic
`ef92608e90a15f9d8b467f3a7ee964b900ea57d740da4503f83199d57e107df1`
in `training2/model.npz`. The trained-single-owner loader branch is now
actually executed. Mean validation loss **2.029155 → 1.904334** is across six
parts. Individual parts are Captain **2.322679 → 2.359872**, positive
**1.924879 → 1.611918**, negative **2.606230 → 2.799006**, mixed
**2.564733 → 2.494988**, Captain single **1.303197 → 1.001306**, and synthetic
single **1.453213 → 1.158915**. Original Captain and negative validation regress;
positive validation is still worse than the historical 1.564318 best. These
holdouts are words/graphs excluded from fine-tuning, not a new Captain scene.

`individual-source1963/` copies original topology/sizes/memberships and the
previous model's exact saved 1963 geometry. It is bound against every promoted
model center. `source-binding.json` binds that geometry and the overview 289
source explicitly. Policy v2 changes source binding only; it does not silently
rewrite the parent checkpoint's old training-input metadata.

Four matched Captain policies at **seed 96301**, each **1,024** unique model
proposals, reuse identical source features, vocabulary, source gates and Gumbel
values. Individual frozen: **1963**, 886 legal / 623 ties; individual trained:
**1963**, 843 legal / 611 ties. Overview frozen: **289 → 288**, 870 legal /
685 ties, two improving legal actions / one accepted. Overview trained:
**289 → 288**, 834 legal / 657 ties, one improving legal action / one accepted.
Both winning actions have identical wire SHA
`b3977bdf420bc6532b16ace16bdae3f47dc94f28a28deb97be93a4257733771d`:
cycle **[204,205,210]**, amplitudes **[0,-.2,0]**, frozen rank **99**, trained
rank **155**. **Learning caused this Captain gain is not established**.
The trained winner is used for the overview product candidate; one model,
`db.EmployeeStockGuideAudience`, moves. The prior individual view is preserved
exactly. All four stages peak together at **93.8 MiB** and add no updates during
inference. **8,192 completed new actions** are separate from the one failed,
unlogged collector TRY and from read-only verification calls.

Independent `.tmp/verify_single_owner_learning.py` regenerates all 32 fixture
inputs without scoring, replays every model score/schedule, reconstructs all
8,192 completed actions/labels, and **full-MEASUREs all 4,096 synthetic actions
+ 66 Captain samples, including both winners = 4,162**. It reconstructs all
23,552 data rows/folds/sampling and **replays all 20,512 optimizer updates**,
all 32 epoch losses and the selected weights exactly. No new model is selected
or published by verification. Peaks are **91.8 MiB records / 93.6 MiB training**.
Every Captain Native score is not independently recomputed; no such claim.

Product overview audit reports **288 = 191 edge crossings + 92 edge/card hits
+ 5 bundle/edge hits**, all original sizes, 1,244 models, 1,727 canonical
relationships, 1,035 physical overview cards, 49 leaf cards, 1,039 rendered
lines, 2,078 valid boundary endpoints, zero spacing/hard/overlap, and overview
area **1,348,777,883.9906998**. Individual **1,963 = 1,483 + 480** and its
full geometry/routes remain unchanged. A versioned apply adapter changes only
the descriptive algorithm string for this trained critic. Product apply and
audit peak at **102.0 / 105.9 MiB**. The existing memory-bounded composer passes
actual product file load and complete overview→individual→overview route and
metric parity at **116.1 MiB**, below the unchanged 128 MiB guard. Actual
browser/viewport/interaction QA remains unavailable and **browserVerified=false**.

Archive `data/erd-poc/experiments/independent-views-single-owner-learning-20261004/manifest.json`
SHA **6b9b97a2aafd690eed135ce561091e0b98b29758ee70ba73fbeb397a1a956298**
contains **1,113 new physical compressed objects / 37,665,562 bytes**, 2,242 new
+ 370 referenced mappings. Independent restoration verifies **2,612 mappings,
1,300 stored objects and five prerequisite manifests** against live source
hashes. Seal/restore peak at **39.4 / 21.8 MiB**. The prior 289/1963 layout,
audit and provenance are retained in `promotion-before/`. Latest provenance
scopes new training/data counters separately from inherited bounded-policy
history and records the distinct critics supplying each view.

Next work must start from verified **288 / 1963**, not rerun the closed 289 or
1964 schedules. `source-binding.json` is the closed 289/1963 evaluation binding;
bind the newly saved overview geometry explicitly before further inference.
Consider new actual Captain outcome learning with preserved word folds and
fresh cumulative model trajectories. The global geometry floor is unproved.
CLI transport diagnosis is unchanged from the previous continuation; do not
repeat its completed read-only diagnostic or claim a memory cause.

### Previous continuation: single-owner neural moves, verified 1963 and bounded product promotion

Promoted **289 / 1,963**; **150 / 750 remains active and unmet**. Research root
is `.tmp/visualcross-ml-150-750-20261004/single-owner1/`. All jobs below are
terminal. Do not repeat completed unchanged trials. The selected action is
exclusively the neural critic's output; Native only decodes, observes, scores
and accepts/rejects. No native coordinate search, repair or fallback is added.

`single_owner_cycle_model.py` retains the parent's **3x141 feature meanings**
and permits **66 single-owner amplitude vectors**: one of three owners, either
sign, at **[.00025,.0005,.001,.0025,.005,.01,.02,.05,.1,.2,.4]**. Support uses
local nearest eight cards of all original sizes, with both directions and at
least one conflicting owner. Only a directly conflicting owner may move.
Rounded zero moves and duplicate owner/displacement tuples are removed before
Native scoring. This is within-stage uniqueness, not all-history exclusion.

The critic is frozen parent `915a84051eedea56c9695a479737706772eef6213ac8b986118ce952c37a7a10`.
It was trained on multi-owner amplitudes; **no new training updates or
single-owner training** occur here. The future trained-single-owner loader
branch remains unexecuted. Feature inputs remain float32; normalization,
weights, inner arithmetic, coordinates and endpoints remain float64.

At seed **90113**, each frozen view evaluates **1,024 neural actions**.
Individual has **858 legal / 625 ties / one improving action**, overview
**878 legal / 703 ties / no improvement**. Winning individual row **420** is
cycle **[124,301,322]**, amplitudes **[0,0,.2]**, critic score
**-1.7338638815531464**, giving **1,964 -> 1,963**. Saved geometry exactly
matches the model-selected action. Physical and full hard, overlap and spacing
scores remain zero. These are **2,048 new proposals and one distinct gain**.

`individual-walk1` independently full-scores and replays that known winner
once to warm its trajectory; that replay is not a new proposal or another gain.
Frozen neural selection then evaluates **25 new proposals**, admits **22
neutral states**, obtains **zero new strict improvements**, and observes **23
states** over **22 rounds**. It takes **31.021542 seconds**, with a 30-second
loop limit. Saved best stays **1,963**, distinct from experimental neutral
states. Exact observation generation is the principal runtime cost. Total new
Captain proposals in this continuation are **2,073**, with no new training.

Four versioned observer-cache build attempts stop at the unchanged **128 MiB**
guard: **129.8 / 128.4 / 142.4 / 141.7 MiB**. Version 2 also removes a required
array serializer; version 3 restores the exact parent serializer and attempts
integrated cc1. Version 4 omits unreachable fixtures, search/self-test helpers
and heavy headers, with O1, but still exceeds the guard. Every failed source
snapshot and log is retained. **No observer-cache binary executes, no runtime
parity is verified, and none of these attempts creates an ML proposal.** Use
the verified parent binary `b5c355b2675d225114279549051ef201d84d6e56dbe8658810cc9705004f5514`.

`.tmp/verify_single_owner_records.py` independently replays all **2,048 frozen
scores, schedules, actions and labels**, plus all **22 walk rankings**, **25
new cumulative actions**, and **23 exact full-native observations**. It makes
**59 read-only full measurements**: frozen first-16 samples in each view plus
the winner (**33**), every new walk action (**25**), and the known warm action
again (**1**). Other frozen Native action scores are not independently
recomputed. The original warm full check is a separate measurement. MEASURE
and OBS controls leave their sources and action counters unchanged. Verification
peaks at **91.3 MiB**. Its first system-Python invocation fails before importing
NumPy; corrected execution uses existing **`.venv-ml/bin/python`**, without
installing dependencies.

The individual product audit peaks at **81.9 MiB** and confirms **1,483 edge
crossings + 480 edge/card intersections = 1,963**, all **1,244 models / 1,727
relationships**, **3,454 outward boundary endpoints**, original dimensions,
zero spacing violations and area **1,443,630,960.9896998 <= 1.5e9**.
The first composition is stopped at **129.7 MiB**. Reducing V8 old space from
48 to 40 MiB produces a JavaScript heap failure at a sampled peak of **121.0
MiB**. Versioned `compose_independent_views_memory_bounded.cjs` releases unused
input scenes and clones mutable runtime table positions while sharing read-only
scene collections. With a **40 MiB old / 1 MiB semi-space heap**, it completes
at **115.2 MiB**, retaining the same coordinate output, product load checks and
overview -> individual -> overview route-function parity. Product UI controls,
styles and tokens are unchanged. No real Browser, viewport, keyboard or normal
UI interaction QA is claimed; the current tool catalog has no browser control
or computer-use tools. Browser verification stays false.

Completed archive:
`data/erd-poc/experiments/independent-views-single-owner-20261004/manifest.json`,
SHA **`8c24abd16db00344e05fcce1441a00b2c1b002db723185c3f36e93c4b3d52f15`**.
Streaming sealing peaks at **31.3 MiB**, preserving **173 new compressed byte
objects / 16,924,405 bytes**, with **207 new mappings / 75 referenced mappings**.
Independent streaming restoration matches **all 282 mappings / 230 stored
objects / four prerequisites**, at **17.9 MiB**. The old 289/1964 candidate,
audit and full provenance are copied before promotion and archived. New
candidate SHA is **`7aa76f957e12361b8e72add0e12e08d97494d78cc087309d9d15448f237cfcda`**.
Promotion and archive receipts are `.tmp/single-owner-*-receipt.json`; independent
restore evidence is `.tmp/single-owner-archive-independent-verification.json`.
Post-promotion verification confirms identical audited/promoted bytes, unchanged
overview geometry, exactly **one moved individual card**, unchanged original
dimensions and all **1,727 relationship IDs**. Evidence is
`.tmp/single-owner-final-state-verification.json`. A supplemental provenance
reconciliation scopes fresh training/synthetic/branch counters to zero and
preserves old bounded-policy counters separately. It records the frozen
individual critic's parent **5,120 Captain rows / 607 validation / 4,513 training
rows** and **12,192 executed / 10,287 selected parent updates**. The main
manifest and candidate hashes remain unchanged. Before/after provenance and the
reconciliation tool/record are retained in the archive's `promotion/` directory.
There is no commit, push or publication. Next: learn from single-owner outcomes
and fresh relevant positive fixtures with preserved holdouts, then test fresh
NN schedules; retain the verified 1963 winner rather than repeating these runs.

CLI follow-up remains distinct: read-only indexed SQLite with a 1 MiB cache and
one-second query guard finds this thread's **2026-10-04 03:12:43 KST** retry for
**idle timeout waiting for SSE**, retry 1/5. That is not an exact match for the
user's response-body decode error and does not establish memory as its cause.
No proxy/custom provider override, auth/config change, `doctor`, new CLI session
or resume operation is executed. Guidance is `codex resume`, selecting the
affected saved conversation. Redacted evidence is `single-owner1/cli-transport-latest.json`.

### Previous continuation: owner amplitudes, mixed sizes, balanced learning and global support

Retained **289 / 1,964**; **150 / 750 remains active and unmet**. All eight
Captain stages are terminal, with **8,192 neural proposals and zero accepted
improvements**. Do not repeat these completed unchanged trials. No product
candidate or UI change, and no new browser QA. Current research root is
`.tmp/visualcross-ml-150-750-20261004/owner-amplitude-cycles1/`.

`owner_amplitude_cycle_model.py` adds **90 amplitude vectors**: 18 independent
sign-or-zero patterns at each common magnitude **[.001,.005,.02,.1,.4]**, with
at least two nonzero owners and uniform scalar patterns excluded. These are
not three independent continuous magnitudes. Each owner moves toward or away
from the next center, with unchanged sizes, cent rounding and deterministic
endpoint decoding. Features retain the prior 136 core meanings and five extra
per-owner fields. Every actual proposal changes the raw triplet centroid;
quantization and decoded feature motion are checked. Native code only measures
and accepts/rejects the critic's geometry, with no search, repair or fallback.
`synthetic_owner_amplitude_curriculum.py` is an **unexecuted scaffold**.

Frozen equal-size transfer from signed parent `3d166d47...` uses local nearest
eight support, 1,024 unordered triples in both directions and seed 88237.
At 1,024 proposals/view, individual/overview legal counts are **643/552** and
ties **384/278**, with minima **1,964/289** and no accepted improvements.
`mixed_size_owner_cycles.py` removes the equal-size support/decoder restriction
for partial moves while retaining every source size. The first mixed transfer
fails **before ranking or native proposals** because its compatibility check
calls the old equal-size decoder on mixed-size words. Executed code and failure
are retained; the corrected version checks mixed common-amplitude features and
separate equal-size wire controls. Frozen mixed local transfer at seed 88837
gives **704/714 legal**, **420/496 ties**, again no improvement.

`mixed_size_synthetic_fixture.py` creates fresh graphs **64..95**, seeded
**89003+graph**, with varied widths/heights, ordinary cards and small Leaf
groups. Geometry, topology and sizes are created before scoring. Individual
fixtures are flattened without movement. The frozen signed critic selects
**4,096 mixed-size, nonuniform-amplitude proposals / 64 stages**, producing
**612 legal improving actions** across all 32 graphs. Probe graphs 64..67 are
not rerun when collecting 68..95. These synthetic gains do not improve Captain.

Balanced learning combines **5,120 Captain + 4,096 positive + 4,096 negative +
4,096 mixed synthetic rows = 17,408 rows**. Its **13,729 training rows** include
**804 positive / 255 negative / 460 mixed improving examples**, or **1,519**
total, sampled eight-fold. Captain's original **607-row whole-triple fold** is
excluded. Reused uniform graph fold **[1,4,8,10,20,24,26,27]** and new mixed fold
**[65,68,72,74,84,88,90,91]** exclude both views and all actions from optimizer
training, retaining 1,024 validation rows per synthetic kind and **152 mixed
validation positives**. These reused validation folds are not an independent
Captain test set.

The first float64 attempt exceeds the unchanged guard at **142.4 MiB, exit
137**, after completing its dataset; it produces no model or training report.
Versioned `train_balanced_owner_amplitude_model_v2.py` stores and quantizes
**feature inputs as float32 in both training and inference**. Normalization,
weights, inner arithmetic, coordinates and endpoints remain float64. Chunked
comparison verifies that the completed interrupted dataset differs only in
feature precision and the recorded quantization error field. Maximum feature
error is **2.3837506546442455e-7**. Successful training peaks at **122.4 MiB**;
the memory limit is never raised.

All ten parameter groups change, with **17,474 parameters** and unchanged
normalization. Training executes **12,192 Adam updates** and selects epoch
**27 / 10,287 updates**, taking about **4.375 seconds**. Four-part mean Huber
improves **2.687024 -> 2.354630**. Components are distinct:

| Validation kind | Initial | Selected |
| --- | ---: | ---: |
| Captain | 2.279563 | 2.322679 |
| Positive | 3.194869 | 1.924879 |
| Negative | 2.535375 | 2.606230 |
| Mixed | 2.738291 | 2.564733 |

Captain and negative validation regress. Positive replay partially recovers the
earlier regression but remains worse than the original positive critic's
**1.564318**; retain that critic separately. Selected checkpoint is
`training2/model.npz`, SHA
`915a84051eedea56c9695a479737706772eef6213ac8b986118ce952c37a7a10`.

Trained mixed local Captain policies use exactly the frozen local node
observations, vocabulary and Gumbel field. Results are **747/728 legal**,
**407/515 ties**, zero improvements. Frozen/trained local policies share
**583 individual / 639 overview exact wires**; no all-history uniqueness claim.
An input-precision-only control preserves the parent's weights: its maximum
score changes are **4.3433e-6 / 6.6319e-6**, with **zero changed selected indices
or rank positions among the first 1,024 proposals** in either view.

Global support uniformly samples eligible mixed-size source triples across all
distances, with at least two directly conflicting owners in the word. Eligible
counts are **99,318,786 individual / 18,386,625 overview**. Seed 89411 samples
1,024 unordered words in both directions. Global trained proposals give
**327/415 legal**, **115/170 ties**, with no improvements. Global/local policies
are not matched controls. A word's conflicting owners need not all move because
the amplitude vector can hold an owner still. Individual Captain input already
has **1,244 singleton physical owners**; grouped rigidity is not an established
individual-view limitation. This work establishes no global geometry floor.

`.tmp/verify_owner_amplitude_records.py` independently reconstructs the balanced
dataset, graph folds, initial parameters, normalization, parameter changes and
initial/selected validation losses. It regenerates one mixed fixture and its
movement-free flattening, checks global eligible counts against a small
exhaustive control, replays all **4,096 new synthetic actions and labels**, and
compares every sparse result with full native scoring. It replays all **8,192
Captain actions** and saved geometry, additionally full-scores the first 16
actions in each stage (**128 samples**). Thus **4,224 read-only measurements**
are verification, not new neural proposals. Other Captain native scores and the
full optimizer history are not independently recomputed. Sources stay unchanged
under MEASURE. All final physical/full hard, overlap and spacing scores are zero;
the real candidate retains **1,244 models / 1,727 relationships** in each view.

Training verification peaks at **100.6 MiB**, action verification at **99.2**.
Numerical jobs remain serialized, one math thread, nice +10, with the sampled
process-group RSS guard at **128 MiB**, not a hard CPU-percent quota. Other
observed peaks: equal-size transfer **72.8/70.3**, mixed frozen **74.0/70.1**,
mixed trained **73.2/70.3**, global trained **69.5/69.3**, synthetic probe **48.3**,
remaining collection **55.1**. Completed stages are retained, including failures.

The first archive seal is interrupted at **148.5 MiB, exit 137**, with 923
compressed files and no manifest or provenance change. Versioned streaming
sealer reads only the two needed parent manifests, verifies completed partial
byte objects and reuses them without repeating learning/evaluation. It completes
at **33.0 MiB**. The interrupted directory is retained as a verified object
store; it is not a completed research archive.

Completed archive:
`data/erd-poc/experiments/independent-views-owner-amplitudes-20261004-v2/manifest.json`,
SHA `6b31d46cd53ab370048e36ba43ed04bdc423e3507adddd42423dca0426a565a7`.
It stores **18 new logical/physical files / 500,854 logical bytes / 107,755
compressed bytes**, plus **3,321 referenced mappings / 103,922,335 logical
bytes**, reusing old and completed interrupted byte objects. Independent
archive verification restores and live-hash-checks all **3,339 mappings /
2,418 distinct stored objects**, verifies three prerequisites and the retained
product counts/metrics, and passes at **108.1 MiB**. Receipt is
`.tmp/owner-amplitude-archive-independent-verification.json`; sealing and
archive-verification guard logs are retained outside the immutable manifest.
Promotion
provenance appends the completed archive; candidate geometry bytes remain SHA
`da2317d3d9fe72181587f543843bfdde3cc7030ca328e93c4f627ae21ec356e7`.
Next pursue a richer learned action space or Captain-relevant fixture
distribution; preserve the original positive critic and held-out graphs rather
than repeat these fixed-source trials.

CLI clarification still refers to **terminal Codex CLI 0.160.0**. A new bounded,
read-only SQLite scan examines 16 WARN/ERROR rows over 12 hours. It finds a
**2026-10-04 00:54:27 KST** failure during remote compaction in another thread,
explicitly reporting the **60-minute Responses WebSocket connection limit**.
Official WebSocket docs confirm that limit and advise reconnecting. This event
is not yet linked to the user's reported HTTP response-body decoding error.
No proxy/API base override is found in the checked environment, no custom
provider is configured, and the standard TUI log is absent. `codex resume`
guidance is not an executed restart. No settings/authentication changes or
doctor retry, and no causal link to ML resource use. A pending optional
clarification asks for the latest error time and launch command. Redacted
evidence is `cli-transport-followup.json` under the current research root.

### Previous continuation: signed cycle learning and matched Captain policies

Retained **289 / 1,964**; **150 / 750 remains active and unmet**. This turn
adds and actually learns negative cycle outcomes; it does not improve Captain.
No product candidate, UI change or browser QA. All stages are terminal. Preserve
these completed stages and do not repeat unchanged signed policy trials.

New versioned `signed_cycle_model.py` retains the 141 feature meanings and
positive decoder behavior, but permits signed amplitudes. The new action values
are **[-.001, -.005, -.02, -.1, -.4]**. Every actual policy stage checks 96
positive feature/wire parity cases without submitting positive native proposals.
Negative raw moves spread the three centers; quantization and centroid bounds
are checked on all proposed actions. The critic alone chooses directed words
and amplitudes. Native geometry decodes, measures and accepts; no coordinate
search, fallback or repair. Same-size support and fixed source frames remain.
Prior positive words may be reused because negative geometry is different;
there is explicitly no all-history exact-wire uniqueness claim.

Root: `.tmp/visualcross-ml-150-750-20261003/signed-cycle-transfer1/`.
First, `run_signed_cycle_policy.py` uses the normalized positive parent SHA
`9142263c0c82f6278ca43d038f6947452a276fb6c6190ae9eb9abcc72a6de23a`.
This is frozen transfer, **not negative-trained selection**. Local vocabulary
has **5,533 eligible individual / 1,498 overview triples**, sampling 4,096/1,498
unordered triples in both directions. The source-only nearest-eight observation
vocabulary proposes no geometry. At seed 87217, Gumbel seed +1000 and T=1,
1,024 actions/view give **529/462 legal**, **296/213 ties**, no improvements,
minima **1,964/289**. The policy order is fixed before native measurements.

`synthetic_signed_cycle_curriculum.py` reuses all original 32 seeded grouped
fixtures and their movement-free individual flattenings. No source optimization
or fixture regeneration. The frozen positive critic selects **4,096 new negative
proposals / 64 stages**, giving **325 legal improving actions**. All 32 graphs
have positive outcomes. The first four graphs' 512 actions are preserved and
not rerun when collecting the remaining 3,584 actions.

`train_signed_cycle_model.py` initially exceeds the unchanged 128 MiB sampled
RSS limit: **129.2 MiB, exit 137**. Its complete dataset is retained, but there
is no model checkpoint or training report. `train_signed_cycle_model_v2.py`
streams only the inherited positive validation rows rather than materializing
the whole inherited feature tensor. Its dataset is **byte-identical** to the
first attempt's complete dataset; both are independently hash-compared.
The second execution finishes at **117.7 MiB**. No limit increase.

Training combines **5,120 original Captain rows + 4,096 signed synthetic rows =
9,216 rows**. The original Captain 607-row whole-triple validation fold remains
excluded. The same synthetic graph fold **[1,4,8,10,20,24,26,27]** is excluded
from both the parent's positive optimizer training and new signed optimizer
training, retaining **1,024 signed validation rows / 70 positives**. These are
reused validation graphs, not a new independent test set. There are **7,585
training rows / 3,072 negative rows / 255 improving examples**, with improving
rows sampled eight-fold. The parent's 1,024 positive validation rows are used
only for validation; its improving synthetic training examples are **not
replayed into new training**. All ten shared parameter groups change, with
**17,474 parameters** and unchanged 141-feature normalization.

The selected epoch 64 checkpoint executes and selects **9,408 Adam updates**.
Loop duration is about **4.279s**. Equal-weight Captain/signed/positive validation
Huber improves **2.798656 -> 2.669935**. Components are deliberately distinct:
Captain **2.275322 -> 2.279563**, signed **4.556330 -> 2.535375**, positive
**1.564318 -> 3.194869**. Positive validation regression is material: keep the
original positive critic separately and do not treat this as an all-purpose
replacement. Selected model: `training2/model.npz`, SHA
`3d166d47e6de5690bdc4718df7d49633a01a22364f253b1e87bed25dadc79b3e`.
`signed_trained_critic.py` requires the explicit signed trained schema and
selected optimizer updates; metadata is newly constructed without stale parent
counters. `datasetPath` retains the Captain exclusion dataset role, while
`trainingDatasetPath` identifies the actual mixed signed learning dataset.

`run_trained_signed_cycle_policy.py` evaluates the selected signed critic on
exactly the same Captain node observations, vocabulary and Gumbel field as the
frozen controls, again 1,024 actions/view. Results: **504/418 legal**,
**282/170 ties**, no improvements, minima **1,964/289**. Frozen/trained stages
share **542 individual / 534 overview exact wire hashes**; do not claim all
4,096 Captain actions are distinct or that prediction-loss improvement proves
Captain improvement. Saved physical/full geometry matches the model output
(or the unchanged source when no proposal is accepted). Both sources retain
**1,244 full models / 1,727 canonical relationships**, with original sizes.
All final physical/full hard and spacing scores are zero.

`.tmp/verify_signed_cycle_records.py` reconstructs all mixed learning arrays,
stream-compares NPZ payloads, independently rebuilds the streamed positive
validation tensor from its sixteen original stage labels, verifies fold
exclusion, selected parameter changes, unchanged normalization and both initial
and selected validation losses. It reconstructs all **4,096 synthetic model
actions**, labels, feature motions, scores, Gumbel ordering, wires and saved
geometry; **every sparse label matches read-only full native scoring**. It
replays all **4,096 Captain actions**, with additional full scoring of the first
16 actions in each of four stages (**64 Captain samples**). These **4,160
read-only scoring measurements are not new neural proposals**; Captain's
remaining native rejection scores were not independently recomputed. MEASURE
leaves the sources unchanged. Full optimizer history was not replayed.
The verifier passes at **122.0 MiB**.

All jobs remain serialized, one math thread, nice +10, sampled process-group
RSS limit **128 MiB**, with no hard CPU-percentage quota claimed. Other observed
peaks: frozen individual/overview **65.8/70.7 MiB**, synthetic probe **50.4**,
remaining collection **53.8**, trained individual/overview **68.9/64.9**, sealing
**87.3**, independent archive verification **47.5**. The first training guard
termination is explicit failure evidence; it is not a successful checkpoint.

Archive: `data/erd-poc/experiments/independent-views-signed-cycles-20261003/manifest.json`,
SHA `77a3510ea02545f30045f0e1b038617aae7218087495e95e815fd6be8173a80c`.
**618 new logical entries / 22,586,754 bytes**, **550 physical compressed files /
13,880,033 bytes**, plus **782 referenced entries / 14,577,509 bytes**.
Independent verification restores and live-hash-checks all **1,400 mappings /
1,001 distinct stored objects** and eight prerequisite manifests. Promotion
provenance appends the research archive; promoted candidate bytes remain SHA
`da2317d3d9fe72181587f543843bfdde3cc7030ca328e93c4f627ae21ec356e7`.
No new product candidates or UI verification claims.

Next do not repeat these unchanged matched signed controls. The common-amplitude
primitive still preserves a triplet's centroid before quantization even when
negative amplitudes spread it; this is not proof of a global geometry floor.
Consider versioned **neural per-owner amplitude actions** that can move the
centroid, with explicit frozen-transfer versus new-learning provenance. If
training a joint signed critic, replay the parent's actual improving positive
training examples while preserving its held-out graph fold, and stream arrays
under 128 MiB to address the observed positive-validation regression.

CLI followup from the user's terminal-location reply: version **0.160.0**,
no checked-environment proxy/API base URL override, standard TUI log absent.
A bounded read-only incremental SQLite scan since 16:50 KST reads 14 warning/
error rows and finds one **2026-10-03 20:40:35 KST** stream disconnect in another
thread, with a WebSocket term. It is not linked to the affected terminal and
root cause remains unconfirmed. Earlier 24 disconnect/21 decode records remain
historical evidence, not new failures in this turn. Official OpenAI CLI,
troubleshooting, error and resume pages were fetched; `codex resume` is guidance,
not an executed restart. No settings/authentication changes or doctor retry,
and no causal claim that ML resource use causes the transport error. Redacted
followup is `.tmp/cli-transport-terminal-reply-20261003.json` and archived.

### Previous continuation: positive cycle learning, unseen graphs and local support

Retained **289 / 1,964**; **150 / 750 remains active and unmet**. The preceding
turn is progress because it learned amplitude-conditioned outcomes and yielded
verified evidence of zero improving training examples. This turn supplies actual
improving neural actions on separate synthetic graphs and verifies their scores,
then tests the learned policy on Captain and a different local observation
vocabulary. **No new product candidate, promotion, UI change or browser QA**.
All numerical stages are terminal. Do not repeat these completed stages or
infer achievement from synthetic-graph results.

`synthetic_cycle_curriculum.py` builds seeded fixture geometry and topology
before any scoring, with ordinary cards plus small Leaf groups. The critic
alone selects directed words/fractions; native code measures and saves legal
model outputs without search or repair. An initial four-graph probe used
grouped geometry in both views. Its **four overview stages are reused**, while
its **four grouped individual stages / 256 actions are excluded from individual
training**: their reward measured the physical objective with dual admission.
The exact executed probe code is retained in `probe-code/`. Correct individual
sources copy all full cards, positions, relationships and routes into one-card
physical representations without movement, matching the actual individual
view. Every accepted training source has initial physical/full hard scores zero.

Across **32 graphs / 64 valid view stages / 4,096 actions**, there are **1,076
legal improving actions**. `train_positive_cycle_model.py` combines these with
the parent's 5,120 measured examples (**9,216 rows total**). Whole synthetic
graphs **[1,4,8,10,20,24,26,27]**, both views and all their actions, are held out:
**1,024 rows / 272 positive examples**. The original Captain 607-row validation
fold also stays excluded. Training uses **7,585 rows / 804 positive examples**,
with positive rows sampled eight-fold. All ten shared parameter groups change;
the original 141-feature normalization is preserved. There are **13,248 Adam
updates**, with epoch 61's **12,627-update** checkpoint selected. The equal-weight
Captain/synthetic validation criterion improves **3.516655 -> 1.919820**.
Synthetic graph Huber improves **4.785350 -> 1.564318**; Captain Huber slightly
regresses **2.247959 -> 2.275322**. These are deliberately distinct claims.

Executed checkpoint:
`.tmp/visualcross-ml-150-750-20261003/cycle-positive-curriculum1/training1/model.npz`,
SHA `024057ab2266cfc38e0b374d0db440db792c5453cbdf46d5cc0a19c10d1f9233`.
It inherited some parent-only metadata counters. Completed stage references
remain intact; `model-clean-metadata.npz` removes those old counters while
preserving **every learned/initial parameter and normalization array**. Use this
normalized checkpoint as the next parent, SHA
`9142263c0c82f6278ca43d038f6947452a276fb6c6190ae9eb9abcc72a6de23a`.
`metadata-normalization.json` documents the removed fields. `datasetPath` and
its hash refer only to inherited Captain measured-word exclusion;
`trainingDatasetPath` identifies the actual mixed learning dataset.

On **eight completely new seeded graphs 40..47**, parent and trained models
use identical sources, vocabularies and Gumbel fields, 64 proposals per view.
Of **16 matched view comparisons**, trained is better in **8**, equal in **7**,
parent better in **1**. Best-visual sums are **6,718 parent / 6,381 trained**.
This is limited generalization within the seeded grid/Leaf fixture family,
not proof of improvement on Captain or arbitrary ERDs.

`run_positive_cycle_transfer.py` tests 1,024 fresh global-support actions/view
on the unchanged Captain sources. It excludes all parent measured words, both
previous matched-temperature stages and source-identical first walk rounds.
Results: individual **412 legal / 90 ties**, overview **541 legal / 174 ties**;
no accepted improvement, minima **1,964/289**. `local_positive_cycle_support.py`
then forms an observation vocabulary from each owner and two of its eight
nearest equal-size cards, with source distances only, at least two conflicting
owners and no geometry proposal from the support builder. The same frozen
critic selects words and amplitudes in `run_local_positive_cycles.py`.
Eligible local triples are **5,529 individual / 1,461 overview**; sampled
directions **8,192 / 2,922**. Another 1,024 actions/view yield **549/522 legal**
and **285/223 ties**, still no gain. These local stages intentionally share
**16/15 exact wire hashes** with the just-completed global stages; the comparison
is not a matched same-vocabulary experiment. Do not claim all 4,096 Captain
actions are unique or absent from every action measured earlier this turn.
Hard-rejected local actions also have no reported better visual scores.

`.tmp/verify_positive_cycle_records.py` reconstructs the mixed dataset using
streamed NPZ comparisons, checks graph/triple fold exclusion, both validation
losses, initial weights, selected parameter changes and normalization. It
replays **100 synthetic policy stages / 6,400 model actions** (including the
excluded initial probe and fresh controls), their words, scores, Gumbel ordering,
wire bytes and saved physical/full geometry. **All 6,400 sparse labels match
read-only full native scoring** across legality, rejection reason, both visual
scores, spacing and hard scores. MEASURE leaves every source unchanged.
It also replays the **4,096 Captain model actions**; Captain's rejection scores
were not independently recomputed. Full optimizer history was not replayed.
The verifier passes. New numerical proposal count is **10,496**, separate from
the 6,400 read-only verification measurements.

`.tmp/analyze_positive_cycle_family.py` verifies another actionable limitation
of the current primitive. Before cent rounding, a three-owner cycle with
fraction rho preserves its centroid and multiplies center variance by
**1 - 3*rho + 3*rho^2**. Fractions between 0 and 1 contract variance; rho=1
permutes slots. All current fractions are positive, so this family cannot
spread a configuration before quantization. **256 actual-source comparisons**
verify the formula (maximum relative error `1.1302626455387974e-14`) and a
rounding-error bound; maximum observed centroid drift per axis is 0.003334.
This does not establish that contraction causes the plateau or that the target
is impossible. Next test **signed cycle amplitudes** with explicit frozen
transfer versus new training provenance, preserving all native gates and fixed
source frames. `fractional_cycle_model.expand_actions` currently rejects
negative fractions: introduce a versioned compatible signed-feature/decoder
path, not a silent change to archived executed code. Do not repeat these
positive-amplitude controls unchanged.

The **128 MiB guard / one math thread / serialized jobs / nice +10** stay in
place, with no guard kills or increases. Observed peaks: probe **53.6 MiB**,
corrected collection **51.2**, training **110.7** (loop 4.643s), fresh matched
controls **49.2**, Captain global individual/overview **60.2/59.5**, local
**62.9/59.6**, records verification **112.5**, family analysis **42.9**, metadata
normalization **34.1**, sealing **64.2**. No hard CPU-percentage quota is claimed.
No new CLI diagnostics, settings/authentication changes or doctor runs.

Archive:
`data/erd-poc/experiments/independent-views-positive-cycles-20261003/manifest.json`,
SHA `33777419b749246e9e4dc481b912d66924dd8bb003b082b73fe46c40a9103e2c`.
**1,842 new logical entries / 23,667,613 bytes**, sharing **1,281 physical
compressed files / 17,916,524 bytes**, plus **91 referenced entries / 12,141,261
bytes**. All stored/referenced bytes restore with matching hashes; promotion
provenance records the archive and the promoted SHA remains unchanged.

### Previous continuation: learned movement fractions and direct-conflict trajectories

Retained **289 / 1,964**; **150 / 750 remains active and unmet**. A new critic
learns movement amplitude from the previous measured outcomes. Two predefined
stochastic temperatures test fresh triple support, followed by model-generated
neutral trajectories and the existing bounded translation model on their new
geometry. **12,522 submitted model actions, zero strict visual improvements**.
No new product candidate, promotion, UI change or browser verification. All
numerical stages are terminal. Classify this continuation as new learning and
action evidence, not a product improvement or target completion.

`fractional_cycle_model.py` extends the directed 3x136 context to **3x141**:
fraction plus the actual cent-quantized displacements of both directed owners,
normalized by observed relative source scales. The existing 136 features and
normalization remain unchanged. All five new input rows start at zero, exactly
preserving parent predictions. The shared model has **17,474 parameters**;
no names, absolute-coordinate features or learnable coordinate tables.
`train_fractional_cycle_model.py` reconstructs **5,120 rows**, including 2,048
parent rows, 2,048 new unit-cycle outcomes and 1,024 fractional outcomes.
There are **3,072 novel measured rows**, 3,160 multi-conflict rows and **zero
actual improving examples**. Whole unordered triples group every direction
and fraction into one fold; all **607 validation rows / 200 fractional rows**
are absent from the complete parent's measured triples. This is validation on
the same source layouts, not unseen graphs. Fractional rows receive four-fold
training sampling. All **8,800 Adam updates** reproduce bitwise; epoch 62's
**6,820-update** checkpoint is selected. Validation Huber **3.574576 ->
2.247959** establishes prediction learning, not crossing reduction.

Checkpoint: `.tmp/visualcross-ml-150-750-20261003/fraction-conditioned-model1/training1/model.npz`,
SHA `bd4d0612bd9abfdfdb0ccf4e4cbb1192098d212c4691d0096220afcbf8649891`.
Validation passes **51 finite differences**, all ten parameter groups and
all five added input rows (maximum error `2.8898472156924093e-12`), cyclic
score/gradient invariance and **160 actual decoded actions**. Sampling has
independent scalar ordering checks, seed replay, without-replacement checks
and 4,000 empirical first-choice draws (maximum probability error 0.003574).
Every tested trained word changes score with amplitude.

`run_fraction_conditioned_policy.py` ranks 4,096 triples / 8,192 directions
at fractions **.001, .005, .02, .1, .4, 1**, or 49,152 scores per view. **.4
was previously unmeasured**. Temperatures 1 and 4 share the same vocabulary,
scores and Gumbel fields, with their ordering fixed before native measurement.
Four independent stages submit **1,024 actions each**. Legal counts are
individual T1/T4 **437/332**, overview **572/428**; equal-score counts are
**123/94** and **202/149**. All legal minima remain the original **1,964/289**.
Hard-rejected proposals also have no reported visual improvement. Each saved
geometry is byte-identical to the original reference. Complete checkpoint
measured triples are excluded, but matched temperatures deliberately overlap:
**238 individual / 427 overview repeated wire hashes**. Do not call all 4,096
actions distinct or entirely new relative to the other matched stage.

`run_fraction_cycle_walk.py` applies the frozen model successively to its own
generated states. The existing integer-score tie admission is used, with an
explicit assertion of no actual objective regression. Each state recomputes
features and model ordering; native geometry never searches, repairs or supplies
alternative coordinates. Each admitted cycle moves exactly three owners and
at least two are directly conflicting in that state's observations. Individual
admits **17 tie states / 130 actions**, overview **20 / 104**; still zero strict
improvements. Independent replay reconstructs all cumulative physical and full
member translations, routes, identities and areas. Distinct owners directly
conflicting when moved are **20 individual / 16 overview**; all moved-owner
unions are **36/35**. The raw field `distinctMovedDirectConflictOwners` actually
counts the latter union of all cycle owners. Use corrected verification counts
and do not silently change the archived executed code. Maximum net movements
are **120.206 / 201.696** coordinate units. These states were not used for
additional training or promoted.

Individual walk elapsed 47.261s on a 45s limit, including initialization beyond
the final check. Its final round has zero native attempts due to time; the raw
stop reason was overwritten by the no-action branch and does **not** establish
exhaustion of eligible actions. Overview stops at the 20-round limit (39.956s).
Corrected stopping evidence is in `records-verification.json`.

`.tmp/run_fraction_walk_bounded_followups.py` copies the final ML-generated
sources without geometry changes, binds all ten inputs by hash and makes only
a graph-based cut-context map. The branch source identity is the actual walk
report SHA, explicitly not a layout-JSON SHA. The existing synthetic-trained
bounded neural models submit **4,096 actions/view** on these different source
geometries. Individual/overview admit **260/387 neutral actions**, all with
reported objective conflicts in their observed contexts, and finish **1,964/289**.
Frozen checkpoint, every latent prediction, decoded bounds, branch translation
and saved geometry replay pass. Native SAVE independently checks its complete
final metrics: hard conditions, individual hard conditions, overlap and spacing
are zero. It also reloads and checks both cycle final sources, including the
overview walk's last source. This is a frozen model transfer, not new training,
an independent product audit or browser QA. These stages must not be repeated
unchanged.

The new **128 MiB** process-group guard was kept throughout. Observed peaks:
model/sampling validation **47.3/46.4 MiB**, training **116.8**, four sequential
independent inference stages **65.3**, individual/overview walks **73.4/74.3**,
passed records replay **117.7**, passed followup driver **77.8**, sealing **46.6**.
One record verification attempted to load two complete datasets and was killed
at **153.1 MiB**; its script/failure snapshot is retained. Dataset comparison now
streams NPZ member bytes instead of loading a second feature array. The next
attempt passed numerical assertions at **109.9 MiB** but failed final JSON
serialization of NumPy int64; the final explicit conversion passes. A followup
driver first failed after completed individual inference because it read `hard`
instead of `hardConditions`; its **39.5 MiB** failure snapshot is retained.
The completed 4,096-action inference was not rerun: only its saved records were
verified before starting overview. No limits were raised and no doctor run
was started. RSS is sampled, not an absolute allocator cap; the 0.2 MiB very
short analysis sample is undersampled and is not a credible true peak.

Archive: `data/erd-poc/experiments/independent-views-fraction-conditioned-cycles-20261003/manifest.json`,
SHA `b8671d7ba4028368a2d6097c491280463edd94e1e5eb24f5a2318f4e2149d304`.
**573 new logical entries / 65,910,504 bytes**, sharing **361 physical compressed
files / 26,908,195 bytes**, plus **406 referenced logical entries / 38,001,622
bytes**. All stored and referenced bytes restore with matching hashes. Static
inputs and identical layout JSON copies reuse old or current compressed bytes.
Promotion provenance records the archive; the promoted candidate SHA remains
`da2317d3d9fe72181587f543843bfdde3cc7030ca328e93c4f627ae21ec356e7`.
Next investigate positive model-generated coordinated examples on separate
synthetic graphs with whole-graph holdout. The current actual cycle dataset
contains no improving examples; do not repeat more unchanged temperature,
triple or bounded followup trials, and do not infer a global decoder floor.

CLI followup: terminal version 0.160.0 rechecked, with no proxy variables or
API base-URL override in the checked environment/config. Standard TUI log file
is absent. Existing redacted diagnostics retain 24 disconnect / 21 decode events
in other threads; the latest retained event is 02:44:59 KST, with a later bounded
incremental scan returning no matching events. No fresh SQLite scan was done
here and the affected terminal/latest failure time remain unidentified. The
official OpenAI error and CLI resume references were fetched. Network/server
root cause and a causal relation to ML resource use are unconfirmed. No CLI
settings/authentication changes, session restarts or doctor retries.

### Previous continuation: direction-sensitive cycles and matched decoder observations

Retained **289 / 1,964**; **150 / 750 remains active and unmet**. This turn
trains a cycle critic on the preceding turn's actual outcomes and changes the
candidate support to coordinated conflicts. It also tests endpoint decoders
and smaller simultaneous movements, then attributes hard rejections without
changing source geometry or acceptance gates. **3,840 submitted model actions
produce zero accepted improvements.** All numerical stages are terminal; no
product candidate, promotion, UI change, browser check, or CLI configuration
change. Two completion console outputs were lost during compaction; final
reports and process state were recovered without restarting their stages.

`directed_cycle_model.py` uses ordered pairs `(a,b), (b,c), (c,a)`, each with
the complete 136-feature pair context. A shared encoder feeds mean and second
moment pooling plus a nonlinear interaction head: **17,154 shared parameters**.
The score is invariant to cyclic rotation but can distinguish reversal.
Warm initialization equals the mean of the parent's **directed** predictions,
not the previous undirected cycle proxy. No absolute coordinates, model-name
inputs or learnable coordinate tables. All ten parameter groups change.

`train_directed_cycle_model.py` rebuilds 2,048 measured cycle examples from
`active-pair-model1/{individual,overview}-cycles1`; only 88 touch two or more
directly conflicting owners, and none is an actual improvement. Validation
holds out whole unordered triples with both directions in the same fold:
512 rows / 22 multi-conflict rows, on the same two source layouts, **not unseen
graphs**. Multi-conflict training rows receive four-fold sampling. All 2,240
Adam updates reproduce bitwise; epoch 19's 532-update checkpoint is selected.
Validation Huber decreases **2.87848 -> 2.55930**. Direction preference checks
on held-out unequal targets are 66/115 individual and 69/122 overview, versus
53/115 and 62/122 for the initial directed warm start. These checks establish
limited prediction learning, not causal visual improvement.

Checkpoint: `.tmp/visualcross-ml-150-750-20261003/directed-cycle-model1/training1/model.npz`,
SHA `52fa90607c2fe9f5acfa5d49e1f086bf1f221252ab8c5aaa26816e04aedc5c14`.
Training peak 93.5 MiB. Functional validation passes 46 finite differences
across all ten parameter groups (maximum error 1.56e-11), warm parity and
cyclic score/gradient invariance. Peak 33.6 MiB; native geometry not called.

`run_directed_cycle_policy.py` samples 4,096 distinct same-size unordered
triples per view, each touching at least two current physical-view conflicts,
and proposes both directions. It excludes every previously measured triple,
including held-outs. Eligible unmeasured support is 1,730,948 individual /
45,564 overview triples. The frozen model ranks 8,192 directions per view in
chunks of 256 and submits its first 1,024. The existing interior-anchor
decoder converts each word to three simultaneous slot translations. Native
geometry only decodes, measures and accepts; no search or repair.

- Individual directed cycles: 296 legal / 728 hard, best legal 1,979;
  9.044 s wall, 57.5 MiB peak. Retained 1,964.
- Overview directed cycles: 476 legal / 520 hard / 28 projection,
  best legal 310; 8.007 s wall, 57.4 MiB peak. Retained 289.

All 16,384 scores, candidate support, rankings and exact action wires replay.
None of the hard-rejected unit cycles has a better visual count before the
hard gate either. Tiny exhaustive support checks, seeded sampling, measured
triple exclusion and two/three-conflict eligibility pass; validation peak
33.0 MiB. This does not establish an unavoidable layout floor.

**Matched endpoint controls:** `run_cycle_endpoint_transfer.py` retains the
same first 128 neural words/scores/card deltas and replaces only port pooling
with circular means or independent endpoint offsets. Ambiguous circular
means reject without fallback. `run_collinear_cycle_transfer.py` additionally
uses source-collinear interior anchors on non-tangent source rays; tangent
source rays retain the original fixed interior anchors. Source anchors are
fixed once; no candidate repair. Each of six controls submits 128 actions,
with no improvement. Legal counts (individual / overview): circular 53/64,
independent 55/70, collinear 55/70. All observed endpoint groups have no wrap
discontinuity, so the arithmetic wrap example does **not** explain this
observed plateau. Endpoint algebra, strict anchors, zero-source preservation
and native zero-action measurement pass; numerical validation peak 40.4 MiB.
These frozen controls were **not trained on the changed endpoint decoder**.

**Read-only native attribution:** `ml_batch_hard_diagnostics.cpp` reuses the
unchanged batch decoder and gates, supports only DIAG/QUIT, and never proposes,
commits, repairs or saves geometry. It decomposes hard counts into own-card
interior, adjacent-edge proper crossing, forbidden endpoint contact and
outward-boundary failure with the gate's exact pair multiplicity. The first
32 words of each of eight stages (256 observations) exactly reproduce their
recorded native results. All attributed failures are adjacent-edge crossings;
the other three components are zero in these prefixes. Overview projection
failures are left unclassified. Source geometry and counters remain unchanged,
including after repeat observations and zero actions. This is prefix evidence,
not attribution for every proposal. Self-tests pass 96 hard-component sums,
576 full batch comparisons and 1,764 sparse comparisons. Observation wall
32.168 s, peak 51.1 MiB. The first compile failed from a local helper-name
collision (175.8 MiB); its snapshot is retained. Corrected build peak 204.9
MiB. The old environment and all its inputs remain unchanged.

**Matched smaller-movement controls:** `run_fractional_cycle_transfer.py`
retains each model-selected word and score but decodes fixed fractions
0.001 / 0.005 / 0.02 / 0.1 of its three simultaneous translations. Fractions
were defined before measurements, not selected by native geometry. Each of
eight stages submits 128 actions; no improvement. Legal counts by fraction
are individual 87/40/29/24 and overview 125/65/45/25. Individual ties are
49/10/2/1; overview ties are 97/26/12/0. Remaining legal outputs are worse;
no hard-rejected output has a better visual count. Tiny movement increases
legality but often leaves the objective unchanged. The unit-fraction critic
was **not trained on these fractions**. No coordinate fallback or search.
128 algebra checks pass, including exact zero source and unit-action parity
(40.3 MiB). Sequential inference peak 64.3 MiB. Fractional replay passes all
1,024 wires and unchanged saves at 54.7 MiB.

Single-process verification reconstructs the data and all training updates,
replays the two primary and six endpoint stages, and checks saved physical/
full positions and canonical routes against source bytes. Peak **114.9 MiB**.
The separate fractional verifier covers the other eight stages. These record
replays do not recompute native scores; the diagnostic prefixes do. All jobs
are serialized, one math thread, nice +10, under the 256 MiB process-group
RSS guard. Archive sealing uses a reduced 128 MiB guard and peaks at 37.8
MiB. This is not a hard CPU-percent quota. Two missing endpoint-inference
peak measurements remain null rather than being invented or rerun.

**Next research:** the new coordinated-cycle and fractional results are
available for action-conditioned training; they have not yet trained a model.
The present critic only trained the old unit-cycle outcomes. Keep whole-triple
validation and exclude the complete parent measured set from fresh inference.
Distinguish stochastic model exploration from repeated greedy prefixes: the
current ranking has no positive actual-source training examples. Do not repeat
these fixed endpoint/fraction controls unchanged, weaken hard gates, or treat
lower prediction loss as target achievement. Gate rejection alone cannot
explain the absence of gain in the legal outputs.

Archive: `data/erd-poc/experiments/independent-views-directed-cycles-20261003/`.
Manifest SHA `7edd561798e51ef2081468c1fd7cf4d4ceb36fbe25ff360b5e8076da4313610c`.
All 196 new compressed files (11,752,623 logical bytes) and 44 referenced
prior files (6,341,339 logical bytes) restore and hash correctly. Retained
primary SHA remains `da2317d3d9fe72181587f543843bfdde3cc7030ca328e93c4f627ae21ec356e7`.
Do not rerun exclusive training1, any of these 16 inference stages, validation
writers, the diagnostic observer, records verifiers or sealer unchanged.

### Earlier continuation: new active outcomes and neural three-card cycles

Retained **289 / 1,964**; **150 / 750 remains active and unmet**. The previous
goal turn is progress: it implemented full-context ranking and supplied new
measured active-conflict outcomes that changed this turn's training. This turn
uses those outcomes, tests an expanded action family, and reduces verification
memory. It produces no improved product candidate or promotion. All numerical
handles from this turn are terminal; no CLI troubleshooting changes.

`train_active_pair_outcomes.py` adds 1,948 genuinely new measured pairs to the
parent's dataset, removing 100 duplicates. The combined 3,985 rows contain
2,400 directly conflicting pairs and zero improvement examples. Validation
holds out 486 new pairs absent from the parent's **complete measured dataset**,
stratified by view and legality; these are the same two layouts, not unseen
graphs. Source input bytes and original measured actions replay before training.
The old normalization is retained. All 4,380 Adam updates replay bitwise;
epoch 54's 3,942-update checkpoint is selected. Held-out Huber decreases
3.33171 -> 2.23674, without a verified decrease in product visualCross.

Checkpoint: `.tmp/visualcross-ml-150-750-20261003/active-pair-model1/training1/model.npz`,
SHA `0d2e71959ef3d8c889a0954885f52ce390974ff20c89a87a7ade11a023d4f985`.
`run_active_pair_policy.py` excludes every previously measured pair, including
validation rows, from the inference vocabulary. The frozen network ranks
43,788 unmeasured eligible individual pairs and 6,523 overview pairs. It submits
1,024 per view, with no repeated measured actions and no accepted improvement.

`run_neural_cycle_policy.py` draws 4,096 distinct equal-size unordered triples
uniformly, conditioned on direct physical-view conflict and excluding three
isolated single cards. It proposes both cycle directions (8,192 per view).
Each proposal permutes three source slots at once, decoded with the existing
source-preserving interior-anchor endpoints. It is ranked by the mean of three
trained single-pair neural predictions. **The critic is not trained on cycle
outcomes, and its additive proxy assigns both directions exactly the same
score.** Stable ordering tests both directions. Native geometry only decodes,
measures and accepts; no new native code, search or coordinate repair.

Actual stages, each 1,024 proposals and zero accepted improvements:

- Individual unmeasured pairs: 605 legal / 419 hard; three ties, best legal
  1,964; 8.153 s wall, 112.4 MiB peak.
- Overview unmeasured pairs: 653 legal / 352 hard / 19 projection; no ties,
  best legal 291; 7.339 s wall, 73.6 MiB peak. Retained source remains 289.
- Individual cycles: 439 legal / 585 hard; no ties, best legal 1,977;
  9.899 s wall, 71.1 MiB peak.
- Overview cycles: 769 legal / 255 hard; no ties, best legal 298;
  8.250 s wall, 61.8 MiB peak.

All 50,311 unmeasured-pair scores, 16,384 cycle-proxy scores, exact rankings
and 4,096 complete action wires replay. Saved physical/full positions and
canonical routes are byte-identical to the earlier source saves. Replay does
not recompute native scores. Functional cycle checks cover both views, seeded
vocabulary replay, same-size and direct-conflict support, exact three-card
delta construction and distinct inverse outputs. Independent scalar factor
scores agree within 3.56e-15. Training peak 113.4 MiB; functional cycle
validation 46.5 MiB; sealing 37.2 MiB. No browser check or new product audit.

**Resource change:** initial verification with separate replay subprocesses
peaked at 249.3 MiB. The same verification now executes replays sequentially
inside one Python process and passes at **143.2 MiB**. Keep this version.
All numerical jobs remain serialized, one math thread, nice +10, under the
256 MiB process-group RSS guard. This is not a hard CPU-percent quota.

**Evidence that changes the next action:** 1,866 / 2,048 cycle proposals move
at least two owners that are not isolated single cards, so the cycle experiment
does not merely shuffle inert cards. However, only 88 / 2,048 proposals touch
at least two directly conflicting owners (72 individual / 16 overview).
Only four touch three directly conflicting owners.

Direction blindness is explicit and measured: all 1,024 forward/inverse
direction pairs have identical neural proxy scores, but **1,022 pairs have
different measured outcomes**. Individual: 510 / 512 differ, including 189
legality differences and 123 pairs whose two legal directions have different
visual counts. Overview: 512 / 512 differ, including 135 legality differences
and 303 pairs whose two legal directions have different visual counts. These
figures can be reconstructed from the archived consecutive action rows.
The current additive pair proxy cannot express this distinction. The next
model should encode the ordered cycle direction and learn actual cycle
outcomes, with candidate support that includes coordinated conflicts. The
2,048 cycle outcomes have not yet been used for training. These observations
do not prove the target impossible or establish an unavoidable decoder floor.

Archive: `data/erd-poc/experiments/independent-views-active-pairs-cycles-20261003/`.
Manifest SHA `71a9241f281efc2ec0bc811d7f9a605790ff34f4971109cda698970ede05ef87`.
All 67 new compressed files (6,132,636 logical bytes) and 48 referenced prior
files (6,276,699 logical bytes) restore and hash correctly. The retained primary
candidate SHA remains `da2317d3d9fe72181587f543843bfdde3cc7030ca328e93c4f627ae21ec356e7`.
Do not rerun the exclusive sealer, training1 or these four inference stages.

### Earlier continuation: full conflict context and complete pair ranking

Retained **289 / 1,964**; **150 / 750 remains active and unmet**. The new
**12,993-parameter** critic ranks every eligible same-size pair in
bounded chunks, then submits its first 1,024 proposals per view. All 2,048
submitted swaps touch a current physical-view crossing or card hit, but none
strictly improves the objective. Saved positions and routes are byte-identical
to the established source saves. No new product candidate, promotion or browser
check. All numerical handles are terminal.

`full_context_pair_model.py` retains the old 64 pair features and appends
native features `24:56` and `60:64` for both cards: 136 inputs preserve every
native node feature, including all four observed conflict segments. The old
adapted checkpoint is warm-started with zero extra weight rows; initial
predictions are exactly preserved. Coordinates and model names are not inputs
or trainable tables. The four previous immutable-source stages reconstruct
2,037 unique measured examples, of which 452 touch direct conflicts and zero
are actual improvements. Active examples receive four-fold sampling. Only
pairs absent from the parent checkpoint's training stages enter validation:
251 validation pairs, **19 active validation pairs**, on these same two
layouts, not unseen graphs. All 2,000 Adam updates replay bitwise; the selected
epoch-17 checkpoint contains 425 updates. Active-validation Huber decreases
4.13264 -> 3.65315; this does not establish product improvement.

Checkpoint: `.tmp/visualcross-ml-150-750-20261003/full-pair-model1/training1/model.npz`,
SHA `d82cd4d2dc81a5f5a8bb972a8db0e7c2bdd4560c1f6144e5d4babb4c0a2e46cb`.
`run_full_context_pairs.py` ranks the entire eligible vocabulary using only
the frozen network, in chunks of 2,048. The structural mask requires a direct
physical-view conflict and excludes exchanges of two isolated single cards.
It does not cover conflicts affected only indirectly by pooled ports. A
combinatorial count independently checks vocabulary completeness within this
scope. Native geometry remains an immutable-source decoder and evaluator,
with no search, coordinate repair or new native-code changes in this turn.

- Individual: 45,122 eligible / 114,546 same-size pairs; 1,024 submitted,
  611 legal / 413 hard, one tie, best legal 1,964. Wall 12.727 s, peak 116 MiB.
- Overview: 7,589 eligible / 72,117 same-size pairs; 1,024 submitted,
  680 legal / 329 hard / 15 projection, zero ties, best legal 290.
  Retained source remains 289. Wall 8.041 s, peak 78.3 MiB.

All 52,711 neural scores, feature chunks, rankings and 2,048 exact action
wires replay. Replay does not recompute native scores. The existing unchanged
native evaluator's earlier validation remains applicable. The expanded-feature
test reconstructs all native inputs and checks 31 finite differences, including
eight added weight rows; maximum error 6.65e-11. Verification peak 167.1 MiB;
training 80.2 MiB; feature validation 32.6 MiB; archive sealing 35.4 MiB.
All jobs are serialized, one math thread, nice +10, with the 256 MiB group RSS
guard; this is not a hard CPU-percent quota.

**Evidence for the next change:** the model predicts improvements for 72 of
the submitted pairs (13 individual / 59 overview); none improves in native
evaluation. The previous avoidance of directly conflicting pairs is fixed,
but scoring these active actions remains inaccurate. Training still contains
no positive improvement examples and only 19 active validation pairs. The
new 2,048 measured active-conflict outcomes have not been used for training.
Further work should change the learning signal or action family, not rerun
these frozen stages or merely extend neutral walks. This does not prove the
target impossible or establish the decoder as the cause of the plateau.

Archive: `data/erd-poc/experiments/independent-views-full-context-pairs-20261003/`.
Manifest SHA `6d999d68bba87603e5316f04ffcbc9d4d0d33f01f746e727802dfbd8c96148c1`.
All 46 new compressed files (3,478,050 logical bytes) and 49 referenced prior
archive files (9,124,696 logical bytes) restore and hash correctly. Prior
immutable archives are referenced to avoid duplicating fixtures and binaries.
Do not rerun the exclusive archive writer, training1 or either full1 stage.

CLI clarification: terminal CLI 0.160.0 was rechecked. Existing retained
diagnostics contain 24 transport events and do not establish a resource-related
cause or complete coverage of the affected terminal process. No fresh broad
scan, `codex doctor`, settings change or authentication change was performed.
The pending approximate-error-time question remains relevant. `codex resume`
can select the interrupted CLI session; no terminal session was restarted here.

### Earlier continuation: neutral pair trajectories followed by bounded models

Retained **289 / 1,964**; **150 / 750 remains active and unmet**. This turn
tested 1,189 cumulative pair proposals and 7,293 subsequent bounded-translation
proposals. The pair models admitted 20 neutral states and the follow-up models
accepted 597 neutral moves, with **zero strict objective improvements**.
Four complete experimental product candidates pass verification; none is
promoted. No numerical process from these stages remains running.

The new joint native environment adds `OBS <complete neural action>`: it
decodes and measures exactly the supplied geometry, then emits its 64 native
node features if legal. It never commits the observation, changes the original
source/best state, increments action counters or selects an alternative action.
Startup/TRY/MEASURE/SAVE behavior is retained. Binary and exact build inputs:
`.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/`.
The build uses C++17 and `-O0 -ffp-contract=off`; source SHA
`0bf63bb9502dfbfcea70f09a4db09a30885f35a022324f54da85f75f01bd1301`.

`run_anchor_pair_walk.py` composes model-selected equal-size slot transpositions
into one permutation. Every candidate is decoded against the original source
and its pooled interior anchors, so translations do not accumulate rounding
drift. A fresh model observation follows each admitted nonregressing state.
The model alone ranks each seeded uniform pair vocabulary. The sole structural
mask excludes exchanges between two isolated **single** cards, whose same-size
rectangle multiset and routes cannot change; multi-member owners remain
eligible. Previously visited permutations are skipped. Native SAVE retains
only strict improvements, independently of the experimental neutral walk.

Both walks use the previous adapted 8,385-parameter pair checkpoint, unchanged:
`pair-anchor-sparse1/anchor-outcomes-v1.npz`, SHA
`9b25f5ab8be75151f60a7796baa4616a487a322e708da93ab5da85bc9432ba0a`.
**No new weights are trained in this continuation.** Each walk has a 20-second
active-work cap, 1,024-considered-proposal budget, 16 rounds, and at most 128
model-ranked proposals per round. Observations, source permutations, neural
score hashes, full action wire hashes and admissions are retained and replayed.

- Individual walk: 1,024 proposals, 4 neutral admissions, 0 strict improvements;
  14.823 s wall / 75.3 MiB peak. Native reasons: 884 legal, 140 hard.
- Overview walk: 165 proposals, 16 neutral admissions, 0 strict improvements;
  15.379 s wall / 69.7 MiB peak. Reasons: 132 legal, 33 hard.
- Follow-up individual bounded model: 4,125 proposals, 324 neutral admissions,
  still 1,964; complete workflow 22.531 s / 134.4 MiB peak.
- Follow-up overview bounded model: 3,168 proposals, 273 neutral admissions,
  still 289; workflow 28.593 s / 184.3 MiB peak. Its auxiliary individual
  score 4,459 is **not** the independent retained 1,964.

`export_anchor_pair_walk.py` first replays the model trajectory, then performs
one fixed decoding of its final permutation to create a downstream model input.
The existing experimental admission allowance is set to 1 solely to serialize
a tie; assertions require actual objective nonregression (observed regression
is zero). This does not alter either strict-best walk output or the promoted
candidate. Source input bytes, checkpoint, trajectory, native decoder and
decoded full positions/routes are verified. The overview apply script now
names this architecture accurately in experimental candidate metadata.

Both neutral snapshots and both follow-up candidates pass actual product
renderer checks: complete cards/relationships, unchanged card dimensions,
straight outward-facing boundary endpoints, zero hard/spacing violations,
and area <=1.5e9. No actual browser QA. The individual follow-up area is
1,442,970,448.675, but its objective remains 1,964. Snapshot peaks are
174.1 / 191.9 MiB. The native suite still passes 576 original and 1,764 sparse
comparisons. Additional actual-view protocol checks compare eight measurements
with the preceding native binary, test zero/neutral/composed/invalid states,
verify fresh feature scales, unchanged source observations and untouched action
counters. Observation validation peak: 68.2 MiB; compile peak: 205.9 MiB.

**New diagnosis that changes the next action:** only 107 / 1,024 individual
walk proposals and 7 / 165 overview proposals touch direct objective conflicts
according to the native local-cost features. **None of the 20 admitted states
touches those direct conflicts.** This flag does not include indirect effects
of shared-endpoint pooling. Most neutral wandering still avoids the difficult
part of the objective; increasing these walks unchanged is not justified.

The pair ranker also consumes only native node feature slices `0:24` and
`56:60`. It drops **all four explicit conflict-line observations (`32:56`)**,
even though those observations were computed and archived. This is a concrete
representation limitation, not a proven cause of the plateau. The next useful
model change is to retain the full conflict geometry and focus candidate
support on current conflicts. For example, append omitted node features to
the old pair input (136 dimensions can retain all original 64 features plus
36 omitted features per node), warm-start existing weights with zero extra
rows, and learn from already measured/replayable outcomes. No such expanded
model or pressure-conditioned candidate policy is implemented yet. Candidate
coverage can be widened through batched neural ranking, without native brute
force geometry search, if measured resource use permits it.

Archive: `data/erd-poc/experiments/independent-views-pair-walk-20261003/`.
Manifest SHA `418e96cec17920817e0a9c50b064b88d0d256df5f8fc1181a5614872523276f0`.
All 232 compressed files (31,587,185 logical bytes) round-trip and hash correctly;
sealing peak 38.1 MiB. `support-analysis.json` records the diagnosis. The
exclusive sealer and these six stages must not be rerun unchanged. All numerical
jobs stayed serialized, one math thread, nice +10, under the 256 MiB group guard;
this is not a hard CPU-percentage quota. No CLI troubleshooting changes this turn.

### Earlier continuation: learned anchor pair proposals and exact sparse scoring

Retained **289 / 1,964**; the 150 / 750 target is still unmet. Four frozen-model
stages emitted **2,048 nonidentity equal-size card swaps**, with zero accepted
improvements. All four saved position/route snapshots are byte-identical to the
unchanged source saves from the reference evaluator. No product candidate was
created or promoted, and no numerical worker remains. This addresses the prior
identity-output failure, but does not establish that pair swaps can reach the
targets or prove that the targets are impossible.

`ml_joint_batch_environment.cpp` now has opt-in `--sparse-scoring 1`. It preserves
the complete decoder and acceptance gates, measures exact score differences
against private copies of the immutable source, and includes moved cards,
changed lines, indirectly pooled endpoints, incident edges and projected
endpoint-identity changes. It cannot select, alter or repair coordinates.
The default full-scoring path remains available. The frozen joint runner does
not enable this flag automatically; the new pair runner explicitly enables it.

The new native binary is
`.tmp/visualcross-ml-150-750-20261003/pair-anchor-sparse1/environment`, compiled
with C++17, `-O0 -ffp-contract=off`; exact build inputs are retained. Its tests
pass the existing 576 comparisons plus **1,764 sparse comparisons**, including
repeated noncommitting actions and legal/frame/spacing/hard outcomes. Actual
Captain validation compares 24 actions per view across v7, new full scoring
and new sparse scoring: responses, initial observations and unchanged saves
match exactly. Per-view measurement-and-save times were 3.436 / 2.433 seconds
for v7 and 0.458 / 0.344 seconds for sparse scoring. These are small timing
samples, include SAVE, and are not an application-wide performance claim.

`run_anchor_pair_policy.py` reuses the trained 8,385-parameter pair MLP but
changes the decoder from unrestricted canonical-ray swaps to equal-size swaps
with source-preserving pooled interior-anchor endpoints. A seeded uniform
vocabulary of 4,096 pairs is ordered only by model scores; the first 512 are
evaluated from the same source. No geometry-based proposal ranking or repair.
The 64-feature schema matches the old model; independently reconstructed
native length scales agree within 5e-12. Source positions and endpoints are
fixed decoding data, never trainable coordinate tables.

The first two stages are frozen transfer, with **no new training**. Their 1,024
measured results then train `train_anchor_pair_outcomes.py`, a Huber regression
of exact visual outcomes with penalties for invalid geometry. All six network
parameter groups change. **280 updates** execute; epoch 31's **217-update**
checkpoint is selected. Held-out action loss declines 4.08585 -> 2.51749;
the split holds out actions on these same layouts, not unseen graphs. There
are **zero actual improvement examples**. Lower regression loss does not
mean lower product visualCross. A new vocabulary seed is used for the two
adapted-model stages, so legality differences are not a controlled ablation.

Actual stages (each 512 nonidentity proposals, zero accepted):

- Individual transfer: 200 legal / 312 hard; 5.254 s wall, 69.1 MiB peak.
- Overview transfer: 367 legal / 137 hard / 8 projection; 4.224 s, 67.1 MiB.
- Individual adapted: 418 legal / 94 hard; 5.180 s, 66.7 MiB.
- Overview adapted: 440 legal / 71 hard / 1 projection; 3.967 s, 66.2 MiB.

All 2,048 neural rankings and full action wire hashes replay; every saved
geometry matches the source. All 280 Adam updates, history losses and selected
weights reproduce bitwise. The new regression gradient passes 31 finite
differences (maximum error 2.80e-11). Replay does not recompute all 2,048 native
scores. Compile peak was 203.9 MiB, actual-geometry validation 80.2 MiB,
training 48.6 MiB, record verification 88.8 MiB, archive sealing 32.5 MiB.
All numerical jobs stayed serialized under the 256 MiB group guard, one math
thread and nice +10. This does not enforce a hard CPU percentage.

Archive: `data/erd-poc/experiments/independent-views-anchor-pairs-20261003/`.
Manifest SHA `665f7d88ea6c816e0da21a50a8390f916cb4e4e3be86ca5739632114bd1b31e8`.
All 121 compressed files (14,250,236 logical bytes) restore and hash correctly.
Adapted checkpoint SHA
`9b25f5ab8be75151f60a7796baa4616a487a322e708da93ab5da85bc9432ba0a`.
Do not rerun the exclusive archive writer or these four stages unchanged.
Further work needs a material change in model proposal support or learning
signal; these sampled one-pair actions supply no positive improvement labels.

CLI side issue: the user confirmed terminal Codex CLI, locally version 0.160.0.
The bounded read-only log scan from 15:29:29 to 16:50:09 KST on October 3 found
three warning/error rows and no new matching transport errors. The affected
error's approximate time is requested asynchronously and still pending.
No CLI settings, authentication or running user sessions were changed. Do not
rerun `codex doctor` (its previous RSS-guard failure remains relevant), and do
not infer that visualcross resource use caused the transport error.

### Earlier continuation: differentiable slot training and corrected assignment relaxation

Retained **289 / 1,964**; the 150 / 750 goal remains active and unmet. Six
new stages completed **1,019 network updates and 262 frozen hard-output
batches**. All 262 proposals were legal; **259 were exactly the zero-action
wire output**. The remaining three overview proposals scored 360. None improved
the stable product, and no numerical workers remain. This evidence points to
insufficient proposal diversity, not rejection by the hard-validity gate.
Do not rerun these six stages unchanged.

`joint_relaxed_slot_policy.py` adds `RelaxedSlotAnchorPolicy`. Inference keeps
the existing hard equal-size slot sorting and source interior-anchor endpoints.
A separate training forward uses a finite log-space Sinkhorn relaxation:
eight row/column normalization passes and a final row normalization. Scores
are centered and normalized within shape groups; fixed source relaxation is
subtracted so zero neural output preserves every source position and endpoint.
All 6,273 shared MLP parameters can receive gradients. Card coordinates, shape
groups and endpoints remain fixed decoding data, never trainable lookup tables.
The complete continuous training forward is differentiated; no straight-through
derivative of the hard assignment is claimed.

The runner exposes `--slot-relaxation`, requires active-pair continuous loss,
and uses chunks of 4,096. At most 262,144 within-shape pair entries may be
stored (Captain needs 230,336 individual / 145,269 overview). Only hard frozen
`forward()` outputs are submitted to the native environment. Training can use
`--relaxed-spacing-weight 0` because the hard decoder retains the source
rectangle multiset; the native spacing gate is unchanged. This opt-in flag
does not change other models' loss defaults. Temperature and effective spacing
weight are recorded and checked during product auditing.

**Material diagnosis and correction:** version 1 used fixed uniform target ranks.
Its finite normalization did not approach the hard permutation reliably at low
temperature. At saved final weights, reducing temperature from 0.1 to 0.05 left
maximum soft/hard position gaps of **177.264 individual / 450.699 overview**.
A seven-score fixture even mapped distinct nodes to the same most-likely slot.
These were limitations of the surrogate, not failures of hard product geometry.

Version 2 uses **sorted current score values** as the kernel's column targets,
including their locally exact value derivatives. It fixes the nonuniform-rank
fixture to maximum assignment error 8.70e-17. On the exact same saved weights,
temperature 0.05 reduces the position gaps to **0.000962 / 0.000788**; hard
inference outputs stay bitwise identical. This comparison is diagnostic only:
it changes a training-version buffer, performs no training or new native
measurements, and promotes no candidate. At temperature 0.1 the same-weight
gaps are still 23.31 / 126.10, so do not claim all-temperature equality or a
guarantee for arbitrarily tied scores. Actual v2 training stages used end 0.1.

The checkpoint `relaxed_slots` buffer preserves version 1 behavior when loading
old models. New training defaults to version 2; `--slot-relaxation-version 1`
is explicit legacy behavior. Generic loading recognizes the relaxed-slot marker
before the existing slot/anchor markers. Warm loading verifies the version
buffer and other source-bound buffers, preventing silent version transfer.

Both validation suites pass **51 finite differences each**: 23 assignment
derivatives, 22 actual-view joint action derivatives and six full geometry-loss
derivatives. Both views preserve zero outputs at temperatures 0.1, 0.5 and 2,
replay existing hard outputs, and load checkpoints exactly. Version 2 also
bitwise-replays four version-1 relaxed outputs using the archived old module.
Validation peaks were 176.5 / 183.8 MiB. Neither validation performs new native
geometry measurements or claims a derivative of hard assignment. Exact source
snapshots for both versions and every stage are preserved.

Actual stages (updates / frozen batches; peak RSS):

- `individual-relaxed-slot1`, v1, soft spacing weight 20: 156 / 40; 210.1 MiB.
- `overview-relaxed-slot1`, v1, weight 20: 248 / 63; 176.9 MiB.
- `individual-relaxed-free1`, v1, weight 0: 154 / 40; 224.9 MiB.
- `overview-relaxed-free1`, v1, weight 0: 256 / 65; 182.3 MiB.
- `individual-sorted-relaxed1`, v2, weight 0: 101 / 27; 232.6 MiB.
- `overview-sorted-relaxed1`, v2, weight 0: 104 / 27; 182.2 MiB.

All saved scores, candidate hashes and complete product checks match the prior
source: individual SHA `42adab07b311c16463bd7f5dc0ee68941d6047ac0372415393ad9355c126fe22`;
overview SHA `117f0609ec0b2a1a02f5d8fbe40223ab43e3f47bb603d8724a3e1a36eb82abd8`.
All cards/relationships, dimensions, boundary endpoints, spacing and the 1.5e9
area limit remain verified. Overview's auxiliary individual 4,447 is not the
independently retained 1,964. No actual browser QA. Wall-clock temperature
schedules differ, so the training trials are not a controlled causal ablation.

An exact source conflict enumeration also checks the slot family's support.
It conservatively allows arbitrary same-size permutations and includes indirect
endpoint changes from pooling. Individual can move 1,197 cards; overview can
move 949 physical / 963 full cards. Fixed conflicts are only **6 individual /
4 overview**, with named edge/card witnesses; initial totals match native
1,964 / 289. These bounds do not rule out either target and do not prove that
the targets are attainable. Source-slot constraints alone are not a sufficient
explanation for the observed unchanged outputs. Analysis peak: 44.3 MiB.

Next work should test a learned proposal distribution that actually changes
card order, with exact native feedback and reproducible inference. Consider
nonlocal/stochastic model proposals and check the existing `learn_pair_policy.py`
before duplicating prior pair-policy work. Merely increasing iterations of this
deterministic scalar-ranking path is not justified by the 259 identity outputs.
This next proposal family is **not implemented** by the current continuation.

Archive: `data/erd-poc/experiments/independent-views-relaxed-slots-20261003/`.
Manifest SHA `ff7361d3f42483722771f4921ba1fa09082ed6fafafb36f670f6a8c6c923a0f0`.
596 restored-hash-verified files; 64,663,624 logical stored bytes and 56,761,049
unique bytes. 207 hardlinks share identical copies **only within the new
archive**, never mutable source files. Archive peak: 23.1 MiB. Contains both
validation versions, six full stages, gap diagnoses, fixed-support witnesses,
current sources, native binary, stable layout/audit and pre-append provenance.
Do not rerun exclusive validation, diagnostic or sealing writers.

Native v7 was unchanged; actual build inputs remain in the verified global-order
archive. Jobs remain serialized, one math thread, nice +10, with the 256 MiB
process-group guard. Each training limit is 20 seconds; replay/product audit
time is additional. This is not a hard 20% CPU quota. No CLI diagnosis or
configuration changes occurred; the affected terminal/time question is pending.

### Previous continuation: source-preserving slot models and bounded reward caching

Retained **289 / 1,964**; the 150 / 750 goal remains active and unmet. Six
completed model stages produced 475 network updates and 120 frozen batches;
all saved source geometry and passed full product audits. No numerical workers
remain. The substantive improvement is avoiding repeated native measurements,
not a lower crossing count. Do not repeat these stages unchanged.

`joint_slot_anchor_policy.py` composes equal-size source-slot permutations with
fixed interior-anchor endpoint rays. `joint_anchor_ray_policy.py` extracts the
unchanged endpoint operations into `InteriorAnchorEndpoints`; four legacy
checkpoint actions replay exactly. The generic loader recognizes the new
`slot_anchor_rays` marker before the existing anchor marker. The runner permits
shared interior-anchor endpoints with slot ordering in either view. Only the
32 shared output-head weights are trained. Sorting and endpoint decoding do
not optimize geometry, search alternate positions, or repair proposals.

Slot validation attempts and their exact sources are preserved:

- Attempt 1 required bitwise zero-endpoint equality and failed on three of
  6,908 float components, maximum error 7.105427357601002e-15. The corrected
  check uses actual quantized endpoint decoding and the established 1e-8
  geometry tolerance. This was a validation correction, not a model change.
- Attempt 2 completed individual checks, then raw lexicographic rectangle
  sorting mispaired overview points whose nominally equal x values differed
  at the last floating bit. Attempt 3 matches rounded coordinate keys, still
  requiring every rectangle coordinate within 1e-8 and exhausting the multiset.
  It reuses completed individual/legacy checks only by identical source/binary
  hashes. Both failed attempts remain archived.
- Combined completed checks cover all 3,454 zero-output endpoints per view,
  12 rectangle fixtures, exact class/generic checkpoint loading, 24 actual
  native reward probes and six replayed Adam updates. Larger untrained fixture
  permutations move up to 969 individual / 741 overview cards, but can violate
  hard or projection constraints. The decoder does not guarantee validity.
  Validation peaks were 52.7 / 64.3 / 56.6 MiB.

`joint_reward_training.py` now has an opt-in, per-trainer LRU cache of up to
128 **full identical MEASURE command strings**. The native batch environment
always evaluates against its immutable source; only these noncommitting results
are reusable. TRY acceptance is never cached. Cache entries are isolated between
trainers and returned result mutations cannot corrupt them. Default size zero
preserves the previous trace. The runner exposes `--reward-cache-size` only for
slot reward training and records logical probes, actual native calls and reuse
separately. Trace replay also validates cache hits and repeated result equality.

Cache validation reuses recorded results from the first three actual stages;
it performs **zero new native geometry evaluations**. All original Adam updates,
final weights and wire actions match exactly with a cache of 128. Cache size
zero reproduces the original individual trace hash; size two exercises eviction
on the overview trace without changing training. LRU eviction, result/source
isolation, rejecting TRY and invalid sizes, and detecting changed cache flags
or results pass. Validation peak: 54.2 MiB. This establishes semantic parity;
it is not itself a measured runtime speedup.

Actual completed stages (updates / logical probes / native MEASURE calls /
reused results / frozen batches; every saved score remains its source):

- Individual slot-anchor1: 17 / 136 / 136 / 0 / 5; peak 143.8 MiB.
- Overview slot-anchor1: 24 / 192 / 192 / 0 / 6; peak 182.1 MiB.
- Individual slot-graph1, 16 graph channels: 32 / 128 / 128 / 0 / 8;
  peak 140.7 MiB. Only 23 probes were legal and none improved 1,964.
- Individual slot-cached1: 116 / 928 / 94 / 834 / 29; peak 145.6 MiB.
- Overview slot-cached1: 256 / 2,048 / 75 / 1,973 / 64; peak 175.6 MiB.
- Overview slot-graph1, 16 graph channels and cache: 30 / 240 / 192 / 48 / 8;
  peak 180.5 MiB.

Totals: **3,672 logical training probes, 817 actual native MEASURE calls,
2,855 reused results, 475 updates and 120 uncached TRY batches**. Validation
fixtures are separate. Both cached geometry-only runs reproduce their original
actual native training prefixes exactly (17 / 24 updates), then extend them
under the same 20-second training limit. That limit excludes later replay and
product auditing. Individual saves retain SHA
`42adab07b311c16463bd7f5dc0ee68941d6047ac0372415393ad9355c126fe22`;
overview saves reserialize unchanged geometry as
`117f0609ec0b2a1a02f5d8fbe40223ab43e3f47bb603d8724a3e1a36eb82abd8`.
Overview's auxiliary individual score 4,447 is not the retained independent
1,964. All cards, relationships, dimensions, spacing, hard validity and area
limits are verified. No candidate was promoted and no actual browser QA ran.

The native v7 binary was not rebuilt. SHA
`e2a58da4e295d6fd26f9473d1ff371f7cea7ad7e4685b786cefa3838b58d3082`;
its actual build inputs are in the verified global-order archive (manifest
`b9fa65014a331faf993beda3ad6a8241400637bdc7df4072519a47ef2ea07e3b`).
Later component source snapshots are not the v7 build inputs. Jobs remain
serialized, one math thread, nice +10, under the 256 MiB process-group guard;
this is not a hard 20% CPU quota.

Archive: `data/erd-poc/experiments/independent-views-slot-anchor-20261003/`.
Manifest SHA `5625bc792b0c4595909682225f7cc5d06d2f18b019c1d826114e184f6012ca16`.
539 files / 43,319,903 stored bytes, restored hashes verified; archive peak
28.6 MiB. Contains all failed and successful validation attempts, all six stages,
current sources, binary, cache parity proof, stable layout/audit and pre-append
provenance. Do not rerun exclusive validation/sealing writers.

Next research must change the learned proposal family or its training signal:
additional identical scalar-ranking-head training did not improve either view.
Keep the cache for compatible immutable-source reward jobs, but do not count
cache hits as new measured geometry or infer target impossibility from this
plateau. A coordinated graph reordering or endpoint policy remains untested
by this experiment.

CLI follow-up remains pending for the affected terminal/session and latest
error time. This continuation fetched official troubleshooting documentation
and made a lightweight read-only check of selected config keys/log filenames;
it did not rerun doctor, scan session history again, or change CLI configuration.
The body-decoding error's root cause and recovery remain unconfirmed.

### Previous continuation: explicit two-separator contexts and their reachability limit

Retained **289 / 1,964**; the full 150 / 750 goal remains active and unmet.
This continuation implemented and tested a new learned-action scope, then
established that this scope alone cannot reach either target. It did not
produce a numeric improvement. All numerical jobs finished.

`learned_pair_contexts.py` and its independent C++ counterpart
`learned_pair_contexts.h` compute source-bound two-vertex separator groups from
the dominant biconnected edge block. They examine pairs from its 32 highest
degree vertices, retain nontrunk components, include their outside attachments,
deduplicate memberships, and assign stable independent context indices.
Coordinates and geometry scores are not used to choose memberships. This is
not an exhaustive all-pairs decomposition.

The explicit `pair-cut` mode uses `source-bound-pair-contexts-v1` and
`independent-context-index-v1`. It has 30 individual / 18 overview contexts;
context index 0 does **not** mean physical card 0. Native loading recomputes
the expected map from the source graph and rejects a changed map. Context
features, action bounds, inference, and independent endpoint/position replay
use the same explicit indexing. Existing core/cut modes retain their previous
physical-root indexing. The unchanged 64-feature model representation permits
explicit transfer from cut-trained weights; reports identify both modes.

Build/validation evidence:

- The initial `-O1` compile was stopped by the unchanged 256 MiB guard at
  263.7 MiB. `-O0 -ffp-contract=off` succeeded, peaking at 217.8 MiB. Native
  `component-environment-v27-o0` SHA is
  `bc60819f4d526b92efc036567851e95110f1ed0be88706d416f531ab9fc8c986`.
- All 14,424 native local/full comparisons passed, including 704 new pair
  context comparisons, 320 bounded cases, and 228 accepted grouped moves.
  Noncommitting probes preserve both views and their routes. The Python
  topology fixture covers cycles, attachments, duplicate/reordered edges,
  label renaming and a 1,500-node chain without recursion.
- The legacy 20,000-action cut-policy replay passes with current Python code.
  The first validation then failed because its fixture directory lacked
  `nodes.tsv`; that failure and exact sources are retained. The corrected
  second validation reused those completed checks only after asserting all
  source and binary hashes were identical; it did not rerun them.
- Actual source maps match the prior graph analysis exactly; all 48 translation
  boxes match between Python and C++. Source-only save/replay retains 1,964 /
  289; both deliberately corrupted context maps are rejected. This does not
  assert that a zero moving-ray action preserves every existing endpoint.
  Validation attempt peaks were 77.6 / 63.2 MiB.

Two frozen-model rollouts use the existing bounded-policy-training1 individual
and overview checkpoints. No new model training was performed:

- Individual: 1,907 actions over 20 pressured contexts; 173 accepted neutral
  moves, 953 visual regressions and 781 hard failures. Saved score 1,964;
  candidate SHA `0614eb443b1d8917f224eb2027a3af2cb7cf245922a392fe2c2ccdcdf4878d70`.
- Overview: 1,853 actions over 15 pressured contexts; 167 accepted neutral
  moves, 882 visual regressions and 804 hard failures. Saved overview 289;
  candidate SHA `cf20c93a3d55fb8fe1e59e0aa54db4c9a4735c1b208de4cfec75c79263b71b21`.
  Its auxiliary full-view score 4,459 is not the retained independent 1,964.

Every action was independently replayed and each saved product was audited
with all cards/relationships, original dimensions, hard validity, spacing and
the 1.5e9 area limit intact. Neither candidate was promoted. Peaks were 132.9 /
191.0 MiB. All jobs remain serial, one math thread, nice +10, with the 256 MiB
RSS guard; this is not a hard 20% CPU quota. No actual browser QA.

**Decisive scope evidence:** the union of pair contexts moves only 168 full
cards in individual and 165 physical / 181 full cards in overview. All canonical
members are considered when deciding whether a rendered route could change,
including possible representative changes. Initial conflict enumeration exactly
matches native counts. Individual has **1,357 fixed crossings + 436 fixed card
hits = 1,793** immutable conflicts under this action scope; overview has
**142 + 71 = 213**. Witness lists preserve every edge/card identity. Their 520 /
136 fixed routes and 224 / 53 hit cards remain unchanged after the two actual
model rollouts. The fixed conflicts involve 416 individual / 154 overview cards
outside the move scope. Coverage analysis peaked at 36.8 MiB.

These are lower bounds **only for the frozen pair-context translation scope**,
not for the full goal or for later endpoint/other-node policies. At most 171 /
76 of the current conflicts are even affected, so do not train more pair-only
models as the route to 750 / 150. Further proposals must change the fixed
support outside these groups. A relevant next research direction is to combine
learned order-changing slot assignments with a source-preserving endpoint
representation. Prior slot reward training used canonical endpoints and changed
zero card positions; prior anchor rays preserved ports but retained source pair
order. A combination is not implemented or validated yet. Do not simply repeat
either prior global stage or weaken native acceptance constraints.

Archive: `data/erd-poc/experiments/independent-views-pair-contexts-20261003/`.
Manifest SHA `0db970ab5c40b8aae5ff387a5f002ebbd420eeba77fb6bbffb342d8fb9b87004`.
170 files / 7,304,155 stored bytes with restored hashes verified; archive peak
22.1 MiB. Contains current source snapshots, the exact native binary, both
validation attempts, 3,760 frozen actions, product audits, fixed-conflict
witnesses, and unchanged stable layout/audit plus pre-append provenance. The
existing trained checkpoints reference their verified bounded-policy archive.
Do not rerun exclusive writers or these unchanged completed rollouts.

CLI follow-up remains pending for the affected terminal/session ID and latest
error time. No CLI diagnostics, config changes, or doctor reruns occurred in
this goal continuation; the unresolved CLI question does not block ML work.

### Previous continuation: anchor rays, loss calibration, and core graph attribution

Retained **289 / 1,964**; the 150 / 750 goal remains active and unmet. Six
completed model stages produced **927 new network updates and 242 frozen
batches**, all independently replayed and product-audited. None improved the
stable geometry. No numerical workers remain from this continuation.

`joint_anchor_ray_policy.py` adds `AnchorRayPolicy`: existing shared node
weights drive source-order DAG translations, while fixed interior anchors
derive endpoint phase offsets. The anchors trace inward from original boundary
rays and have a 0.01 inset toward the card center. Offsets are relative to their
initial phases, preserving all original endpoints at zero output. Offsets for
coincident source endpoints on the same card are mean-pooled. There is no
separate trainable endpoint head, alternative-position search, or repair.
Hard-validity failures remain possible and the unchanged native gates reject
them. Source pair order remains constrained; cent rounding uses an explicit
straight-through surrogate gradient.

Validation history is retained, including failures. Attempt 1 used a smaller
inset and no pooling; before training, the model changed to the current inset
and shared-endpoint pooling. Attempts 2-4 exposed piecewise phase boundaries
and cancellation at very small full-loss finite-difference steps. A targeted
step sweep justified distinct phase/full-loss step sizes. The final check
replaced absolute-error monotonicity with scale-aware adjacent-estimate
agreement and tightened full-loss relative tolerance from 5e-4 to 1e-4.
`anchor-ray-validation5` passes 192 strict-interior fixture endpoints, all
3,454 original endpoints per view at zero, 30 continuous derivatives per view,
frozen loading, and three legacy checkpoint action replays. Nonzero random
fixtures can still fail hard/projection checks; they are not legal candidates.
The final validation peaked at 136.5 MiB. Its runner snapshot predates the
later temperature-end option; four subsequent complete stage audits cover it.

Loss calibration reused eight frozen source/legal outputs at four temperatures.
All 32 binary counts exactly match their existing native measurements. At
temperature 1, a legal individual layout with 1,973 conflicts had lower proxy
loss than the 1,964 source. Temperature 0.05 corrected that ranking. The runner
now accepts `--temperature-end` (default 1, preserving the old schedule), with
0.01 <= end <= start <= 256. Sharpening this loss removed the observed ranking
bias but did not produce a better accepted layout; weak crossing-depth loss
also failed to break the plateau.

Actual completed stages (updates / frozen batches; hard / legal batches):

- Individual anchor rays: 165 / 43; 40 / 3; best legal 1,973.
- Overview anchor rays: 230 / 59; 38 / 21; best legal 289.
- Individual sharper loss: 153 / 40; 30 / 10; best legal 1,964.
- Overview sharper loss: 101 / 27; 2 / 25; best legal 289.
- Individual depth loss: 89 / 24; 14 / 10; best legal 1,964.
- Overview depth loss: 189 / 49; 23 / 26; best legal 289.

All saved candidates retain their source geometry. The overview's auxiliary
individual score of 4,447 is not the independently retained 1,964. Stage peaks
were 154.7-181.1 MiB; jobs remained serialized, one math thread, nice +10, with
the 256 MiB process-group RSS guard. This is not a hard 20% CPU quota. No UI
changes or actual browser verification occurred.

Graph-only attribution exactly matches native conflict totals. Individual has
1,907 of 1,964 conflicts inside one biconnected block of 581 cards and 1,116
edges (1,463 crossings plus 444 card hits). Overview has 231 of 289 conflicts
inside a 206-card, 315-edge block. This is attribution, not a crossing lower
bound or a proof that either target is attainable.

Removing pairs drawn from each dominant block's 32 highest-degree nodes tested
496 pairs per view. Expanded contexts include attached outside branches.
Individual has 24 new multicard contexts beyond existing single-articulation
contexts, covering 95 of the 581 core cards; overview has 13, covering 43 of
206. The largest new individual context has 84 physical cards (45 core cards)
between `db.Company` and `db.VentureCapital`. This pair scan is not exhaustive
and proposes no coordinates. **Next research:** give these source-bound
two-vertex separator contexts an explicit learned-action schema and native
validation. Existing `core`/`cut` maps are indexed by a single physical root;
do not silently reinterpret them. No pair-context decoder is implemented yet.
Do not repeat these six global anchor stages unchanged.

Archive: `data/erd-poc/experiments/independent-views-anchor-rays-20261003/`.
Manifest SHA `93edf0bd5700c25d909004bc578c2a237787364708636fd5b68fe4c531fb13e1`.
583 files have restored hashes verified; 113,292,267 stored bytes; archive peak
24.2 MiB. Includes failed validation snapshots, every stage/checkpoint, current
sources, calibration, graph analyses, and unchanged stable layout/audit with
the provenance snapshot before appending this archive. Native joint-batch-v7
references the verified global-order archive containing its actual build
inputs. The prior separation-DAG archive is also referenced by hash.
Current anchor module SHA:
`177546391a0eecd9ebd480b8ace04517c5f28e5c971af974356e5e076fd2c829`.
Current joint runner SHA:
`74d7a499c080b997d2fdf47711142868ccbd38324c34d573199def7b0e28f66f`.
Do not rerun exclusive validation/sealing writers.

CLI follow-up: user confirmed terminal Codex CLI. Version 0.160.0; seven TTY
clients were observed at 15:29 KST. An incremental read-only log scan from
14:16:41 to 15:29:29 found one warning/error row and no new transport events.
Earlier retained events still cannot be associated with the affected terminal.
The asynchronous request for its `tty` or session ID and latest error time is
pending. Root cause remains unconfirmed; no auth/config/network changes or
doctor rerun. The redacted incremental report is
`.tmp/cli-transport-incremental-20261003.json`. Do not repeat the scan without
new evidence or rerun the doctor that exceeded the resource guard.

### Previous continuation: source-order separation DAG outputs

Retained **289 / 1,964**; no promoted geometry changed. The 150 / 750 goal
remains active and unmet. This continuation adds a validated joint neural
output layer, but its best new isolated candidates are **303 / 2,010**, both
worse than the stable product. All numerical jobs finished; no live workers.
Use `.venv-ml/bin/python` for ML scripts. System Python lacks NumPy.

`diagnose_learned_route_constraints.cpp` replayed the previous bounded models'
8,284 individual and 6,382 overview actions exactly. Hard rejections numbered
3,395 and 2,854, but **zero had positive visual gain**. The optional hard-subtype
preview branch therefore had zero selected cases and was not exercised; do not
claim that branch was validated. The diagnostic compile peaked at 220.2 MiB,
and replay at 50.2 MiB. This evidence shifted work toward coordinated proposals
instead of weakening validity checks.

`joint_separation_policy.py` retains one already separating axis per physical
card pair and builds source-coordinate DAGs. Neural fractions decode all card
translations inside those inequalities and the original frame. Redundant
inequalities implied by frame/step bounds are omitted. Backward upper-bound
propagation reserves room for downstream nodes. Each node is quantized to
integer cents before its successors, avoiding half-tie drift. No crossing
objective, alternative-position search, or repair is part of this decoder.
Continuous surrogate gradients are checked; discrete cent rounding uses an
explicit straight-through estimator. Retaining source pair order may constrain
reachable layouts; it does not prove that 150 / 750 is attainable.

There are node-only `SeparationDagPolicy` and node/endpoint
`SeparationDagPortPolicy` variants. The latter preserves every original endpoint
at zero output and conditions ray phases on the decoded card movement. The
former uses the existing canonical center-ray decoder: zero movement resets
manual ports and scores **2,190 individual / 365 overview**, versus the stable
1,964 / 289. Overview node-only DAG requires `--grouped-route-loss`; its loss
uses actual member anchors and representative selection. Both overview variants
check that all members remain inside their physical owner cards.

Validation evidence:

- First overview finite difference at step 1e-6 failed and is retained in
  `separation-dag-validation/failure.json` (peak 189.3 MiB). No implementation
  change or tolerance relaxation was used to resolve that numerical check.
- `separation-dag-validation2` passes 192 geometry cases, 72 continuous decoder
  derivatives, the tight half-tie fixture, and 28 network derivatives per view
  with converging steps 1e-7 and 1e-8. It preserves all 3,454 original endpoints
  at zero, verifies frozen loads, and checks six noncommitting batches per view
  with zero spacing/frame failures (hard/projection failures remain possible).
  Peak 181.3 MiB. Its snapshots predate the node-only class; preserve them.
- `separation-node-validation` passes 17 continuous node-network derivatives,
  frozen loading, five legal native batches, and replays 98 earlier frozen
  batches including 52 legacy rigid-branch batches. Peak 90.8 MiB.
- `separation-overview-validation` passes 12 end-to-end continuous derivatives
  through grouped route loss and the node network (max absolute error 5.69e-5),
  five noncommitting batches with no spacing/frame failures, full-card
  containment, frozen loading, and rejection of a missing grouped-loss flag.
  Two deliberately random fixtures fail projection and are not valid candidates.
  Peak 98.2 MiB. This includes the current overview runner integration.

Actual model stages, each independently replayed and product-audited:

- Individual/overview node-and-port DAG: 70 / 101 updates, 19 / 27 frozen
  batches, all hard-rejected; retained source scores 1,964 / 289.
- Individual canonical stage 1: 173 updates, 45 legal batches, saved 2,066;
  v11 port refinement reaches 2,022. Stage 2 warm-starts the first best model
  against the original source, changes temperature/depth loss, performs 171
  updates and 44 legal batches, saves 2,057; port refinement reaches 2,010.
- Overview canonical stage 1: 256 updates, 65 batches (64 legal, one projection
  failure), saved 309; v11 port refinement reaches 303. Stage 2 changes to direct
  crossing/hit loss, adds 256 updates / 65 batches, and still saves 309. It has
  no additional port stage. These are isolated regressions, never promotions.
- One port invocation used system Python and failed before inference because
  NumPy was absent. That directory is retained. The corrected existing-venv
  invocation is `individual-separation-port2-venv`; no package was installed.

Total: **1,027 new network updates, 265 frozen joint batches, 53,387 frozen
endpoint actions**, across nine completed stages. Stage peaks were
140.1-185.7 MiB; all jobs remained serialized, one math thread, nice +10, and
256 MiB process-group RSS guard. This is not a hard 20% CPU quota. Product
audits preserve all 1,244 models, 1,727 relationships, sizes, spacing, valid
straight boundary endpoints, and the 1.5e9 area limit. No actual browser QA.

Archive: `data/erd-poc/experiments/independent-views-separation-dag-20261003/`.
Manifest SHA `43fe872904c46c4f3e2330eaca3cfa837c3a6f0cb434539e77921b8e2e64ea7a`.
749 files have restored hashes verified; 159,142,055 stored bytes; archive peak
25.8 MiB. Includes the failed checks, every checkpoint, exact versioned source
snapshots, current source, and unchanged stable layout/audit/provenance. The
reused joint-batch-v7 binary's actual build inputs remain in the referenced
global-order archive, not the current component source snapshots. v26 and v11/
port-v5 dependencies reference their original archives by verified hashes.
Do not rerun exclusive validation/sealing scripts or completed stages unchanged.
Current joint runner SHA is
`f606cb70ab8e157b17bd2eb364dedfde4e6cbd7c617c32389dc3326532b91b6a`;
DAG module SHA is
`045b9552977486eb1b7e0f5fdb8a04968f97cd6ff706c3e9d917a04d14083b34`.

Next research needs a genuinely different learned proposal scope or a route
representation that preserves more of the existing port quality while remaining
valid. Do not repeat the saturated canonical warm-start stages or promote their
regressions. The frozen rejection diagnosis does not support treating validity
gates as the sole cause of stalled visual improvement.

CLI follow-up: rechecked installed version 0.160.0 and read the existing redacted
diagnostics only; no additional log-database scan, auth/config/network change,
or doctor rerun. The prior asynchronous request for the affected terminal's
`tty` and latest error time remains unresolved. Body-decoding transport errors
are confirmed in other retained threads, but their root cause and association
with the affected terminal are still unknown. Official troubleshooting was
fetched again; it does not establish that resource pressure caused this error.

### Previous continuation: bounded neural translation output

Promoted **291 / 1,966 -> 289 / 1,964**, preserving the complete previous best.
Actual product-file loading and full overview -> individual -> overview route
function parity pass. All 1,244 models, 1,727 canonical relationships, original
sizes, straight boundary routes, zero spacing/overlap/hard violations, and the
1.5e9 area limit remain valid. Product verification peaked at 190.1 MiB.
This is runtime verification, not an actual browser rendering check. The
150 / 750 goal remains unmet and active. All jobs in this continuation finished.
Stable layout, both source layouts, audit, provenance and archive manifest
hashes were independently checked after promotion. The existing F5 configuration
still reads `captain-ml-independent-views.layout.json`.

`ml_component_environment.cpp` now has a distinct `bounded-moving-ray` decoder.
For each moved/unmoved card pair in both physical and individual geometries,
it keeps one currently separating axis, chosen by greater clearance with
micro-unit ties. Intersecting those inequalities with the fixed source frame
gives a translation box containing zero. Bounds round inward to cents with a
small numerical margin. The trained model emits two dimensionless tanh latents;
one deterministic output transformation maps them into the current box and
quantizes the displacement. It never searches alternative positions or repairs
a rejected proposal. Geometry acceptance is unchanged. Zero latent means zero
translation; moving-ray endpoint decoding still follows its existing contract.

The four former frame-room features now encode spacing-constrained room.
`relative-bounded-translation-context-v7-64` explicitly separates these features
and action units from legacy models. Training rejects mixed action units in
either direction, and inference checks the checkpoint and native decoder.
`learned_bounded_replay.py` independently reconstructs each current box and
decoded displacement for every action, including rejected actions, before
replaying all accepted card/route changes. Frozen graph-cut contexts remain
source-bound; no graph-context or acceptance fallback was added.

Native v26 SHA is
`ce208890d9ba6213fe266cbd3cee0b4e7c7277f22e50c9bd01fd6cc225f0a96a`.
The guarded compile peaked at 248.1 MiB. Validation passes all 13,720 native
local/full comparisons, including 1,152 bounded trials and 71 accepted grouped
bounded moves, noncommitting-state preservation, saturation and cent rounding.
All 2,279 actual-source boxes agree between Python and C++; the legacy 20,000
cut-policy actions replay unchanged. Unit-mismatch guards pass. Validation
peak was 180.9 MiB; exact sources and hashes are preserved in the archive.

Two fresh policies trained on 64 synthetic graphs each; no actual Captain
examples were used. In total, 276,480 reward measurements produced 25,830
updates. Individual has 1,948 positive grouped examples, 12,600 updates,
best epoch 5, held-out NLL 6.8872 -> 1.6004. Overview has 1,687 positive grouped
examples, 13,230 updates, best epoch 5, NLL 7.9443 -> 1.9079. These NLLs use
different action units from previous models and must not be compared directly.
Training peak was 184.8 MiB. Checkpoints under
`.tmp/visualcross-ml-150-750-20261003/bounded-policy-training1/`:
`individual.npz` SHA
`52dfe940a7498f12cbfa016232044ce7df4bd4ade31a57abaef3ede80571aab2`;
`overview.npz` SHA
`39a135d12be3e7c76d6005b87df65226464307232782920448db1ad68c417992`.

- `individual-bounded-trained1`: 10,662 actions, 1,966 -> 1,965 via one
  CompanyAddressChangeEvent card displacement (-76.82, +5.72).
- `overview-bounded-trained1`: 7,761 actions, 291 -> 290 via one
  AgendaBranchChange card displacement (+5.90, +1.41).
- `individual-bounded-port1`: v11 endpoint policy, 20,000 actions,
  1,965 -> 1,964. `overview-bounded-port1`: 14,271 actions, 290 -> 289.
- `individual-bounded-trained2`: 8,284 actions, retained 1,964;
  `overview-bounded-trained2`: 6,382 actions, retained 289.

All six stages pass frozen action/geometry replay and product audits: 67,360
actual actions, 986 accepted grouped actions. The four bounded stages have
33,089 actions and zero spacing/frame rejections. Their improvements move
single cards; this is not a causal ablation proving the decoder's contribution.
The final individual metric is 1,483 edge crossings + 481 card hits. Overview
is 191 edge crossings + 93 ordinary-card hits + five bundle hits. Individual
area is 1,443,630,960.9897; overview area is 1.34877788399e9. The overview
candidate's auxiliary individual score 4,447 is not the independent view's
score. Stage peaks were 132.4-192.7 MiB, always one job and one math thread.

Archive: `data/erd-poc/experiments/independent-views-bounded-policy-20261003/`.
Manifest SHA `b30a88360738baf1701fd29e59ef7449b6653a9496e2ecb9779b8937883ae154`.
All 276 stored files pass restored-hash checks; stored bytes 21,365,008;
archive peak 26.6 MiB. It includes both models/data, all six stages, exact
v26 implementation and binary, validation, the combined best, and the complete
previous 291 / 1,966 product. v11 / port-v5 dependencies reference the older
node-port archive by hash. Do not rerun exclusive `validate_bounded_policy.py`,
`train_bounded_policies.py`, `summarize_bounded_rollouts.py`,
`seal_bounded_policy_experiments.py`, or `promote_bounded_verified.py`.

The spacing-rejection hypothesis is supported, but visual gains remain small:
hard route validity and visual regression now dominate rejected bounded moves.
Both second bounded stages failed to improve after endpoint refinement.
Do not simply repeat them unchanged. A useful next investigation is which
route constraints block the coordinated learned changes, or a distinct learned
joint node/endpoint output. At the end of this earlier continuation, a global
separation-DAG decoder was still unimplemented (see the newer section above).
Preserve ML-only proposals and the
256 MiB guard. This is not a hard 20% CPU quota. No UI code changed.

CLI side issue refresh at 14:16 KST: npm CLI 0.160.0; 24 retained disconnect
events, 21 body-decoding errors, latest 02:44:59 KST, all in three other threads.
Current-thread matches remain zero. The bounded read-only scan peaked at
19.3 MiB. The shared log database is open in the daemon and two terminal
clients, but this does not identify which terminal the user's error concerns.
An asynchronous question asking for `tty` and the last error time is pending;
do not repeat it or rescan during goal continuations without new evidence.
No auth/config/network changes; no rerun of the over-budget doctor command.
Official troubleshooting was fetched; it does not establish this root cause:
https://learn.chatgpt.com/docs/reference/troubleshooting

### Previous continuation: learned articulation-cut contexts

Promoted **294 / 1,975 -> 291 / 1,966** after actual product-file loading,
complete route equality, and overview -> individual -> overview verification.
All 1,244 models, 1,727 canonical relationships, original sizes, straight
boundary routes, zero spacing/overlap/hard violations, and the 1.5e9 area limit
are preserved. Product verification peaked at 162.2 MiB. This verifies runtime
and route functions, not actual browser rendering. The 150 / 750 goal remains
unmet and active. All numerical jobs in this continuation are finished.
The stable candidate, audit, provenance, both source layouts and sealed manifest
were independently checked for matching hashes and metrics after promotion.

`learned_branch_map.py` now supports `--branch-mode cut`: remove an articulation
root from its connected component, retain its smaller components and the root,
and exclude the largest trunk. Ties use input-node order. Duplicate edges and
self loops do not affect membership. Non-articulations remain singletons.
This uses only graph topology to define contexts, not geometry proposals.
The new source-bound v2 map records mode, source SHA and all ten input hashes.
Native v25 independently reconstructs the same map. Legacy core mode retains
its existing v1 contract. Every displacement still comes from a trained neural
policy, with unchanged geometry gates and frozen geometry/action replay.
Core-to-cut checkpoint transfer is allowed and explicitly reported.

On the prior source, cut contexts cover 230 multi-card groups (maximum 93)
in individual view and 303 (maximum 81) in overview. Nineteen / 42 changed
contexts include cycles. These are topology observations, not crossing lower
bounds or evidence that the target is reachable.

Native validation passes 12,568 local/full comparisons, including 1,536 cut
trials and 98 accepted cut-group moves. Noncommitting measurements preserve
state. Both modes agree between Python and C++ on both actual views; wrong
modes are rejected. A legacy 20,000-action replay remains exact, training-label
contracts pass, and 44 neural derivatives have maximum error 4.8914e-10.
Validation peak was 77.9 MiB. Native `component-environment-v25` SHA is
`485261e4e4d06bb318d5ce5b744e3a61f6b6ca89c2aeb76442720a0c520cd13a`;
the guarded compile peaked at 247.7 MiB.

Two fresh policies trained on 64 synthetic graphs each, using 274,496 reward
measurements in total and no actual Captain examples. Individual training has
12,720 updates (best epoch 3), 1,279 positive grouped actions; overview has
13,170 updates (best epoch 6), 1,135 positive grouped actions. Training peak
was 171.3 MiB. Both retain `relative-component-context-v2-64` and moving-ray
decoding, with `branchMode=cut`. Checkpoints under
`.tmp/visualcross-ml-150-750-20261003/cut-policy-training1/`:
`individual.npz` SHA
`397865149462c402a7fc707840c8ed6dee1ecdf9f42fca8236f1aa9466b51de7`;
`overview.npz` SHA
`c47aca2cc07d4372c416414e86378c82bcd0a74aac5fe43058805eb1e63d8dcd`.

- `overview-cut-policy1`: v15 transfer, retained 294.
- `individual-cut-policy1`: prior core-trained model, 1,975 -> 1,974.
- `overview-cut-trained1`: new cut model, 294 -> 292.
- `individual-cut-trained1`: new cut model, 1,974 -> 1,970; one gain comes
  from a six-card cyclic OptionExerciseClaim group action.
- `individual-cut-port1`: v11 endpoint refinement, 1,970 -> 1,968.
- `overview-cut-port1`: v11 endpoint refinement, retained 292.
- `individual-cut-trained2`: new cut model after ports, 1,968 -> 1,966.
- `overview-cut-trained2`: new cut model after ports, 292 -> 291.

All eight stages pass frozen neural replay and product audits: 143,071 actual
actions, 1,021 accepted grouped actions. Final individual metrics are 1,488
edge crossings + 478 card hits; overview is 187 edge crossings + 98 ordinary
card hits + six bundle hits. No causal ablation isolates translation from
associated moving-ray endpoint changes. Stage peaks were 136.1-189.8 MiB.
Detailed rejection/gain counts are in `cut-trained-rollout-diagnosis.json`.

Sealed archive: `data/erd-poc/experiments/independent-views-cut-policy-20261003/`.
Manifest SHA `1a583402110e95bc20f4f84ed60d20760c131e75c60238e0a9ee3719cfeff8f9`.
It preserves all stages, models and data, v25 binary and exact source snapshots,
validation, the combined best, and the complete previous 294 / 1,975 product.
All 370 restored hashes pass; stored bytes 27,531,160; archive peak 25.6 MiB.
Do not rerun exclusive `seal_cut_policy_experiments.py`,
`promote_cut_verified.py`, `train_cut_policies.py`, or
`validate_cut_branch_policy.py`.

At the end of this earlier continuation the proposed next direction was a learned translation output layer
bounded by current card-spacing and frame constraints, with a new action-unit
contract, fresh training and independent native/Python replay. The motivation
is the many spacing rejections (7,359 / 20,000 in individual-trained1), not a
proven gain. It is now implemented and assessed above. Bounds must contain zero and decode one learned latent directly;
do not add a heuristic search or rejected-action repair fallback. A larger
global separation-DAG decoder is also only a proposal. Keep one numeric job,
one math thread, nice +10 and the 256 MiB group RSS guard. This is not a hard
20% CPU quota. No UI code changed in this continuation.

### Previous continuation: learned graph-branch policy

Promoted **299 / 1,981 -> 294 / 1,975** after actual product-file loading,
complete route equality, and overview -> individual -> overview verification.
All 1,244 models, 1,727 canonical relationships, sizes, straight boundary routes,
zero spacing violations, and the area limit are preserved. Product verification
peaked at 169.5 MiB. This is runtime and route-function verification, not an
actual browser rendering check. The 150 / 750 goal remains unmet and active.
All jobs in this continuation are finished.

`learned_branch_map.py` now creates source-bound, coordinate-free graph contexts:
each root in the simple graph's two-core owns its attached trees; other contexts
remain single cards. The individual view has 139 multi-card contexts, maximum
75 cards; overview has 122, maximum 27. `ComponentEnvironment::context` applies
one trained neural displacement to all members, preserving internal geometry.
`--branch-moves` selects this decoder in both rollout wrappers; it requires the
moving-ray component policy. All geometry gates are unchanged. Frozen replay
checks every moved physical/full card and route, with source/input/map hashes.
Native v24 independently reconstructs the graph map and checks it against the
Python map before use. Actual individual and overview maps agree.

The native synthetic and noncommitting adaptation dataset paths now support
`--branches 1`; every row records its branch mode and context size. The learner's
`--branch-training` requires matching rows and writes the decoder mode into the
checkpoint; a branch-trained model requires branch inference. Both v23 and v24
pass 12,568 full/local geometry comparisons, including 3,072 branch trials and
440 accepted grouped moves. Noncommitting trials preserve state. Existing
19,913 actual moving-ray actions replay unchanged. The neural policy passes
44 derivatives (maximum error 4.8914e-10). Exact source snapshots are retained.

Two small policies were trained on 64 synthetic graphs each. The individual
model used 7,215 updates (best epoch 3); overview used 8,570 (best epoch 4).
Synthetic observations contain 280,512 actions, including 1,000 / 892 positive
grouped actions. Actual Captain probing measured 1,408 / 5,696 actions without
committing positions and found zero positive rewards. Those actual probes were
therefore not used for adaptation. Training peak was 183.0 MiB. Checkpoints live
in `.tmp/visualcross-ml-150-750-20261003/branch-policy-training1/`:
`individual-synthetic.npz` SHA
`fb0dade64e0e4f42c0e03508ec448fb121a31a32376ed0fe4c358d187e0a9a41`,
`overview-synthetic.npz` SHA
`11793ff5b65e5582716d71b813f60cd292f0cd8982fc56e9f940d32169d1e787`.
Both use the existing 64-feature schema and moving-ray decoder; only the second
requires the overview objective. Native `component-environment-v24` SHA is
`0be42b1a7a3b6f932631211e0c10dd38bf788a28f232699b0a98b25007ce063f`.

- `individual-branch-policy1`: v15 transfer, 20,000 actions, retained 1,981.
- `overview-branch-policy1`: v15 transfer, 18,998 actions, 299 -> 295.
  One two-card ShareholdersMeetingShareholderAttendance action gains one;
  the other three improving actions move single cards.
- `overview-branch-policy2`: v15 transfer, 19,836 actions, 295 -> 294.
- `individual-branch-trained1`: new individual model, 10,211 actions,
  1,981 -> 1,978; all three improving actions move single cards.
- `overview-branch-trained1`: new overview model, 9,225 actions, retained 294.
- `individual-branch-port1`: v11 endpoint refinement, 20,000 actions,
  1,978 -> 1,977. `overview-branch-port1`: 13,756 actions, retained 294.
- `overview-branch-policy3`: v15 after endpoint refinement, 19,358 actions,
  retained 294. `individual-branch-trained2`: new individual model after
  endpoint refinement, 20,000 actions, 1,977 -> 1,975 via two single-card actions.

Final independent individual metrics are 1,502 edge crossings + 473 card hits;
the total fell by six although edge crossings increased by ten. Overview is
196 crossings + 93 ordinary-card hits + five bundle hits. Do not claim the
entire gain comes from multi-card moves or that a causal ablation was performed.

Sealed archive: `data/erd-poc/experiments/independent-views-branch-policy-20261003/`.
Manifest SHA `546f0e702d851ffc0c20be4d85b786ccc1a50367bfbf80ac84bd9bb171b8cec4`.
It preserves nine stages, 151,384 frozen policy actions, 1,086 accepted grouped
actions, both new models and their data, exact v23/v24 sources and binaries,
validation, the combined best, and the complete previous 299 / 1,981 product.
All 366 restored hashes pass; stored bytes 33,198,518; archive peak 26.7 MiB.
Stage peaks were 132.5-187.5 MiB; v23/v24 compiles peaked at 241.3/221.7 MiB.
Do not rerun exclusive `seal_branch_policy_experiments.py`,
`promote_branch_verified.py`, or the training/validation snapshot writers.

Continue from the new source hashes. The new individual policy improved again
after endpoint refinement; verified successful rollout rewards are available
for a future, explicitly source-bound training dataset. Repeating unchanged
random Captain adaptation produced no positive examples and should not be the
default next step. Broader cyclic graph contexts have not been implemented or
tested. They would require a fresh graph/map contract and replay validation.

CLI side issue: installed npm CLI is 0.160.0. A bounded read-only log refresh at
13:29 KST still found 24 old disconnect events, 21 with body-decoding errors,
latest 02:44:59 KST, in three other threads; current-thread matches remain zero.
Terminal process coverage and root cause are unconfirmed. Peak 19.3 MiB.
No config/auth/network changes, and no repeat of the over-budget doctor command.

### Previous continuation: rigid neural patches

The previous continuation improved the individual view **1,990 -> 1,981** and
promoted the independently verified **299 / 1,981** combined candidate.
Actual product-file loading preserves all 1,244 models, 1,727 relationships,
coordinates, card sizes and straight boundary routes. Full route equality,
overview -> individual -> overview switching, zero spacing violations, and
the area limit pass. The product audit peaked at 166.6 MiB. This is runtime
and route-function verification, not an actual browser rendering check.
The 150 / 750 goal remains unmet and active. All jobs in this continuation
are finished.

The decisive new architecture is `RigidPatchPortPolicy`: it pools the features
of a fixed graph branch before neural inference and shares its displacement
across every member. Common frame bounds preserve the branch inside the source
frame. The only trainable quantities remain shared neural weights. Graph masks
contain a conflicted two-core root plus its attached nodes outside that core;
there are no coordinate targets. `--rigid-patch` requires `--patch-nodes`.
The generic checkpoint loader, warm-loading checks and reports recognize it.
Both actual views pass 104 derivatives, exactly equal branch translations,
zero inactive outputs/gradients, exact frozen roundtrips, and 3,454 original
endpoints at zero output; validation peak was 118.3 MiB.

- `individual-rigid-branch1` (FileAttachment branch, eight cards): 201 updates,
  52 frozen batches, 32 legal, best 1,989. Winning checkpoint step 128 moves
  every branch card by exactly (+4.00, -3.02) and also learns endpoint phases.
  This does not isolate the causal contribution of the node translation.
  Peak 153.6 MiB; source candidate SHA
  `f95ae83d890cd8447a6db8b5870266e03d099289520c0c261441b3f6953323db`.
- `overview-rigid-branch1` (BulkEmail, four cards): 285 updates, 73 batches,
  63 legal, best 299, peak 178.8 MiB. `overview-rigid-file1` (FileAttachment,
  27 physical cards): 285 updates, 73 batches, 43 legal, best 299,
  peak 178.2 MiB. No overview improvement was saved.
- Learned refinement after the 1,989 candidate: port-v5 / model-v11
  `individual-rigid-port1` -> 1,983 (20,000 actual actions, peak 139.9 MiB);
  port-v5 / model-v10 `individual-rigid-port2` -> 1,983 (20,000, 134.2 MiB);
  component-v22 / model-v15 `individual-rigid-node1` -> 1,982
  (19,913, 133.6 MiB); port-v5 / model-v11 `individual-rigid-port3` -> 1,981
  (20,000, 140.4 MiB). Final individual candidate SHA
  `b89daa3062ca9aa67432bf0d20b206c7c68fe12eeb3881ac1332f4b60bd39e0e`.

`RewardHeadTrainer` now supports jointly training `wo` and `ewo` with explicit
parameter scales. Every scaled parameter probe and Adam update is replayed;
all other weights are checked unchanged. The runner exposes
`--reward-endpoint-head --reward-port-scale 4`. A coupled fixture replays
1,280 probes / 160 updates (final squared error 0.0000115168), the original
fixture still replays 768 / 96 with identical error, and both previous actual
Captain traces replay 536 probes / 134 updates exactly. Validation peak 51.5 MiB.
The three actual coupled trials did not improve either source:
`individual-coupled-reward1` (18 updates, six batches, minimum legal 1,993,
144 probes, peak 132.6 MiB), `overview-coupled-reward1` (32, nine, 299,
256 probes, 177.5 MiB), and `individual-coupled-wide1` (39, eleven, 2,021,
156 probes, 136.2 MiB). A larger overview radius is now allowed only when exact
reward training avoids the large projected-pair loss. Native gates are unchanged.

Read-only conflict analysis found 441 / 449 degree-one nodes in the individual /
overview graph; their largest remaining local conflict totals were only four /
three. Their simple-graph two-cores have 650 / 332 nodes. These are structural
observations, not a crossing lower bound. Concentrating only on single leaves
cannot explain the reduction needed for the goal. Analysis peak 30.5 MiB.

That continuation's sealed archive:
`data/erd-poc/experiments/independent-views-rigid-branches-20261003/`.
Manifest SHA `501be2995e0294f5cc1a57f8f2e1ff562fca75d5069a66760f21273a0da85e43`.
It preserves ten stages, 224 frozen neural batches, 79,913 additional learned
policy actions, 556 actual reward probes, exact code/mask/test snapshots, the
combined best, and the complete previous 299 / 1,990 product. All 688 file
restored hashes pass; stored bytes 71,048,684, archive peak 25.6 MiB. Unchanged
native binaries and trained policy dependencies are referenced by hash from
older sealed archives. Do not rerun the exclusive
`seal_rigid_branch_experiments.py` or `promote_rigid_verified.py`.

The graph-branch extension proposed in that continuation is now implemented
and verified above. Existing `jointMoves` still only recognizes packed Leaf
parents; the new `branchMoves` mode supplies the separate source-bound contexts.

Previous localized continuation: six localized neural training stages produced **no
improvement** over 299 / 1,990. All 294 frozen output batches and 536
noncommitting reward measurements were replayed; the promoted candidate is
unchanged. Every job in this continuation has finished. The 150 / 750 goal
remains active.

`PatchPortPolicy` in `joint_neural_ports.py` adds immutable physical-node and
endpoint masks to `SharedEndpointPolicy`. Incident endpoints are expanded to
whole coincident source groups. Only neural weights generate changed values;
the masks contain no proposed positions or coordinate targets. Whole-scene
losses and native geometry gates remain in force. `--patch-nodes` binds each
mask to its source candidate hash and view, copies it into the stage, and
checks it on audit and warm loading. The generic checkpoint loader recognizes
this variant. Actual-view validation passes 104 derivatives, exact frozen
roundtrips, 3,454 zero-output endpoints per view, and exactly zero inactive
outputs and gradients; validation peak was 110.9 MiB.

Current conflict enumeration exactly matches native baselines: individual
1,492 crossings + 498 card hits, overview 199 + 100. A 24-card individual mask
covers connections involved in 1,326 of the 1,990 conflicts; the corresponding
19-card overview context covers 127 of 299. These are coverage measurements,
not reductions or proven reachable targets. The six stages were:

- `individual-patch-port1`: 24 cards, 748 active endpoints, 185 updates,
  48 batches, 21 legal, minimum legal 1,990, peak 144.2 MiB.
- `overview-patch-port1`: 19 cards, 267 active endpoints, 318 updates,
  81 batches, 43 legal, minimum legal 299, peak 177.3 MiB.
- `individual-sparse-patch1`: only `db.RsuGrantContractModusignDocument`,
  four endpoints, 201 updates, 52 batches, 43 legal, minimum 1,990,
  peak 147.2 MiB.
- `overview-sparse-patch1`: only `db.OptionExerciseClaimAttachment`,
  six endpoints, 297 updates, 76 batches, 75 legal, minimum 299,
  peak 180.5 MiB.
- `individual-patch-reward1`: 45 replayed Adam head updates, 180 replayed
  reward probes, 13 trained batches. Eleven batches were legal, minimum
  1,993; minimum legal probe 1,991. Retained 1,990; peak 134.6 MiB.
- `overview-patch-reward1`: 89 replayed Adam head updates, 356 replayed
  reward probes, 24 trained batches. Seven batches were legal, minimum
  299; minimum legal probe 300. Retained 299; peak 185.6 MiB.

The reward runner now permits a localized neural patch as well as ordering
models. These two reward stages train only the node output matrix `wo`;
all other weights are fixed and endpoint heads remain zero. They do not test
joint exact-reward learning of coordinates and endpoint phases. Small masked
surrogate trials and the current node-head-only reward trials did not improve
the count; do not repeat them unchanged or claim that legal proposals imply
progress toward the goal. Coupled node/endpoint reward learning remains
untested. No native acceptance changes or product TypeScript changes were made.

Previous localized archive:
`data/erd-poc/experiments/independent-views-local-patch-20261003/`.
Manifest SHA `eb0115629ff49922b7cc8283f772b1d4c1c65716aca5a095808afa71137d7a19`.
It preserves six stages, exact masks and conflict analysis, source snapshots,
validation, and the retained best: 622 restored-hash-verified files,
71,131,984 stored bytes, archive peak 23.5 MiB. The unchanged v7 native binary
is referenced by hash from the prior global-order archive. Do not rerun the
exclusive `seal_local_patch_experiments.py`. No new actual-browser check was
performed. Single numerical job, one math thread, nice +10, and the 256 MiB
process-group RSS guard remain the resource policy; these are not a hard
20-percent CPU quota.

The user confirmed the transport error appears in terminal Codex CLI. Installed
npm package version remains 0.160.0. A fresh bounded read-only scan at
2026-10-03 12:46 KST found the same 24 stored transport events (21 decode,
one reset), latest at 02:44:59 KST, across three other threads. There were no
matching events attributed to this thread in that scan. Terminal-process
coverage and the root cause remain unconfirmed; do not call the issue fixed.
Scan peak was 19.1 MiB. No auth, configuration, or network settings were changed.
Do not rerun `codex doctor --json` unchanged: its earlier 313.1 MiB guard kill
produced no diagnosis. The optional `/new` short-response reproduction check
remains unanswered. Official CLI command documentation was fetched again.

Previous global-order continuation: five new stages, 304 frozen neural batches, and 128
noncommitting reward measurements produced **no improvement** over 299 / 1,990.
The promoted candidate is unchanged and the 150 / 750 goal remains active.
All numerical jobs from this continuation are finished. The new evidence rules
out simply continuing the current whole-graph ordering objectives unchanged.

- `RayConditionedPolicy` rotates a port's reference phase with the network's
  predicted card-center direction and learns the remaining phase offset. Its
  104 actual-view derivatives and frozen roundtrips pass; zero output preserves
  3,454 endpoints in each view. `individual-ray-conditioned1` had 67 proposals
  (58 spacing rejects, 9 hard rejects). `overview-ray-conditioned1` had 76
  proposals, 9 legal, best 299. No improvement was saved.
- `GridOrderPolicy` learns logits and within-cell offsets, then stably sorts
  them into separated grid cells. Its decoder has no access to geometry scores
  and does no assignment search. The original soft-rank straight-through
  gradient is biased: `individual-grid-order1` passed all 43 native geometry
  checks but worsened from a best 8,093 to a final 21,708 conflicts. Do not
  interpret the 24 smooth-surrogate derivatives as derivatives of hard sorting.
- `SlotPermutationPolicy` learns a shared ranking head for equal-size source
  slots: 108 shape groups and 1,197 movable cards. The rectangle-multiset
  invariant holds before native displacement rounding; every rounded output
  still requires strict native validation. `individual-slot-reward1` replays
  128 reward probes, 32 Adam head updates, and 9 trained output batches. All
  probes and outputs were legal, but none beat the canonical-route baseline
  2,139. Its saved experimental 2,139 candidate uses the +200 regression
  allowance and changes **zero card positions**. It is not an improved layout
  and must not replace the promoted 1,990 candidate.
- `joint_graph_stress.py` trains continuous ordering codes against sampled
  graph distances, including every graph edge. `individual-grid-stress1`
  reduced its loss from 0.26184 to 0.07792, but all 109 legal hard layouts were
  worse: minimum 9,094, final 22,872. One noncommitting measurement of the
  trained continuous code with a fixed tanh frame decoder still scored 18,475
  with native combined spacing count 530. Hard checks were skipped at that
  spacing rejection; zero reported hard counts there are not hard validity.

Native joint batch **v7** adds `MEASURE`, which returns exact scores without
changing the retained candidate or inference-action counter. A dedicated
fixture measures 2,139 under an allowance that would admit it, saves and checks
the unchanged 1,990 source, and then proves that `TRY` does admit the same
output as an unsaved positive control. This keeps reward samples out of
candidate acceptance. Original native checks still pass 576 comparisons.
New exact-reward training updates only shared NN head weights, never card
coordinates; both its parameter probes and full Adam trajectory are replayed.

Final unit checks pass 24 soft-rank surrogate derivatives, 64 randomized grid
separation checks, 64 rectangle-multiset checks, 24 graph-stress network
derivatives, exact frozen roundtrips, and a reward fixture with 768 replayed
probes / 96 replayed updates. Fixture numbers are distinct from the 128 actual
Captain reward measurements. Final fixture-suite peak was 32.0 MiB, native
compile peak 229.3 MiB, and maximum training/audit stage peak 174.9 MiB. There
were no new product TypeScript changes or browser rendering checks.

Previous global-order archive:
`data/erd-poc/experiments/independent-views-global-order-20261003/`.
Manifest SHA `b9fa65014a331faf993beda3ad6a8241400637bdc7df4072519a47ef2ea07e3b`.
It preserves 5 stages, 304 trained batches, 128 actual reward probes, 571 files
with restored hashes checked, and 68,501,636 stored bytes. Archive peak 24.4 MiB.
It includes the unpromoted 2,139 regression branch, exact implementation
snapshots, validation sources, native binaries, and the unchanged best.
Do not rerun the exclusive `seal_global_order_experiments.py`.

Next work needs a different structural representation or objective that helps
the actual conflict count. Feasible global ordering alone, lower graph-distance
stress, and the current shared-head reward experiment did not do so. Preserve
all cards, canonical relationships, strict geometry gates, ML-only proposals,
and the existing resource guard; do not lower the target or relax validation.

Previous active-loss continuation: ten bounded training stages and 749 frozen neural batches
produced **no improvement** over 299 / 1,990. The promoted candidate and its
audit/provenance are unchanged. All numerical jobs in this continuation are
finished. Do not repeat these stages unchanged or treat replay/audit success as
evidence that their rejected proposals met the geometry constraints.

The new research implementation includes:

- A continuous outward-boundary loss, including the gradient through the peer
  endpoint. The former boolean eligible-face switch could create a card-sized
  loss jump at a near-tangent endpoint. The new loss catches the tiny inward
  corner cut and passes four peer-position derivatives and six boundary cases.
- `joint_active_proxy.py` evaluates every current near pair in chunks of at
  most 16,384. An explicit 24-sigma logistic-tail truncation bounds the scalar
  soft-loss difference by 0.00013737 on Captain; this is not a native-count or
  gradient error bound. Hard constraints and native acceptance are unchanged.
- An optional log-depth crossing signal supplies gradients for deep crossings
  where the logistic count saturates. It changes training, not the measured
  visual-conflict score.
- `SharedEndpointPolicy` mean-pools neural outputs for exactly coincident source
  endpoints on the same card. The immutable grouping is part of the neural
  network, and every saved output is replayed unchanged through the native
  decoder. The generic checkpoint loader recognizes this model variant.
- Optional `--spacing-padding 0` removes the extra two-pixel training gap.
  Training still requests 56 x 42, above the native 55.99 x 41.99 thresholds;
  native acceptance is unchanged. The valid baseline's spacing penalty drops
  from 167.62 to zero, but the trials did not improve the accepted score.

Final active-loss checks pass 96 full-population/finite-gradient comparisons
with tiny chunks, plus 104 route/network derivatives across the actual views.
Both views preserve all 3,454 endpoints at zero output, preserve their 17 / 16
shared endpoint groups, and reproduce saved model outputs exactly. Final actual
view validation peak was 108.2 MiB. Numerical stage peaks were at most 234.9 MiB;
the final individual / overview gap trials peaked at 137.9 / 178.9 MiB.

In the shared-output trials, 19/50 individual proposals and 27/75 overview
proposals were legal, with minimum legal scores 2,000 and 299. Removing the
extra spacing padding yielded minimum legal scores 1,996 and 299. None beats
the retained product. These short, time-bounded trials are not a controlled
proof that the architectural changes alone improve optimization.

Previous sealed archive:
`data/erd-poc/experiments/independent-views-active-loss-20261003/`.
Manifest SHA `34b61244415a1e5a2ea8ebf2829dcc1751719b508da906bdb22f917d6f6c1e4b`.
It preserves 10 stages, 749 frozen neural batches, 1,178 restored-hash-checked
files, 172,840,333 stored bytes, exact stage implementation snapshots, final
validation sources, checkpoints, and the retained best. Archive peak 25.3 MiB.
Do not rerun its exclusive `seal_active_loss_experiments.py`. No product
TypeScript or native decoder changed in this continuation; browser rendering
was not checked again.

That active-loss work required a material training/architecture change. Repeating
the wide random branch is not justified: its three successive stages rejected
all 318 proposals for spacing, retaining 1,990. The new losses and shared-output
model are validated building blocks, not evidence that 150 / 750 is reachable
with the present optimizer. Keep the goal active and the strict native gate.

Previous continuation: `joint_neural_ports.py` implements a shared node/edge
network predicting translations and perimeter offsets (8,804 parameters, or
9,828 with 16 graph channels). Both heads are trained; source geometry is fixed
decoding data, never a trainable coordinate table. Both views reproduce all
3,454 original endpoints at zero output; 104 derivative checks and frozen model
roundtrips pass. Experimental directories use `individual-node-port*` and
`overview-node-port1` under the active experiment root.

An initial 1,991 branch (`individual-node-port-refine3`) FAILED the combined
product loader: a near-corner inward endpoint escaped the old shrunken own-card
test. Do not promote that branch or treat its standalone audit as sufficient.
The loss, standalone audit, and native policy local/global scores now include
the product's .011 outward-boundary contract. Joint batch v6 and port v5 pass
their native tests. A different frozen checkpoint (node-port2 step 164) passes
the stricter contract. `individual-node-port-stage2` -> `strict1` -> `strict2`
-> `strict3` yields 2,005 -> 1,993 -> 1,991 -> **1,990**, with outward endpoints
verified. Combined actual file loading and overview/individual/overview route
switching pass at 299 / 1,990 (peak 160.0 MiB). The candidate is promoted; this
is a six-conflict gain over the former stable 1,996, not proof of a causal
benefit from the joint architecture alone. There was no matched ablation.

Previous sealed archive: `data/erd-poc/experiments/independent-views-node-port-20261003/`.
Manifest SHA `d89b33a73859650536a1eca4baaa87b44fb2148b4f84e82aa6c3705c5b115da8`.
It contains 11 completed stages, 207 frozen joint NN batches, 118,130 actual
additional policy actions, 570 files with restored hashes checked, and
66,405,476 stored bytes. Archive peak 25.8 MiB. It preserves the former best,
the rejected 1,991 branch, exact source versions, all checkpoints, and the new
combined candidate. The manifest records the pre-promotion state; current
candidate audit/provenance records promotion. Do not rerun its exclusive
`seal_node_port_experiments.py` or `promote_node_port_verified.py`.

Final native versions: joint batch v6 (576 geometry comparisons), port v5
(2,048 local/global comparisons), component v22 (6,424 local/global and 3,072
exploration comparisons). The stricter global/local policy scores agree.
The general search score was not changed; only policy scores include the new
outward endpoint condition. Base policy tests still pass 132 derivatives;
the new model passes 104 more after adding the outward loss. Final compile
peak 240.7 MiB, numerical training peak 219.6 MiB; all jobs are finished.
Actual browser rendering remains unverified. No product TypeScript changed.

The subsequent active-loss continuation above trained from this valid best
with the new outward loss and strict native versions. Earlier node-port1/2
weights were trained before the outward loss existed; their rerun through the
stricter validator is not new training. Do not reuse the rejected branch or
the older permissive native binaries for further candidate acceptance.

Codex CLI transport diagnosis: installed CLI 0.160.0; earlier saved evidence
contains response decoding failures and connection resets, with root cause
unconfirmed. A bounded current-thread log check at 2026-10-03 09:01 KST found
no matching disconnect/decode/reset failures in the retained 1,000 rows.
That check does not cover a separate terminal conversation. A subsequent
read-only, all-retained-thread WARN/ERROR scan at 10:25 KST covered the latest
24 hours: 47 rows, 24 matching transport events in three other threads,
including 21 decode errors and one connection reset. Latest retained matching
event: 02:44:59 KST, target `codex_core::responses_retry`. Current thread had
zero matching events. Neither scan proves resolution or full terminal-process
coverage. Sanitized evidence: `.tmp/cli-transport-all-threads-diagnostic.json`.
No raw messages/credentials were stored. No CLI configuration was changed.
A repeat of the same bounded scan at 11:06 KST returned the same 47 rows,
24 transport events, 21 decode errors, and one reset; peak RSS was 19.4 MiB.
CLI 0.160.0 and its local help were reconfirmed. A scan at 11:51 KST returned
48 warning/error rows and the same 24 transport events (21 decode, one reset);
its peak RSS was 19.3 MiB. The installed package still reports 0.160.0, and
the user-level config has no custom model provider or transport-feature
override. The inspected shell has no proxy environment variables; this does
not rule out system proxies or network intermediaries. The terminal error's
root cause remains unconfirmed. A separate `codex doctor
--json` attempt did not finish: the 256 MiB process-group RSS guard stopped
it at 313.1 MiB (exit 137). This is not a completed config/auth diagnosis,
and the diagnostic must not be repeated unchanged under this resource budget.
Its sanitized outcome is `.tmp/cli-transport-followup.json`. Official CLI
documentation confirms `/new` starts a fresh chat in the same CLI session;
using a short no-tools prompt there is a scope test, not a confirmed fix.
For recurring disconnects, a fresh focused Codex chat can read this active
state to recover the research work. The optional question about whether a new
CLI conversation also fails is still unanswered; do not repeatedly ask it.

New goal experiment root: `.tmp/visualcross-ml-150-750-20261003/`.
`baseline-audit/audit.json` remeasures 299 / 1,997 through the actual product
loader and browser route functions, with explicit 150 / 750 thresholds and
`thresholdsMet:false`. Baseline combined file hash is still
`fac66acdda729c4812d0349397056aea455cee3fc4114c1ad02daf3f73311010`.
Read-only topology analysis (`structure.json`) found 1,473 / 1,492 individual
edge crossings confined to the graph's 2-core; moving tree fringes alone has
little remaining potential.

- `component-environment-v21` adds bounded exploratory admission, restores the
  best action prefix, and verifies that prefix against frozen model outputs.
  Native tests: 6,424 original comparisons plus 3,072 exploratory comparisons,
  367 uphill moves, 12 restored checkpoints. Compile peak 238.0 MiB.
  `individual-explore1` and `overview-explore1` both returned the unchanged
  baseline (20,000 proposals each; peaks 144.5 / 183.1 MiB). Do not repeat the
  same experiments without a material change. No promotion occurred.
- New `ml_joint_batch_environment.cpp`, `joint_layout_proxy.py`, and
  `run_joint_neural_layout.py` implement a 6,306-parameter network predicting
  all physical-card translations together. Training changes network weights;
  every candidate is a saved frozen forward pass. Exact geometry only decodes
  and evaluates, never repairs or searches. The best legal improvement alone
  is retained; otherwise source geometry is unchanged. Native batch tests:
  192 full comparisons / 168 legal batches, compile peak 214.4 MiB. NumPy
  gradients pass 44 finite-difference checks. Actual browser remains unverified.
- `individual-joint1` (crossing-depth loss) and `individual-joint2` (unclipped
  sigmoid margins) improved surrogate losses but worsened actual conflicts;
  all candidates were rejected. Both replayed 65 frozen networks and retained
  1,997 exactly (peaks 198.6 / 219.5 MiB). Original Python sources are preserved
  as `source-*.py` inside each experiment. The v3 loss differentiates
  boundary-clipped ports and uses tighter margins. `individual-joint3` found
  legal 2,027, still above the best 1,997, and retained the source. Warm-started
  `individual-joint4` found legal 2,021 (step 236, frozen hash
  `0d9bf7ccdb3474a45f8842d92ce6bc349f20eb5824c25bee382b3cf150b3fa79`),
  also retained source. Peaks 208.0 / 230.3 MiB. Never infer success from proxy loss.
- `joint-batch-environment-v2` allows an explicit <=200 temporary regression
  only for isolated neural batch candidates; geometry/area limits remain hard.
  `individual-joint-stage1` replays the trained 2,021 model with no further
  training and allowance 100. All positions and canonical boundary routes
  replay exactly; product audit passes, peak 192.9 MiB. It is not promoted.
  Subsequent ML port models v11/v10 yield `individual-joint-ports1`=2,006 and
  `individual-joint-ports2`=2,004; moving-ray v15 then yields
  `individual-joint-refine1`=1,999 and `individual-joint-refine2`=1,996. The last
  stage passes all frozen action/geometry replay and product checks, peak
  142.6 MiB. Net gain over the previous best is only one conflict; there is no
  matched ablation establishing that coordinated training caused this gain.
- Current proxy v4 uses boundary-clipped segment/rectangle separating axes.
  Its binary baseline count agrees with independent native geometry exactly:
  1,540 edge crossings + 539 card hits = 2,079 for canonical source ports.
  See `proxy-baseline-parity.json`; 44 gradient checks still pass.
  `individual-joint5` continues the 2,021 frozen model with this corrected loss
  and yields 2,018, peak 219.3 MiB. It is not promoted.
- `overview-joint1` yields 321, worse than 299. Its initial product audit hit
  the RSS guard at 262.3 MiB and was killed. The runner now ends its NumPy
  training worker before starting the product audit, checking saved output
  hashes between processes. Recovery replayed all 129 frozen batches; the
  resumed audit peaked at 182.9 MiB. A complete frozen inference-to-audit run
  (`overview-joint-stage1`) reproduced the same 321 at 180.3 MiB. No browser
  rendering was checked. Do not describe the interrupted run as under budget.
- The actual file loader and real browser route functions verify the combined
  **299 / 1,996** candidate, including overview-to-individual-to-overview route
  equality. Audit peak 168.5 MiB. This candidate is promoted to
  `data/erd-poc/candidates/captain-ml-independent-views.layout.json`; F5 already
  reads this path. Its audit explicitly records 150 / 750 and thresholds false.
- Permanent archive: `data/erd-poc/experiments/independent-views-joint-20261003/`.
  Manifest SHA `4b0fb4622af263fc7af95c433bc8edfb44889194835600925a0994df14b28fda`.
  It preserves 14 stages, 120,000 policy proposals, 482 coordinated NN batches,
  negative results, exact training source versions, inputs, frozen checkpoints,
  prior product metadata and validation. All 894 stored files passed restored
  SHA verification. The archive's promoted:false records its pre-promotion
  state; current product audit/provenance records the subsequent promotion.
  Never rerun the exclusive `seal_joint_experiments.py` into this directory.

### Annealed and attached-port follow-up: best unchanged at 299 / 1,996

- Loss smoothing can now anneal from a configurable temperature to one. The
  default remains one. Temperature 1/8/32 passes 132 finite-difference checks.
  `individual-annealed1` (1024 max step, 20-second training, initial factor 32)
  produced 2,034 after 57 frozen batches, worse than 1,996; peak 189.2 MiB.
- New `joint_grouped_routes.py` models the actual shortest external member,
  rigid Leaf member offsets, physical boundary clipping and changing group
  endpoints. Its canonical overview baseline equals native 364. An added
  differentiable penalty preserves required Leaf pairs. The grouped loss
  passes 56 derivative comparisons, including active representation penalties.
  Continuous clipping intentionally omits final endpoint quantization; native
  geometry remains the authority for every candidate.
- `overview-grouped1` yielded 325; 112/129 proposals changed a required Leaf
  representation. Adding the representation loss in `overview-grouped2` made
  all 103 batches legal and yielded 314. Frozen port models v11/v10 then yielded
  `overview-grouped-ports1`=308 and `overview-grouped-ports2`=305. This branch is
  unpromoted. Peaks: 179.6 / 177.8 / 188.9 / 190.1 MiB respectively.
- New native `joint-batch-environment-v3` supports `--attached-ports 1`: every
  existing full relationship endpoint follows its owner's neural translation.
  It does not select or repair coordinates. Both decoder modes pass 384 exact
  geometry comparisons (323 legal batches); attached endpoint offsets are
  independently checked. Compile peak 225.3 MiB; test peak 2.7 MiB.
  Runner `--attached-ports` replays all translated endpoints, verifies the
  recorded decoder and prevents mixing incompatible warm-start decoders.
- The attached loss exactly preserves baseline 299 / 1,996 at zero neural
  output and passes 38 further derivative checks (peak 91.6 MiB). However,
  `overview-attached1` accepted no batch (104 hard-condition rejects, one
  spacing reject), and `individual-attached1` rejected all 55 for hard geometry.
  Source geometry was retained and product-audited. Peaks 181.0 / 197.8 MiB.
  The present soft visual loss excludes adjacent-edge crossings and own-card
  reentry, while native validation requires them to remain zero. This is the
  next concrete loss-design issue; do not relax native constraints or repeat
  the same unconstrained attached training. A lower proxy value is not success.
- Every joint run now saves exact `source-*.py/cpp` snapshots before training.
  The earlier annealed/grouped snapshots were reconstructed and their hashes
  exactly matched each run's recorded implementation hashes before archiving.
- Permanent follow-up archive:
  `data/erd-poc/experiments/independent-views-attached-20261003/`, manifest SHA
  `fbc3202ef449e97b0d18a87791ae7aca2c7c25bcbd77221396c714fe338e0efa`.
  Seven stages, **15,933 actual port-policy proposals** (40,000 was only the
  requested aggregate ceiling), 449 frozen coordinated batches, 718 restored
  file hashes verified, 36,445,526 stored bytes. Archive peak 24.6 MiB.
  It includes all negative results, source versions, native/model dependencies,
  validation and the current stable 299 / 1,996 candidate. No candidate from
  these seven stages was promoted. Do not rerun `seal_attached_experiments.py`.

### Hard geometry, graph inputs and sampled global training: best unchanged

- `joint_hard_geometry.py` adds continuous penalties for adjacent-edge crossings
  and own-card reentry, on full relationships and projected overview routes.
  It excludes identical shared endpoints that remain attached to the same card.
  The two prior frozen failing batches are fully explained: individual 24
  adjacent crossings => native hard 48; overview full 46 adjacent + 7 own hits
  => 99 and projected 7 adjacent + 7 own hits => 21. Baseline penalty is zero.
  Forty finite-difference comparisons pass. Boundary-contact loss is still
  absent; native boundary contacts remain strict and are never waived.
- Runner options `--hard-weight` and `--quantized-loss` train on rounded model
  displacements. Rounding uses an explicitly documented straight-through
  gradient estimator; exact frozen outputs and geometry still replay. With
  attached ports, `individual-hard1` had 34/49 legal batches, best legal 2,000;
  `overview-hard1` had 43/75 legal, best 299. `individual-hard2` warm-started
  trained step 188 at lower learning rate: 27/50 legal, best 1,999. No promotion.
  Peaks 201.2 / 174.4 / 212.9 MiB. Whole runs completed and source retained.
- `--graph-channels 16` appends topology-only graph inputs to the 64 relative
  inputs (7,330 trained parameters). `joint-input-features.npy` is saved and
  hashed; it is necessary to replay these models, rather than using only the
  original 64-column observation JSON. Warm starts verify this feature hash.
  `individual-graph1` used a dense normalized Laplacian eigensolve, had 32/50
  legal batches, best 2,000, and peaked at **255.8 MiB**. It is a negative result.
- The current `joint_graph_features.py` uses sparse adjacency operations, 64
  subspace iterations, component-constant removal, thin QR and a small projected
  eigensolve. It allocates no dense node-by-node spectral matrix. Edge-order
  determinism, isolated rows and fixture eigen residual tests pass. Captain
  features are approximate (maximum eigen residual 0.0064061, not an exact
  eigensolve). `individual-graph2` had 29/49 legal, best 1,999, peak 194.1 MiB.
- `joint_sampled_proxy.py` estimates the crossing/card-hit training loss from
  uniformly sampled pairs, with population weights. Active card spacing is
  still checked exactly in each training step. Its 24 derivative comparisons
  and full-population loss/gradient parity pass. The base loss's 132 comparisons
  across temperatures 1/8/32 still pass. Sampling never changes native candidate
  acceptance, product verification, the fixed source frame or the area cap.
- `--sampled-pairs` permits `--max-step` up to 100,000 within the source frame;
  it avoids materializing all possible expanded crossing tensors. `--head-std`
  optionally initializes a fresh network output head before training. Every
  tested candidate is still a frozen trained forward pass. `individual-global1`
  (random head .08, max step 50,000, 8,192 pairs) evaluated 129 batches, all
  rejected for spacing; conflicts rose to tens of thousands. Peak 139.8 MiB.
  `individual-global2` starts at the source with zero head, 32,768 pairs and a
  lower learning rate: 71/95 legal, best 2,090, peak 139.9 MiB. Sample estimates
  fluctuate substantially; they must never be reported as actual visual counts.
- New permanent archive:
  `data/erd-poc/experiments/independent-views-hard-graph-20261003/`, manifest SHA
  `2f05b5c9bba31537a969d4006421f014b8ec8d7057de8e2c0be1cd07c3ef7f1f`.
  Seven completed stages, 497 frozen neural batches, all 748 restored file hashes
  verified, 47,743,347 stored bytes. Archive peak 23.9 MiB. Exact per-stage
  implementation snapshots include the replaced dense graph implementation.
  Current code and all model/native dependencies are included. No candidate
  from these seven stages was promoted. Do not rerun `seal_hard_graph_experiments.py`.

The shared edge MLP proposed after these seven negative stages has since been
implemented and verified; see the latest node/port continuation at the top.
All native decoders remain deterministic and may not search or repair ports.

All runs listed above have finished. The goal is still active and unmet.
Small neutral moves and the current local surrogate have plateaued. Further
work needs a material model/loss change, not another identical rollout.
Current coordinated NN uses 64 relative input features, 64/32 hidden units,
fixed source-frame bounds, and an explicit canonical or attached-port decoder.
For overview training use `--grouped-route-loss` (also enabled implicitly by
`--attached-ports`) to model grouped member routes. Hard losses now cover
adjacent crossings and own-card reentry; endpoint parameters are still fixed
or canonical and need to become learned outputs as described above.

Current user direction: **“니가 줄이지 말고 ML모델이 줄이게끔 하라.”**
Use the learned proposal workflow for subsequent reduction work. The earlier
native node/port search results below are historical; do not resume manual
coordinate selection or native heuristic proposal search as the active path.

### Completed target: overview 299 and individual 1,997

The completed goal is **“계속 진행해서 개별 보기도 2000 언더로 줄이고 개요도 300이하로 줄여보자.”**
Do not mark this goal complete at an intermediate reduction. Keep the ML-only
proposal requirement and approximately 20% resource operating target. Research
jobs remain serialized, single-threaded, nice +10, with the 256MiB group RSS guard.

- Previous shared-position product-verified research candidate:
  `data/erd-poc/candidates/captain-leaf-card-connections-ml-386.layout.json`.
  Original run: `.tmp/visualcross-ml-targets-20261003/round7fixed2/`.
  **Overview 386 / individual 3,453**, hash
  `907cf119e674b265dbbbaf47c0308f8a86a0dfef92715973fb221db4aec045fc`.
  Complete 1,727 canonical relations, 49 Leaf cards, unchanged card sizes and
  rigid Leaf membership/geometry, zero overlaps/spacing, actual file load and
  browser route-function parity verified. Actual UI/browser remains unverified.
- The old v1 action space could not affect 3,257 individual-view conflicts.
  New `ml_component_environment.cpp` supports all 1,035 physical components;
  each Leaf moves rigidly. It mirrors shortest external group representatives
  and Leaf clipping, rejecting representation changes. No native coordinate
  proposals, searches, or repairs. `export_learned_components.cjs` and
  `apply_learned_components.cjs` validate complete two-view mappings and output.
- New policies have 64 relative features and 9,685 parameters. Frozen checkpoints
  v2-v5 and complete training data/traces are in the experiment root above.
  v2: ray ports; v3: compound synthetic training; v4: attached representative
  ports; v5: every port attached, dense/hub synthetic data, smaller learned
  mixture variance. v2-v5 use synthetic-only training.
- Later v6 adapts to 23,488 non-committing Captain reward samples (140 positives)
  plus the synthetic data. It is explicitly Captain-trained, not an unseen-graph
  result. `round6` yields 387/3,460, model hash
  `2ff93c849d0c0cf73afdfc7ca019bc74dea841d942b58ccc8e869dc60341ec75`.
  v7 uses synthetic joint parent/Leaf moves (same 9,685 parameters, schema
  `relative-joint-context-v3-64`), checkpoint hash
  `fe1624651ea1a05aac93662637f090f5e56cd6c5be6b83db113834f3461700eb`.
  Its verified result is `round7fixed2` above, after 12,000 proposals/12.52s.
- Verified ML chain from 417/4,585: 401/3,935 -> 395/3,663 -> 392/3,547 ->
  391/3,529 -> 387/3,492. Root v2 matched untrained control gives 404/4,067
  at the same 12,000 proposals/seed. Later stages are not separate causal
  ablation claims. Final stage 20,000 proposals, 14.45s inference, 22.97s whole
  workflow, 186.4MiB measured peak. Training/compile peaks stayed below 256MiB.
- `run_learned_leaf_layout.py --components --environment <binary> --checkpoint
  <model> --source <layout> --payload <payload> --out <fresh-directory>` now
  exports, infers, replays all model actions, applies, and audits serially.
  Use the memory wrapper; optionally `--observations` and `--budget` <= 20,000.
- A separate canonical-start experiment (all ports expressed as center rays)
  regressed to 437/3,576 and was rejected for promotion. It now has an explicitly
  **experimental-not-promoted** snapshot inside `canonical1/` for longer ML
  rollouts. `canonical2` is 421/3,536; `canonical3` is 411/3,467. These are NOT
  the best candidate. `--experimental` is restricted to `.tmp`, keeps every
  structural/coordinate audit, and must never be used to claim target success.
- New runtime binary: `component-environment-v13`, hash
  `7293f85a87a1e932e47c991489031a4e9f48959e561f8e9dbf902cd0459d0c19`.
  A joint-move rounding bug in v10 was caught before application: original
  sub-cent coordinates were rounded in the individual state but not the
  physical state. v13 quantizes the displacement once, preserves both input
  coordinate precisions, and stabilizes nearest-neighbor distance ties at
  1e-6. It passes 1,088 global delta comparisons, fractional-center regression
  checks and translation-invariant features, followed by actual product audit.
- Current bottleneck: high-pressure hubs and dense areas reject most moves.
  Next work should use measured rejection reasons/model adaptation or expand
  learned joint moves; do not resume manual/native heuristic coordinate search.
- Preserved checkpoints: `data/erd-poc/checkpoints/leaf-component-policy-v2.npz`
  through v7, with training reports/data archives. All 112,000 actions replayed.
  Step evidence: `data/erd-poc/experiments/learned-components-20261003/`.
  v6 adaptation data are in `adapt387/`; its 20s non-committing evaluation left
  the source geometry intact. The older archived 387/3,492 candidate remains
  available with its own source provenance. Do not rerun `seal_progress.py` or
  `seal_extension.py` blindly: both preserve archives with exclusive writes.

### CLI transport interruptions and independent-view experiment

#### Historical checkpoint for the completed 300 / 2,000 target (2026-10-03)

- **The previous numeric targets were met: overview 299 / individual 1,997.**
  This historical checkpoint is superseded by 299 / 1,996 above; the new
  150 / 750 goal remains active. The then-promoted combined file:
  `data/erd-poc/candidates/captain-ml-independent-views.layout.json`, SHA
  `fac66acdda729c4812d0349397056aea455cee3fc4114c1ad02daf3f73311010`.
  Audit and provenance files use the same stem without `.layout`.
- F5: select **Run Captain (ML independent views)**, the first launch profile.
  Turn off **both June bundles and Leaf cards** for the independent individual
  geometry. Every other grouping combination uses overview geometry. Card
  positions and ports switch together; manual edits and viewport are retained
  per view. Reset and Refresh return to overview geometry. Legacy files and
  the older 468 launch profile remain available.
- Frozen neural policies produced every new position and endpoint. Native code
  only decoded, measured and accepted/rejected their actions. Both views preserve
  1,244 models / 1,727 relationships and all card dimensions; the overview keeps
  49 rigid Leaf cards and one external line per required pair. Zero spacing
  violations and the existing 1.5e9 area cap hold in both views.
- The actual product file loader and the real browser route functions agree on
  both scores, all endpoint routes, and the overview→individual→overview round
  trip. Final composition source: `.tmp/visualcross-ml-targets-20261003/combined-final/`.
  Source stages: `individual43` (SHA
  `81f9b2eb511503ba4f9a4cc940507fda95f2542b623449da51a0577ec2b0857d`)
  and `overview-final-moving1` (SHA
  `23c020f04cbc7ce41c244f70e628d8272d7fb85fa03a07c70450a36e9448fc95`).
- Permanent final archive:
  `data/erd-poc/experiments/independent-views-neutral-area-20261003/`.
  It includes 53 completed stages / 980,972 additional replayed proposals, input
  and final geometries, current source snapshots, runtime binaries, new frozen
  overview-only v16 weights/training data, pair-policy-v1 and negative collections,
  intermediate combined-file audits and final composition. Every compressed file
  passed decompressed SHA-256 verification. Earlier archive lineage is listed in
  the promoted provenance. Do not rerun `seal_neutral_area.py` (exclusive writes).
- Learning progression: neutral component moves reached individual 2,136 and
  overview 326. Model-generated bounded spacing exploration temporarily allowed
  up to 200 extra conflicts within the existing 1.5e9 cap, then learned refinement
  improved both. Port neutral admission (native v4) and moving-endpoint rays
  reached the final thresholds. Source snapshots preserve the exact mode flags;
  no higher-scoring intermediate was promoted. The pair swap model had no real
  gain; overview326 adaptation had only one positive and was not retrained.
- TypeScript's actual bounded F5 build passed at 190.7MiB; 62 relevant integration
  checks passed, including three new tests for atomic view switching, separate
  edits/viewports, empty Leaf groups and rejection of incomplete/detached geometry.
  Native global v4 passes 768 comparisons / 54 accepted / 8 temporary-regression
  cases. Native port v4 passes 2,048 comparisons / 260 accepted / 218 neutral cases.
- **No real browser, viewport, screenshot or visual signoff is claimed.** Browser
  plugin bootstrap failed with `Importing module "node:process" is not allowed
  in node_repl` before browser selection. UI Design Workflow and Web Design
  Guidelines source review were applied; detailed evidence is archived in
  `ui-validation.json`. This is separate from the successful route-function VM
  verification and actual product file loader.
- Resource policy: one numerical job, one math-library thread, nice +10 applied
  by the wrapper, 256MiB process-group RSS monitoring, ~20s / <=20k proposals per
  rollout. This is conservative operation, not a measured hard 20% CPU quota.
  One early combined-file check was killed at a 457MiB monitor sample; small
  comparisons plus a 128MiB V8 heap cap resolved it. Final composition peaked at
  169.2MiB; archive creation at 27.5MiB. All numerical jobs are finished.
- CLI 0.160.0 transport failures remain a separate, unconfirmed server/network
  issue. Historical diagnostic below is retained. A latest bounded 1,000-row
  log scan found no matches; that does not prove the issue is fixed. No credentials
  or network settings were changed. A fresh Codex chat can read this checkpoint
  if repeated long-thread streaming/compaction failures prevent continuation.

- Previously archived isolated results: **individual 2,159** at
  `data/erd-poc/experiments/independent-views-moving-ray-20261003/individual17/candidate.individual.layout.json`
  (SHA `60376dcc5e32403674667d2e367b61826933f153505b77487fe3dedd1b441526`)
  and **overview 332** at
  `data/erd-poc/experiments/independent-views-moving-ray-20261003/overview-moving-ray1/candidate.layout.json`
  (SHA `9382bf40f766e977f830ee1840ac65287a5e94e1b4ef16e8f24c7806118436d4`).
  These are separate experiments, not a combined product result. Both targets
  remain unmet. Individual stages 9-13: 2,219 -> 2,217 -> 2,190 -> 2,182 -> 2,163;
  latest individual breakdown: 1,576 edge crossings + 583 card intersections,
  all 1,244 cards/1,727 relations/3,454 boundary endpoints, no spacing violations.
  Overview canonical ray stages: 357 -> 343; then learned ports 336 -> 335
  and learned neighbor pairs -> 333, moving-endpoint rays -> 332 (associated
  individual score 4,200). The latest area stays 1.0790939194464e9.
- New frozen models v9 (swap), v10 (absolute boundary ports), v11 (residual
  boundary ports), v12-v13 (global spacing), v14 (rigid neighbor pairs) and
  training data/reports are archived under `data/erd-poc/checkpoints/`.
  Native swap, port and neighbor decoding are replayed from the neural actions as
  well as the frozen neural outputs. No heuristic coordinate search is used.
- Current extension under validation: `ml_global_environment.cpp` learns two
  log spacing scales, preserving card dimensions and translating Leaf groups
  rigidly. Unlike earlier fixed-frame experiments, it may use the existing
  product area cap **1.5e9** (source area 1.0790939194464e9). This change was
  explicitly announced; it does not shrink cards or raise the product cap.
  Global v3 passes 512 full metric comparisons / 26 accepted actions, rigid Leaf
  checks, attached-port checks and translation-invariant features (233.2MiB
  compile peak). Frozen v12/v13 training and complete real-product workflows
  passed, but accepted no real moves: ray resets regressed the individual score;
  attached global moves hit hard geometry constraints, and overview moves often
  changed the fixed Leaf projection. `individual14/15` and `overview-global1/2`
  preserve this negative result; do not repeat those unchanged experiments.
- Component v17 adds one model-generated rigid translation of a card plus its
  nearest rectangle neighbor; no metric selects the neighbor and no rejected
  action tries alternatives. v14 learned this operation from 135,360 synthetic
  actions (8,317 positive), 2,115 contexts; schema
  `relative-neighbor-pair-context-v6-64`. Checkpoint SHA
  `afdf7982c59b89b93bc1f9d0ca39168bdfadbe55097017c76204125c9cc24c10`;
  30 epochs / 5.11s / 153.3MiB. Native v17 SHA
  `cc42465327dbdc31ef07c08625463465c74ba0884f6a9dd0ce84da82045e7ff8`;
  4,208 full comparisons / 54 accepted neighbor moves / 126 swaps pass.
  Exact decoded neighbor, final positions and attached routes replay passed
  for individual16 (2,161) and overview-neighbor1 (333), prior to v15 below.
- Permanent extension archive: `data/erd-poc/experiments/independent-views-extension-20261003/`.
  It stores 15 completed stages, 217,642 additional proposals, all exported
  geometry/final snapshots, frozen checkpoints v9-v14, compressed training
  data and traces, source snapshots and runtime binaries. Every compressed file
  passed a decompressed SHA-256 check; archive peak 25.4MiB. Existing archives
  remain intact. Do not rerun `seal_independent_extension.py` blindly (exclusive
  writes). New port adaptation collection code is newer than this snapshot.
- `adapt2161/` is an exact export of individual16 for
  non-committing Captain reward collection. First attached-port collection
  sampled 1,664 actions / 13 high-pressure contexts / 4 positives in 22.27s,
  with source geometry unchanged. A broader pass uses 8 samples per context
  and the existing optimized v15 environment: 541 contexts / 4,856 actions /
  only 4 positives in 6.72s. New residual-port collection covers 468 contexts /
  29,952 actions / only 6 positives in 5.09s. It verifies exact unchanged card
  positions and routes after non-committing sampling. These scarce new positives
  were preserved without another redundant adapted fit. Port environment v3
  passes 1,024 full comparisons / 22 accepted fixture actions; compile 228.3MiB.
- Frozen v15 uses a new `moving-ray` decoder: translate the model-selected
  component, keep stationary opposite endpoints fixed, and reattach only moving
  endpoints by boundary rays. Internal rigid-component edges translate as a
  whole. There is no native coordinate selection, alternative search or repair.
  Schema stays `relative-component-context-v2-64`; decoder metadata is checked.
  Training: 64 synthetic graphs / 2,222 contexts / 142,208 actions / 9,938
  positives, 30 epochs / 5.17s / 152.9MiB, selected epoch6. Checkpoint SHA
  `0dbb227368f10b06bd2b0d9ab3f2a776b0c7c48384696dbcc39967e8030fc2d7`.
  Native component v18 SHA
  `415c8eee1066035ea62cefe2a013d9f6f8686986037833210ddfeef6cbca9dc8`;
  5,352 full comparisons, including 65 accepted moving-ray fixture actions,
  stationary-port preservation and translation invariance; compile 234.6MiB.
  Both real stages pass frozen neural output replay AND exact decoded final
  card positions/canonical routes, followed by actual product renderer audits.
  Individual17: 2,161 -> 2,159, 14.57s whole workflow / 141.5MiB. Overview:
  333 -> 332, 20.80s whole workflow / 188.5MiB. Neither is installed in the UI.
- Permanent latest archive:
  `data/erd-poc/experiments/independent-views-moving-ray-20261003/` contains both
  latest candidates, 40,000 additional actions, all input/final geometry,
  frozen v15 weights/training data, source snapshots, binaries, and the complete
  non-committing Captain collections above. Compressed round-trip hashes pass;
  archive peak 21.1MiB. Do not rerun `seal_moving_ray.py` blindly (exclusive writes).
- Important scale correction for future work: component action scale is already
  **max(512, median external relationship length)**, not a maximum of 512.
  Actual observations include scales up to about 18,000 and proposed moves up
  to about 17,000. Simply adding a larger movement range is not a new solution.
  Current local move policies yield very few positive Captain samples. Further
  work needs a new learning/action-space reason (for example coordinated or
  pair-context-aware nonlocal actions), not identical repeated rollouts. Such
  extensions are not implemented yet. All numerical jobs have finished at this
  checkpoint; both goal thresholds and product view integration remain open.

- User reports repeated `stream disconnected before completion: Transport error:
  network error: error decoding response body` in **Codex CLI** (0.160.0).
  Read-only current-thread log inspection found 29 generation retries with that
  message, 5 with `Connection reset by peer (os error 54)`, and 4 remote-compaction
  retries. Last observed failure: 2026-10-03 02:32:28 +09:00. The underlying
  server/network cause is unconfirmed. No proxy environment variables were set.
  Sanitized evidence: `.tmp/visualcross-ml-targets-20261003/cli-transport-diagnostic.json`.
  Do not reset credentials, alter network policy, or claim resource limits fix it.
- Preserve results on disk and keep tool output short. If the CLI repeatedly
  fails while compacting this long thread, a fresh chat can read this active
  section and resume the saved candidate. Do not rerun a process while its live
  handle exists; inspect the saved audit and process state first.
- View-specific positions have only been tested in an isolated experiment.
  The optional user preference question about allowing different positions in
  overview and individual views is unanswered. Product view behavior is unchanged.
  These individual-only scores must not be combined with the shared overview
  score to claim that the complete product goal has been reached.
- Frozen v3/v2 policies and v13/v14 environments improved the separate graph:
  3,453 -> 2,714 -> 2,566 -> 2,446 -> 2,410 -> 2,348 -> 2,297 -> 2,270 -> **2,227**,
  preserving 1,244 actual cards and all
  1,727 relations. Each stage passed frozen action replay, exact product renderer
  route/metric parity, all 3,454 boundary endpoints and zero spacing violations.
  First stage explicitly canonicalized center-ray ports (3,453 -> 3,662 before
  learning). Later stages continue without another representation reset.
  Verified candidate: `.tmp/visualcross-ml-targets-20261003/individual8/candidate.individual.layout.json`,
  SHA `249974acc2090e87eb51596815a71b343ff7ddf70ecd639ec0c7141869ab90b3`;
  audit in the same directory. Shared overview preservation and actual browser
  verification are **not** claimed.
- New `scripts/erd-poc/run_individual_policy_experiment.py` runs a single next
  stage from an already audited directory, saving full details in `workflow.log`
  and a compact `workflow.audit.json`. Use `.venv-ml/bin/python` under the 256MiB
  memory guard with `--previous`, fresh `--out`, `--checkpoint`, `--environment`,
  `--payload` and `--seed`. It caps each inference at 20 seconds/20,000 proposals
  and executes inference, full frozen replay and product audit serially.
- v14 adds an explicitly isolated `--overview-only` admission objective. Both
  views still preserve all nodes, sizes, relationships, endpoint geometry and
  hard conditions. Individual visual conflicts may rise. Applying this output
  requires `--experimental` and a `.tmp` destination; default shared-view
  admission is unchanged. Expanded self-test passes 2,148 global comparisons,
  objective monotonicity, fractional coordinates and translation invariance.
  Binary SHA `674d8ed6974d3ba42e55793b199f1e2867313e4665d4749d41872afe33aa7233`.
  Measured compilation peak 200.6MiB; experiment workflow peaks <=179.6MiB.
- Overview-only results: `overview-only1` 382/3,567 (v7), `overview-only2`
  **362/3,649** (v3), `overview-only3` unchanged after another 20,000 proposals.
  The complete actual product audits, source hashes and action replays pass;
  all 49 Leaf cards and 1,727 canonical relationships remain. Best isolated
  overview file: `.tmp/visualcross-ml-targets-20261003/overview-only2/candidate.layout.json`,
  SHA `8c044736890d5c2cabfb4b15543201b32277fb41a71921a3b4f706d053b16429`.
  These are not replacements for the shared 386/3,453 candidate. Further
  identical v3 rollouts on this overview state need a new reason after the
  observed zero gain. A different trained decoder or a disclosed canonical
  representation experiment can test the current hard-condition bottleneck.
- `adapt2270/` contains the exact individual7 state exported for non-committing
  Captain reward collection (`--decoder ray`, v14). Collection completed in
  20.0296s, 98 contexts / 10,752 samples / 227 positive actions, 5.9MiB peak,
  with source geometry unchanged. `dataset.report.json` records full hashes.
- v8 uses this data plus the v2 synthetic data (Captain training is explicit).
  Checkpoint SHA `c8977eb46de5ea53024d78a67aaf98a3af6fc717aa53aa7fa8cde01222b995dd`;
  ray decoder, minimum log sigma -7, gain cap 64, adaptation weight 64, seed181,
  30 epochs / 7.307s / 181.8MiB peak, selected epoch9. Held-out validation is
  synthetic only. Its actual product result is individual8 above (22.87s whole
  workflow, 132.5MiB peak). Training exposed integer JSON reward casting under
  the default exponent; `load_data` now makes rewards float64. Integer reward
  weighting regression and all 44 gradient checks pass (max error 4.90e-10).
- v15 changes only overview-only observation prioritization: visible overview
  conflicts drive which contexts are shown to the model, rather than hidden
  individual-view conflicts. Shared and individual default objectives remain
  unchanged. It also verifies the runtime objective in the policy report.
  Binary SHA `fe2f03bc5c9527d5988982cc30d53ae2fd02ccfa5e8b4f63a6afa9adb06100ff`;
  self-test still passes 2,148 comparisons. Compilation peak 244.5MiB.
- With v15, `overview-only4` improves the isolated overview to **355** (its
  associated full graph is 3,775, not an individual-view improvement). Full
  product loading, relationship/Leaf membership, exact boundary endpoints and
  model action replay pass. 25.79s total workflow, 179.2MiB peak.
  SHA `9fac1d0bd508c8cc8cc5dbf89fb60af3db7499d1de7e67c53a217c5574aa722c`.
- Permanent independent experiment archive:
  `data/erd-poc/experiments/independent-views-20261003/manifest.json`.
  `individual.layout.json` is 2,227 and `overview.layout.json` is 355; these
  are distinct layouts, not an integrated product result. All 226,730 proposed
  actions and observation records are archived with their original hashes,
  product audits and workflow reports. v8 checkpoint/training data are archived
  in `data/erd-poc/checkpoints/leaf-component-policy-v8.*`. Preserve older
  candidate directories and do not rerun the exclusive-write sealing scripts.

Next useful work: continue from the verified `individual8` and `overview-only4`
directories with the corresponding learned policies, or train a measured
extension if gains plateau. The isolated overview still inherits 46 hidden
individual hard conditions; an explicitly unpromoted center-ray representation
experiment may test this bottleneck without weakening any geometry audit.
Both target thresholds remain unmet, and product integration plus actual UI
verification remain outstanding if view-specific positions are adopted.

The v1 details below describe the previously completed pilot:

- Frozen model: `data/erd-poc/checkpoints/leaf-card-policy-v1.npz` (9,301 parameters).
  It predicts a conditional mixture of continuous card displacements from 58
  relative geometry features. It receives no model names, absolute coordinates,
  Captain examples, or final Captain coordinates during training.
- Training: 64 procedural graphs; graph-disjoint training/validation split;
  60,704 random actions, 7,565 improvements. The stored checkpoint is epoch 6
  (1,140 updates), selected by validation likelihood, before Captain inference.
- Equal 12,000-proposal test from the saved 418 payload: untrained weights
  remain at **418 / individual 4,590**; learned weights yield **417 / 4,585**.
  Initial individual score is 4,591. Four additional synthetic graphs give
  aggregate visual 465 untrained vs 452 learned; the learned model loses one
  of the four comparisons. This is a pilot, not a universal improvement claim.
- Latest candidate: `data/erd-poc/candidates/captain-leaf-card-connections-ml-417.layout.json`.
  Native code validates/applies only the supplied policy action. It does not
  generate, search for, project, or repair candidate moves during inference.
  All 12,000 actions replay from the frozen checkpoint; independent inference
  reproduces the same geometry. Product load/coverage/spacing/route audits pass.
- Run `scripts/erd-poc/run_learned_leaf_layout.py` inside
  `scripts/erd-poc/run_memory_bounded.py` with `--source`, `--payload`, and a
  fresh `--out` directory. It compiles the environment, exports both views,
  infers, replays, applies, and audits serially. There is no heuristic fallback.
- Single CPU thread, 256MiB guard; full workflow 19.26s and peak measured
  process-group RSS 241.3MiB. Training takes 4.98s. The requested approximately
  20% host-resource budget is an operating target, not a measured CPU quota.
- This uses the saved 1,244-model payload. Actual app UI and fresh Captain
  source analysis remain unverified; F5 still uses the screen-verified 468.
  See `captain-leaf-card-connections.md` for reproducible commands and links.

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
