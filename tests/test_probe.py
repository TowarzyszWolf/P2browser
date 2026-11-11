"""Behavioral checks for the native probe; opt in on a prepared build host.

These tests do not emulate Pentium II, restrict the machine to 384 MB, or
establish compatibility with public websites. GUI tests keep the engine's
sandbox enabled and exercise only local documents.
"""

import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[1]
BINARY = PROJECT / "build" / "p2browser-probe"
RUN_BINARY = os.environ.get("P2_RUN_PROBE_TESTS") == "1"
RUN_GUI = os.environ.get("P2_RUN_GUI_TESTS") == "1"
TIME_SCALE = int(os.environ.get("P2_GUI_TEST_TIME_SCALE", "1"))
if not 1 <= TIME_SCALE <= 10:
    raise ValueError("P2_GUI_TEST_TIME_SCALE must be between 1 and 10")
BUILD_VERSION = re.search(r"^VERSION\s*\?=\s*(\S+)", (PROJECT / "Makefile").read_text(), re.M).group(1)


@unittest.skipUnless(
    RUN_BINARY, "set P2_RUN_PROBE_TESTS=1 in the native build environment"
)
class ProbeCliTests(unittest.TestCase):
    def run_probe(self, *args, env=None):
        self.assertTrue(BINARY.is_file(), "Build the native probe before running CLI tests")
        return subprocess.run(
            [str(BINARY), *args],
            capture_output=True,
            text=True,
            env=env,
            timeout=15,
        )

    def test_version_does_not_need_a_display_or_create_a_report(self):
        env = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="", GDK_BACKEND="x11")
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "not-created"
            result = self.run_probe("--version", "--report-dir", str(report), env=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("p2browser-probe " + BUILD_VERSION, result.stdout)
            self.assertIn("WebKitGTK", result.stdout)
            self.assertFalse(report.exists())

    def test_invalid_arguments_do_not_create_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "new report"
            invalid_arguments = (
                ("--quit-after", "-1"),
                ("--quit-after", "86401"),
                ("--self-test", "https://example.invalid/"),
                ("--memory-limit-mib", "-1"),
                ("--memory-limit-mib", "65537"),
                ("--memory-self-test", "--self-test"),
                ("--memory-self-test", "--memory-limit-mib", "96"),
                ("https://example.invalid/one", "https://example.invalid/two"),
            )
            for args in invalid_arguments:
                with self.subTest(args=args):
                    result = self.run_probe("--report-dir", str(report), *args)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertFalse(report.exists())

    def test_existing_report_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            marker = Path(tmp) / "events.tsv"
            marker.write_text("preserve-existing-report", encoding="utf-8")
            result = self.run_probe("--report-dir", tmp)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertEqual(
                marker.read_text(encoding="utf-8"), "preserve-existing-report"
            )

    def test_no_display_is_reported_as_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "report"
            env = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="", GDK_BACKEND="x11")
            result = self.run_probe("--report-dir", str(report), env=env)
            self.assertEqual(result.returncode, 3, result.stderr)
            events_file = report / "events.tsv"
            self.assertIn(
                "display_unavailable", events_file.read_text(encoding="utf-8")
            )
            self.assertEqual(report.stat().st_mode & 0o777, 0o700)
            self.assertEqual(events_file.stat().st_mode & 0o777, 0o600)


@unittest.skipUnless(
    RUN_GUI, "set P2_RUN_GUI_TESTS=1 with Xvfb, D-Bus and a working engine sandbox"
)
class ProbeGuiTests(unittest.TestCase):
    def run_gui(self, *args):
        for tool in ("dbus-run-session", "xvfb-run"):
            self.assertIsNotNone(shutil.which(tool), "%s is required" % tool)
        self.assertTrue(BINARY.is_file(), "Build the native probe before GUI tests")
        return subprocess.run(
            ["dbus-run-session", "--", "xvfb-run", "-a", str(BINARY), *args],
            capture_output=True,
            text=True,
            timeout=45 * TIME_SCALE,
        )

    def test_local_render_and_js_form_in_both_modes(self):
        for javascript in (False, True):
            with self.subTest(javascript=javascript):
                with tempfile.TemporaryDirectory() as tmp:
                    report = Path(tmp) / "report"
                    args = [
                        "--data-dir", str(PROJECT / "fixtures"),
                        "--self-test",
                        "--report-dir", str(report),
                        "--quit-after", str(30 * TIME_SCALE),
                    ]
                    if javascript:
                        args.append("--javascript")
                    result = self.run_gui(*args)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    test_result = json.loads(
                        (report / "self-test.json").read_text(encoding="utf-8")
                    )
                    self.assertTrue(test_result["success"])
                    self.assertEqual(test_result["javascript"], javascript)
                    self.assertEqual(test_result["scope"], "local_fixture_only")
                    self.assertEqual(
                        (report / "snapshot.png").read_bytes()[:8],
                        b"\x89PNG\r\n\x1a\n",
                    )
                    events = (report / "events.tsv").read_text(encoding="utf-8")
                    self.assertIn("sandbox\tenabled", events)
                    self.assertEqual(
                        report.stat().st_mode & 0o777, 0o700
                    )

    def test_page_error_text_is_not_in_event_log(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            token = "PRIVATE-PAGE-TEXT-DO-NOT-LOG"
            page = folder / "private-page.html"
            page.write_text(
                "<!doctype html><meta charset='utf-8'>"
                "<script>throw new Error('" + token + "')</script>",
                encoding="utf-8",
            )
            report = folder / "report"
            result = self.run_gui(
                "--javascript", "--report-dir", str(report),
                "--quit-after", str(4 * TIME_SCALE), page.as_uri(),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            events = (report / "events.tsv").read_text(encoding="utf-8")
            self.assertIn("javascript_error", events)
            self.assertNotIn(token, events)
            self.assertNotIn(page.as_uri(), events)

    def test_unsupported_media_is_reported_without_page_crash_or_private_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            token = "PRIVATE-MEDIA-DO-NOT-LOG"
            page = folder / (token + ".html")
            page.write_text(
                "<!doctype html><meta charset='utf-8'>"
                "<video controls src='data:video/x-p2-unsupported;base64,AA=='></video>"
                "<script>document.querySelector('video').load()</script>",
                encoding="utf-8",
            )
            report = folder / "report"
            result = self.run_gui(
                "--javascript", "--video-compat", "--report-dir", str(report),
                "--quit-after", str(4 * TIME_SCALE), page.as_uri(),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            events = (report / "events.tsv").read_text()
            self.assertIn("video_output\tcompat_gl_sink_disabled", events)
            self.assertIn("media_error\tcode=4;kind=unsupported_source", events)
            self.assertNotIn("web_process_terminated", events)
            self.assertNotIn(token, events)
            self.assertNotIn(page.as_uri(), events)

    def test_tabs_reclaim_resume_and_text_in_both_js_modes(self):
        for javascript in (False, True):
            with self.subTest(javascript=javascript):
                with tempfile.TemporaryDirectory() as tmp:
                    report = Path(tmp) / "report"
                    args = ["--data-dir", str(PROJECT / "fixtures"),
                            "--memory-self-test", "--report-dir", str(report),
                            "--quit-after", str(30 * TIME_SCALE)]
                    if javascript:
                        args.append("--javascript")
                    result = self.run_gui(*args)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    outcome = json.loads((report / "memory-self-test.json").read_text())
                    self.assertTrue(outcome["success"])
                    self.assertEqual(outcome["tab_limit"], 5)
                    self.assertEqual(outcome["discarded"], 3)
                    self.assertTrue(outcome["resumed"])
                    self.assertTrue(outcome["text_mode"])
                    events = (report / "events.tsv").read_text()
                    for marker in ("sandbox\tenabled", "tab_limit\treached",
                                   "memory_reclaim\tbackground_tabs=3",
                                   "tab_resume\treloaded", "text_mode\tenabled_"):
                        self.assertIn(marker, events)


if __name__ == "__main__":
    unittest.main()
