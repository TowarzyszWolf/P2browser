#!/bin/sh
# Sample a Linux process and its descendants without reading command lines.
# sum_rss_kib double-counts shared pages: it is NOT unique physical memory.
# PSS apportions shared pages; a tree total is reported only when complete.
# P2B_PROC_ROOT can select a procfs snapshot for repeatable parser tests.
set -u
umask 077
LC_ALL=C
export LC_ALL

usage() { printf '%s\n' 'Usage: monitor.sh PID NEW_OUTPUT_FILE [SECONDS, default 30]'; }
case ${1-} in -h|--help) usage; exit 0 ;; esac
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then usage >&2; exit 64; fi
p2_pid=$1
p2_output=$2
p2_seconds=${3:-30}
p2_proc=${P2B_PROC_ROOT:-/proc}
case $p2_pid in ''|*[!0-9]*) usage >&2; exit 64 ;; esac
case $p2_seconds in ''|*[!0-9]*) usage >&2; exit 64 ;; esac
if [ "${#p2_pid}" -gt 10 ] || [ "$p2_pid" -lt 1 ] || [ "$p2_pid" -gt 2147483647 ] || \
   [ "${#p2_seconds}" -gt 5 ] || [ "$p2_seconds" -lt 1 ] || [ "$p2_seconds" -gt 86400 ]; then
    printf '%s\n' 'PID must be positive; duration must be between 1 and 86400 seconds.' >&2
    exit 64
fi

# After removing greedy parenthesized comm, starttime is field 20. Reading the
# whole record first also handles the newline characters Linux permits in comm.
p2_identity=$(awk '
    BEGIN {
      while ((getline part < ARGV[1]) > 0) line=line " " part
      close(ARGV[1]); sub(/^ /, "", line)
      sub(/^[0-9]+ \(.*\) /, "", line); n=split(line, field, /[[:space:]]+/)
      if (n >= 20 && field[20] ~ /^[0-9]+$/ && field[1] !~ /^[ZX]$/) print field[20]
      exit }
' "$p2_proc/$p2_pid/stat" 2>/dev/null) || p2_identity=
if [ -z "$p2_identity" ]; then
    printf '%s\n' 'The requested process does not exist or its status is unreadable.' >&2
    exit 66
fi
if [ -e "$p2_output" ] || [ -L "$p2_output" ]; then
    printf '%s\n' 'Output already exists; refusing to overwrite it.' >&2
    exit 73
fi
set -C
exec 3> "$p2_output"
set +C
if ! {
    printf '%s\n' '# P2Browser process-tree samples v2' \
        '# sum_rss_kib double-counts shared pages; samples are best-effort snapshots.' \
        '# NA means unavailable; unreadable_processes means RSS/swap sums may be incomplete.' \
        '# sum_pss_kib apportions shared pages and is NA if any counted process lacks PSS.'
    printf 'elapsed_seconds\tprocesses\tunreadable_processes\tsum_rss_kib\tsum_vmswap_kib\tmem_available_kib\tswap_free_kib\tsum_pss_kib\tpss_unreadable_processes\n'
} >&3; then
    printf '%s\n' 'Writing process samples failed.' >&2
    exit 74
fi
trap 'printf "# interrupted\n" >&3; exit 130' INT
trap 'printf "# terminated\n" >&3; exit 143' TERM

# Civil time can jump after NTP corrections; Linux uptime is monotonic.
read_clock() {
    IFS=' ' read -r p2_uptime p2_idle < /proc/uptime || return 1
    p2_clock_seconds=${p2_uptime%%.*}
    case $p2_clock_seconds in ''|*[!0-9]*) return 1 ;; esac
}
if ! read_clock; then
    printf '%s\n' 'Cannot read the Linux monotonic uptime clock.' >&2
    exit 69
fi
p2_start=$p2_clock_seconds
while :; do
    if ! read_clock; then
        printf '%s\n' 'Cannot read the Linux monotonic uptime clock.' >&2
        exit 69
    fi
    p2_now=$p2_clock_seconds
    p2_elapsed=$((p2_now - p2_start))
    # Explicit getline calls make disappearing procfs files nonfatal.
    p2_sample=$(awk -v wanted="$p2_pid" -v identity="$p2_identity" -v proc="$p2_proc" '
        function readstat(path,    line,part,fields,n,lines) {
            while ((getline part < path) > 0) { line=line " " part; lines++ }
            close(path)
            if (!lines) return 0
            sub(/^ /, "", line)
            if (line !~ /^[0-9]+ \(.*\) /) return 0
            current_pid=line; sub(/ .*/, "", current_pid)
            sub(/^[0-9]+ \(.*\) /, "", line)
            n=split(line, fields, /[[:space:]]+/)
            if (n < 20 || fields[2] !~ /^[0-9]+$/ || fields[20] !~ /^[0-9]+$/) return 0
            current_parent=fields[2]; current_start=fields[20]; current_state=fields[1]
            return 1
        }
        function readpss(path,    line,fields,n,result,total,found,invalid) {
            while ((result=(getline line < path)) > 0) {
                if (line ~ /^Pss:/) {
                    n=split(line, fields, /[[:space:]]+/)
                    if (n != 3 || fields[2] !~ /^[0-9]+$/ || fields[3] != "kB") invalid=1
                    else { total+=fields[2]; found++ }
                }
            }
            close(path)
            return result >= 0 && found && !invalid ? sprintf("%.0f", total) : "NA"
        }
        BEGIN {
            for (i=1; i<ARGC; i++) {
                if (readstat(ARGV[i])) {
                    parent[current_pid]=current_parent
                    start[current_pid]=current_start
                    state[current_pid]=current_state
                }
            }
            if (!(wanted in start) || start[wanted] != identity || state[wanted] ~ /^[ZX]$/) exit 3
            selected[wanted]=1
            do {
                changed=0
                for (pid in parent) {
                    if (!(pid in selected) && parent[pid] in selected) { selected[pid]=1; changed=1 }
                }
            } while (changed)
            for (pid in selected) {
                # A PID may have disappeared or been recycled since the tree scan.
                if (!readstat(proc "/" pid "/stat") || current_start != start[pid] || current_state ~ /^[ZX]$/) continue
                status=proc "/" pid "/status"
                rss="NA"; swap="NA"; lines=0
                while ((getline line < status) > 0) {
                    lines++
                    split(line, field, /[[:space:]]+/)
                    if (field[1] == "VmRSS:" && field[2] ~ /^[0-9]+$/) rss=field[2]+0
                    if (field[1] == "VmSwap:" && field[2] ~ /^[0-9]+$/) swap=field[2]+0
                }
                close(status)
                # rollup avoids parsing every mapping on newer kernels. Linux
                # 4.9 (Debian 9) has only smaps, and some systems restrict both.
                pss=readpss(proc "/" pid "/smaps_rollup")
                if (pss == "NA") pss=readpss(proc "/" pid "/smaps")
                # Discard recycled PIDs; process identities are not just PIDs.
                if (!readstat(proc "/" pid "/stat") || current_start != start[pid] || current_state ~ /^[ZX]$/) continue
                count++
                if (!lines || rss == "NA" || swap == "NA") unreadable++
                if (rss != "NA") { rss_sum+=rss; rss_count++ }
                if (swap != "NA") { swap_sum+=swap; swap_count++ }
                if (pss == "NA") pss_unreadable++
                else { pss_sum+=pss; pss_count++ }
            }
            mem="NA"; swapfree="NA"
            while ((getline line < (proc "/meminfo")) > 0) {
                split(line, field, /[[:space:]]+/)
                if (field[1] == "MemAvailable:" && field[2] ~ /^[0-9]+$/) mem=field[2]
                if (field[1] == "SwapFree:" && field[2] ~ /^[0-9]+$/) swapfree=field[2]
            }
            close(proc "/meminfo")
            printf "%d\t%d\t%s\t%s\t%s\t%s\t%s\t%d\n", count, unreadable,
                rss_count ? sprintf("%.0f", rss_sum) : "NA",
                swap_count ? sprintf("%.0f", swap_sum) : "NA", mem, swapfree,
                pss_count && !pss_unreadable ? sprintf("%.0f", pss_sum) : "NA", pss_unreadable
            exit
        }
    ' "$p2_proc"/[0-9]*/stat)
    p2_status=$?
    if [ "$p2_status" -eq 3 ]; then
        printf '# process ended or PID was reused\n' >&3 || exit 74
        break
    elif [ "$p2_status" -ne 0 ]; then
        printf '# sampling failed\n' >&3 || exit 74
        exit 74
    fi
    if ! printf '%s\t%s\n' "$p2_elapsed" "$p2_sample" >&3; then
        printf '%s\n' 'Writing process samples failed.' >&2
        exit 74
    fi
    [ "$p2_elapsed" -ge "$p2_seconds" ] && break
    p2_wait=$((p2_seconds - p2_elapsed))
    [ "$p2_wait" -gt 2 ] && p2_wait=2
    sleep "$p2_wait"
done
