#!/bin/sh
# Run inside the amd64 full-system test image. /results must be writable.
set -eu
umask 077
LC_ALL=C
export LC_ALL
p2_output=${1:-/results/run}
p2_mode=${P2_GUEST_MODE:-comparison}
case $p2_mode in comparison) p2_timeout=1200 ;; stress) p2_timeout=1500 ;; *) printf 'Unknown guest mode.\n' >&2; exit 64 ;; esac
p2_seconds=${P2_STRESS_SECONDS:-45}
case $p2_seconds in ''|*[!0-9]*|????*) printf 'Invalid stress duration.\n' >&2; exit 64 ;; esac
[ "$p2_seconds" -ge 15 ] && [ "$p2_seconds" -le 180 ] || exit 64
p2_cases=${P2_STRESS_CASES:-all}
p2_stress_rows=$(python3 - "$p2_cases" <<'PY'
import sys
names = {'dom-no-js', 'images-off', 'images-on', 'js-off', 'js-on',
         'mixed-on', 'tabs-baseline', 'tabs-pressure'}
cases = sorted(names) if sys.argv[1] == 'all' else sys.argv[1].split(',')
if not cases or len(cases) != len(set(cases)) or any(c not in names for c in cases):
    raise SystemExit('Invalid stress case selection')
print(len(cases) + 1)
PY
)
case $p2_output in /*) ;; *) printf '%s\n' 'Use an absolute output path.' >&2; exit 64 ;; esac
if ! mkdir -- "$p2_output"; then
    printf '%s\n' 'A new output directory is required.' >&2
    exit 73
fi
if [ -r /artifacts/rootfs.ext4.gz ]; then
    gzip -t /artifacts/rootfs.ext4.gz
    gzip -dc /artifacts/rootfs.ext4.gz | dd of="$p2_output/rootfs.ext4" bs=1M conv=sparse status=none
    p2_expected_rootfs=$(cat /artifacts/ROOTFS-SHA256)
    p2_actual_rootfs=$(sha256sum "$p2_output/rootfs.ext4" | cut -d ' ' -f 1)
    [ "$p2_expected_rootfs" = "$p2_actual_rootfs" ] || { printf 'Root filesystem checksum mismatch.\n' >&2; exit 1; }
else
    cp --sparse=always /artifacts/rootfs.ext4 "$p2_output/rootfs.ext4"
fi
# Refresh harness scripts without rebuilding the large Debian base image.
p2_harness=${P2_HARNESS_DIR:-/opt/p2-harness}
p2_scripts='full-guest-init.sh guest-benchmark.sh'
[ "$p2_mode" != stress ] || p2_scripts="$p2_scripts stress-benchmark.sh"
for p2_script in $p2_scripts; do
    [ -r "$p2_harness/$p2_script" ] || { printf 'Missing harness script: %s\n' "$p2_script" >&2; exit 1; }
    case $p2_script in
        full-guest-init.sh) p2_target=/usr/local/sbin/p2-full-init ;;
        guest-benchmark.sh) p2_target=/usr/local/bin/p2-full-benchmark ;;
        stress-benchmark.sh) p2_target=/opt/p2browser/emulation/stress-benchmark.sh ;;
    esac
    debugfs -w -R "rm $p2_target" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    debugfs -w -R "write $p2_harness/$p2_script $p2_target" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    debugfs -w -R "set_inode_field $p2_target mode 0100755" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    debugfs -R "dump $p2_target $p2_output/$p2_script.verified" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    cmp "$p2_harness/$p2_script" "$p2_output/$p2_script.verified" || exit 1
done
if [ -n "${P2_TESTS_DIR:-}" ]; then
    p2_test_source=$P2_TESTS_DIR/test_probe.py
    p2_test_target=/opt/p2browser/tests/test_probe.py
    test -r "$p2_test_source"
    debugfs -w -R "rm $p2_test_target" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    debugfs -w -R "write $p2_test_source $p2_test_target" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    debugfs -w -R "set_inode_field $p2_test_target mode 0100644" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    debugfs -R "dump $p2_test_target $p2_output/test_probe.py.verified" "$p2_output/rootfs.ext4" >> "$p2_output/patch.log" 2>&1
    cmp "$p2_test_source" "$p2_output/test_probe.py.verified"
    sha256sum "$p2_test_source" > "$p2_output/test-source-sha256.txt"
fi
sha256sum "$p2_harness/full-guest-init.sh" "$p2_harness/guest-benchmark.sh" > "$p2_output/harness-sha256.txt"
printf 'Starting full-system Pentium II test; serial log: %s/serial.log\n' "$p2_output"
p2_status=0
timeout --foreground "$p2_timeout" qemu-system-i386 \
    -machine pc,accel=tcg -cpu pentium2 -smp 1 -m 384 \
    -nic none -nographic -monitor none -no-reboot \
    -drive "file=$p2_output/rootfs.ext4,format=raw,if=ide" \
    -kernel /artifacts/vmlinuz -initrd /artifacts/initrd.img \
    -append "root=/dev/sda rw console=ttyS0,115200 panic=-1 init=/usr/local/sbin/p2-full-init p2mode=$p2_mode p2seconds=$p2_seconds p2cases=$p2_cases" \
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
if [ "$p2_mode" = stress ]; then
    p2_summary=$p2_output/extracted/p2-results/stress/summary.tsv
    p2_expected_rows=$p2_stress_rows
else
    p2_summary=$p2_output/extracted/p2-results/summary.tsv
    p2_expected_rows=6
fi
if grep -q '^P2FULLFAIL:' "$p2_output/serial.log" || [ ! -s "$p2_summary" ]; then
    printf '%s\n' 'Guest setup or result extraction failed.' >&2
    exit 1
fi
if ! awk -v expected="$p2_expected_rows" 'END { exit NR == expected ? 0 : 1 }' "$p2_summary"; then
    printf '%s\n' 'The expected browser cases were not all recorded.' >&2
    exit 1
fi
printf 'Guest completed. Inspect %s for individual outcomes.\n' "$p2_summary"
