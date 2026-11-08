#!/usr/bin/env python3
"""Create a small newc initramfs without root, mknod, or guest execution."""

import gzip
from pathlib import Path
import stat
import sys


def make_archive(base):
    archive = bytearray()
    inode = 0

    def add(name, mode, data=b"", rdev_major=0, rdev_minor=0):
        nonlocal inode
        inode += 1
        encoded_name = name.encode("ascii") + b"\0"
        fields = (
            inode, mode, 0, 0, 1, 0, len(data),
            0, 0, rdev_major, rdev_minor, len(encoded_name), 0,
        )
        archive.extend(b"070701" + b"".join(f"{v:08x}".encode("ascii") for v in fields))
        archive.extend(encoded_name)
        archive.extend(b"\0" * (-len(archive) % 4))
        archive.extend(data)
        archive.extend(b"\0" * (-len(archive) % 4))

    for directory in ("bin", "dev", "etc", "proc", "sys", "tmp"):
        add(directory, stat.S_IFDIR | (0o1777 if directory == "tmp" else 0o755))
    add("dev/console", stat.S_IFCHR | 0o600, rdev_major=5, rdev_minor=1)
    add("bin/busybox", stat.S_IFREG | 0o755, (base / "busybox").read_bytes())
    for applet in ("sh", "awk", "cat", "date", "mkdir", "mount", "poweroff", "uname"):
        add("bin/" + applet, stat.S_IFLNK | 0o777, b"busybox")
    add("init", stat.S_IFREG | 0o755, (base / "init").read_bytes())
    add("collect-report.sh", stat.S_IFREG | 0o755, (base / "collect-report.sh").read_bytes())
    add("packages.txt", stat.S_IFREG | 0o644, (base / "packages.txt").read_bytes())
    add("etc/os-release", stat.S_IFREG | 0o644, (
        b'NAME="P2 boot-check initramfs"\n'
        b'ID=p2-boot-check\n'
        b'PRETTY_NAME="P2 boot-check (Debian 12 kernel and BusyBox packages)"\n'
    ))
    add("TRAILER!!!", 0)
    return bytes(archive)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: build-initramfs.py OUTPUT.cpio.gz")
    base = Path(__file__).resolve().parent
    output = Path(sys.argv[1])
    with output.open("wb") as stream:
        with gzip.GzipFile(filename="", fileobj=stream, mode="wb", mtime=0) as compressed:
            compressed.write(make_archive(base))
