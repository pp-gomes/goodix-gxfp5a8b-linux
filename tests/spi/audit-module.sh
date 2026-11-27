#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Check the inert artifact built with Ubuntu 7.0.0-43-generic on x86-64.
set -eu

if [ "$#" -ne 1 ]; then
	echo "Usage: sh $0 path/to/goodix_gxfp5a8b.ko" >&2
	exit 2
fi

module=$1
symbols=$(nm --defined-only "$module")
imports=$(nm --undefined-only "$module")
sections=$(readelf -SW "$module")
relocations=$(readelf -rW "$module")
metadata=$(modinfo "$module")

if printf '%s\n' "$symbols" | grep -E '[[:space:]](init_module|cleanup_module|__mod_device_table__.*)$' >/dev/null ||
   printf '%s\n' "$sections" | grep -E '[[:space:]]\.(init|exit)\.(text|data)[[:space:]]' >/dev/null ||
   printf '%s\n' "$metadata" | grep -E '^(alias|softdep|weakdep|parm):' >/dev/null; then
	echo 'Unexpected module entry point, alias or parameter' >&2
	exit 1
fi

if [ -n "$(modinfo -F depends "$module")" ]; then
	echo 'Unexpected dependency on a loadable kernel module' >&2
	exit 1
fi

for symbol in gx_spi_driver gx_spi_acpi_ids gx_spi_linux_execute gx_spi_read_cpu; do
	printf '%s\n' "$symbols" | awk -v symbol="$symbol" '
		$NF == symbol { found = 1 }
		END { exit !found }
	'
done
for callback in gx_spi_linux_probe gx_spi_linux_remove; do
	printf '%s\n' "$relocations" | grep -E "[[:space:]]${callback}[[:space:]]+\+" >/dev/null
done

# Explicit imports for allocation, synchronization, timing, cached identity
# and compiler instrumentation. Review new imports against source before use.
printf '%s\n' "$imports" | awk '
	BEGIN {
		all = "__fentry__ __init_swait_queue_head __kmalloc_cache_noprof"
		all = all " __ref_stack_chk_guard __stack_chk_fail"
		all = all " __ubsan_handle_load_invalid_value __ubsan_handle_out_of_bounds"
		all = all " __x86_indirect_thunk_rax __x86_return_thunk"
		all = all " acpi_device_hid acpi_get_handle complete device_match_acpi_handle"
		all = all " fwnode_property_read_u32_array get_device is_acpi_device_node"
		all = all " kfree kmalloc_caches ktime_get mutex_init_generic mutex_lock mutex_unlock"
		all = all " pci_bus_type platform_bus_type put_device random_kmalloc_seed"
		all = all " refcount_warn_saturate software_node_fwnode strcmp wait_for_completion"
		count = split(all, names, " ")
		for (i = 1; i <= count; i++)
			allowed[names[i]] = 1
	}
	NF && !($NF in allowed) {
		print "Unexpected kernel import: " $NF > "/dev/stderr"
		failed = 1
	}
	END { exit failed }
'

echo 'Inert SPI module audit passed'
