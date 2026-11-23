#!/usr/bin/env python3
"""Require the exact source test names in an actual raw KUnit run.
Does not create KTAP or treat a successful raw-output runner exit as test success.
"""
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
raw = Path(sys.argv[2]).read_text(errors='replace')
expected = set(re.findall(r'KUNIT_CASE\((\w+)\)', source))
lines = [re.sub(r'^\s*\[[^\]]+\]\s?', '', line) for line in raw.splitlines()]
passed = set()
for line in lines:
    match = re.match(r'^\s*ok\s+\d+\s+(\w+)(?:\s|$)', line)
    if match and not re.search(r'#\s*SKIP', line, re.I):
        passed.add(match[1])
issues = []
if not re.search(r'(?:KTAP|TAP) version \d+', raw):
    issues.append('No raw KTAP/TAP header found')
if not expected or expected - passed:
    issues.append('Missing non-skipped passes: ' + ', '.join(sorted(expected - passed)))
for line in lines:
    if re.match(r'^\s*not ok\b', line) or re.search(
            r'WARNING:|BUG:|KASAN:|KCSAN:|UBSAN:|possible circular locking dependency|INFO: task .* blocked', line):
        issues.append(line)
if issues:
    raise SystemExit('NOT VALIDATED:\n' + '\n'.join(issues))
print(f'PASS: all {len(expected)} named cases present as non-skipped passes; no matched kernel diagnostics.')
print('Review full raw log and effective config; absence of diagnostics is not a leak/sanitizer proof.')
