#!/bin/sh
# Run inside the amd64 full-system test image. /results must be writable.
set -eu
umask 077
LC_ALL=C
export LC_ALL
p2_output=${1:-/results/run}
case $p2_output in /*) ;; *) printf '%s\n' 'Use an absolute output path.' >&2; exit 64 ;; esac
if ! mkdir -- "$p2_output"; then
    printf '%s\n' 'A new output directory is required.' >&2
    exit 73
fi
cp --sparse=always /artifacts/rootfs.ext4 "$p2_output/rootfs.ext4"
# Refresh harness scripts without rebuilding the large Debian base image.
p2_harness=${P2_HARNESS_DIR:-/opt/p2-harness}
for p2_script in full-guest-init.sh guest-benchmark.sh; do
    [ -r "$p2_harness/$p2_script" ] || { printf 'Missing harness script: %s\n' "$p2_script" >&2; exit 1; }
    case $p2_script in
        full-guest-init.sh) p2_target=/usr/local/sbin/p2-full-init ;;
        guest-benchmark.sh) p2_target=/usr/local/bin/p2-full-benchmark ;;
    esac
    debugfs -w -R "rm $p2_target" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    debugfs -w -R "write $p2_harness/$p2_script $p2_target" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    debugfs -w -R "set_inode_field $p2_target mode 0100755" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    debugfs -R "dump $p2_target $p2_output/$p2_script.verified" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    cmp "$p2_harness/$p2_script" "$p2_output/$p2_script.verified" || exit 1
done
sha256sum "$p2_harness/full-guest-init.sh" "$p2_harness/guest-benchmark.sh" > "$p2_output/harness-sha256.txt"
printf 'Starting full-system Pentium II test; serial log: %s/serial.log\n' "$p2_output"
p2_status=0
timeout --foreground 720 qemu-system-i386 \
    -machine pc,accel=tcg -cpu pentium2 -smp 1 -m 384 \
    -nic none -nographic -monitor none -no-reboot \
    -drive "file=$p2_output/rootfs.ext4,format=raw,if=ide" \
    -kernel /artifacts/vmlinuz -initrd /artifacts/initrd.img \
    -append 'root=/dev/sda rw console=ttyS0,115200 panic=-1 init=/usr/local/sbin/p2-full-init' \
    > "$p2_output/serial.log" 2>&1 || p2_status=$?
printf '%s\n' "$p2_status" > "$p2_output/qemu-exit.txt"
# Keep the disk when shutdown is incomplete, for offline diagnosis.
mkdir "$p2_output/extracted"
debugfs -R "rdump /var/lib/p2-results $p2_output/extracted" \
    "$p2_output/rootfs.ext4" > "$p2_output/extraction.log" 2>&1 || :
cat "$p2_output/serial.log"
if [ "$p2_status" -ne 0 ]; then
    printf 'QEMU failed or timed out (exit %s); logs and disk retained.\n' "$p2_status" >&2
    exit 1
fi
if ! tr -d '\r' < "$p2_output/serial.log" | grep -qx P2FULLDONE; then
    printf '%s\n' 'Guest did not complete the full test; inspect serial.log.' >&2
    exit 1
fi
if grep -q '^P2FULLFAIL:' "$p2_output/serial.log" || \
   [ ! -s "$p2_output/extracted/p2-results/summary.tsv" ]; then
    printf '%s\n' 'Guest setup or result extraction failed.' >&2
    exit 1
fi
if ! awk 'END { exit NR == 6 ? 0 : 1 }' "$p2_output/extracted/p2-results/summary.tsv"; then
    printf '%s\n' 'The expected five browser cases were not all recorded.' >&2
    exit 1
fi
printf '%s\n' 'Guest completed. Inspect extracted/p2-results/summary.tsv for individual outcomes.'
