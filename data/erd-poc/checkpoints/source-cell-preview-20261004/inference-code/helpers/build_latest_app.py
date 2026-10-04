"""Build current TS sources with the already cached native compiler, no download."""
import json
import os
from pathlib import Path
import subprocess

compiler = Path('/Users/lky/.npm/_npx/fd45a72a545557e9/node_modules/@esbuild/darwin-arm64/bin/esbuild')
sources = sorted(str(p) for p in Path('src').rglob('*.ts') if not p.name.endswith('.d.ts'))
environment = dict(os.environ, GOMAXPROCS='1', GOMEMLIMIT='64MiB')
version = subprocess.check_output([str(compiler), '--version'], text=True, env=environment).strip()
subprocess.run([str(compiler), *sources, '--outbase=src', '--outdir=out', '--format=cjs',
                '--platform=node', '--target=es2022', '--tsconfig=tsconfig.json', '--log-level=warning'],
               check=True, env=environment)
print(json.dumps(dict(compiler=str(compiler), compilerVersion=version, sourceFiles=len(sources),
                      buildMode='transformation-only', fullTypecheck=False, newDependenciesInstalled=False)))
