"""Artifact privacy, native ABI dependencies, and Debian package compatibility."""

import hashlib
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[1]
VERSION = "0.1.1"


@unittest.skipUnless(shutil.which("dpkg-deb") and shutil.which("tar"), "Debian packaging tools required")
class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.project = Path(self.tmp.name) / "project with spaces"
        for folder in ("packaging", "tools", "fixtures", "src", "docs", "build", "dist"):
            (self.project / folder).mkdir(parents=True)
        for file in ("build-deb.sh", "p2browser-probe.desktop"):
            shutil.copy(PROJECT / "packaging" / file, self.project / "packaging" / file)
        for script in ("collect-report.sh", "monitor.sh"):
            (self.project / "tools" / script).write_text("#!/bin/sh\nprintf '%s\\n' diagnostic-fixture\n")
        (self.project / "fixtures/start.html").write_text("<!doctype html><title>Fixture</title>")
        (self.project / "README.md").write_text("Source fixture\n")
        self.env = dict(os.environ, VERSION=VERSION, SOURCE_DATE_EPOCH="1794096000")

    def run_builder(self, mode, *, check=True, env=None):
        return subprocess.run(
            ["sh", str(self.project / "packaging/build-deb.sh"), mode],
            env=env or self.env,
            text=True,
            capture_output=True,
            check=check,
        )

    def compile_fixture(self):
        source = self.project / "src/main.c"
        source.write_text("int main(void) { return 0; }\n")
        binary = self.project / "build/p2browser-probe"
        subprocess.run(["cc", "-o", str(binary), str(source)], check=True)
        return binary

    def package_field(self, package, field):
        return subprocess.check_output(["dpkg-deb", "-f", str(package), field], text=True).strip()

    def test_diagnostics_has_no_gui_or_python_dependency_and_files_are_root_owned(self):
        self.run_builder("diagnostics")
        package = self.project / f"dist/p2browser-diagnostics_{VERSION}_all.deb"
        self.assertEqual(self.package_field(package, "Architecture"), "all")
        dependencies = self.package_field(package, "Depends")
        self.assertNotIn("webkit", dependencies)
        self.assertNotIn("python", dependencies)
        payload = subprocess.check_output(["dpkg-deb", "--fsys-tarfile", str(package)])
        with tarfile.open(fileobj=io.BytesIO(payload)) as archive:
            members = {member.name: member for member in archive.getmembers()}
            for name in ("./usr/bin/p2browser-collect-report", "./usr/bin/p2browser-monitor"):
                self.assertIn(name, members)
                self.assertEqual(members[name].mode, 0o755)
            self.assertTrue(all(member.uid == 0 and member.gid == 0 for member in members.values()))

    @unittest.skipUnless(shutil.which("ar"), "binutils ar required")
    def test_packages_have_xz_members_and_no_maintainer_scripts(self):
        self.run_builder("diagnostics")
        package = self.project / f"dist/p2browser-diagnostics_{VERSION}_all.deb"
        members = subprocess.check_output(["ar", "t", str(package)], text=True).splitlines()
        self.assertIn("control.tar.xz", members)
        self.assertIn("data.tar.xz", members)
        self.assertFalse(any(name.endswith(".zst") for name in members))
        control_payload = subprocess.check_output(["dpkg-deb", "--ctrl-tarfile", str(package)])
        with tarfile.open(fileobj=io.BytesIO(control_payload)) as archive:
            names = {Path(name).name for name in archive.getnames()}
            self.assertFalse(names.intersection({"preinst", "postinst", "prerm", "postrm", "config", "triggers"}))

    def test_source_excludes_private_and_generated_files(self):
        (self.project / "build/private-report.txt").write_text("PRIVATE-REPORT")
        (self.project / "dist/old-binary.deb").write_text("BINARY")
        for path in (self.project / ".env", self.project / "src/.env.local", self.project / "docs/.env.secret"):
            path.write_text("SECRET")
            path.chmod(0)
        (self.project / "tools/__pycache__").mkdir()
        (self.project / "tools/__pycache__/generated.pyc").write_bytes(b"PYC")
        external = Path(self.tmp.name) / "external-secret"
        external.write_text("OUTSIDE-SOURCE")
        (self.project / "docs/link").symlink_to(external)
        self.run_builder("source")
        archive_path = self.project / f"dist/p2browser-source_{VERSION}.tar.gz"
        with tarfile.open(archive_path) as archive:
            names = archive.getnames()
            prefix = f"p2browser-{VERSION}"
            self.assertIn(f"{prefix}/README.md", names)
            self.assertTrue(all(name == prefix or name.startswith(prefix + "/") for name in names))
            self.assertFalse(any(".env" in Path(name).name or "/build/" in name or "/dist/" in name or "__pycache__" in name for name in names))
            self.assertTrue(archive.getmember(f"{prefix}/docs/link").issym())
            self.assertFalse(any(member.isfile() and b"OUTSIDE-SOURCE" in archive.extractfile(member).read() for member in archive.getmembers()))

    def test_source_is_reproducible_despite_input_mtime_changes(self):
        self.run_builder("source")
        archive = self.project / f"dist/p2browser-source_{VERSION}.tar.gz"
        before = hashlib.sha256(archive.read_bytes()).hexdigest()
        os.utime(self.project / "README.md", (1800000000, 1800000000))
        self.run_builder("source")
        self.assertEqual(before, hashlib.sha256(archive.read_bytes()).hexdigest())

    def test_invalid_version_is_rejected_before_packaging(self):
        env = dict(self.env, VERSION="0.1;touch should-not-exist")
        result = self.run_builder("diagnostics", check=False, env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid VERSION", result.stderr)
        self.assertEqual(list((self.project / "dist").iterdir()), [])

    @unittest.skipUnless(shutil.which("cc") and shutil.which("dpkg-shlibdeps") and shutil.which("readelf"), "Native compiler and dpkg-dev required")
    def test_probe_metadata_matches_actual_elf_and_build_suite(self):
        self.compile_fixture()
        self.run_builder("all")
        packages = list((self.project / "dist").glob(f"p2browser-probe_{VERSION}_amd64-*.deb"))
        self.assertEqual(len(packages), 1)
        package = packages[0]
        self.assertEqual(self.package_field(package, "Architecture"), "amd64")
        self.assertIn(f"p2browser-diagnostics (>= {VERSION})", self.package_field(package, "Depends"))
        payload = subprocess.check_output(["dpkg-deb", "--fsys-tarfile", str(package)])
        with tarfile.open(fileobj=io.BytesIO(payload)) as archive:
            info = archive.extractfile("./usr/share/doc/p2browser-probe/build-info.txt").read().decode()
            suite = re.search(r"^Build suite: (.+)$", info, re.M).group(1)
            self.assertTrue(package.name.endswith(f"_amd64-{suite}.deb"))
            self.assertIn("./usr/share/p2browser-probe/start.html", archive.getnames())

    @unittest.skipUnless(shutil.which("cc") and shutil.which("dpkg-shlibdeps") and shutil.which("dpkg-query"), "Native compiler and Debian metadata required")
    def test_native_dependencies_use_symbol_minima_not_full_installed_versions(self):
        self.compile_fixture()
        self.run_builder("probe")
        package = next((self.project / "dist").glob("p2browser-probe_*.deb"))
        dependencies = self.package_field(package, "Depends")
        required = re.search(r"libc6 \(>= ([^)]+)\)", dependencies)
        self.assertIsNotNone(required)
        installed = subprocess.check_output(["dpkg-query", "-W", "-f=${Version}", "libc6:amd64"], text=True).strip()
        self.assertNotEqual(required.group(1), installed)
        comparison = subprocess.run(["dpkg", "--compare-versions", required.group(1), "lt", installed])
        self.assertEqual(comparison.returncode, 0)

    @unittest.skipUnless(shutil.which("cc") and shutil.which("readelf"), "C compiler and readelf required")
    def test_other_elf_architectures_are_not_mislabeled_as_amd64(self):
        binary = self.compile_fixture()
        data = bytearray(binary.read_bytes())
        self.assertEqual(data[:4], b"\x7fELF")
        self.assertEqual(data[5], 1)  # little-endian test fixture
        data[18:20] = b"\x03\x00"  # EM_386
        binary.write_bytes(data)
        result = self.run_builder("probe", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native amd64 only", result.stderr)
        self.assertEqual(list((self.project / "dist").glob("p2browser-probe_*.deb")), [])

    @unittest.skipUnless(shutil.which("cc") and shutil.which("readelf"), "C compiler and readelf required")
    def test_dependency_metadata_errors_have_no_guessed_fallback(self):
        self.compile_fixture()
        fake_bin = self.project / "fake-bin"
        fake_bin.mkdir()
        tool = fake_bin / "dpkg-shlibdeps"
        tool.write_text("#!/bin/sh\nprintf '%s\\n' 'metadata fixture failure' >&2\nexit 42\n")
        tool.chmod(0o755)
        env = dict(self.env, PATH=str(fake_bin) + os.pathsep + os.environ.get("PATH", ""))
        result = self.run_builder("probe", check=False, env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("metadata fixture failure", result.stderr)
        self.assertEqual(list((self.project / "dist").glob("p2browser-probe_*.deb")), [])


if __name__ == "__main__":
    unittest.main()
