#!/usr/bin/env python3
"""Collect text evidence only; runs even after fetch/config/build/test failure."""
from pathlib import Path
import hashlib
import json
import os
import sys
from build_evidence import collect

root = Path(sys.argv[1]).resolve()
root.mkdir(parents=True, exist_ok=True)
build = root.parent / 'work/build'
if build.exists():
    try:
        collect(build, root)
    except Exception as exc:
        (root / 'collection-error.log').write_text(str(exc) + '\n')
status_file = root / 'status.json'
if not status_file.exists():
    status_file.write_text(json.dumps({'status': 'not_run', 'reason': 'Job stopped before test orchestrator completed',
                                      'kernel_executed_and_verified': False}, indent=2) + '\n')
status = json.loads(status_file.read_text())
manifest = {}
for p in sorted(root.rglob('*')):
    if not p.is_file() or p.name == 'artifact-sha256.json':
        continue
    if p.suffix not in {'.log', '.json', '.config', '.patch', '.ktap', '.txt'}:
        raise SystemExit('Unexpected evidence payload: ' + str(p))
    if p.read_bytes().startswith((b'\x7fELF', b'MZ')):
        raise SystemExit('Compiled binary in artifact directory: ' + str(p))
    manifest[str(p.relative_to(root))] = hashlib.sha256(p.read_bytes()).hexdigest()
(root / 'artifact-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
summary = os.environ.get('GITHUB_STEP_SUMMARY')
if summary:
    with open(summary, 'a') as out:
        out.write('### Goodix UML evidence\n\n')
        out.write(f"Status: **{status['status']}**. Verified kernel execution: **{status.get('kernel_executed_and_verified', False)}**.\n\n")
        out.write('Inspect status.json, raw runtime log, effective config and verified-results.json. '
                  'A compiled kernel or raw runner exit 0 alone is not test approval.\n')
print(json.dumps(status, indent=2))
