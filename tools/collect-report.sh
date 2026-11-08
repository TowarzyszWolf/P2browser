#!/bin/sh
# Read-only inventory for Debian 9+; no Python or administrator access required.
# P2B_PROC_ROOT is intended for tests or a mounted procfs snapshot.
set -u
umask 077
LC_ALL=C
export LC_ALL

usage() { printf '%s\n' 'Usage: collect-report.sh [NEW_OUTPUT_DIRECTORY]'; }
case ${1-} in -h|--help) usage; exit 0 ;; esac
if [ "$#" -gt 1 ]; then usage >&2; exit 64; fi
p2_proc=${P2B_PROC_ROOT:-/proc}
p2_output=${1:-p2browser-report-$(date -u +%Y%m%dT%H%M%SZ)-$$}
case $p2_output in -*) p2_output=./$p2_output ;; esac
if ! mkdir -m 700 -- "$p2_output"; then
    printf '%s\n' 'Cannot create a new private report directory; existing directories are never reused.' >&2
    exit 73
fi
p2_report=$p2_output/report.txt
if ! {
    printf '%s\n' 'P2Browser diagnostic inventory v1' \
        'Units: KiB and MiB are binary units.' \
        'This inventory does not collect hostnames, usernames, serial numbers, network configuration, URLs, or command lines.'
    printf '\n[system]\n'
    printf 'architecture='; uname -m 2>/dev/null || printf 'unknown\n'
    printf 'kernel='; uname -r 2>/dev/null || printf 'unknown\n'
    if [ -r /etc/os-release ]; then
        awk -F= '$1 == "ID" || $1 == "VERSION_ID" || $1 == "VERSION_CODENAME" || $1 == "PRETTY_NAME" { print }' /etc/os-release
    else
        printf 'os_release=unknown\n'
    fi
    printf '\n[cpu]\n'
    if [ -r "$p2_proc/cpuinfo" ]; then
        awk -F: '
            /^[[:space:]]*(model name|cpu MHz|flags)[[:space:]]*:/ {
                key=$1; gsub(/^[[:space:]]+|[[:space:]]+$/, "", key)
                if (!seen[key]++) {
                    value=substr($0, index($0, ":")+1)
                    gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
                    print key "=" value; found=1
                }
            }
            END { if (!found) print "cpu=unknown" }
        ' "$p2_proc/cpuinfo"
    else
        printf 'cpu=unknown\n'
    fi
    printf '\n[memory_kib]\n'
    if [ -r "$p2_proc/meminfo" ]; then
        awk '
            $1 ~ /^(MemTotal:|MemFree:|MemAvailable:|Buffers:|Cached:|SwapTotal:|SwapFree:)$/ && $2 ~ /^[0-9]+$/ {
                sub(/:$/, "", $1); print $1 "=" $2; found=1
            }
            END { if (!found) print "memory=unknown" }
        ' "$p2_proc/meminfo"
    else
        printf 'memory=unknown\n'
    fi
    printf '\n[swap_devices]\n'
    if [ -r "$p2_proc/swaps" ]; then
        # Never print swap filenames: they may contain personal filesystem paths.
        awk 'NR > 1 && $3 ~ /^[0-9]+$/ && $4 ~ /^[0-9]+$/ { count++; size+=$3; used+=$4 }
             END { printf "count=%d\nsize_kib=%.0f\nused_kib=%.0f\n", count, size, used }' "$p2_proc/swaps"
    else
        printf 'swap_devices=unknown\n'
    fi
    printf '\n[display_adapters]\n'
    if command -v lspci >/dev/null 2>&1; then
        lspci -mm 2>/dev/null | awk '
            /"(VGA compatible controller|3D controller|Display controller)"/ { print; found=1 }
            END { if (!found) print "display_adapter=unknown" }
        '
    else
        printf 'display_adapter=unknown (lspci unavailable)\n'
    fi
    printf '\n[known_packages]\n'
    if command -v dpkg-query >/dev/null 2>&1; then
        for p2_package in dillo libglib2.0-0 libglib2.0-0t64 libgtk-3-0 libgtk-3-0t64 libwebkit2gtk-4.0-37 libwebkit2gtk-4.1-0 libwebkitgtk-6.0-4 libjavascriptcoregtk-4.0-18 libjavascriptcoregtk-4.1-0; do
            p2_version=$(dpkg-query -W -f='${db:Status-Status}\t${Version}\t${Architecture}\n' "$p2_package" 2>/dev/null) || p2_version=
            case $p2_version in
                installed*) printf '%s\t%s\n' "$p2_package" "$p2_version" ;;
                *) printf '%s\tunknown (not installed or unavailable)\n' "$p2_package" ;;
            esac
        done
    else
        printf 'packages=unknown (dpkg-query unavailable)\n'
    fi
} > "$p2_report"; then
    printf '%s\n' 'Writing the diagnostic report failed.' >&2
    exit 74
fi
printf 'Report saved: %s\n' "$p2_report"
