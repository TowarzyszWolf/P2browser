"""Build-host checks; Python is not required on the Pentium II."""
import os
from pathlib import Path
import resource
import signal
import stat
import subprocess
import sys
import tempfile
import time
import unittest

PROJECT = Path(__file__).resolve().parents[1]
COLLECT = PROJECT / "tools" / "collect-report.sh"
MONITOR = PROJECT / "tools" / "monitor.sh"


class DiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.proc = self.root / "proc"
        self.proc.mkdir()
        (self.proc / "cpuinfo").write_text(
            "processor : 0\nmodel name : Pentium II\ncpu MHz : 400.00\n"
            "flags : fpu tsc mmx\nSerial : SECRET_SERIAL\n"
            "\nprocessor : 1\nmodel name : DO_NOT_REPEAT\n")
        (self.proc / "meminfo").write_text(
            "MemTotal: 377856 kB\nMemFree: 100000 kB\nMemAvailable: 200000 kB\n"
            "SwapTotal: 478208 kB\nSwapFree: 400000 kB\n")
        (self.proc / "swaps").write_text(
            "Filename Type Size Used Priority\n"
            "/home/SECRET_USER/PRIVATE_SWAP file 478208 78208 -2\n")
        self.env = os.environ.copy()
        self.env["P2B_PROC_ROOT"] = str(self.proc)

    def tearDown(self):
        self.tmp.cleanup()

    def run_script(self, script, *args, env=None):
        return subprocess.run(["sh", str(script), *map(str, args)], env=env or self.env,
                              text=True, capture_output=True, timeout=10)

    def process(self, pid, parent, rss, swap, comm="test", start=123, state="S"):
        location = self.proc / str(pid)
        location.mkdir(exist_ok=True)
        fields = ["0"] * 50
        fields[0], fields[1], fields[19] = state, str(parent), str(start)
        (location / "stat").write_text(f"{pid} ({comm}) " + " ".join(fields) + "\n")
        (location / "status").write_text(
            f"Name:\tPRIVATE_PROCESS_NAME\nVmRSS:\t{rss} kB\nVmSwap:\t{swap} kB\n")

    def samples(self, path):
        return [line.split("\t") for line in path.read_text().splitlines()
                if line and not line.startswith("#")][1:]

    def test_collect_permissions_and_private_field_filtering(self):
        output = self.root / "report"
        result = self.run_script(COLLECT, output)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = output / "report.txt"
        text = report.read_text()
        self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o700)
        self.assertEqual(stat.S_IMODE(report.stat().st_mode), 0o600)
        self.assertIn("model name=Pentium II", text)
        self.assertIn("size_kib=478208", text)
        self.assertIn("used_kib=78208", text)
        for forbidden in ("SECRET_USER", "PRIVATE_SWAP", "SECRET_SERIAL", "DO_NOT_REPEAT"):
            self.assertNotIn(forbidden, text)

    def test_collect_never_reuses_existing_directory(self):
        output = self.root / "existing"
        output.mkdir()
        (output / "report.txt").write_text("keep")
        result = self.run_script(COLLECT, output)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((output / "report.txt").read_text(), "keep")

    def test_collect_missing_optional_commands_and_proc(self):
        import shutil
        commands = self.root / "bin"
        commands.mkdir()
        for name in ("mkdir", "awk", "uname", "date"):
            (commands / name).symlink_to(shutil.which(name))
        env = self.env.copy()
        env["PATH"] = str(commands)
        env["P2B_PROC_ROOT"] = str(self.root / "unavailable")
        output = self.root / "partial"
        result = subprocess.run(["/bin/sh", str(COLLECT), str(output)], env=env,
                                text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        text = (output / "report.txt").read_text()
        self.assertIn("cpu=unknown", text)
        self.assertIn("lspci unavailable", text)
        self.assertIn("dpkg-query unavailable", text)

    def test_monitor_process_tree_and_complex_comm(self):
        self.process(100, 1, 10, 1)
        self.process(101, 100, 20, 2, comm="space ) name")
        self.process(102, 101, 30, 3, comm="a) b) c")
        self.process(103, 100, 40, 4, comm="line\nbreak )")
        self.process(999, 1, 99999, 99999)
        output = self.root / "samples.tsv"
        result = self.run_script(MONITOR, 100, output, 1)
        self.assertEqual(result.returncode, 0, result.stderr)
        samples = self.samples(output)
        self.assertGreaterEqual(len(samples), 1)
        self.assertEqual(samples[0][1:7], ["4", "0", "100", "10", "200000", "400000"])
        self.assertEqual(samples[0][7:], ["NA", "4"])
        self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)
        self.assertNotIn("PRIVATE_PROCESS_NAME", output.read_text())
        self.assertIn("double-counts", output.read_text())

    def test_monitor_prefers_pss_rollup_and_preserves_zero(self):
        self.process(100, 1, 10, 1)
        self.process(101, 100, 20, 2)
        for pid, pss in ((100, 7), (101, 0)):
            location = self.proc / str(pid)
            (location / "smaps_rollup").write_text(
                f"00000000-ffffffff ---p 00000000 00:00 0 [rollup]\n"
                f"Rss: 999 kB\nPss: {pss} kB\nPss_Anon: 888 kB\n")
            (location / "smaps").write_text(
                "00000000-00001000 r--p 00000000 00:00 0 /PRIVATE_MAPPING\n"
                "Pss: 10000 kB\n")
        output = self.root / "pss-rollup.tsv"
        result = self.run_script(MONITOR, 100, output, 1)
        self.assertEqual(result.returncode, 0, result.stderr)
        sample = self.samples(output)[0]
        self.assertEqual(sample[1:7], ["2", "0", "30", "3", "200000", "400000"])
        self.assertEqual(sample[7:], ["7", "0"])
        self.assertNotIn("PRIVATE_MAPPING", output.read_text())

    def test_monitor_pss_falls_back_to_sum_of_smaps_mappings(self):
        self.process(100, 1, 10, 1)
        self.process(101, 100, 20, 2)
        (self.proc / "100" / "smaps").write_text(
            "00000000-00001000 r--p 00000000 00:00 0 /PRIVATE_FIRST\n"
            "Pss: 4 kB\nPss_Dirty: 999 kB\n"
            "00001000-00002000 rw-p 00000000 00:00 0 /PRIVATE_SECOND\n"
            "Pss: 6 kB\n"
            "00002000-00003000 rw-p 00000000 00:00 0\nPss: 0 kB\n")
        # A rollup may exist but be unreadable/empty; smaps remains useful.
        (self.proc / "101" / "smaps_rollup").write_text("")
        (self.proc / "101" / "smaps").write_text("Pss: 8 kB\n")
        output = self.root / "pss-smaps.tsv"
        result = self.run_script(MONITOR, 100, output, 1)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.samples(output)[0][7:], ["18", "0"])
        self.assertNotIn("PRIVATE_", output.read_text())

    def test_monitor_missing_pss_marks_tree_total_unavailable(self):
        self.process(100, 1, 10, 1)
        self.process(101, 100, 20, 2)
        (self.proc / "100" / "smaps_rollup").write_text("Pss: 7 kB\n")
        output = self.root / "pss-partial.tsv"
        result = self.run_script(MONITOR, 100, output, 1)
        self.assertEqual(result.returncode, 0, result.stderr)
        # The known process cannot make a partial tree sum look complete.
        self.assertEqual(self.samples(output)[0][7:], ["NA", "1"])

    def test_monitor_rejects_malformed_pss_measurements(self):
        self.process(100, 1, 10, 1)
        for index, measurement in enumerate(("-1 kB", "wrong kB", "7 MB", "7")):
            (self.proc / "100" / "smaps_rollup").write_text(f"Pss: {measurement}\n")
            (self.proc / "100" / "smaps").write_text(
                f"Pss: 3 kB\nPss: {measurement}\n")
            output = self.root / f"pss-invalid-{index}.tsv"
            result = self.run_script(MONITOR, 100, output, 1)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(self.samples(output)[0][7:], ["NA", "1"])

    def test_monitor_invalid_pid_duration_and_existing_output(self):
        output = self.root / "samples.tsv"
        for pid in ("", "0", "-1", "no", "99999999999999999", "999999"):
            result = self.run_script(MONITOR, pid, output, 1)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())
        self.process(100, 1, 10, 1)
        for duration in ("0", "no", "-1", "99999999999"):
            result = self.run_script(MONITOR, 100, output, duration)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())
        output.write_text("keep")
        result = self.run_script(MONITOR, 100, output, 1)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_text(), "keep")

    def test_monitor_handles_disappearing_descendant(self):
        self.process(100, 1, 10, 1)
        self.process(101, 100, 20, 2)
        output = self.root / "samples.tsv"
        child = subprocess.Popen(["sh", str(MONITOR), "100", str(output), "2"],
                                 env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.wait_for_sample(output)
        for path in (self.proc / "101").iterdir():
            path.unlink()
        (self.proc / "101").rmdir()
        _, stderr = child.communicate(timeout=6)
        self.assertEqual(child.returncode, 0, stderr)
        self.assertEqual(self.samples(output)[-1][1], "1")

    def wait_for_sample(self, path):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if path.exists() and self.samples(path):
                return
            time.sleep(0.02)
        self.fail("monitor did not write a sample")

    def test_monitor_stops_on_reused_root_pid(self):
        self.process(100, 1, 10, 1, start=123)
        output = self.root / "samples.tsv"
        child = subprocess.Popen(["sh", str(MONITOR), "100", str(output), "2"],
                                 env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.wait_for_sample(output)
        self.process(100, 1, 999, 999, start=124)
        _, stderr = child.communicate(timeout=6)
        self.assertEqual(child.returncode, 0, stderr)
        self.assertIn("PID was reused", output.read_text())
        self.assertEqual(len(self.samples(output)), 1)

    def test_monitor_can_be_cancelled(self):
        self.process(100, 1, 10, 1)
        output = self.root / "samples.tsv"
        child = subprocess.Popen(["sh", str(MONITOR), "100", str(output), "30"],
                                 env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.wait_for_sample(output)
        child.send_signal(signal.SIGTERM)
        _, stderr = child.communicate(timeout=6)
        self.assertEqual(child.returncode, 143, stderr)
        self.assertIn("# terminated", output.read_text())

    def test_monitor_real_process_tree(self):
        grandchild_code = "import time; time.sleep(30)"
        child_code = (
            "import subprocess,sys,time; "
            f"subprocess.Popen([sys.executable, '-c', {grandchild_code!r}]); "
            "print('ready', flush=True); time.sleep(30)")
        root_code = (
            "import subprocess,sys,time; "
            f"child=subprocess.Popen([sys.executable, '-c', {child_code!r}], stdout=subprocess.PIPE); "
            "child.stdout.readline(); print('ready', flush=True); time.sleep(30)")
        process = subprocess.Popen([sys.executable, "-c", root_code], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, start_new_session=True)
        try:
            self.assertEqual(process.stdout.readline().strip(), "ready")
            env = self.env.copy()
            env["P2B_PROC_ROOT"] = "/proc"
            output = self.root / "real-samples.tsv"
            result = self.run_script(MONITOR, process.pid, output, 1, env=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            sample = self.samples(output)[0]
            self.assertGreaterEqual(int(sample[1]), 3)
            self.assertGreater(int(sample[3]), 0)
        finally:
            os.killpg(process.pid, signal.SIGTERM)
            process.communicate(timeout=5)

    def test_write_failures_do_not_report_success(self):
        def prohibit_writes():
            resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))
        self.process(100, 1, 10, 1)
        invocations = (
            ["sh", str(COLLECT), str(self.root / "write-failure")],
            ["sh", str(MONITOR), "100", str(self.root / "write-failure.tsv"), "1"])
        for invocation in invocations:
            result = subprocess.run(invocation, env=self.env, text=True, capture_output=True,
                                    timeout=6, preexec_fn=prohibit_writes)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("Report saved:", result.stdout)


if __name__ == "__main__":
    unittest.main()
