#!/bin/sh
# Invoke inside this directory's Docker image, with the repository at /work.
set -eu
export LC_ALL=C
p2_root=/work
p2_source=$p2_root/build/engine-current/webkitgtk-2.54.2
p2_build=$p2_root/build/engine-current/jsc-i386
test -s "$p2_source/CMakeLists.txt"
mkdir -p "$p2_build"
python3 "$p2_root/experimental/engine-current/apply-compat-patches.py" "$p2_source"
cmake -S "$p2_source" -B "$p2_build" -G Ninja \
    -DPORT=JSCOnly \
    -DCMAKE_TOOLCHAIN_FILE="$p2_root/experimental/engine-current/pentium2.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE='-O1 -DNDEBUG -g0' \
    -DCMAKE_CXX_FLAGS_RELEASE='-O1 -DNDEBUG -g0' \
    -DENABLE_C_LOOP=ON -DENABLE_JIT=OFF -DENABLE_DFG_JIT=OFF \
    -DENABLE_FTL_JIT=OFF -DENABLE_WEBASSEMBLY=OFF \
    -DENABLE_WEBASSEMBLY_BBQJIT=OFF -DENABLE_WEBASSEMBLY_OMGJIT=OFF \
    -DUSE_SYSTEM_MALLOC=ON -DUSE_SYSTEM_UNIFDEF=ON \
    -DENABLE_UNIFIED_BUILDS=ON -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
    -DDEVELOPER_MODE=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
printf '\nConfigured JSC-only. Building jsc with two jobs (60-minute bound).\n'
timeout --kill-after=10 3600 cmake --build "$p2_build" --target jsc --parallel 2
file "$p2_build/bin/jsc"
LD_LIBRARY_PATH="$p2_build/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    qemu-i386 -cpu pentium2 "$p2_build/bin/jsc" \
    "$p2_root/experimental/engine-current/smoke.js"
