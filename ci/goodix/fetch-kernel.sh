#!/bin/bash
# Execute on the hosted CI runner, never on the notebook.
set -euo pipefail
if [[ $# != 1 ]]; then
    echo "Usage: $0 new-kernel-directory" >&2
    exit 2
fi
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
readarray -t pin < <(python3 - "$script_dir/kernel.json" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
print(p['repository'])
print(p['commit'])
PY
)
mkdir -- "$1"
git -C "$1" init --quiet
git -C "$1" remote add origin "${pin[0]}"
git -C "$1" -c protocol.version=2 fetch --no-tags --depth=1 origin "${pin[1]}"
git -C "$1" checkout --quiet --detach FETCH_HEAD
actual=$(git -C "$1" rev-parse HEAD)
[[ "$actual" == "${pin[1]}" ]]
git -C "$1" fsck --no-reflogs
git -C "$1" show -s --format=fuller HEAD
printf '\nExpected/actual commit: %s\n' "$actual"
