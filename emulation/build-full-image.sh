#!/bin/sh
# Build disk artifacts without mounting a filesystem or running an i386 guest.
set -eu
LC_ALL=C
export LC_ALL
if [ "$#" -ne 2 ]; then
    printf '%s\n' 'Usage: build-full-image.sh GUEST_ROOT NEW_ARTIFACT_DIRECTORY' >&2
    exit 64
fi
p2_root=$1
p2_out=$2
test -d "$p2_root/etc"
mkdir "$p2_out"
p2_kernel=$(find "$p2_root/boot" -maxdepth 1 -name 'vmlinuz-*-686' -type f)
p2_initrd=$(find "$p2_root/boot" -maxdepth 1 -name 'initrd.img-*-686' -type f)
test "$(printf '%s\n' "$p2_kernel" | wc -l)" -eq 1
test "$(printf '%s\n' "$p2_initrd" | wc -l)" -eq 1
test -n "$p2_kernel"
test -n "$p2_initrd"
cp "$p2_kernel" "$p2_out/vmlinuz"
cp "$p2_initrd" "$p2_out/initrd.img"
cp "$p2_root/opt/p2browser/compile-packages.tsv" "$p2_out/compile-packages.tsv"
cp "$p2_root/opt/p2browser/runtime-packages.tsv" "$p2_out/runtime-packages.tsv"

# A non-sparse source file is needed: swapon rejects holes inside a swapfile.
# The source extents are populated into ext4 by mke2fs -d. No host swapon.
dd if=/dev/zero of="$p2_root/swapfile" bs=1M count=512 status=none
chmod 600 "$p2_root/swapfile"
mkswap "$p2_root/swapfile"
printf '/swapfile none swap sw 0 0\n' > "$p2_root/etc/fstab"
truncate -s 3G "$p2_out/rootfs.ext4"
mkfs.ext4 -F -L p2-experiment -d "$p2_root" "$p2_out/rootfs.ext4"
printf '%s\n' \
    'guest_architecture=i386' \
    'cpu_model=pentium2' \
    'nominal_ram_mib=384' \
    'swapfile_mib=512' \
    'rootfs_size_gib=3' \
    'root_device=/dev/sda' \
    'guest_init=/usr/local/sbin/p2-full-init' \
    > "$p2_out/profile.txt"
sha256sum "$p2_out/vmlinuz" "$p2_out/initrd.img" > "$p2_out/BOOT-SHA256SUMS"
