#!/bin/sh
# Run only in the dedicated guest, as p2test. No external URLs are accepted.
set -u
umask 077
ulimit -c 0
LC_ALL=C.UTF-8
LANG=C.UTF-8
DISPLAY=:99
XDG_RUNTIME_DIR=/run/user/1000
export LC_ALL LANG DISPLAY XDG_RUNTIME_DIR
p2_project=/opt/p2browser
p2_results=/var/lib/p2-results
p2_probe=$p2_project/build/p2browser-probe
p2_supervisor=$p2_project/build/case-supervisor
p2_fixture=file://$p2_project/fixtures/start.html
p2_xvfb_pid=

cleanup() {
    if [ -n "$p2_xvfb_pid" ]; then kill "$p2_xvfb_pid" 2>/dev/null || :; fi
}
trap cleanup EXIT HUP INT TERM
[ "$(id -un)" = p2test ] || { printf 'Must run as p2test.\n' >&2; exit 1; }
printf 'case\tstatus\texit_code\tload_finished\tself_test\trender_evidence\tpss_max_kib\trss_max_kib\tvmswap_max_kib\tprocesses_max\tpss_missing_samples\tsamples\n' > "$p2_results/summary.tsv"

Xvfb "$DISPLAY" -screen 0 1000x740x24 -nolisten tcp -ac \
    > "$p2_results/xvfb.log" 2>&1 &
p2_xvfb_pid=$!
p2_ready=0
for p2_attempt in $(seq 1 30); do
    if xdpyinfo > "$p2_results/display.txt" 2>&1; then p2_ready=1; break; fi
    kill -0 "$p2_xvfb_pid" 2>/dev/null || break
    sleep 1
done
[ "$p2_ready" -eq 1 ] || { printf 'Xvfb failed.\n' >&2; exit 1; }

printf 'Guest CLI versions (failures are retained):\n'
timeout --kill-after=5 20 "$p2_probe" --version > "$p2_results/probe-version.txt" 2>&1
printf 'probe-version exit=%s\n' "$?"
cat "$p2_results/probe-version.txt"
timeout --kill-after=5 20 dillo --version > "$p2_results/dillo-version.txt" 2>&1
printf 'dillo-version exit=%s\n' "$?"
cat "$p2_results/dillo-version.txt"

run_case() {
    p2_case=$1
    p2_kind=$2
    p2_limit=$3
    shift 3
    p2_dir=$p2_results/$p2_case
    mkdir "$p2_dir" || return 1
    printf '\nP2CASEBEGIN %s\n' "$p2_case"
    # The small subreaper retains detached DPI/WebKit helpers. Its own PSS
    # is included for every case; timeout, D-Bus and Xvfb are outside this tree.
    dbus-run-session -- timeout --kill-after=5 "$p2_limit" \
        "$p2_supervisor" "$p2_dir/app.pid" "$@" > "$p2_dir/output.log" 2>&1 &
    p2_runner=$!
    p2_pid=
    for p2_attempt in $(seq 1 30); do
        if [ -s "$p2_dir/app.pid" ]; then
            p2_pid=$(cat "$p2_dir/app.pid")
            break
        fi
        kill -0 "$p2_runner" 2>/dev/null || break
        sleep 1
    done
    p2_monitor_pid=
    if [ -n "$p2_pid" ]; then
        sh "$p2_project/tools/monitor.sh" "$p2_pid" "$p2_dir/process-tree.tsv" 30 \
            > "$p2_dir/monitor.log" 2>&1 &
        p2_monitor_pid=$!
    fi
    # Memory cases remain open; self-tests produce their own view snapshots.
    if [ "$p2_kind" = dillo ] || [ "$p2_kind" = memory ]; then
        sleep 15
        xwininfo -root -tree > "$p2_dir/windows.txt" 2>&1 || :
        timeout --kill-after=3 15 import -window root "$p2_dir/root.png" \
            > "$p2_dir/snapshot.log" 2>&1 || :
    fi
    p2_rc=0
    wait "$p2_runner" || p2_rc=$?
    if [ -n "$p2_monitor_pid" ]; then
        wait "$p2_monitor_pid" || :
    fi
    python3 - "$p2_case" "$p2_kind" "$p2_rc" "$p2_dir" "$p2_results/summary.tsv" <<'PY'
import csv
import json
from pathlib import Path
import sys

name, kind, rc, location, summary = sys.argv[1:]
rc = int(rc)
folder = Path(location)
status = {0: 'EXIT_OK', 124: 'TIMEOUT', 132: 'SIGILL', 133: 'SIGTRAP', 139: 'SIGSEGV',
          134: 'SIGABRT', 137: 'SIGKILL_REVIEW_KERNEL_LOG'}.get(rc, 'EXIT_' + str(rc))
events_file = folder / 'app' / 'events.tsv'
events = events_file.read_text(errors='replace') if events_file.exists() else ''
loaded = '\tload\tfinished\n' in events
sandbox_ok = '\tsandbox\tenabled\n' in events
if kind == 'memory' and rc == 0:
    if '\tweb_process_terminated\t' in events:
        status = 'WEB_PROCESS_TERMINATED'
    elif '\tload_failed\t' in events or not loaded:
        status = 'LOAD_NOT_CONFIRMED'
    elif not sandbox_ok:
        status = 'SANDBOX_NOT_CONFIRMED'
    else:
        status = 'LOADED'
test = 'not_requested'
render = 'none'
png = folder / 'root.png'
if png.exists() and png.read_bytes()[:8] == b'\x89PNG\r\n\x1a\n':
    render = 'root_png_requires_visual_review'
if kind == 'dillo' and rc == 124:
    status = 'TIMED_STOP'
if kind == 'selftest':
    result_file = folder / 'app' / 'self-test.json'
    result = {}
    try:
        result = json.loads(result_file.read_text())
    except (OSError, ValueError):
        pass
    expected_js = name.endswith('js-on')
    snapshot = folder / 'app' / 'snapshot.png'
    image_ok = snapshot.exists() and snapshot.read_bytes()[:8] == b'\x89PNG\r\n\x1a\n'
    ok = (result.get('success') is True and result.get('javascript') is expected_js
          and result.get('scope') == 'local_fixture_only' and image_ok and sandbox_ok)
    test = 'passed' if ok else 'failed_or_missing'
    if rc == 0:
        status = 'PASS' if ok else 'SELFTEST_FAILED'
    if image_ok:
        render = 'view_png_and_selftest' if ok else 'view_png_requires_review'
rows = []
samples = folder / 'process-tree.tsv'
if samples.exists():
    with samples.open() as stream:
        rows = list(csv.DictReader((line for line in stream if not line.startswith('#')), delimiter='\t'))
def maximum(field):
    values = [int(row[field]) for row in rows if row.get(field, '').isdigit()]
    return str(max(values)) if values else 'NA'
missing = sum(row.get('sum_pss_kib', 'NA') == 'NA' for row in rows)
line = '\t'.join([name, status, str(rc), str(loaded).lower(), test, render,
                  maximum('sum_pss_kib'), maximum('sum_rss_kib'), maximum('sum_vmswap_kib'),
                  maximum('processes'), str(missing), str(len(rows))])
with Path(summary).open('a') as stream:
    stream.write(line + '\n')
print(line, flush=True)
PY
    printf 'P2CASEEND %s exit=%s\n' "$p2_case" "$p2_rc"
    free -k > "$p2_dir/memory-after.txt"
    ps -u "$(id -u)" -o pid,ppid,rss,comm > "$p2_dir/processes-after.txt"
    return 0
}

run_case dillo-memory dillo 35 dillo -g 1000x740+0+0 "$p2_fixture"
run_case webkit-memory-js-off memory 90 "$p2_probe" --data-dir "$p2_project/fixtures" \
    --report-dir "$p2_results/webkit-memory-js-off/app" --quit-after 30
run_case webkit-memory-js-on memory 90 "$p2_probe" --data-dir "$p2_project/fixtures" \
    --report-dir "$p2_results/webkit-memory-js-on/app" --quit-after 30 --javascript
run_case webkit-selftest-js-off selftest 105 "$p2_probe" --data-dir "$p2_project/fixtures" \
    --report-dir "$p2_results/webkit-selftest-js-off/app" --self-test --quit-after 90
run_case webkit-selftest-js-on selftest 105 "$p2_probe" --data-dir "$p2_project/fixtures" \
    --report-dir "$p2_results/webkit-selftest-js-on/app" --self-test --quit-after 90 --javascript
printf 'Running opt-in CLI/GUI regression tests in the full i386 guest.\n'
p2_regression_rc=0
timeout --kill-after=10 600 env P2_RUN_PROBE_TESTS=1 P2_RUN_GUI_TESTS=1 P2_GUI_TEST_TIME_SCALE=5 \
    python3 -m unittest discover -s "$p2_project/tests" -p test_probe.py -v \
    > "$p2_results/probe-regression-tests.log" 2>&1 || p2_regression_rc=$?
printf '%s\n' "$p2_regression_rc" > "$p2_results/probe-regression-exit.txt"
printf 'Guest CLI/GUI regression exit=%s\n' "$p2_regression_rc"
printf 'Browser suite completed; inspect statuses and kernel log for failures.\n'
exit 0
