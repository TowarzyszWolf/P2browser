#!/bin/sh
# Build an authenticated target sysroot without executing i386 package tools.
set -eu
apt-get update
apt-get install -y --no-install-recommends \
    libglib2.0-bin:amd64 libglib2.0-dev-bin:amd64 libgdk-pixbuf2.0-bin:amd64 \
    gperf bubblewrap xdg-dbus-proxy
mkdir -p /tmp/p2-gtk-debs/partial /tmp/p2-gtk-root /opt/p2-gtk-sysroot/usr
: > /tmp/p2-empty-status
apt-get -o APT::Architecture=i386 -o Dir::State::status=/tmp/p2-empty-status \
    -o Dir::Cache::archives=/tmp/p2-gtk-debs \
    --download-only --no-install-recommends -y install \
    libgtk-3-dev libsoup-3.0-dev libgcrypt20-dev libtasn1-6-dev libharfbuzz-dev \
    libjpeg62-turbo-dev libepoxy-dev libxml2-dev libpng-dev libsqlite3-dev \
    libwebp-dev libxslt1-dev libfreetype-dev libfontconfig-dev libseccomp-dev
for p2_archive in /tmp/p2-gtk-debs/*.deb; do
    p2_arch=$(dpkg-deb -f "$p2_archive" Architecture)
    case $p2_arch in i386|all) ;; *) printf 'Wrong target architecture: %s\n' "$p2_arch" >&2; exit 1 ;; esac
    dpkg-deb -f "$p2_archive" Package Version Architecture >> /opt/p2-gtk-sysroot/packages.txt
    dpkg-deb -x "$p2_archive" /tmp/p2-gtk-root
done
for p2_dir in usr/include usr/lib/i386-linux-gnu lib/i386-linux-gnu usr/lib/pkgconfig usr/share/pkgconfig; do
    if [ -d "/tmp/p2-gtk-root/$p2_dir" ]; then
        mkdir -p "/opt/p2-gtk-sysroot/$(dirname "$p2_dir")"
        cp -a "/tmp/p2-gtk-root/$p2_dir" "/opt/p2-gtk-sysroot/$p2_dir"
    fi
done
rm -rf /tmp/p2-gtk-debs /tmp/p2-gtk-root /tmp/p2-empty-status /var/lib/apt/lists/*
