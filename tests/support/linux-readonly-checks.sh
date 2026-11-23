#!/usr/bin/env bash
# GXFP5A8B: cached Linux metadata only. No device opens, IRQ requests or writes.
# Output goes to stdout; the operator may redirect it to a regular local file.
set -u
set -o pipefail
export LC_ALL=C

usage() {
    cat <<'EOF'
Usage: bash linux-readonly-checks.sh [--device spi-GXFP5A8B:00] [--irq N] [--logs]
       bash linux-readonly-checks.sh --snapshot-root DIR [same options]

Reads allowlisted cached sysfs/proc metadata for the Goodix node and ancestors.
Does not read cpu_version/wakeup_cpu_version, debugfs registers, GPIO values,
ACPI status/resources methods, PCI config/resources, or any /dev device.
Never mounts, loads modules, binds drivers, changes PM or requests GPIO/IRQ.
spi1.0 is rejected, including symlinks resolving into it. No fallback scanning.
--irq N: only if independently identified; GPIO259 is NOT Linux IRQ259.
--logs: read existing dmesg (no clearing); optional and permissions may deny it.
--snapshot-root: inspect a saved/mock sys/ and proc/ tree; disables live logs.
No sudo is invoked. Missing metadata is reported, never repaired.
EOF
}

die() { printf 'ERROR: %s\n' "$*" >&2; exit 2; }
sensor_name=spi-GXFP5A8B:00
snapshot_root=
irq_number=
logs=0
while (($#)); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --device|--snapshot-root|--irq)
            (($# >= 2)) || die "missing value for $1"
            case "$1" in
                --device) sensor_name=$2 ;;
                --snapshot-root) snapshot_root=$2 ;;
                --irq) irq_number=$2 ;;
            esac
            shift 2 ;;
        --logs) logs=1; shift ;;
        *) die "unknown option: $1" ;;
    esac
done
[[ $sensor_name =~ ^spi-GXFP5A8B:[[:xdigit:]]{2}$ ]] ||
    die 'only a named spi-GXFP5A8B:XX node is allowed; spi1.0 is forbidden'
[[ -z $irq_number || $irq_number =~ ^[0-9]{1,8}$ ]] || die 'invalid IRQ number'
for utility in readlink cat awk date dirname; do
    command -v "$utility" >/dev/null || die "required utility missing: $utility"
done

if [[ -n $snapshot_root ]]; then
    snapshot_root=$(readlink -f -- "$snapshot_root") || die 'invalid snapshot root'
    [[ -d $snapshot_root && $snapshot_root != / ]] || die 'snapshot root must be a saved tree, not /'
    [[ $logs == 0 ]] || die '--logs is unavailable with --snapshot-root'
fi
sys_root=${snapshot_root}/sys
proc_root=${snapshot_root}/proc
devices_root=$sys_root/devices

# All sysfs content reads, including identity checks, go through this guard.
allowed_sys_path() {
    case "/$1/" in */spi1.0/*) return 1 ;; esac
    case "$1" in "$devices_root"/*|"$sys_root/kernel/irq/"*) return 0 ;; esac
    return 1
}

cached_value() {
    local resolved
    resolved=$(readlink -f -- "$1" 2>/dev/null) || return 1
    allowed_sys_path "$resolved" || return 1
    [[ -f $resolved && -r $resolved ]] || return 1
    cat -- "$resolved" 2>/dev/null
}

proc_path() {
    local resolved
    resolved=$(readlink -f -- "$1" 2>/dev/null) || return 1
    case "$resolved" in "$proc_root"/*) ;; *) return 1 ;; esac
    [[ -f $resolved && -r $resolved ]] || return 1
    printf '%s\n' "$resolved"
}

show_cached() {
    local value
    if value=$(cached_value "$1"); then
        printf '%s: %s\n' "$1" "$value"
    else
        printf '%s: unavailable or excluded\n' "$1"
    fi
}

show_link() {
    local dest
    if [[ -L $1 ]] && dest=$(readlink -f -- "$1" 2>/dev/null); then
        case "/$dest/" in */spi1.0/*) printf '%s: excluded\n' "$1"; return ;; esac
        printf '%s -> %s\n' "$1" "$dest"
    else
        printf '%s: absent/unbound\n' "$1"
    fi
}

show_node() {
    local node=$1 field
    allowed_sys_path "$node/placeholder" || return 0
    printf '\nNODE %s\n' "$node"
    show_link "$node/subsystem"
    show_link "$node/driver"
    show_link "$node/driver/module"
    show_link "$node/firmware_node"
    # Common cached device identification and PCI identity; no config/BAR reads.
    for field in modalias vendor device class revision subsystem_vendor subsystem_device; do
        [[ -e $node/$field || -L $node/$field ]] && show_cached "$node/$field"
    done
    for field in path hid uid; do
        [[ -e $node/firmware_node/$field || -L $node/firmware_node/$field ]] &&
            show_cached "$node/firmware_node/$field"
    done
    # Generic driver-core PM state/policy: reading never requests resume here.
    for field in runtime_status runtime_active_time runtime_suspended_time \
                 runtime_enabled control autosuspend_delay_ms wakeup; do
        show_cached "$node/power/$field"
    done
    # SPI counters reside in RAM; parent counters may include other clients.
    for field in messages transfers errors timedout bytes bytes_tx bytes_rx; do
        [[ -e $node/statistics/$field || -L $node/statistics/$field ]] &&
            show_cached "$node/statistics/$field"
    done
    return 0
}

target_link=$sys_root/bus/spi/devices/$sensor_name
target=$(readlink -f -- "$target_link" 2>/dev/null) || die 'Goodix node unavailable; no bus scan performed'
[[ -d $target ]] || die 'Goodix node unavailable; no bus scan performed'
allowed_sys_path "$target/placeholder" || die 'target outside allowed tree or resolves into spi1.0'
alias_value=$(cached_value "$target/modalias") || alias_value=
hid_value=$(cached_value "$target/firmware_node/hid") || hid_value=
case "$alias_value" in
    acpi:GXFP5A8B:*|spi:GXFP5A8B) ;;
    *) [[ $hid_value == GXFP5A8B ]] || die 'Goodix identity not confirmed; stopped before walking parents' ;;
esac

printf 'GXFP5A8B read-only metadata snapshot; not a hardware readiness test\n'
date -u '+Start UTC: %Y-%m-%dT%H:%M:%SZ'
printf 'Source: %s\n' "${snapshot_root:-live cached metadata}"
printf 'Named target: %s\nResolved target: %s\n' "$target_link" "$target"
if proc_file=$(proc_path "$proc_root/sys/kernel/osrelease"); then
    printf 'Kernel release: '; cat -- "$proc_file"
fi
if proc_file=$(proc_path "$proc_root/version"); then
    printf 'Kernel version: '; cat -- "$proc_file"
fi

# Only ancestors of the named Goodix node; never descend to siblings/flash.
node=$target
depth=0
while [[ $node == "$devices_root/"* && $depth -lt 16 ]]; do
    show_node "$node"
    node=$(dirname -- "$node")
    depth=$((depth + 1))
done

printf '\nExisting INT3455 ACPI/controller metadata (not pin register state)\n'
shopt -s nullglob
for acpi_link in "$sys_root"/bus/acpi/devices/INT3455:*; do
    acpi_node=$(readlink -f -- "$acpi_link" 2>/dev/null) || continue
    allowed_sys_path "$acpi_node/placeholder" || continue
    for field in path hid uid; do show_cached "$acpi_node/$field"; done
    show_link "$acpi_node/physical_node"
    if [[ -L $acpi_node/physical_node ]]; then
        gpio_parent=$(readlink -f -- "$acpi_node/physical_node" 2>/dev/null) || continue
        show_node "$gpio_parent"
    fi
done

printf '\nCached gpiochip labels/ranges only; no line requests or value reads\n'
for chip_link in "$sys_root"/bus/gpio/devices/gpiochip*; do
    chip=$(readlink -f -- "$chip_link" 2>/dev/null) || continue
    allowed_sys_path "$chip/placeholder" || continue
    for field in label base ngpio; do show_cached "$chip/$field"; done
done

printf '\nExisting IRQ lines (text filter; absence is not absence of IRQ mapping)\n'
if proc_file=$(proc_path "$proc_root/interrupts"); then
    awk 'NR == 1 || tolower($0) ~ /gxfp|goodix|pxa2xx|intel.lpss|int3455|pinctrl|gpio/' \
        "$proc_file"
else
    printf 'proc interrupts unavailable\n'
fi
if [[ -n $irq_number ]]; then
    printf '\nIRQ %s: number supplied by operator, not derived from GPIO259\n' "$irq_number"
    for field in chip_name hwirq type name actions; do
        show_cached "$sys_root/kernel/irq/$irq_number/$field"
    done
    for field in spurious smp_affinity_list effective_affinity_list; do
        irq_file=$proc_root/irq/$irq_number/$field
        if proc_file=$(proc_path "$irq_file"); then
            printf '%s: ' "$irq_file"; cat -- "$proc_file"
        fi
    done
fi

printf '\nAlready loaded relevant modules (no module loading)\n'
if proc_file=$(proc_path "$proc_root/modules"); then
    awk 'tolower($1) ~ /goodix|gxfp|pxa2xx|intel_lpss|pinctrl_ice|spi_pxa/' "$proc_file"
fi
if [[ $logs == 1 ]]; then
    printf '\nExisting kernel log only, without clearing or following\n'
    if command -v dmesg >/dev/null; then
        dmesg --color=never 2>/dev/null |
            awk 'tolower($0) ~ /goodix|gxfp|pxa2xx|intel.lpss|int3455/ && $0 !~ /spi1[.]0/' ||
            printf 'kernel log unavailable/permission denied\n'
    fi
fi
printf '\nNOT COLLECTED: GPIO latch/voltage/ownership registers, SPI registers or waveform.\n'
printf 'No readiness conclusion; no sensor operations requested.\n'
date -u '+End UTC: %Y-%m-%dT%H:%M:%SZ'
