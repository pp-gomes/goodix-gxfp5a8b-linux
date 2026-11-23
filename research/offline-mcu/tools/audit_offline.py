"""Check that the protocol object depends only on memory helpers, never I/O."""
import subprocess
import sys

result = subprocess.run(['nm', '-u', sys.argv[1]], check=True,
                        text=True, capture_output=True)
symbols = {line.split()[-1] for line in result.stdout.splitlines() if line.split()}
unexpected = symbols - {
    'memcpy', 'memset', 'memmove',
    '__stack_chk_fail', '__stack_chk_fail_local',  # Compiler stack protector.
}
if unexpected:
    raise SystemExit('Unexpected dependencies: ' + ', '.join(sorted(unexpected)))
print('PASS offline object dependencies:', ', '.join(sorted(symbols)) or '(none)')
