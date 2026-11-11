#!/bin/sh
# Optional test driver, not a browser runtime dependency. Local fixtures only.
set -eu
umask 077
ulimit -c 0
exec python3 - <<'PY'
import csv
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def stop(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=7)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def setting_int(name, default, low, high):
    value = os.environ.get(name, str(default))
    if not value.isdecimal() or not low <= int(value) <= high:
        raise ValueError('%s must be between %d and %d' % (name, low, high))
    return int(value)


def read_events(folder):
    try:
        rows = (folder / 'app' / 'events.tsv').read_text(errors='replace').splitlines()
    except OSError:
        return []
    return [line.split('\t', 2) for line in rows if len(line.split('\t', 2)) == 3]


def event_seen(events, kind, detail=None):
    return any(row[1] == kind and (detail is None or row[2] == detail) for row in events)


def max_field(rows, field):
    values = [int(row[field]) for row in rows if row.get(field, '').isdigit()]
    return max(values) if values else 'NA'


if os.geteuid() == 0:
    raise SystemExit('Run the stress suite as the ordinary desktop/test user, without sudo.')
project = Path(os.environ.get('P2_PROJECT_DIR', '/opt/p2browser')).resolve()
results = Path(os.environ.get('P2_STRESS_RESULTS', '/var/lib/p2-stress-results')).resolve()
duration = setting_int('P2_STRESS_SECONDS', 45, 15, 180)
capture_delay = setting_int('P2_STRESS_CAPTURE_DELAY_SECONDS', 5, 1, 30)
pressure_budget = setting_int('P2_STRESS_MEMORY_LIMIT_MIB', 96, 16, 16384)
display = os.environ.get('P2_STRESS_DISPLAY', ':98')
if not display.startswith(':') or not display[1:].isdigit():
    raise SystemExit('P2_STRESS_DISPLAY must be a local display such as :98.')
results.mkdir(mode=0o700, parents=False, exist_ok=True)
if (results / 'summary.tsv').exists() or (results / 'generated').exists():
    raise SystemExit('A new stress results directory is required; existing results are preserved.')

env = dict(os.environ, LC_ALL='C.UTF-8', LANG='C.UTF-8', DISPLAY=display,
           XDG_RUNTIME_DIR=os.environ.get('XDG_RUNTIME_DIR', '/run/user/1000'))
generated = results / 'generated'
with (results / 'generation.log').open('x') as log:
    subprocess.run([sys.executable, str(project / 'fixtures/stress/generate.py'), str(generated)],
                   check=True, stdout=log, stderr=subprocess.STDOUT, env=env, timeout=120)
manifest = json.loads((generated / 'manifest.json').read_text())
specs = {spec['name']: spec for spec in manifest['runs']}
selection = os.environ.get('P2_STRESS_CASES', ','.join(specs)).split(',')
if (not selection or any(name not in specs for name in selection)
        or len(selection) != len(set(selection))):
    raise SystemExit('P2_STRESS_CASES must contain unique names from the fixture manifest.')

profile = {'scope': 'offline_generated_fixtures', 'duration_seconds': duration,
           'capture_delay_seconds': capture_delay,
           'selected_cases': selection, 'pressure_budget_mib': pressure_budget,
           'fixture_manifest_sha256': hashlib.sha256((generated / 'manifest.json').read_bytes()).hexdigest(),
           'screenshot': 'Xvfb root capture requires visual review',
           'timings': 'Emulator/host time; not a simulation of Pentium II 400 MHz',
           'cpu_scope': 'RUSAGE_CHILDREN after reaping: browser descendants, excluding supervisor/driver/monitor',
           'pss_scope': 'browser subtree plus subreaper; excludes driver, Xvfb, D-Bus and monitor'}
(results / 'profile.json').write_text(json.dumps(profile, indent=2) + '\n')

columns = ['case', 'status', 'exit_code', 'load_finished', 'fixture_ready', 'sandbox',
           'memory_pressure', 'background_reclaimed', 'ready_elapsed_ms', 'pss_max_kib',
           'rss_max_kib', 'vmswap_max_kib', 'processes_max', 'pss_missing_samples',
           'samples', 'root_png', 'memory_limit_mib']
columns += ['cpu_user_us', 'cpu_system_us', 'cpu_total_us', 'cpu_total_ms']
summary = (results / 'summary.tsv').open('x', newline='')
writer = csv.DictWriter(summary, fieldnames=columns, delimiter='\t')
writer.writeheader()
summary.flush()

xvfb_log = (results / 'xvfb.log').open('x')
xvfb = subprocess.Popen(['Xvfb', display, '-screen', '0', '1000x740x24',
                         '-nolisten', 'tcp', '-ac'], stdout=xvfb_log,
                        stderr=subprocess.STDOUT, env=env)
all_ok = True
active_runner = active_monitor = active_capture = None
active_app_pid = None
try:
    for _ in range(30):
        with (results / 'display.txt').open('w') as log:
            check = subprocess.run(['xdpyinfo'], stdout=log, stderr=subprocess.STDOUT,
                                   env=env, timeout=5)
        if check.returncode == 0:
            break
        if xvfb.poll() is not None:
            raise RuntimeError('Xvfb failed; inspect xvfb.log')
        time.sleep(1)
    else:
        raise RuntimeError('Xvfb did not become ready')

    for name in selection:
        spec = specs[name]
        folder = results / name
        folder.mkdir(mode=0o700)
        probe = [str(project / 'build/p2browser-probe'), '--report-dir', str(folder / 'app'),
                 '--quit-after', str(duration), '--fixture-ready-title', spec['title']]
        if spec['javascript']:
            probe.append('--javascript')
        if spec['images']:
            probe.append('--images')
        budget = pressure_budget if spec.get('memory_limit_mib') else 0
        if budget:
            probe.extend(['--memory-limit-mib', str(budget)])
        if spec.get('reclaim_background'):
            probe.append('--reclaim-background')
        for background in spec.get('background', []):
            probe.extend(['--background-tab', (generated / background).as_uri()])
        probe.append((generated / spec['fixture']).as_uri())
        (folder / 'workload.json').write_text(json.dumps(
            dict(spec, effective_duration_seconds=duration, effective_memory_limit_mib=budget),
            indent=2) + '\n')
        print('P2STRESSBEGIN ' + name, flush=True)
        output = (folder / 'output.log').open('x')
        active_runner = subprocess.Popen(
            ['dbus-run-session', '--', 'timeout', '--kill-after=5', str(duration + 60),
             str(project / 'build/case-supervisor'), str(folder / 'app.pid'), *probe],
            stdout=output, stderr=subprocess.STDOUT,
            env=dict(env, P2_SUPERVISOR_USAGE_FILE=str(folder / 'cpu.tsv'),
                     XDG_CONFIG_HOME=str(folder / 'config'), XDG_CACHE_HOME=str(folder / 'cache')))
        active_app_pid = None
        for _ in range(100):
            try:
                active_app_pid = int((folder / 'app.pid').read_text().strip())
                break
            except (OSError, ValueError):
                if active_runner.poll() is not None:
                    break
                time.sleep(0.1)
        monitor_log = (folder / 'monitor.log').open('x')
        if active_app_pid:
            active_monitor = subprocess.Popen(
                ['sh', str(project / 'tools/monitor.sh'), str(active_app_pid),
                 str(folder / 'process-tree.tsv'), str(duration + 30)],
                stdout=monitor_log, stderr=subprocess.STDOUT, env=env)
        else:
            active_monitor = None

        deadline = time.monotonic() + duration + 75
        ready_since = None
        snapshot_log = (folder / 'snapshot.log').open('x')
        while active_runner.poll() is None and time.monotonic() < deadline:
            events = read_events(folder)
            if ready_since is None and event_seen(events, 'fixture_ready', 'matched'):
                ready_since = time.monotonic()
            if (active_capture is None and ready_since is not None
                    and time.monotonic() - ready_since >= capture_delay):
                active_capture = subprocess.Popen(
                    ['timeout', '--kill-after=3', '20', 'import', '-window', 'root', str(folder / 'root.png')],
                    stdout=snapshot_log, stderr=subprocess.STDOUT, env=env)
            time.sleep(0.5)
        forced = active_runner.poll() is None
        if forced:
            if active_app_pid:
                try:
                    os.kill(active_app_pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            stop(active_runner)
        rc = active_runner.wait()
        if rc < 0:
            rc = 128 - rc
        output.close()
        if active_monitor:
            try:
                active_monitor.wait(timeout=7)
            except subprocess.TimeoutExpired:
                stop(active_monitor)
        monitor_log.close()
        if active_capture:
            try:
                active_capture.wait(timeout=24)
            except subprocess.TimeoutExpired:
                stop(active_capture)
        snapshot_log.close()

        events = read_events(folder)
        loaded = event_seen(events, 'load', 'finished')
        ready = event_seen(events, 'fixture_ready', 'matched')
        sandbox = event_seen(events, 'sandbox', 'enabled')
        pressure = event_seen(events, 'memory_pressure', 'budget_exceeded')
        reclaimed = any(row[1] == 'memory_reclaim' and row[2].startswith('background_tabs=')
                        and row[2].split('=', 1)[1].isdigit()
                        and int(row[2].split('=', 1)[1]) > 0 for row in events)
        status = {124: 'TIMEOUT', 132: 'SIGILL', 133: 'SIGTRAP', 134: 'SIGABRT',
                  137: 'SIGKILL_REVIEW_KERNEL_LOG', 139: 'SIGSEGV'}.get(rc, 'EXIT_' + str(rc))
        if forced:
            status = 'DRIVER_TIMEOUT'
        elif rc == 0:
            if event_seen(events, 'web_process_terminated'):
                status = 'WEB_PROCESS_TERMINATED'
            elif event_seen(events, 'load_failed') or not loaded:
                status = 'LOAD_NOT_CONFIRMED'
            elif not sandbox:
                status = 'SANDBOX_NOT_CONFIRMED'
            elif not ready:
                status = 'FIXTURE_NOT_READY'
            elif spec.get('reclaim_background') and not reclaimed:
                status = 'RECLAIM_NOT_CONFIRMED'
            else:
                status = 'PASS_RECLAIM' if reclaimed else 'READY'
        samples = []
        sample_file = folder / 'process-tree.tsv'
        if sample_file.exists():
            with sample_file.open() as stream:
                samples = list(csv.DictReader((line for line in stream if not line.startswith('#')),
                                              delimiter='\t'))
        complete_samples = [row for row in samples if row.get('sum_pss_kib', '').isdigit()]
        if status in ('READY', 'PASS_RECLAIM') and not complete_samples:
            status = 'READY_WITHOUT_MEMORY_SAMPLES'
        png = folder / 'root.png'
        png_ok = png.exists() and png.read_bytes()[:8] == b'\x89PNG\r\n\x1a\n'
        ready_times = [row[0] for row in events if row[1:] == ['fixture_ready', 'matched']
                       and row[0].isdigit()]
        cpu_user = cpu_system = cpu_total = cpu_ms = 'NA'
        try:
            with (folder / 'cpu.tsv').open() as stream:
                usage = list(csv.DictReader(stream, delimiter='\t'))
            if (len(usage) == 1 and usage[0].get('children_user_us', '').isdigit()
                    and usage[0].get('children_system_us', '').isdigit()):
                cpu_user = int(usage[0]['children_user_us'])
                cpu_system = int(usage[0]['children_system_us'])
                cpu_total = cpu_user + cpu_system
                cpu_ms = '%.3f' % (cpu_total / 1000.0)
        except (OSError, ValueError, csv.Error):
            pass
        result = dict(case=name, status=status, exit_code=rc,
                      load_finished=str(loaded).lower(), fixture_ready=str(ready).lower(),
                      sandbox=str(sandbox).lower(), memory_pressure=str(pressure).lower(),
                      background_reclaimed=str(reclaimed).lower(),
                      ready_elapsed_ms=ready_times[0] if ready_times else 'NA',
                      pss_max_kib=max_field(samples, 'sum_pss_kib'),
                      rss_max_kib=max_field(samples, 'sum_rss_kib'),
                      vmswap_max_kib=max_field(samples, 'sum_vmswap_kib'),
                      processes_max=max_field(samples, 'processes'),
                      pss_missing_samples=sum(row.get('sum_pss_kib', 'NA') == 'NA' for row in samples),
                      samples=len(samples), root_png='requires_visual_review' if png_ok else 'missing',
                      memory_limit_mib=budget, cpu_user_us=cpu_user, cpu_system_us=cpu_system,
                      cpu_total_us=cpu_total, cpu_total_ms=cpu_ms)
        writer.writerow(result)
        summary.flush()
        print('P2STRESSRESULT ' + json.dumps(result, sort_keys=True), flush=True)
        with (folder / 'memory-after.txt').open('x') as log:
            subprocess.run(['free', '-k'], stdout=log, stderr=subprocess.STDOUT, env=env)
        with (folder / 'processes-after.txt').open('x') as log:
            subprocess.run(['ps', '-u', str(os.geteuid()), '-o', 'pid,ppid,rss,comm'],
                           stdout=log, stderr=subprocess.STDOUT, env=env)
        all_ok = all_ok and status in ('READY', 'PASS_RECLAIM')
        active_runner = active_monitor = active_capture = None
        active_app_pid = None
finally:
    if active_app_pid and active_runner is not None and active_runner.poll() is None:
        try:
            os.kill(active_app_pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    for process in (active_runner, active_monitor, active_capture, xvfb):
        stop(process)
    xvfb_log.close()
    summary.close()
print('P2STRESSDONE all_cases_ready=' + str(all_ok).lower(), flush=True)
raise SystemExit(0 if all_ok else 1)
PY
