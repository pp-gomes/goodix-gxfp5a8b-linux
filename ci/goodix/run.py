#!/usr/bin/env python3
"""CI-only staged config/build/UML execution. No sensor or host module operations."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from verify_ktap import verify
from build_evidence import collect, verify_link


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def requested_values(text):
    return dict(re.findall(r'^(CONFIG_\w+)=(.*)$', text, re.M))


def effective_values(text):
    values = requested_values(text)
    values.update({x: 'n' for x in re.findall(r'^# (CONFIG_\w+) is not set$', text, re.M)})
    return values


def config_mismatches(requested, effective):
    return {k: {'requested': v, 'effective': effective.get(k, 'absent')}
            for k, v in requested.items()
            if effective.get(k, 'n' if v == 'n' else 'absent') != v}


def command(argv, cwd, log, seconds, env=None):
    print('+ ' + ' '.join(map(str, argv)), flush=True)
    with log.open('w') as stream:
        stream.write('COMMAND: ' + ' '.join(map(str, argv)) + '\n')
        stream.flush()
        process = subprocess.Popen(list(map(str, argv)), cwd=cwd, stdout=stream,
                                   stderr=subprocess.STDOUT, env=env, start_new_session=True)
        try:
            status = process.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise RuntimeError(f'Wall timeout after {seconds}s: {log.name}')
    return status


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--profile', choices=['basic', 'lockdep', 'kasan', 'kcsan'], required=True)
    args = parser.parse_args()
    kernel, source, work, artifacts = [p.resolve() for p in
                                      (args.kernel, args.source, args.work, args.artifacts)]
    artifacts.mkdir(parents=True, exist_ok=True)
    status = {'profile': args.profile, 'status': 'not_run', 'phase': 'preflight',
              'kernel_executed_and_verified': False, 'started_utc': time.strftime('%FT%TZ', time.gmtime())}
    original = {}
    build = work / 'build'
    code = 1
    try:
        pin = json.loads((HERE / 'kernel.json').read_text())
        actual = subprocess.check_output(['git', '-C', str(kernel), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != pin['commit']:
            raise RuntimeError('Kernel commit does not match pin')
        dirty = subprocess.check_output(['git', '-C', str(kernel), 'status', '--porcelain'], text=True)
        if dirty:
            raise RuntimeError('Expected a clean, freshly fetched kernel checkout')
        provenance = {**pin, 'actual_commit': actual,
                      'kernel_makefile_sha256': digest(kernel / 'Makefile'),
                      'kunit_runner_sha256': digest(kernel / 'tools/testing/kunit/kunit.py'),
                      'ci_repository': os.environ.get('GITHUB_REPOSITORY'),
                      'ci_commit': os.environ.get('GITHUB_SHA'),
                      'ci_run_id': os.environ.get('GITHUB_RUN_ID'),
                      'ci_run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT'),
                      'transport': 'absent'}
        if not re.search(r'^VERSION = 6$', (kernel / 'Makefile').read_text(), re.M) or not re.search(
                r'^PATCHLEVEL = 12$', (kernel / 'Makefile').read_text(), re.M):
            raise RuntimeError('Expected Linux 6.12 Makefile')
        (artifacts / 'kernel-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        work.mkdir(parents=True, exist_ok=False)
        (work / 'uml-tmp').mkdir()
        build.mkdir()
        files = ['Kbuild', 'Kconfig', 'Makefile', 'gx_offline.h', 'gx_protocol_core.h',
                 'gx_protocol_core.c', 'gx_exchange.c', 'gx_rx_staging.c',
                 'gx_test_event_source.c', 'gx_transport_disabled.c', 'gx_kunit.c', 'tests/fixtures.h']
        original = {name: digest(source / name) for name in files}
        (artifacts / 'gx-source-sha256.json').write_text(json.dumps(original, indent=2) + '\n')
        target = kernel / 'drivers/misc/gx_offline'
        target.mkdir()
        for name in files:
            dest = target / name
            dest.parent.mkdir(exist_ok=True)
            shutil.copy2(source / name, dest)
            if digest(dest) != original[name]:
                raise RuntimeError('Integrated source differs: ' + name)
        with (kernel / 'drivers/misc/Kconfig').open('a') as f:
            f.write('\nsource "drivers/misc/gx_offline/Kconfig"\n')
        with (kernel / 'drivers/misc/Makefile').open('a') as f:
            f.write('\nobj-$(CONFIG_GX_OFFLINE_KUNIT_TEST) += gx_offline/\n')
        subprocess.run(['git', '-C', str(kernel), 'add', '-N', 'drivers/misc/gx_offline'], check=True)
        (artifacts / 'integration.patch').write_bytes(subprocess.check_output(['git', '-C', str(kernel), 'diff']))
        fragment = (HERE / 'profiles/basic.config').read_text()
        if args.profile != 'basic':
            fragment += (HERE / f'profiles/{args.profile}.config').read_text()
        config = artifacts / 'requested.config'
        config.write_text(fragment)
        runner = kernel / 'tools/testing/kunit/kunit.py'
        common = ['--arch=um', f'--build_dir={build}', f'--kunitconfig={config}']
        env = {**os.environ, 'TMPDIR': str(work / 'uml-tmp'), 'PYTHONUNBUFFERED': '1',
               'KBUILD_VERBOSE': '1'}
        status['phase'] = 'config'
        result = command([sys.executable, runner, 'config', *common], kernel,
                         artifacts / 'config.log', 600, env)
        if (build / '.config').exists():
            shutil.copy2(build / '.config', artifacts / 'kernel.config')
            mismatches = config_mismatches(requested_values(fragment), effective_values((build / '.config').read_text()))
            (artifacts / 'config-compatibility.json').write_text(json.dumps(mismatches, indent=2) + '\n')
            if mismatches:
                status['status'] = 'configuration_incompatible'
                raise RuntimeError('Requested options unavailable; see config-compatibility.json (no fallback)')
        if result or not (build / '.config').exists():
            raise RuntimeError('KUnit configuration failed')
        status['phase'] = 'build'
        result = command([sys.executable, runner, 'build', *common, '--jobs=2'], kernel,
                         artifacts / 'build.log', 2400, env)
        collect(build, artifacts)
        if result:
            raise RuntimeError('UML kernel build failed')
        # Build may regenerate configuration: check again before any execution.
        shutil.copy2(build / '.config', artifacts / 'kernel.config')
        if config_mismatches(requested_values(fragment), effective_values((build / '.config').read_text())):
            raise RuntimeError('Configuration changed during build')
        provenance['effective_config_sha256'] = digest(build / '.config')
        provenance['uml_image_sha256'] = digest(build / 'linux')
        (artifacts / 'kernel-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        status['phase'] = 'object_audit'
        # Linux 6.12 flattens built-in composite targets into their archive.
        # Audit the actual linked inputs, never manufacture a standalone .o.
        archive = verify_link(build, artifacts, source / 'gx_kunit.c')
        result = command([sys.executable, source / 'tools/audit-offline.py',
                          archive], kernel,
                         artifacts / 'object-audit.log', 60, env)
        if result:
            raise RuntimeError('Offline object symbol audit failed')
        status['phase'] = 'execute'
        status['execution_attempted'] = True
        result = command([sys.executable, runner, 'exec', *common, '--timeout=120',
                          '--raw_output=all', '--kernel_args=mem=1024M', 'gx-offline-v07'], kernel,
                         artifacts / 'exec.log', 300, env)
        if result:
            raise RuntimeError('UML execution failed (raw status is not test approval)')
        raw = artifacts / 'kernel-runtime.log'
        shutil.copy2(build / 'test.log' if (build / 'test.log').exists() else artifacts / 'exec.log', raw)
        status['phase'] = 'verify'
        parsed = command([sys.executable, runner, 'parse', raw], kernel,
                         artifacts / 'upstream-parser.log', 60, env)
        # Preserve complete raw log even if strict verification fails.
        verified, ktap = verify((source / 'gx_kunit.c').read_text(), raw.read_text(errors='replace'))
        if parsed:
            raise RuntimeError('Upstream KUnit parser rejected results')
        (artifacts / 'gx-offline-v07.ktap').write_text(ktap)
        (artifacts / 'verified-results.json').write_text(json.dumps(verified, indent=2) + '\n')
        status.update(status='passed', phase='complete', kernel_executed_and_verified=True)
        code = 0
    except Exception as exc:
        if status['status'] == 'not_run':
            status['status'] = 'failed'
        status['error'] = str(exc)
        print(str(exc), file=sys.stderr, flush=True)
    finally:
        if build.exists():
            try:
                collect(build, artifacts)
            except Exception as exc:
                (artifacts / 'collection-error.log').write_text(str(exc) + '\n')
        # In failure cases this is generated config, not necessarily a booted config.
        if (build / '.config').exists():
            shutil.copy2(build / '.config', artifacts / 'kernel.config')
        if (build / 'test.log').exists():
            shutil.copy2(build / 'test.log', artifacts / 'kernel-runtime.log')
        elif status.get('execution_attempted') and (artifacts / 'exec.log').exists():
            shutil.copy2(artifacts / 'exec.log', artifacts / 'kernel-runtime.log')
        runtime = artifacts / 'kernel-runtime.log'
        if runtime.exists():
            # Extract actual output even on failure/truncation, without implying
            # it is a complete or passing stream. The untouched log is primary.
            lines = runtime.read_text(errors='replace').splitlines(keepends=True)
            first = next((i for i, line in enumerate(lines)
                          if re.search(r'(?:KTAP|TAP) version 1', line)), None)
            if first is not None:
                status['kernel_ktap_observed'] = True
                (artifacts / 'runtime-stream.ktap').write_text(''.join(lines[first:]))
        if original:
            unchanged = all(digest(source / name) == sha for name, sha in original.items())
            status['original_source_unchanged'] = unchanged
            if not unchanged:
                status['status'], status['error'], code = 'failed', 'Original source changed', 1
        status['finished_utc'] = time.strftime('%FT%TZ', time.gmtime())
        (artifacts / 'status.json').write_text(json.dumps(status, indent=2) + '\n')
    return code


if __name__ == '__main__':
    sys.exit(main())
