#!/bin/sh
# Run with no network, disks, KVM, display, or host CPU passthrough.
set -eu
LC_ALL=C
export LC_ALL
p2_log=$(mktemp /tmp/p2-boot-check.XXXXXX)
trap 'rm -f -- "$p2_log"' EXIT HUP INT TERM
p2_status=0
timeout --foreground 120 qemu-system-i386 \
    -machine pc,accel=tcg -cpu pentium2 -smp 1 -m 384 \
    -nic none -nographic -monitor none -no-reboot \
    -kernel /opt/p2/vmlinuz -initrd /opt/p2/initramfs.cpio.gz \
    -append 'console=ttyS0,115200 panic=-1 rdinit=/init' \
    > "$p2_log" 2>&1 || p2_status=$?
cat "$p2_log"
if [ "$p2_status" -ne 0 ]; then
    printf 'QEMU failed or timed out (exit %s).\n' "$p2_status" >&2
    exit 1
fi
if ! tr -d '\r' < "$p2_log" | grep -qx P2BOOTPASS; then
    printf '%s\n' 'Guest did not report P2BOOTPASS.' >&2
    exit 1
fi
