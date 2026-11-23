#!/bin/bash
# Compile kernel objects only. Never build/load a physical driver or run insmod.
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
if [[ $# -lt 1 ]]; then
    echo "Usage: $0 /path/to/configured/kernel-headers [output-directory]" >&2
    exit 2
fi
kernel_dir=$(realpath -- "$1")
output_dir=$(realpath -m -- "${2:-$source_dir/build/$(basename "$kernel_dir")}")
linux_dir=$(dirname -- "$source_dir")
case "$output_dir/" in
    "$source_dir/build/"*) ;;
    "$linux_dir/"*)
        echo "Inside linux/, output must be under v0.7/build; prior versions are read-only" >&2
        exit 2
        ;;
esac
mkdir -p -- "$output_dir/tests"
cp -- "$source_dir"/*.c "$source_dir"/*.h "$source_dir/Kbuild" "$source_dir/Makefile" "$output_dir/"
cp -- "$source_dir/tests/fixtures.h" "$output_dir/tests/"
make -C "$kernel_dir" M="$output_dir" CONFIG_GX_OFFLINE_KUNIT_TEST=m W=1 gx_offline_test.o 2>&1 | tee "$output_dir/compile.log"
python3 "$source_dir/tools/audit-offline.py" "$output_dir/gx_offline_test.o" | tee "$output_dir/audit.log"
echo "Objects compiled, NOT executed. KUnit runtime requires a separate UML kernel."
