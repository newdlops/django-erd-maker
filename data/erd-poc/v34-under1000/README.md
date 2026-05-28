# v34 Under-1000 Captain Artifact

This directory freezes the first Captain ERD path that crossed the user's
visual threshold:

- `under1000.json`: final measured layout.
- `under1000.tsv`: final positions fed to the C++ skip-CG postpass.
- `repair1.tsv`: pre-scale v34 carrier-aware repair positions.
- `postpass-iter3.json`: C++ fixed-point intermediate before repair.
- `bundleorbit1.tsv`: bundle-orbit intermediate.
- `groupanchor2.tsv`: earlier group-anchor intermediate.

Final metrics from `under1000.json`:

```text
edgeCrossings=999
nodeOverlaps=0
bundleNodeOverlaps=12
bbox=3.46B
```

Fast replay:

```bash
.venv-ml/bin/python scripts/erd-poc/v35_replay_under1000.py --mode quick
```

Full replay is much slower because it reruns exact v34 search:

```bash
.venv-ml/bin/python scripts/erd-poc/v35_replay_under1000.py --mode full
```
