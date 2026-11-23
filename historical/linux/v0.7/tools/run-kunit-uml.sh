#!/bin/bash
# Offline UML only. Copies a supplied complete kernel tree; no downloads.
set -euo pipefail
if [[ $# != 2 ]]; then
    echo "Usage: $0 /path/to/full/linux-source /new/analysis-workspace" >&2
    exit 2
fi
source_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
kernel_dir=$(realpath -- "$1")
workspace=$(realpath -m -- "$2")
if [[ ! -f "$kernel_dir/tools/testing/kunit/kunit.py" ]]; then
    echo "Unavailable: complete kernel source with tools/testing/kunit/kunit.py required" >&2
    exit 3
fi
case "$workspace/" in
    "$kernel_dir/"*|"$source_dir/"*)
        echo "Workspace must be outside the input trees" >&2
        exit 2
        ;;
esac
# Must not already exist: do not overwrite an existing kernel or prior version.
mkdir -- "$workspace"
mkdir -- "$workspace/linux"
cp -a --reflink=auto -- "$kernel_dir/." "$workspace/linux/"
mkdir -- "$workspace/linux/drivers/misc/gx_offline"
cp -- "$source_dir"/*.c "$source_dir"/*.h "$source_dir/Kbuild" "$source_dir/Kconfig" "$source_dir/Makefile" "$workspace/linux/drivers/misc/gx_offline/"
cp -a -- "$source_dir/tests" "$workspace/linux/drivers/misc/gx_offline/"
printf '\nsource "drivers/misc/gx_offline/Kconfig"\n' >> "$workspace/linux/drivers/misc/Kconfig"
printf '\nobj-$(CONFIG_GX_OFFLINE_KUNIT_TEST) += gx_offline/\n' >> "$workspace/linux/drivers/misc/Makefile"
cd -- "$workspace/linux"
python3 tools/testing/kunit/kunit.py run \
    --arch=um --kunitconfig="$source_dir/.kunitconfig" \
    --build_dir="$workspace/build" --timeout=120 'gx-offline-v07' \
    2>&1 | tee "$workspace/kunit.log"
