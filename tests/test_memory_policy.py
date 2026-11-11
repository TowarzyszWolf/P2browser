"""Native C tests for advisory memory decisions and procfs accounting."""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import unittest

PROJECT = Path(__file__).resolve().parents[1]


class Sample(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in
                ("pss_kib", "rss_kib", "swap_kib", "available_kib", "swap_free_kib")] + [
        ("processes", ctypes.c_uint), ("pss_missing", ctypes.c_uint),
        ("pss_complete", ctypes.c_int), ("available_known", ctypes.c_int),
        ("swap_free_known", ctypes.c_int)]


@unittest.skipUnless(shutil.which("cc"), "C compiler required on the build host")
class MemoryPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        library = Path(cls.build.name) / "policy.so"
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                        "-shared", "-fPIC", str(PROJECT / "src/memory-policy.c"),
                        "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.p2_memory_sample.argtypes = [ctypes.c_char_p, ctypes.c_long, ctypes.POINTER(Sample)]
        cls.lib.p2_memory_sample.restype = ctypes.c_int
        cls.lib.p2_memory_level.argtypes = [ctypes.POINTER(Sample), ctypes.c_uint64]
        cls.lib.p2_memory_level.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.proc = Path(self.tmp.name)
        (self.proc / "meminfo").write_text("MemAvailable: 200000 kB\nSwapFree: 400000 kB\n")

    def tearDown(self):
        self.tmp.cleanup()

    def process(self, pid, parent, pss=10, rss=30, swap=0, start=123, comm="test", state="S"):
        folder = self.proc / str(pid)
        folder.mkdir(exist_ok=True)
        fields = ["0"] * 50
        fields[0], fields[1], fields[19] = state, str(parent), str(start)
        (folder / "stat").write_text(f"{pid} ({comm}) " + " ".join(fields) + "\n")
        (folder / "status").write_text(f"VmRSS:\t{rss} kB\nVmSwap:\t{swap} kB\n")
        rollup = folder / "smaps_rollup"
        if pss is None:
            if rollup.exists():
                rollup.unlink()
        else:
            rollup.write_text(f"Rss: {rss} kB\nPss: {pss} kB\nPss_Anon: {pss} kB\n")
        return folder

    def sample(self, pid=100):
        result = Sample()
        valid = self.lib.p2_memory_sample(os.fsencode(self.proc), pid, ctypes.byref(result))
        return valid, result

    def level(self, sample, budget):
        return self.lib.p2_memory_level(ctypes.byref(sample), budget)

    def test_process_tree_pss_and_complex_names(self):
        self.process(100, 1, pss=10, rss=30, swap=1, comm="line\nbreak ) name")
        self.process(101, 100, pss=20, rss=30, swap=2)
        self.process(102, 101, pss=30, rss=30, swap=3, comm="a) b) c")
        self.process(200, 1, pss=9999)
        self.process(300, 100, pss=9999, state="Z")
        valid, result = self.sample()
        self.assertEqual(valid, 1)
        self.assertEqual((result.processes, result.pss_kib, result.rss_kib, result.swap_kib), (3, 60, 90, 6))
        self.assertTrue(result.pss_complete)
        self.assertEqual((result.available_kib, result.swap_free_kib), (200000, 400000))

    def test_smaps_fallback_and_rollup_precedence(self):
        folder = self.process(100, 1, pss=5)
        (folder / "smaps").write_text("0000-1000 rw-p ...\nPss: 70 kB\nPss: 80 kB\n")
        self.assertEqual(self.sample()[1].pss_kib, 5)
        (folder / "smaps_rollup").unlink()
        result = self.sample()[1]
        self.assertEqual(result.pss_kib, 150)
        self.assertTrue(result.pss_complete)

    def test_zero_pss_is_known_but_missing_child_pss_is_not_a_total(self):
        self.process(100, 1, pss=0)
        valid, result = self.sample()
        self.assertTrue(valid and result.pss_complete)
        self.assertEqual(result.pss_kib, 0)
        self.process(101, 100, pss=None)
        result = self.sample()[1]
        self.assertFalse(result.pss_complete)
        self.assertEqual((result.processes, result.pss_missing), (2, 1))
        result.pss_kib = 100000
        self.assertEqual(self.level(result, 1), 0)

    def test_invalid_pss_numbers_units_and_overflow_are_unknown(self):
        folder = self.process(100, 1)
        for value in ("-1 kB", "4 MB", "12 kB junk", "18446744073709551616 kB"):
            with self.subTest(value=value):
                (folder / "smaps_rollup").write_text("Pss: " + value + "\n")
                result = self.sample()[1]
                self.assertFalse(result.pss_complete)
                self.assertEqual(result.pss_missing, 1)
        (folder / "smaps_rollup").write_text("Pss: 18446744073709551615 kB\n")
        self.process(101, 100, pss=1)
        result = self.sample()[1]
        self.assertFalse(result.pss_complete)
        self.assertEqual(result.pss_kib, 2**64 - 1)

    def test_missing_or_invalid_memory_does_not_report_critical(self):
        self.process(100, 1)
        for content in ("", "MemAvailable: -1 kB\nSwapFree: 0 kB\n",
                        "MemAvailable: 0 kB\nSwapFree: 0 MB\n",
                        "MemAvailable: 0 kB\nMemAvailable: 0 kB\nSwapFree: 0 kB\n"):
            with self.subTest(content=content):
                (self.proc / "meminfo").write_text(content)
                result = self.sample()[1]
                self.assertEqual(self.level(result, 0), 0)

    def test_pressure_thresholds_and_critical_precedence(self):
        sample = Sample(pss_kib=100, pss_complete=1, available_known=1,
                        swap_free_known=1, available_kib=20000, swap_free_kib=20000)
        self.assertEqual(self.level(sample, 0), 0)
        self.assertEqual(self.level(sample, 100), 0)
        self.assertEqual(self.level(sample, 99), 1)
        sample.available_kib, sample.swap_free_kib = 12*1024-1, 16*1024-1
        self.assertEqual(self.level(sample, 0), 2)
        self.assertEqual(self.level(sample, 1), 2)
        sample.available_kib = 12*1024
        self.assertEqual(self.level(sample, 0), 0)
        sample.available_kib, sample.swap_free_kib = 0, 16*1024
        self.assertEqual(self.level(sample, 0), 0)

    def test_missing_root_and_wrong_recorded_pid_clear_output(self):
        result = Sample(pss_kib=99999, pss_complete=1)
        for pid in (0, -1, 999):
            valid = self.lib.p2_memory_sample(os.fsencode(self.proc), pid, ctypes.byref(result))
            self.assertEqual(valid, 0)
            self.assertEqual(bytes(result), bytes(Sample()))
        folder = self.process(100, 1)
        (folder / "stat").write_text((folder / "stat").read_text().replace("100 (", "101 (", 1))
        self.assertEqual(self.sample()[0], 0)

    def test_recycled_child_pid_is_discarded_during_sample(self):
        self.process(100, 1, pss=10)
        folder = self.process(101, 100, pss=None)
        fifo = folder / "smaps_rollup"
        os.mkfifo(fifo)
        errors = []
        def writer():
            try:
                with fifo.open("w") as stream:
                    self.process(101, 100, pss=None, start=999)
                    stream.write("Pss: 9999 kB\n")
            except BaseException as error:
                errors.append(error)
        # The writer cannot open the FIFO until the reader has selected child 101.
        thread = threading.Thread(target=writer, daemon=True)
        thread.start()
        valid, result = self.sample()
        thread.join(timeout=3)
        self.assertFalse(thread.is_alive())
        self.assertFalse(errors)
        self.assertEqual((valid, result.processes, result.pss_kib), (1, 1, 10))

    def test_native_self_sample_has_complete_nonzero_pss(self):
        result = Sample()
        valid = self.lib.p2_memory_sample(b"/proc", os.getpid(), ctypes.byref(result))
        self.assertEqual(valid, 1)
        self.assertTrue(result.pss_complete)
        self.assertGreater(result.pss_kib, 0)
        self.assertGreater(result.rss_kib, 0)

    def test_recycled_root_invalidates_the_whole_sample(self):
        self.process(100, 1, pss=10)
        folder = self.process(101, 100, pss=None)
        fifo = folder / "smaps_rollup"
        os.mkfifo(fifo)
        errors = []
        def writer():
            try:
                with fifo.open("w") as stream:
                    self.process(100, 1, pss=10, start=999)
                    stream.write("Pss: 9999 kB\n")
            except BaseException as error:
                errors.append(error)
        thread = threading.Thread(target=writer, daemon=True)
        thread.start()
        valid, result = self.sample()
        thread.join(timeout=3)
        self.assertFalse(thread.is_alive())
        self.assertFalse(errors)
        self.assertEqual(valid, 0)
        self.assertEqual(bytes(result), bytes(Sample()))

    def test_long_mapping_names_cannot_be_mistaken_for_pss_fields(self):
        folder = self.process(100, 1, pss=None)
        # fgets splits this header immediately before the fake Pss suffix.
        (folder / "smaps").write_text("x" * 511 + "Pss: 9999 kB\nPss: 7 kB\n")
        valid, result = self.sample()
        self.assertEqual((valid, result.pss_kib, result.pss_complete), (1, 7, 1))
