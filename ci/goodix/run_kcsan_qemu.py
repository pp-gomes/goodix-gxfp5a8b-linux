#!/usr/bin/env python3
"""Independent hosted x86_64/QEMU KCSAN runner. No physical devices."""
from pathlib import Path
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from verify_ktap import verify
from build_evidence import collect
from kcsan_qemu_checks import audit_link, runtime_evidence, collect_runtime, collect_boot_commands
from run import digest, requested_values, effective_values, config_mismatches, command


def classify_failure(phase):
    return {
        'preflight': 'environment_or_provenance',
        'config': 'configuration',
        'build': 'build',
        'image_config_audit': 'image_configuration',
        'object_audit': 'link_or_instrumentation_audit',
        'execute': 'qemu_execution_or_timeout',
        'runtime_instrumentation': 'runtime_evidence_or_kernel_diagnostic',
        'verify': 'test_output_or_kernel_diagnostic',
    }.get(phase, 'validation')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    kernel, source, work, artifacts = [p.resolve() for p in
                                      (args.kernel, args.source, args.work, args.artifacts)]
    artifacts.mkdir(parents=True, exist_ok=True)
    status = {'profile': 'kcsan-qemu-x86_64', 'status': 'not_run', 'phase': 'preflight',
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
                      'transport': 'absent', 'arch': 'x86_64', 'qemu_accel': 'tcg', 'guest_cpus': 2}
        if not re.search(r'^VERSION = 6$', (kernel / 'Makefile').read_text(), re.M) or not re.search(
                r'^PATCHLEVEL = 12$', (kernel / 'Makefile').read_text(), re.M):
            raise RuntimeError('Expected Linux 6.12 Makefile')
        (artifacts / 'kernel-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        work.mkdir(parents=True, exist_ok=False)
        if not (kernel / 'arch/x86/Kconfig').is_file():
            raise RuntimeError('Linux x86 architecture support unavailable')
        if not shutil.which('qemu-system-x86_64'):
            raise RuntimeError('qemu-system-x86_64 unavailable')
        (work / 'qemu-tmp').mkdir()
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
        fragment = (HERE / 'profiles/kcsan-x86_64.config').read_text()
        config = artifacts / 'requested.config'
        config.write_text(fragment)
        runner = kernel / 'tools/testing/kunit/kunit.py'
        common = ['--arch=x86_64', f'--build_dir={build}', f'--kunitconfig={config}']
        env = {**os.environ, 'TMPDIR': str(work / 'qemu-tmp'), 'PYTHONUNBUFFERED': '1',
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
        result = command(['make', 'ARCH=x86_64', f'O={build}', '-j2', 'V=1',
                          'bzImage', 'compile_commands.json'], kernel,
                         artifacts / 'build.log', 3600, env)
        collect(build, artifacts)
        if result:
            raise RuntimeError('Instrumented x86_64 kernel build failed')
        # Build may regenerate configuration: check again before any execution.
        shutil.copy2(build / '.config', artifacts / 'kernel.config')
        if config_mismatches(requested_values(fragment), effective_values((build / '.config').read_text())):
            raise RuntimeError('Configuration changed during build')
        provenance['effective_config_sha256'] = digest(build / '.config')
        image = build / 'arch/x86/boot/bzImage'
        provenance['boot_image_sha256'] = digest(image)
        provenance['vmlinux_sha256'] = digest(build / 'vmlinux')
        (artifacts / 'kernel-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        status['phase'] = 'image_config_audit'
        extraction = subprocess.run(['bash', str(kernel / 'scripts/extract-ikconfig'), str(image)],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        (artifacts / 'image-config-extraction.log').write_bytes(extraction.stderr)
        (artifacts / 'boot-image.config').write_bytes(extraction.stdout)
        if extraction.returncode:
            raise RuntimeError('Cannot extract boot image configuration; see image-config-extraction.log')
        if extraction.stdout != (build / '.config').read_bytes():
            raise RuntimeError('Boot image embedded configuration differs from effective .config')
        status['phase'] = 'object_audit'
        archive = audit_link(build, artifacts, source / 'gx_kunit.c')
        result = command([sys.executable, source / 'tools/audit-offline.py',
                          archive], kernel,
                         artifacts / 'object-audit.log', 60, env)
        if result:
            raise RuntimeError('Offline object symbol audit failed')
        status['phase'] = 'execute'
        status['execution_attempted'] = True
        qemu = ['qemu-system-x86_64', '-machine', 'q35', '-accel', 'tcg,thread=multi',
                '-cpu', 'max', '-smp', '2', '-m', '2048', '-nodefaults',
                '-display', 'none', '-monitor', 'none', '-serial', 'stdio',
                '-no-reboot', '-kernel', str(image), '-append',
                'console=ttyS0 kunit.enable=1 kunit.filter_glob=gx-offline-v07 '
                'kunit_shutdown=reboot kcsan.early_enable=1 loglevel=7']
        (artifacts / 'qemu-command.json').write_text(json.dumps(qemu, indent=2) + '\n')
        result = command(qemu, kernel, artifacts / 'qemu.log', 600, env)
        collect_runtime(artifacts)
        if result:
            raise RuntimeError('QEMU exited unsuccessfully; inspect qemu.log')
        raw = artifacts / 'kernel-runtime.log'
        status['phase'] = 'runtime_instrumentation'
        runtime = runtime_evidence(raw.read_text(errors='replace'))
        (artifacts / 'runtime-instrumentation.json').write_text(json.dumps(runtime, indent=2) + '\n')
        if digest(image) != provenance['boot_image_sha256']:
            raise RuntimeError('Boot image changed during execution')
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
        status['failure_kind'] = classify_failure(status['phase'])
        status['error'] = str(exc)
        print(str(exc), file=sys.stderr, flush=True)
    finally:
        if build.exists():
            try:
                collect(build, artifacts)
            except Exception as exc:
                (artifacts / 'collection-error.log').write_text(str(exc) + '\n')
        try:
            collect_boot_commands(build, artifacts)
            collect_runtime(artifacts)
        except Exception as exc:
            (artifacts / 'runtime-collection-error.log').write_text(str(exc) + '\n')
        if (build / '.config').exists():
            shutil.copy2(build / '.config', artifacts / 'kernel.config')
        if original:
            unchanged = all(digest(source / name) == sha for name, sha in original.items())
            status['original_source_unchanged'] = unchanged
            if not unchanged:
                status['status'], status['error'], code = 'failed', 'Original source changed', 1
                status['kernel_executed_and_verified'] = False
        status['finished_utc'] = time.strftime('%FT%TZ', time.gmtime())
        (artifacts / 'status.json').write_text(json.dumps(status, indent=2) + '\n')
    return code


if __name__ == '__main__':
    sys.exit(main())
