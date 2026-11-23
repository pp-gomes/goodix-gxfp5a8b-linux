"""Fail-closed evidence checks for the separate x86_64 KCSAN job."""
import json
import re
import subprocess

from build_evidence import PARTS, REL, check_members, check_image
from verify_ktap import DIAGNOSTIC


def check_instrumentation(commands, imports, defined):
    for name in PARTS:
        if '-fsanitize=thread' not in commands.get(name, ''):
            raise ValueError('Missing compiler instrumentation: ' + name)
    for name in ('gx_exchange', 'gx_kunit'):
        if not re.search(r'\b__tsan_(?:read|write|read_write)\w*', imports.get(name, '')):
            raise ValueError('Missing instrumented memory access imports: ' + name)
    names = {line.split()[-1] for line in defined.splitlines() if len(line.split()) >= 3}
    if not {'kcsan_init', '__tsan_read8', '__tsan_write8'} <= names:
        raise ValueError('KCSAN runtime absent from vmlinux')


def audit_link(build, artifacts, source):
    archive = build / REL / 'built-in.a'
    members = subprocess.check_output(['ar', 't', str(archive)], text=True)
    (artifacts / 'goodix-archive-members.log').write_text(members)
    check_members(members.splitlines())
    elf = (build / 'vmlinux').read_bytes()
    if elf[:6] != b'\x7fELF\x02\x01' or int.from_bytes(elf[18:20], 'little') != 62:
        raise ValueError('vmlinux is not an ELF64 x86_64 kernel')
    header = subprocess.check_output(['readelf', '-h', str(build / 'vmlinux')], text=True)
    (artifacts / 'vmlinux-elf-header.log').write_text(header)
    symbols = subprocess.check_output(['nm', '--defined-only', str(build / 'vmlinux')], text=True)
    (artifacts / 'vmlinux-defined-symbols.log').write_text(symbols)
    check_image(symbols, source.read_text())
    if b'gx-offline-v07\0' not in elf:
        raise ValueError('Missing suite name in vmlinux')
    commands, imports = {}, {}
    for name in PARTS:
        commands[name] = (build / REL / ('.' + name + '.o.cmd')).read_text()
        imports[name] = subprocess.check_output(
            ['nm', '-u', str(build / REL / (name + '.o'))], text=True)
    (artifacts / 'instrumentation-imports.json').write_text(
        json.dumps(imports, indent=2) + '\n')
    check_instrumentation(commands, imports, symbols)
    (artifacts / 'instrumentation-audit.log').write_text(
        'PASS: six compile commands use -fsanitize=thread; gx_exchange and gx_kunit\n'
        'import memory-access hooks; KCSAN runtime and 25 cases are linked in vmlinux.\n')
    return archive


def runtime_evidence(raw):
    # Match complete kernel messages, not command lines or names in source.
    lines = [re.sub(r'^\s*\[\s*\d+(?:\.\d+)?\]\s*', '', line).strip()
             for line in raw.splitlines()]
    if 'kcsan: enabled early' not in lines or 'kcsan: strict mode configured' not in lines:
        raise ValueError('Missing runtime KCSAN early-enable/strict confirmation')
    cpus = [int(n) for n in re.findall(r'smp: Brought up \d+ node[s]?, (\d+) CPUs', raw)]
    if not cpus or max(cpus) < 2:
        raise ValueError('Missing confirmation of two online guest CPUs')
    if not re.search(r'Linux version 6\.12[^\s]*-gx-v07-kcsan\b', raw):
        raise ValueError('Missing expected guest kernel version')
    if any(DIAGNOSTIC.search(line) or re.search(r'\bBUG:\s*KCSAN:', line, re.I)
           or 'kcsan: non-strict' in line.lower() for line in lines):
        raise ValueError('Kernel diagnostic observed; inspect complete QEMU log')
    return {'kcsan_enabled_early': True, 'kcsan_strict_runtime': True,
            'online_cpus': max(cpus)}


def collect_runtime(artifacts):
    raw_file = artifacts / 'qemu.log'
    if not raw_file.exists():
        return
    raw = raw_file.read_text(errors='replace')
    (artifacts / 'kernel-runtime.log').write_text(raw)
    lines = raw.splitlines(keepends=True)
    diagnostics = [line for line in lines if re.search(r'kcsan|data.race|WARNING:|BUG:|panic|Oops:', line, re.I)]
    (artifacts / 'kcsan-diagnostics.log').write_text(''.join(diagnostics))
    first = next((i for i, line in enumerate(lines) if re.search(r'(?:KTAP|TAP) version 1', line)), None)
    if first is not None:
        (artifacts / 'runtime-stream.ktap').write_text(''.join(lines[first:]))


def collect_boot_commands(build, artifacts):
    paths = sorted((build / 'arch/x86/boot').rglob('.*.cmd'))
    (artifacts / 'boot-build-commands.log').write_text(''.join(
        f'FILE: {p.relative_to(build)}\n{p.read_text()}\n' for p in paths))
