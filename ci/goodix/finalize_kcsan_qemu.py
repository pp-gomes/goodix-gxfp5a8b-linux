#!/usr/bin/env python3
"""Always collect QEMU text evidence, then reuse the artifact hash manifest."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys

from kcsan_qemu_checks import collect_runtime, collect_boot_commands

root = Path(sys.argv[1]).resolve()
root.mkdir(parents=True, exist_ok=True)
try:
    collect_runtime(root)
    build = root.parent / 'work/build'
    collect_boot_commands(build, root)
    if (build / '.config').exists():
        shutil.copy2(build / '.config', root / 'kernel.config')
except Exception as exc:
    (root / 'qemu-collection-error.log').write_text(str(exc) + '\n')

# The common finalizer owns manifest validation; its UML summary is suppressed.
env = dict(os.environ)
summary = env.pop('GITHUB_STEP_SUMMARY', None)
result = subprocess.run([sys.executable, str(Path(__file__).with_name('finalize.py')), str(root)], env=env)
if summary:
    status = json.loads((root / 'status.json').read_text())
    with open(summary, 'a') as stream:
        stream.write('### Goodix KCSAN x86_64/QEMU\n\n')
        stream.write(f"Status: **{status['status']}**; verified execution: "
                     f"**{status.get('kernel_executed_and_verified', False)}**.\n\n")
        stream.write('Inspect status.json, config-compatibility.json, instrumentation-audit.log, '
                     'qemu.log, kcsan-diagnostics.log and verified-results.json. '
                     'Configuration/build/boot failures do not establish a Goodix race.\n')
sys.exit(result.returncode)
