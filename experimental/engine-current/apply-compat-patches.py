#!/usr/bin/env python3
"""Exclude disabled libpas units on GCC i386/system-malloc builds.

The upstream allocator retains its Apple/BSD license. No live allocator
semantics change: libpas is already disabled on ADDRESS32 in pas_config.h.
"""
from pathlib import Path
import sys

source = Path(sys.argv[1])
target = source / "Source/bmalloc/CMakeLists.txt"
original = target.read_text()
marker = "# P2_COMPAT_LIBPAS_ADDRESS32_GUARD"
if marker not in original:
    needle = 'WEBKIT_FRAMEWORK_DECLARE(bmalloc)\n'
    if original.count(needle) != 1:
        raise SystemExit("Unexpected upstream bmalloc CMake file; review the patch")
    block = (marker + '\n'
             'if (USE_SYSTEM_MALLOC AND CMAKE_SIZEOF_VOID_P EQUAL 4)\n'
             '    list(FILTER bmalloc_SOURCES EXCLUDE REGEX "^libpas/")\n'
             '    set(bmalloc_C_SOURCES "")\n'
             'endif ()\n\n')
    original = original.replace(needle, block + needle)
    target.write_text(original)
print("Applied/reused disabled-libpas ADDRESS32/system-malloc source filter")

# pas_process_is_shutting_down() returns a constant false outside Windows.
# Its unconditional header inclusion pulls the 64-bit-only libpas pair type
# into WTF's i386 system-malloc build. Preserve Windows behavior, avoid the
# unused dependency elsewhere.
threading = source / "Source/WTF/wtf/Threading.cpp"
text = threading.read_text()
thread_marker = "// P2_COMPAT_UNIX_PROCESS_EXIT"
if thread_marker not in text:
    include = '#include <bmalloc/pas_process.h>\n'
    call = '    if (pas_process_is_shutting_down())\n        return;\n'
    if text.count(include) != 1 or text.count(call) != 1:
        raise SystemExit("Unexpected upstream Threading.cpp; review the patch")
    text = text.replace(include, thread_marker + '\n#if OS(WINDOWS)\n' + include + '#endif\n')
    text = text.replace(call, '#if OS(WINDOWS)\n' + call + '#endif\n')
    threading.write_text(text)
print("Applied/reused Unix thread-exit libpas dependency guard")
