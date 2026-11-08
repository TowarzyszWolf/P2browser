#!/bin/sh
# PID 1 for the dedicated, offline Bookworm i386 test guest.
set -u
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
umask 077
p2_results=/var/lib/p2-results

finish() {
    p2_finish_status=$1
    dmesg > "$p2_results/kernel-final.log" 2>&1 || :
    free -k > "$p2_results/memory-final.txt" 2>&1 || :
    printf '\nP2 full-guest suite exit: %s\n' "$p2_finish_status"
    if [ -f "$p2_results/summary.tsv" ]; then
        printf 'P2SUMMARYBEGIN\n'
        cat "$p2_results/summary.tsv"
        printf 'P2SUMMARYEND\n'
    fi
    printf 'P2FULLDONE\n'
    sync
    /bin/busybox poweroff -f
    while :; do sleep 60; done
}
fail() {
    printf 'P2FULLFAIL: %s\n' "$1"
    finish 1
}

mkdir -p /proc /sys /dev /run /tmp /var/lib
mountpoint -q /proc || mount -t proc proc /proc || exit 1
mountpoint -q /sys || mount -t sysfs sysfs /sys || exit 1
mountpoint -q /dev || mount -t devtmpfs devtmpfs /dev || exit 1
mountpoint -q /run || mount -t tmpfs -o mode=755,size=32m tmpfs /run || exit 1
mkdir -p /dev/pts /dev/shm /run/dbus /run/user/1000
chmod 755 /run/dbus /run/user
mountpoint -q /dev/pts || mount -t devpts devpts /dev/pts || exit 1
mountpoint -q /dev/shm || mount -t tmpfs -o mode=1777,size=64m tmpfs /dev/shm || exit 1
chmod 1777 /tmp
mkdir "$p2_results" || exit 1
chown p2test:p2test "$p2_results" /run/user/1000
chmod 700 "$p2_results" /run/user/1000
trap 'fail interrupted' INT TERM

printf '\nP2 full guest: i386 userspace, Xvfb, local fixture only.\n'
printf 'TCG timings are not Pentium II 400 MHz timings.\n'
awk -F: '
    /^processor[[:space:]]*:/ { count++ }
    /^cpu family[[:space:]]*:/ { family=$2+0 }
    /^model[[:space:]]*:/ { model=$2+0 }
    /^flags[[:space:]]*:/ {
        seen++; n=split($2, f, /[[:space:]]+/)
        for (i=1; i<=n; i++) if (f[i]=="sse" || f[i]=="sse2" || f[i]=="lm") bad++
    }
    END { if (count!=1 || family!=6 || model!=5 || seen!=1 || bad) exit 1 }
' /proc/cpuinfo || fail 'unexpected CPU profile'
awk '/^MemTotal:/ { total=$2 } END { if (total<350000 || total>393216) exit 1 }' \
    /proc/meminfo || fail 'unexpected RAM profile'
sh /opt/p2browser/tools/collect-report.sh "$p2_results/system" || fail 'inventory failed'
cat "$p2_results/system/report.txt"
dpkg-query -W -f='${Package}\t${Version}\t${Architecture}\n' \
    libc6 libgtk-3-0 libwebkit2gtk-4.1-0 libjavascriptcoregtk-4.1-0 \
    dillo xvfb dbus-x11 bubblewrap > "$p2_results/runtime-packages.tsv" 2>&1 || :
cat "$p2_results/runtime-packages.tsv"

# ext4 imported with mkfs -d may contain holes in the prepared swap file.
# Allocate extents in the guest and confirm activation rather than assuming it.
printf 'Preparing 512 MiB swap inside the disposable guest.\n'
p2_swap_ok=0
if fallocate -l 512M /swapfile && chmod 600 /swapfile && \
   mkswap /swapfile && swapon /swapfile; then
    p2_swap_ok=1
else
    printf 'Swap extent allocation failed; writing a non-sparse file.\n'
    swapoff /swapfile 2>/dev/null || :
    rm -f /swapfile
    if dd if=/dev/zero of=/swapfile bs=1M count=512 && chmod 600 /swapfile && \
       mkswap /swapfile && swapon /swapfile; then
        p2_swap_ok=1
    fi
fi
[ "$p2_swap_ok" -eq 1 ] || fail 'cannot activate 512 MiB swap'
awk 'NR>1 { total+=$3 } END { if(total<524000 || total>524288) exit 1 }' \
    /proc/swaps || fail 'unexpected active swap size'
cat /proc/swaps
/bin/busybox ifconfig lo 127.0.0.1 netmask 255.0.0.0 up || fail 'cannot enable loopback IPC'
printf 'Loopback enabled for local DPI/D-Bus communication; no network card is present.\n'
free -k > "$p2_results/memory-before.txt"
dmesg > "$p2_results/kernel-before.log"

dbus-uuidgen --ensure=/etc/machine-id || fail 'cannot prepare machine-id'
if [ -r /usr/share/dbus-1/system.conf ]; then
    dbus-daemon --system --fork || fail 'cannot start system D-Bus'
else
    printf 'No system bus configuration; using private session D-Bus buses.\n'
fi
printf 'Starting browser suite as p2test; sandbox remains enabled.\n'
p2_suite_status=0
su -s /bin/sh p2test -c /usr/local/bin/p2-full-benchmark || p2_suite_status=$?
finish "$p2_suite_status"
