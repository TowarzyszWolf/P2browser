#!/bin/sh
# Authenticate and download on the native host; configure packages in i386.
# This avoids PRoot's limitations around APT's download/signature subprocesses.
set -eu
export DEBIAN_FRONTEND=noninteractive
export PROOT_NO_SECCOMP=1 PROOT_FORCE_FOREIGN_BINARY=1
p2_conf=/tmp/p2-native-apt.conf
printf 'APT::Sandbox::User "root";\n' > "$p2_conf"
if [ -f /run/secrets/proxy_ca ]; then
    printf 'Acquire::https::CaInfo "/run/secrets/proxy_ca";\n' >> "$p2_conf"
fi
dpkg --add-architecture i386
apt-get -c "$p2_conf" update
mkdir -p /guest/var/cache/apt/archives/partial /guest/var/lib/apt/lists
apt-get -c "$p2_conf" -o APT::Architecture=i386 \
    -o Dir::State::status=/guest/var/lib/dpkg/status \
    -o Dir::Cache::archives=/guest/var/cache/apt/archives \
    --download-only --no-install-recommends -y install "$@"
for p2_deb in /guest/var/cache/apt/archives/*.deb; do
    p2_arch=$(dpkg-deb -f "$p2_deb" Architecture)
    case $p2_arch in i386|all) ;; *) printf 'Unexpected package architecture: %s\n' "$p2_arch" >&2; exit 1 ;; esac
done
cp -a /var/lib/apt/lists/. /guest/var/lib/apt/lists/
printf 'Native downloaded archives: '
find /guest/var/cache/apt/archives -name '*.deb' | wc -l
proot -q /usr/bin/qemu-i386-static -r /guest \
    -b /dev -b /proc -b /sys -b /tmp -b /etc/resolv.conf \
    -b /run/secrets -b /usr/bin/qemu-i386-static -w / \
    /usr/bin/apt-get -o APT::Sandbox::User=root -o APT::Sandbox::Seccomp=false \
    --print-uris --no-install-recommends -y install "$@"
proot -q /usr/bin/qemu-i386-static -r /guest \
    -b /dev -b /proc -b /sys -b /tmp -b /etc/resolv.conf \
    -b /run/secrets -b /usr/bin/qemu-i386-static -w / \
    /usr/bin/apt-get -o APT::Sandbox::User=root -o APT::Sandbox::Seccomp=false \
    --no-download --no-install-recommends -y install "$@"
rm -rf /guest/var/lib/apt/lists/* /guest/var/cache/apt/archives/*.deb \
    /var/lib/apt/lists/* "$p2_conf"
