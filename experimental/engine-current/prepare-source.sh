#!/bin/sh
# Download the reviewed upstream source without replacing an existing tree.
set -eu
p2_dest=${1:-build/engine-current}
mkdir -p "$p2_dest"
p2_archive=$p2_dest/webkitgtk-2.54.2.tar.xz
p2_tree=$p2_dest/webkitgtk-2.54.2
test ! -e "$p2_archive" && test ! -e "$p2_tree" || {
    printf 'Use a directory without an existing archive/source tree.\n' >&2
    exit 1
}
curl --fail --location --retry 2 \
    https://webkitgtk.org/releases/webkitgtk-2.54.2.tar.xz -o "$p2_archive"
printf 'a76345b391bef6211fb5ab6cfab2e86b285d18cda17cbb9ade595f1252b86065  %s\n' "$p2_archive" | sha256sum -c -
tar -xJf "$p2_archive" -C "$p2_dest"
