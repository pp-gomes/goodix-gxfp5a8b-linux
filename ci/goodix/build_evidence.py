"""Collect real Kbuild outputs and reject incomplete built-in integration."""
from pathlib import Path
import json
import re
import shutil
import subprocess

PARTS = ('gx_protocol_core', 'gx_exchange', 'gx_rx_staging',
         'gx_test_event_source', 'gx_transport_disabled', 'gx_kunit')
REL = Path('drivers/misc/gx_offline')


def collect(build, artifacts):
    """Safe on partial/failed builds; preserve text, never binary payloads."""
    (artifacts / 'generated-objects.txt').write_text(''.join(
        str(p.relative_to(build)) + '\n' for p in sorted(build.rglob('*'))
        if p.is_file() and p.suffix in {'.o', '.a', '.ko'}))
    commands = []
    for p in sorted((build / REL).glob('.*.cmd')):
        commands.append(f'FILE: {p.relative_to(build)}\n{p.read_text()}\n')
    for name in ('.vmlinux.cmd', '.vmlinux.a.cmd', '.vmlinux.o.cmd'):
        p = build / name
        if p.exists():
            commands.append(f'FILE: {name}\n{p.read_text()}\n')
    (artifacts / 'kbuild-commands.log').write_text(''.join(commands))
    p = build / 'compile_commands.json'
    if p.exists():
        shutil.copy2(p, artifacts / p.name)
        entries = [e for e in json.loads(p.read_text()) if 'gx_offline/' in e['file']]
        (artifacts / 'goodix-compile-commands.json').write_text(json.dumps(entries, indent=2) + '\n')


def check_members(members):
    for name in PARTS:
        if sum(Path(m).name == name + '.o' for m in members) != 1:
            raise ValueError('Missing or duplicate built-in member: ' + name)


def check_image(symbols, source):
    defined = {line.split()[-1] for line in symbols.splitlines() if len(line.split()) >= 3}
    required = re.findall(r'KUNIT_CASE\((\w+)\)', source)
    required += ['__kunit_suites_start', '__kunit_suites_end']
    missing = set(required) - defined
    if missing:
        raise ValueError('UML image missing KUnit symbols: ' + ', '.join(sorted(missing)))


def verify_link(build, artifacts, source):
    archive = build / REL / 'built-in.a'
    members = subprocess.check_output(['ar', 't', str(archive)], text=True)
    (artifacts / 'goodix-archive-members.log').write_text(members)
    check_members(members.splitlines())
    for name in PARTS:
        if not (build / REL / (name + '.o')).is_file():
            raise ValueError('Missing compiled object: ' + name)
    symbols = subprocess.check_output(['nm', '--defined-only', str(build / 'linux')], text=True)
    (artifacts / 'uml-defined-symbols.log').write_text(symbols)
    check_image(symbols, source.read_text())
    if b'gx-offline-v07\0' not in (build / 'linux').read_bytes():
        raise ValueError('UML image missing suite name')
    return archive
