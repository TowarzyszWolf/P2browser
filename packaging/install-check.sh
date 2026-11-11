#!/bin/sh
# Integration check inside a disposable Debian 13 amd64 container only.
set -eu
version=${VERSION:-0.2.0~alpha1}
[ "$P2_INSTALL_TEST_CONTAINER" = 1 ] && [ -f /.dockerenv ]
export DEBIAN_FRONTEND=noninteractive
test "$(dpkg --print-architecture)" = amd64
test -z "$(dpkg --print-foreign-architectures)"
apt-get update
apt-get install -y --no-install-recommends \
    "/packages/p2browser-diagnostics_${version}_all.deb" \
    "/packages/p2browser-probe_${version}_amd64-trixie.deb" \
    xvfb xauth dbus-x11 librsvg2-common
test -z "$(dpkg --print-foreign-architectures)"
test -z "$(dpkg --audit)"
dpkg-query -W p2browser-probe p2browser-diagnostics
p2browser-probe --version
test -x /usr/bin/p2browser-collect-report
test -x /usr/bin/p2browser-monitor
useradd --uid 1000 --create-home --shell /bin/sh p2test
printf '%s\n' 'INSTALLATION_CHECK_PASSED: amd64; no foreign architectures; no broken packages'
