#!/usr/bin/env python3
"""Generate bounded, deterministic, offline browser workloads (no dependencies)."""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

PNG_SIZES = (256, 1024, 2048, 4096)
DOM_ROWS = 12000
JS_ROWS = 5000
JS_HEAP_BYTES = 48 * 1024 * 1024
MIXED_ROWS = 2000
MIXED_HEAP_BYTES = 32 * 1024 * 1024
JS_ITERATIONS = 300000


def png_chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", binascii.crc32(kind + payload) & 0xffffffff))


def make_png(size):
    # Reuse two generated RGB scanlines. The bitmap has visible checks and
    # gradients, while generation needs only a few rows rather than 64 MiB.
    rows = []
    for phase in (0, 1):
        row = bytearray(b"\0")  # PNG filter type None.
        for x in range(size):
            cell = ((x // 32) + phase) % 2
            row.extend(((x * 251 // max(1, size - 1)),
                        48 + cell * 160, 208 - cell * 128))
        rows.append(bytes(row))
    encoder = zlib.compressobj(9)
    compressed = bytearray()
    for y in range(size):
        compressed.extend(encoder.compress(rows[(y // 32) % 2]))
    compressed.extend(encoder.flush())
    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
            png_chunk(b"IDAT", bytes(compressed)) + png_chunk(b"IEND", b""))


STYLE = """
html { font: 16px sans-serif; color: #152535; background: #edf0ef; }
body { margin: 20px; max-width: 940px; }
h1 { font-size: 24px; }
.notice { padding: 12px; background: white; border: 1px solid #789; }
.row { margin: 4px 0; padding: 3px 8px; border-left: 3px solid #486; }
.row:nth-child(even) { background: #dce4df; }
.pictures { display: flex; flex-wrap: wrap; gap: 10px; }
.pictures figure { margin: 0; padding: 4px; border: 1px solid #789; background: white; }
.pictures img { width: 220px; height: 180px; object-fit: contain; }
figcaption { font-size: 13px; }
"""


def page(case, title_state, heading, body, script=""):
    return ("<!doctype html>\n<html lang=\"pl\"><head><meta charset=\"utf-8\">\n"
            '<meta name="viewport" content="width=device-width,initial-scale=1">\n'
            f"<title>P2STRESS:{title_state}:{case}</title>\n<style>{STYLE}</style></head>\n"
            f'<body><h1>{heading}</h1>\n'
            '<p class="notice">Lokalny test P2Browser. Zażółć gęślą jaźń. '
            'Bez połączeń sieciowych, fontów internetowych i losowych danych.</p>\n'
            + body + (f"\n<script>\n{script}\n</script>" if script else "") +
            "\n</body></html>\n").encode("utf-8")


def pictures(sizes):
    return '<div class="pictures">\n' + "\n".join(
        f'<figure><img src="assets/checker-{size}.png" width="220" height="180" '
        f'alt="Wzorzec {size} na {size} pikseli"><figcaption>{size} × {size} px</figcaption></figure>'
        for size in sizes) + "\n</div>"


def js_workload(case, rows, heap_bytes):
    # Everything runs before LOAD_FINISHED; no timer, polling or unbounded loop.
    return f"""(function () {{
  'use strict';
  var count = {rows};
  var heapBytes = {heap_bytes};
  var iterations = {JS_ITERATIONS};
  var fragment = document.createDocumentFragment();
  var i, node;
  for (i = 0; i < count; i++) {{
    node = document.createElement('p');
    node.className = 'row';
    node.textContent = 'Węzeł JS ' + i + ': zażółć gęślą jaźń.';
    fragment.appendChild(node);
  }}
  document.getElementById('generated').appendChild(fragment);
  var buffer = new Uint8Array(heapBytes);
  for (i = 0; i < heapBytes; i += 4096) buffer[i] = (i / 4096) % 251;
  var checksum = 1;
  for (i = 0; i < iterations; i++) checksum = (checksum * 1664525 + 1013904223) >>> 0;
  window.p2StressRetainedBuffer = buffer;
  window.p2StressResult = {{elements: count, heapBytes: heapBytes, checksum: checksum}};
  document.getElementById('status').textContent =
    'JS gotowy: ' + count + ' elementów, bufor ' + heapBytes + ' bajtów, suma ' + checksum;
  document.title = 'P2STRESS:ready:{case}';
}}());"""


def fixture_bytes():
    sentence = ("Test układu i pamięci dokumentu: polskie znaki, zwykłe akapity oraz "
                "powtarzalna treść pozwalają porównać przeglądarki w tych samych warunkach. ")
    rows = "\n".join(f'<p class="row" id="r{i:05d}">{i:05d}: {sentence}</p>'
                     for i in range(DOM_ROWS))
    generated = ('<p id="status" class="notice">JS wyłączony lub niewykonany.</p>'
                 '<div id="generated"></div>')
    return {
        "large-dom.html": page("large-dom", "ready", "Duży dokument: 12000 akapitów", rows),
        "images.html": page("images", "ready", "Cztery lokalne obrazy PNG", pictures(PNG_SIZES)),
        "sync-js.html": page("sync-js", "disabled", "Synchroniczny DOM i bufor JavaScript",
                             generated, js_workload("sync-js", JS_ROWS, JS_HEAP_BYTES)),
        "mixed.html": page("mixed", "disabled", "Obrazy, DOM i bufor JavaScript",
                           pictures((2048, 4096)) + generated,
                           js_workload("mixed", MIXED_ROWS, MIXED_HEAP_BYTES)),
    }


def run_specs():
    return [
        {"name": "dom-no-js", "fixture": "large-dom.html", "javascript": False,
         "images": False, "title": "P2STRESS:ready:large-dom", "background": []},
        {"name": "images-off", "fixture": "images.html", "javascript": False,
         "images": False, "title": "P2STRESS:ready:images", "background": []},
        {"name": "images-on", "fixture": "images.html", "javascript": False,
         "images": True, "title": "P2STRESS:ready:images", "background": []},
        {"name": "js-off", "fixture": "sync-js.html", "javascript": False,
         "images": False, "title": "P2STRESS:disabled:sync-js", "background": []},
        {"name": "js-on", "fixture": "sync-js.html", "javascript": True,
         "images": False, "title": "P2STRESS:ready:sync-js", "background": []},
        {"name": "mixed-on", "fixture": "mixed.html", "javascript": True,
         "images": True, "title": "P2STRESS:ready:mixed", "background": []},
        {"name": "tabs-baseline", "fixture": "large-dom.html", "javascript": True,
         "images": False, "title": "P2STRESS:ready:large-dom",
         "background": ["sync-js.html", "sync-js.html"]},
        {"name": "tabs-pressure", "fixture": "large-dom.html", "javascript": True,
         "images": False, "title": "P2STRESS:ready:large-dom",
         "background": ["sync-js.html", "sync-js.html"], "memory_limit_mib": 96,
         "reclaim_background": True},
    ]


def generate(output):
    output = Path(output)
    output.mkdir(mode=0o700)
    (output / "assets").mkdir(mode=0o700)
    files = fixture_bytes()
    files.update({f"assets/checker-{size}.png": make_png(size) for size in PNG_SIZES})
    checks = []
    for name, payload in sorted(files.items()):
        (output / name).write_bytes(payload)
        checks.append({"path": name, "bytes": len(payload),
                       "sha256": hashlib.sha256(payload).hexdigest()})
    manifest = {
        "schema": 1, "generator": "p2stress-v1", "offline": True,
        "generation_environment": {"python": sys.version.split()[0], "zlib": zlib.ZLIB_RUNTIME_VERSION},
        "dom_rows": DOM_ROWS, "js_rows": JS_ROWS, "js_heap_bytes": JS_HEAP_BYTES,
        "mixed_rows": MIXED_ROWS, "mixed_heap_bytes": MIXED_HEAP_BYTES,
        "js_iterations": JS_ITERATIONS,
        "images": [{"path": f"assets/checker-{size}.png", "width": size, "height": size,
                    "nominal_rgba_bytes": size * size * 4} for size in PNG_SIZES],
        "files": checks, "runs": run_specs(),
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", help="new output directory (existing directories are refused)")
    args = parser.parse_args()
    try:
        manifest = generate(args.output)
    except FileExistsError:
        parser.exit(1, "Output already exists; refusing to overwrite fixture data.\n")
    print("Generated %d files and %d bounded offline runs." %
          (len(manifest["files"]), len(manifest["runs"])))


if __name__ == "__main__":
    main()
