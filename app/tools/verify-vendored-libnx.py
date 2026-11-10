#!/usr/bin/env python3
"""Check the pinned SDK source without relying on a nested Git checkout."""
from pathlib import Path
import hashlib, json
deps=Path(__file__).resolve().parents[2]/'dependencies'
pins=json.loads((deps/'libnx-source.sha256.json').read_text())
bad=[name for name,digest in pins.items() if not (deps/'libnx-source'/name).is_file() or hashlib.sha256((deps/'libnx-source'/name).read_bytes()).hexdigest()!=digest]
if bad: raise SystemExit('Pinned libnx source differs: '+', '.join(bad))
print(f'Verified {len(pins)} pinned libnx source files.')
