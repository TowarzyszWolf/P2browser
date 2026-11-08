"""The emulator's supervisor must retain and clean daemonized browser helpers."""
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

PROJECT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("cc") and Path("/proc").is_dir(), "Linux and C compiler required")
class CaseSupervisorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / "supervisor"
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-O2",
                        str(PROJECT / "emulation/case-supervisor.c"), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_existing_pid_file_does_not_launch_a_command(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            pid_file = path / "pid"
            marker = path / "must-not-exist"
            pid_file.write_text("preserve")
            result = subprocess.run([str(self.binary), str(pid_file), sys.executable,
                                     "-c", "from pathlib import Path; Path(" + repr(str(marker)) + ").touch()"],
                                    capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(pid_file.read_text(), "preserve")
            self.assertFalse(marker.exists())

    def test_child_exit_and_sigill_are_preserved(self):
        def no_core():
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        for code, expected in (("raise SystemExit(7)", 7),
                               ("import os,signal; os.kill(os.getpid(),signal.SIGILL)", 132)):
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as tmp:
                result = subprocess.run([str(self.binary), str(Path(tmp) / "pid"),
                                         sys.executable, "-c", code], capture_output=True,
                                        timeout=5, preexec_fn=no_core)
                self.assertEqual(result.returncode, expected, result.stderr)

    def test_double_fork_helper_is_adopted_sampled_and_cleaned(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            helper_file = path / "helper"
            code = (
                "import os,time; from pathlib import Path\n"
                "if os.fork()==0:\n"
                " os.setsid()\n"
                " if os.fork()!=0: os._exit(0)\n"
                " Path(" + repr(str(helper_file)) + ").write_text(str(os.getpid()))\n"
                " time.sleep(30); os._exit(0)\n"
                "time.sleep(4)\n"
            )
            process = subprocess.Popen([str(self.binary), str(path / "pid"), sys.executable, "-c", code],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            helper = None
            try:
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    if helper_file.exists():
                        helper = int(helper_file.read_text())
                        status = Path(f"/proc/{helper}/status").read_text()
                        if f"PPid:\t{process.pid}\n" in status:
                            break
                    time.sleep(0.02)
                else:
                    self.fail("daemon was not adopted by the supervisor")
                output = path / "samples.tsv"
                result = subprocess.run(["sh", str(PROJECT / "tools/monitor.sh"),
                                         str(process.pid), str(output), "1"],
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                first = [row.split("\t") for row in output.read_text().splitlines()
                         if row and not row.startswith("#")][1]
                self.assertGreaterEqual(int(first[1]), 3)
                self.assertGreater(int(first[7]), 0)
                process.communicate(timeout=7)
                self.assertEqual(process.returncode, 0)
                self.assertFalse(Path(f"/proc/{helper}").exists(), "detached helper survived cleanup")
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.communicate(timeout=7)
                if helper and Path(f"/proc/{helper}").exists():
                    try:
                        os.kill(helper, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
