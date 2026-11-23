#!/bin/bash
# Isolated UML only; no downloads, host module loading or physical devices.
set -euo pipefail
if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: $0 /full/linux-source /new/workspace [config-fragment]" >&2
    exit 2
fi
source_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
kernel_dir=$(realpath -- "$1")
workspace=$(realpath -m -- "$2")
fragment=$(realpath -- "${3:-$source_dir/.kunitconfig}")
if [[ ! -f "$kernel_dir/tools/testing/kunit/kunit.py" || ! -f "$kernel_dir/arch/um/Makefile" ]]; then
    echo "Unavailable: full Linux source with KUnit runner and arch/um required" >&2
    exit 3
fi
for tool in gcc make ld flex bison bc python3 perl; do
    if ! command -v "$tool" >/dev/null; then
        echo "Unavailable build dependency: $tool" >&2
        exit 3
    fi
done
case "$workspace/" in
    "$kernel_dir/"*|"$source_dir/"*)
        echo "Workspace must be outside input trees" >&2
        exit 2
        ;;
esac
mkdir -- "$workspace"
mkdir -- "$workspace/linux"
cp -- "$fragment" "$workspace/requested.kunitconfig"
cp -a --reflink=auto -- "$kernel_dir/." "$workspace/linux/"
mkdir -- "$workspace/linux/drivers/misc/gx_offline"
cp -- "$source_dir"/*.c "$source_dir"/*.h "$source_dir/Kbuild" "$source_dir/Kconfig" "$source_dir/Makefile" "$workspace/linux/drivers/misc/gx_offline/"
cp -a -- "$source_dir/tests" "$workspace/linux/drivers/misc/gx_offline/"
printf '\nsource "drivers/misc/gx_offline/Kconfig"\n' >> "$workspace/linux/drivers/misc/Kconfig"
printf '\nobj-$(CONFIG_GX_OFFLINE_KUNIT_TEST) += gx_offline/\n' >> "$workspace/linux/drivers/misc/Makefile"
# Preserve generated configuration even if a subsequent build/run fails.
preserve() {
    status=$?
    trap - EXIT
    if [[ -f "$workspace/build/.config" ]]; then
        cp -- "$workspace/build/.config" "$workspace/generated-kernel.config"
    fi
    if [[ -f "$workspace/build/test.log" ]]; then
        cp -- "$workspace/build/test.log" "$workspace/kernel-runtime.log"
    fi
    printf '%s\n' "$status" > "$workspace/runner-exit-code.txt"
    exit "$status"
}
trap preserve EXIT
cd -- "$workspace/linux"
python3 tools/testing/kunit/kunit.py run \
    --arch=um --kunitconfig="$workspace/requested.kunitconfig" \
    --build_dir="$workspace/build" --timeout=120 --raw_output=all \
    'gx-offline-v07' 2>&1 | tee "$workspace/kunit-raw.log"
python3 tools/testing/kunit/kunit.py parse "$workspace/kunit-raw.log" \
    2>&1 | tee "$workspace/kunit-parsed.log"
python3 "$source_dir/tools/verify-kunit-log.py" \
    "$source_dir/gx_kunit.c" "$workspace/kunit-raw.log" \
    | tee "$workspace/gx-test-verification.log"
