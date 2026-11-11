#!/bin/sh
# Package locally as an ordinary user; no installation, downloads or maintainer scripts.
set -eu
umask 022

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=${VERSION:-0.2.0~alpha1}
epoch=${SOURCE_DATE_EPOCH:-1794355200}
mode=${1:-all}
case "$version" in ''|*[!0-9A-Za-z.+:~_-]*) printf '%s\n' 'Invalid VERSION' >&2; exit 2 ;; esac
case "$epoch" in ''|*[!0-9]*) printf '%s\n' 'Invalid SOURCE_DATE_EPOCH' >&2; exit 2 ;; esac
case "$mode" in all|probe|diagnostics|source) ;; *) printf '%s\n' 'Usage: build-deb.sh [all|probe|diagnostics|source]' >&2; exit 2 ;; esac
export SOURCE_DATE_EPOCH="$epoch"

mkdir -p "$project_dir/build" "$project_dir/dist"
work_dir=$(mktemp -d "$project_dir/build/package.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

suite=unknown
if [ -r /etc/os-release ]; then
    detected_suite=$(sed -n 's/^VERSION_CODENAME=//p' /etc/os-release | tr -d '"' | head -n 1)
    detected_suite=$(printf '%s' "$detected_suite" | tr -cd 'a-zA-Z0-9.+_-')
    [ -z "$detected_suite" ] || suite=$detected_suite
fi

write_control() {
    package=$1
    architecture=$2
    depends=$3
    description=$4
    install -d -m 755 "$work_dir/$package/DEBIAN"
    cat > "$work_dir/$package/DEBIAN/control" <<EOF
Package: $package
Version: $version
Section: devel
Priority: optional
Architecture: $architecture
Maintainer: P2Browser contributors <noreply@example.invalid>
Depends: $depends
Description: $description
 Experimental development tools for the P2Browser project.
 This package does not certify engine compatibility or resource usage on P2.
EOF
    chmod 644 "$work_dir/$package/DEBIAN/control"
}

copy_docs() {
    package=$1
    doc_dir="$work_dir/$package/usr/share/doc/$package"
    install -d -m 755 "$doc_dir"
    for doc in README.md LICENSE COPYING NOTICE THIRD_PARTY.md; do
        if [ -f "$project_dir/$doc" ]; then
            install -m 644 "$project_dir/$doc" "$doc_dir/$doc"
        fi
    done
    if [ -d "$project_dir/docs" ]; then
        install -d -m 755 "$doc_dir/docs"
        for doc in "$project_dir/docs/"*.md; do
            [ ! -f "$doc" ] || install -m 644 "$doc" "$doc_dir/docs/"
        done
        for result in "$project_dir/docs/results/"*.tsv "$project_dir/docs/results/"*-profile.json; do
            if [ -f "$result" ]; then
                install -d -m 755 "$doc_dir/docs/results"
                install -m 644 "$result" "$doc_dir/docs/results/"
            fi
        done
    fi
    cat > "$doc_dir/build-info.txt" <<EOF
Package: $package
Version: $version
Build suite: $suite
Source date epoch: $epoch
EOF
}

finish_package() {
    package=$1
    architecture=$2
    tag=$3
    stage="$work_dir/$package"
    find "$stage" -exec touch -h -d "@$epoch" {} +
    # xz control/data members can also be inspected by older dpkg releases.
    dpkg-deb --root-owner-group -Zxz --build "$stage" \
        "$project_dir/dist/${package}_${version}_${architecture}${tag}.deb"
}

build_diagnostics() {
    package=p2browser-diagnostics
    write_control "$package" all 'awk, coreutils, grep, sed' \
        'Privacy-conscious diagnostic tools for an experimental P2 browser'
    stage="$work_dir/$package"
    install -d -m 755 "$stage/usr/bin"
    install -m 755 "$project_dir/tools/collect-report.sh" "$stage/usr/bin/p2browser-collect-report"
    install -m 755 "$project_dir/tools/monitor.sh" "$stage/usr/bin/p2browser-monitor"
    copy_docs "$package"
    finish_package "$package" all ''
}

binary_architecture() {
    machine=$(LC_ALL=C readelf -h "$project_dir/build/p2browser-probe" | sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')
    case "$machine" in
        'Advanced Micro Devices X86-64') printf amd64 ;;
        *) printf 'This release packages native amd64 only; ELF machine is: %s\n' "$machine" >&2; exit 2 ;;
    esac
}

probe_dependencies() {
    command -v dpkg-shlibdeps >/dev/null 2>&1 \
        || { printf '%s\n' 'dpkg-shlibdeps is required: install dpkg-dev on the native build system.' >&2; exit 1; }
    install -d -m 755 "$work_dir/shlibdeps/debian"
    cat > "$work_dir/shlibdeps/debian/control" <<EOF
Source: p2browser-probe
Section: devel
Priority: optional
Maintainer: P2Browser contributors <noreply@example.invalid>

Package: p2browser-probe
Architecture: amd64
Description: Experimental browser engine probe
 Development build.
EOF
    # Use installed native symbols/shlibs metadata: no cross-architecture
    # manifest, guessed package names, or full build-package version minima.
    install -d -m 755 "$work_dir/shlibdeps/debian/p2browser-probe/usr/bin"
    install -m 755 "$project_dir/build/p2browser-probe" \
        "$work_dir/shlibdeps/debian/p2browser-probe/usr/bin/p2browser-probe"
    dependencies=$(cd "$work_dir/shlibdeps" && dpkg-shlibdeps -O \
        -e debian/p2browser-probe/usr/bin/p2browser-probe)
    dependencies=$(printf '%s\n' "$dependencies" | sed -n 's/^shlibs:Depends=//p')
    [ -n "$dependencies" ] || { printf '%s\n' 'dpkg-shlibdeps returned no runtime dependencies.' >&2; exit 1; }
    printf '%s' "$dependencies"
}

build_probe() {
    [ -f "$project_dir/build/p2browser-probe" ] || { printf '%s\n' 'Build p2browser-probe with make first.' >&2; exit 1; }
    architecture=$(binary_architecture)
    dependencies=$(probe_dependencies)
    package=p2browser-probe
    write_control "$package" "$architecture" "$dependencies, p2browser-diagnostics (>= $version)" \
        'Diagnostic prototype of a native GTK 3 browser engine frontend'
    stage="$work_dir/$package"
    install -d -m 755 "$stage/usr/bin" "$stage/usr/share/p2browser-probe" "$stage/usr/share/applications"
    install -m 755 "$project_dir/build/p2browser-probe" "$stage/usr/bin/p2browser-probe"
    install -m 644 "$project_dir/fixtures/start.html" "$stage/usr/share/p2browser-probe/start.html"
    install -m 644 "$project_dir/packaging/p2browser-probe.desktop" "$stage/usr/share/applications/p2browser-probe.desktop"
    copy_docs "$package"
    printf 'ELF architecture: %s\n' "$architecture" >> "$stage/usr/share/doc/$package/build-info.txt"
    finish_package "$package" "$architecture" "-$suite"
}

build_source() {
    # Exclude private/generated paths before opening their contents. Do not copy
    # the tree into staging and remove secrets afterward. Tar does not dereference
    # symlinks, so symlink targets outside the project are not read either.
    tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner \
        --exclude='./build' --exclude='./dist' --exclude='./.git' \
        --exclude='./.env*' --exclude='*/.env*' \
        --exclude='__pycache__' --exclude='*.pyc' --exclude='*.pyo' \
        --exclude='*.deb' --exclude='*.o' --exclude='core' --exclude='core.*' \
        --transform="s,^\\.,p2browser-$version," \
        -C "$project_dir" -cf "$work_dir/source.tar" .
    gzip -n -c "$work_dir/source.tar" > "$work_dir/source.tar.gz"
    archive="$project_dir/dist/p2browser-source_${version}.tar.gz"
    mv "$work_dir/source.tar.gz" "$archive"
    printf 'Built source archive: %s\n' "$archive"
}

case "$mode" in
    all) build_diagnostics; build_probe ;;
    diagnostics) build_diagnostics ;;
    probe) build_probe ;;
    source) build_source ;;
esac
