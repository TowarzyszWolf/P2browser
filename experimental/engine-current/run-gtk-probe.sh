#!/bin/sh
set -eu
export LC_ALL=C
export PKG_CONFIG_SYSROOT_DIR=/opt/p2-gtk-sysroot
export PKG_CONFIG_LIBDIR=/opt/p2-gtk-sysroot/usr/lib/i386-linux-gnu/pkgconfig:/opt/p2-gtk-sysroot/usr/share/pkgconfig
p2_source=/work/build/engine-current/webkitgtk-2.54.2
p2_build=/work/build/engine-current/gtk-i386
python3 /work/experimental/engine-current/apply-compat-patches.py "$p2_source"
cmake -S "$p2_source" -B "$p2_build" -G Ninja \
    -DPORT=GTK -DUSE_GTK4=OFF \
    -DCMAKE_TOOLCHAIN_FILE=/work/experimental/engine-current/pentium2.cmake \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE='-O1 -DNDEBUG -g0' \
    -DCMAKE_CXX_FLAGS_RELEASE='-O1 -DNDEBUG -g0' \
    -DCMAKE_SYSROOT=/opt/p2-gtk-sysroot \
    -DCMAKE_LIBRARY_PATH=/opt/p2-gtk-sysroot/usr/lib/i386-linux-gnu \
    -DICU_INCLUDE_DIR=/opt/p2-gtk-sysroot/usr/include \
    -DICU_DATA_LIBRARY_RELEASE=/opt/p2-gtk-sysroot/usr/lib/i386-linux-gnu/libicudata.so \
    -DICU_I18N_LIBRARY_RELEASE=/opt/p2-gtk-sysroot/usr/lib/i386-linux-gnu/libicui18n.so \
    -DICU_UC_LIBRARY_RELEASE=/opt/p2-gtk-sysroot/usr/lib/i386-linux-gnu/libicuuc.so \
    -DCMAKE_EXE_LINKER_FLAGS='-m32 -Wl,-rpath-link,/opt/p2-gtk-sysroot/usr/lib/i386-linux-gnu:/opt/p2-gtk-sysroot/lib/i386-linux-gnu' \
    -DCMAKE_SHARED_LINKER_FLAGS='-m32 -Wl,-rpath-link,/opt/p2-gtk-sysroot/usr/lib/i386-linux-gnu:/opt/p2-gtk-sysroot/lib/i386-linux-gnu' \
    -DBWRAP_EXECUTABLE=/usr/bin/bwrap -DDBUS_PROXY_EXECUTABLE=/usr/bin/xdg-dbus-proxy \
    -DENABLE_C_LOOP=ON -DENABLE_JIT=OFF -DENABLE_DFG_JIT=OFF -DENABLE_FTL_JIT=OFF \
    -DENABLE_WEBASSEMBLY=OFF -DENABLE_WEBASSEMBLY_BBQJIT=OFF -DENABLE_WEBASSEMBLY_OMGJIT=OFF \
    -DUSE_SYSTEM_MALLOC=ON -DUSE_SYSTEM_UNIFDEF=ON \
    -DENABLE_INTROSPECTION=OFF -DENABLE_DOCUMENTATION=OFF \
    -DENABLE_JOURNALD_LOG=OFF -DENABLE_MINIBROWSER=OFF -DENABLE_WEBDRIVER=OFF \
    -DENABLE_VIDEO=OFF -DENABLE_WEB_AUDIO=OFF -DENABLE_WEBGL=OFF \
    -DUSE_GSTREAMER=OFF -DENABLE_MEDIA_STREAM=OFF -DENABLE_WEB_RTC=OFF \
    -DENABLE_WEB_CODECS=OFF -DENABLE_MEDIA_RECORDER=OFF \
    -DENABLE_GAMEPAD=OFF -DENABLE_SPEECH_SYNTHESIS=OFF -DENABLE_SPELLCHECK=OFF \
    -DENABLE_WAYLAND_TARGET=OFF -DENABLE_X11_TARGET=ON -DENABLE_QUARTZ_TARGET=OFF \
    -DUSE_GBM=OFF -DUSE_LIBDRM=OFF -DUSE_VULKAN=OFF -DUSE_LIBBACKTRACE=OFF \
    -DUSE_LIBHYPHEN=OFF -DUSE_LIBSECRET=OFF -DUSE_SKIA_OPENTYPE_SVG=OFF \
    -DUSE_JPEGXL=OFF -DUSE_AVIF=OFF -DUSE_WOFF2=OFF -DUSE_LCMS=OFF \
    -DUSE_SYSPROF_CAPTURE=OFF -DUSE_SYSTEM_SYSPROF_CAPTURE=OFF \
    -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON -DDEVELOPER_MODE=OFF \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
printf '\nGTK configuration completed; attempting a bounded renderer build.\n'
timeout --kill-after=10 300 cmake --build "$p2_build" --target WebCore --parallel 2
