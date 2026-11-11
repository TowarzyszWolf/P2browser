"""Opt-in GUI regression: a killed local WebProcess must not kill the UI.

Uses an ordinary user's own browser descendants, Xvfb and XTest. No public
website, decoder, real OOM, or external account participates in this test.
"""

import ctypes
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest


PROJECT = Path(__file__).resolve().parents[1]
BINARY = PROJECT / "build/p2browser-probe"
RUN_GUI = os.environ.get("P2_RUN_GUI_TESTS") == "1"
TIME_SCALE = int(os.environ.get("P2_GUI_TEST_TIME_SCALE", "1"))
if not 1 <= TIME_SCALE <= 10:
    raise ValueError("P2_GUI_TEST_TIME_SCALE must be between 1 and 10")


def read_events(report):
    try:
        lines = (report / "events.tsv").read_text().splitlines()[1:]
    except FileNotFoundError:
        return []
    return [line.split("\t", 2) for line in lines if len(line.split("\t", 2)) == 3]


def detail_fields(event):
    return dict(part.split("=", 1) for part in event[2].split(";") if "=" in part)


def process_tree(root):
    """Return discovered descendants with PID identity, without command lines."""
    records = {}
    for path in Path("/proc").iterdir():
        if not path.name.isdecimal():
            continue
        try:
            text = (path / "stat").read_text()
            end = text.rfind(")")
            rest = text[end + 2:].split()
            records[int(path.name)] = (int(rest[1]), int(rest[19]))
        except (OSError, IndexError, ValueError):
            pass
    result = {}
    parents = {root}
    while parents:
        children = {pid for pid, (parent, _) in records.items()
                    if parent in parents and pid not in result and pid != root}
        result.update({pid: records[pid] for pid in children})
        parents = children
    return result


def web_processes(root):
    workers = {}
    for pid, identity in process_tree(root).items():
        try:
            # Linux comm is limited to 15 bytes; accept its truncated form.
            name = Path(f"/proc/{pid}/comm").read_text().strip()
        except OSError:
            continue
        if name.startswith("WebKitWebProc"):
            workers[pid] = identity
    return workers


class XInput:
    """Real pointer events and ICCCM close, without a window manager."""

    def __init__(self):
        self.x11 = ctypes.CDLL("libX11.so.6")
        self.xtst = ctypes.CDLL("libXtst.so.6")
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        self.x11.XDefaultRootWindow.restype = ctypes.c_ulong
        self.x11.XFlush.argtypes = [ctypes.c_void_p]
        self.x11.XFree.argtypes = [ctypes.c_void_p]
        self.x11.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_uint)]
        self.x11.XGetWindowProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
            ctypes.c_ulong, ctypes.c_long, ctypes.c_long, ctypes.c_int, ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))]
        self.x11.XGetGeometry.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint),
            ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint),
            ctypes.POINTER(ctypes.c_uint)]
        self.x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.x11.XInternAtom.restype = ctypes.c_ulong
        self.x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XKeysymToKeycode.restype = ctypes.c_uint
        self.xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                                  ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
        self.xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                                  ctypes.c_int, ctypes.c_ulong]
        self.xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                               ctypes.c_int, ctypes.c_ulong]
        self.display = self.x11.XOpenDisplay(os.environ["DISPLAY"].encode())
        if not self.display:
            raise AssertionError("Cannot open the private Xvfb display")

    def click(self, x, y):
        if not self.xtst.XTestFakeMotionEvent(self.display, -1, x, y, 0):
            raise AssertionError("XTest pointer motion failed")
        self.xtst.XTestFakeButtonEvent(self.display, 1, 1, 0)
        self.xtst.XTestFakeButtonEvent(self.display, 1, 0, 0)
        self.x11.XFlush(self.display)

    def next_tab(self):
        # Focus the first notebook header, then use GTK's ordinary binding.
        self.click(20, 92)
        control = self.x11.XKeysymToKeycode(self.display, 0xffe3)
        page_down = self.x11.XKeysymToKeycode(self.display, 0xff56)
        self.xtst.XTestFakeKeyEvent(self.display, control, 1, 0)
        self.xtst.XTestFakeKeyEvent(self.display, page_down, 1, 0)
        self.xtst.XTestFakeKeyEvent(self.display, page_down, 0, 0)
        self.xtst.XTestFakeKeyEvent(self.display, control, 0, 0)
        self.x11.XFlush(self.display)

    def close_browser_window(self, browser_pid):
        parent = ctypes.c_ulong()
        root_return = ctypes.c_ulong()
        children = ctypes.POINTER(ctypes.c_ulong)()
        count = ctypes.c_uint()
        root = self.x11.XDefaultRootWindow(self.display)
        if not self.x11.XQueryTree(self.display, root, ctypes.byref(root_return),
                                  ctypes.byref(parent), ctypes.byref(children), ctypes.byref(count)):
            raise AssertionError("Cannot list private Xvfb windows")
        window = None
        pid_atom = self.x11.XInternAtom(self.display, b"_NET_WM_PID", 0)
        try:
            for candidate in list(children[:count.value]):
                actual_type = ctypes.c_ulong()
                actual_format = ctypes.c_int()
                items = ctypes.c_ulong()
                after = ctypes.c_ulong()
                data = ctypes.POINTER(ctypes.c_ubyte)()
                status = self.x11.XGetWindowProperty(self.display, candidate, pid_atom,
                    0, 1, 0, 0, ctypes.byref(actual_type), ctypes.byref(actual_format),
                    ctypes.byref(items), ctypes.byref(after), ctypes.byref(data))
                owner = None
                try:
                    if status == 0 and data and items.value == 1 and actual_format.value == 32:
                        owner = ctypes.cast(data, ctypes.POINTER(ctypes.c_ulong))[0]
                finally:
                    if data:
                        self.x11.XFree(data)
                if owner != browser_pid:
                    continue
                geometry_root = ctypes.c_ulong()
                x = ctypes.c_int()
                y = ctypes.c_int()
                width = ctypes.c_uint()
                height = ctypes.c_uint()
                border = ctypes.c_uint()
                depth = ctypes.c_uint()
                if self.x11.XGetGeometry(self.display, candidate, ctypes.byref(geometry_root),
                    ctypes.byref(x), ctypes.byref(y), ctypes.byref(width), ctypes.byref(height),
                    ctypes.byref(border), ctypes.byref(depth)) and width.value > 500 and height.value > 300:
                    window = candidate
                    break
        finally:
            if children:
                self.x11.XFree(children)
        if window is None:
            raise AssertionError("Browser window disappeared")

        class MessageData(ctypes.Union):
            _fields_ = [("b", ctypes.c_char * 20), ("s", ctypes.c_short * 10),
                        ("l", ctypes.c_long * 5)]

        class ClientMessage(ctypes.Structure):
            _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong),
                        ("send_event", ctypes.c_int), ("display", ctypes.c_void_p),
                        ("window", ctypes.c_ulong), ("message_type", ctypes.c_ulong),
                        ("format", ctypes.c_int), ("data", MessageData)]

        class Event(ctypes.Union):
            _fields_ = [("message", ClientMessage), ("pad", ctypes.c_long * 24)]

        event = Event()
        event.message.type = 33  # ClientMessage
        event.message.send_event = 1
        event.message.display = self.display
        event.message.window = window
        event.message.message_type = self.x11.XInternAtom(self.display, b"WM_PROTOCOLS", 0)
        event.message.format = 32
        event.message.data.l[0] = self.x11.XInternAtom(self.display, b"WM_DELETE_WINDOW", 0)
        self.x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int,
                                        ctypes.c_long, ctypes.POINTER(Event)]
        if not self.x11.XSendEvent(self.display, window, 0, 0, ctypes.byref(event)):
            raise AssertionError("Cannot send a normal window-close request")
        self.x11.XFlush(self.display)


def run_driver(folder, background=False):
    report = folder / "report"
    fixture = folder / "local.html"
    fixture.write_text("<!doctype html><meta charset='utf-8'>"
                       "<title>P2CRASH:ready</title><h1>Local recovery fixture</h1>")
    xinput = XInput()
    with (folder / "browser.log").open("w") as log:
        command = [str(BINARY), "--report-dir", str(report),
            "--quit-after", str(120 * TIME_SCALE), "--fixture-ready-title", "P2CRASH:ready",
            fixture.as_uri()]
        if background:
            command.extend(["--background-tab", fixture.as_uri()])
        browser = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)

        def wait_until(predicate, description, seconds=15):
            deadline = time.monotonic() + seconds * TIME_SCALE
            while time.monotonic() < deadline:
                events = read_events(report)
                if predicate(events):
                    return events
                if browser.poll() is not None:
                    raise AssertionError(f"UI exited before {description}, rc={browser.returncode}")
                time.sleep(.05)
            raise AssertionError(f"Timed out waiting for {description}: {read_events(report)}")

        try:
            first = wait_until(lambda events: any(row[1:] == ["fixture_ready", "matched"]
                                                  for row in events), "initial local load")
            if not any(row[1:] == ["sandbox", "enabled"] for row in first):
                raise AssertionError("Sandbox was not enabled")
            if background:
                wait_until(lambda events: any(row[1:] == ["tab_load", "finished"]
                                              for row in events), "background local load")
                # Remove the foreground WebView without a PID→tab assumption:
                # all remaining live WebViews are now in the background.
                xinput.click(310, 55)
                wait_until(lambda events: any(row[1] == "text_mode" and
                           row[2].startswith("enabled_") for row in events),
                           "foreground text view")
            workers = web_processes(browser.pid)
            if (not background and len(workers) != 1) or not workers:
                raise AssertionError(f"Expected one unambiguously owned WebProcess, found {workers}")
            for pid, identity in workers.items():
                if web_processes(browser.pid).get(pid) == identity:
                    os.kill(pid, signal.SIGKILL)
            expected_active = "0" if background else "1"
            crashed = wait_until(lambda events: any(row[1] == "web_process_terminated" and
                detail_fields(row).get("kind") == "crashed" and
                detail_fields(row).get("active") == expected_active for row in events),
                "named crash event")
            if background and any(row[1] == "web_process_terminated" and
                                  detail_fields(row).get("active") == "1" for row in crashed):
                raise AssertionError("Background crash affected the foreground text view")
            wait_until(lambda events: any(row[1] == "web_process_recovery" and
                       detail_fields(row).get("action") == "notice_shown" for row in events),
                       "native recovery notice")
            time.sleep(.3)
            events = read_events(report)
            if browser.poll() is not None:
                raise AssertionError("The UI exited after its WebProcess died")
            if any(row[1] == "web_process_recovery" and
                   detail_fields(row).get("action") == "reload_requested" for row in events):
                raise AssertionError("The page was reloaded without a user action")
            if sum(row[1:] == ["fixture_ready", "matched"] for row in events) != 1:
                raise AssertionError("The crashed document was automatically reopened")
            if background:
                xinput.next_tab()
                time.sleep(.3)
                if any(row[1] == "web_process_recovery" and
                       detail_fields(row).get("action") == "reload_requested"
                       for row in read_events(report)):
                    raise AssertionError("Selecting the crashed tab automatically reloaded it")
            # Ordinary top-toolbar Odśwież button in the 1000×740 test window.
            xinput.click(200, 18)
            recovered = wait_until(lambda events: sum(row[1:] == ["fixture_ready", "matched"]
                                                       for row in events) >= 2,
                                   "explicit reload and recovered local document")
            if not any(row[1] == "web_process_recovery" and
                       detail_fields(row).get("action") == "reload_requested" for row in recovered):
                raise AssertionError("Manual recovery was not recorded")
            if set(workers) & set(web_processes(browser.pid)):
                raise AssertionError("Killed WebProcess unexpectedly reused")
            xinput.close_browser_window(browser.pid)
            rc = browser.wait(timeout=15 * TIME_SCALE)
            if rc != 0:
                raise AssertionError(f"Normal window close failed, rc={rc}")
            print(json.dumps({"ui_survived": True, "named_crash": True,
                              "background": background,
                              "manual_reload": True, "fixture_loaded_twice": True,
                              "normal_close": True}))
        finally:
            if browser.poll() is None:
                browser.terminate()
                try:
                    browser.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    browser.kill()
                    browser.wait(timeout=5)


@unittest.skipUnless(RUN_GUI, "set P2_RUN_GUI_TESTS=1 in a prepared GUI test environment")
class CrashRecoveryGuiTests(unittest.TestCase):
    def run_case(self, scenario):
        for tool in ("dbus-run-session", "xvfb-run"):
            self.assertIsNotNone(shutil.which(tool), f"{tool} is required")
        self.assertTrue(BINARY.is_file(), "Build the native probe first")
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run(["dbus-run-session", "--", "xvfb-run", "-a",
                sys.executable, str(Path(__file__).resolve()), "--driver", temporary, scenario],
                capture_output=True, text=True, timeout=90 * TIME_SCALE)
            folder = Path(temporary)
            log = (folder / "browser.log").read_text() if (folder / "browser.log").exists() else ""
            self.assertEqual(result.returncode, 0, result.stderr + "\n" + log)
            self.assertIn('"fixture_loaded_twice": true', result.stdout)
            self.assertIn('"normal_close": true', result.stdout)

    def test_killed_web_process_keeps_ui_and_allows_manual_reload(self):
        self.run_case("active")

    def test_background_crash_preserves_foreground_text_and_waits_for_manual_reload(self):
        self.run_case("background")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--driver":
        run_driver(Path(sys.argv[2]), background=sys.argv[3] == "background")
    else:
        unittest.main()
