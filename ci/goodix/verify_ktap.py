#!/usr/bin/env python3
"""Strict gx suite verification. Unit-test inputs are synthetic, not runtime evidence."""
from pathlib import Path
import json
import re
import sys

DIAGNOSTIC = re.compile(
    r'WARNING:|BUG:|KASAN:|KCSAN:|UBSAN:|Kernel panic|Oops:|'
    r'possible circular locking dependency|possible recursive locking|'
    r'INFO: task .* blocked|inconsistent lock state|'
    r'held lock freed|ODEBUG:|BUG kmalloc|unreferenced object')


def verify(source, raw):
    expected = re.findall(r'KUNIT_CASE\((\w+)\)', source)
    if len(expected) != 25 or len(set(expected)) != 25:
        raise ValueError('Expected exactly 25 unique source cases; review a suite change explicitly')
    lines = [re.sub(r'^\s*\[\s*\d+(?:\.\d+)?\]\s?', '', s).rstrip()
             for s in raw.splitlines()]
    for line in lines:
        if DIAGNOSTIC.search(line) or re.match(r'^\s*(?:not ok\b|Bail out!)', line):
            raise ValueError('Failure/diagnostic: ' + line)
    roots = [i for i, s in enumerate(lines) if re.fullmatch(r'(?:KTAP|TAP) version 1', s)]
    if len(roots) != 1:
        raise ValueError('Require one complete root KTAP/TAP stream')
    start = roots[0]
    if sum(bool(re.fullmatch(r'1\.\.1', s)) for s in lines[start + 1:]) != 1:
        raise ValueError('Require root plan 1..1')
    suite = [i for i, s in enumerate(lines) if s.strip() == '# Subtest: gx-offline-v07']
    if len(suite) != 1 or suite[0] <= start:
        raise ValueError('Require exactly one gx-offline-v07 subtest')
    i = suite[0]
    indent = len(lines[i]) - len(lines[i].lstrip())
    if indent == 0:
        raise ValueError('Suite must be nested under root')
    endings = [j for j in range(i + 1, len(lines))
               if re.fullmatch(r'ok 1 (?:- )?gx-offline-v07', lines[j])]
    if len(endings) != 1:
        raise ValueError('Missing exact successful root suite result')
    end = endings[0]
    suite_lines = lines[i + 1:end]
    plans = [j for j, s in enumerate(suite_lines)
             if re.fullmatch(r'\s*1\.\.25', s)]
    if len(plans) != 1:
        raise ValueError('Missing exact suite plan 1..25')
    seen = []
    for j, line in enumerate(suite_lines):
        m = re.fullmatch(r'\s*ok (\d+) (?:- )?(\w+)', line)
        if m:
            if j <= plans[0] or len(line) - len(line.lstrip()) != indent:
                raise ValueError('Case outside suite plan/indent')
            seen.append((int(m[1]), m[2]))
        elif re.match(r'^\s*(?:ok|not ok)\b', line):
            raise ValueError('Non-exact case result (skip, directive or extra result): ' + line)
    if seen != list(enumerate(expected, 1)):
        raise ValueError('Missing, duplicated, reordered or unexpected case result')
    root_plan = next(j for j in range(start + 1, len(lines)) if lines[j] == '1..1')
    if not start < root_plan < i < end:
        raise ValueError('Invalid root plan/suite order')
    for j, line in enumerate(lines):
        if re.match(r'^\s*(?:ok|not ok)\b', line) and not i < j <= end:
            raise ValueError('Result outside selected suite')
    # Preserve the original unmodified runtime log separately. This is extracted KTAP.
    ktap = '\n'.join(lines[start:end + 1]) + '\n'
    return {'suite': 'gx-offline-v07', 'passed': len(seen), 'skipped': 0,
            'failed': 0, 'cases': [name for _, name in seen]}, ktap


def main():
    source, log, output = map(Path, sys.argv[1:])
    result, ktap = verify(source.read_text(), log.read_text(errors='replace'))
    output.mkdir(exist_ok=True, parents=True)
    (output / 'gx-offline-v07.ktap').write_text(ktap)
    (output / 'verified-results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
