"""Verify bounded offline input data independently of the browser engine."""
import binascii
import hashlib
from html.parser import HTMLParser
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

PROJECT = Path(__file__).resolve().parents[1]
GENERATOR = PROJECT / 'fixtures/stress/generate.py'
spec = importlib.util.spec_from_file_location('stress_generator', GENERATOR)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class ResourceParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.resources = []
        self.rows = 0
        self.titles = []
        self.in_title = False

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        for name in ('src', 'href', 'srcset'):
            if name in attrs:
                self.resources.append(attrs[name])
        if attrs.get('class') == 'row':
            self.rows += 1
        if tag == 'title':
            self.in_title = True

    def handle_endtag(self, tag):
        if tag == 'title':
            self.in_title = False

    def handle_data(self, data):
        if self.in_title:
            self.titles.append(data)


class StressFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.output = Path(cls.temp.name) / 'data'
        cls.manifest = generator.generate(cls.output)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_output_is_deterministic_and_hashes_match(self):
        second = Path(self.temp.name) / 'second'
        generator.generate(second)
        first_files = sorted(path.relative_to(self.output) for path in self.output.rglob('*') if path.is_file())
        other_files = sorted(path.relative_to(second) for path in second.rglob('*') if path.is_file())
        self.assertEqual(first_files, other_files)
        for name in first_files:
            self.assertEqual((self.output / name).read_bytes(), (second / name).read_bytes(), str(name))
        for item in self.manifest['files']:
            data = (self.output / item['path']).read_bytes()
            self.assertEqual(len(data), item['bytes'])
            self.assertEqual(hashlib.sha256(data).hexdigest(), item['sha256'])

    def test_large_document_has_bounded_size_and_exact_row_count(self):
        data = (self.output / 'large-dom.html').read_bytes()
        self.assertGreaterEqual(len(data), 1024 * 1024)
        self.assertLessEqual(len(data), 5 * 1024 * 1024)
        parser = ResourceParser()
        parser.feed(data.decode('utf-8'))
        self.assertEqual(parser.rows, 12000)
        self.assertEqual(parser.titles, ['P2STRESS:ready:large-dom'])
        self.assertIn('Zażółć gęślą jaźń', data.decode('utf-8'))

    def test_resources_stay_local_and_scripts_have_no_network_or_timers(self):
        for item in self.manifest['files']:
            if not item['path'].endswith('.html'):
                continue
            text = (self.output / item['path']).read_text(encoding='utf-8')
            parser = ResourceParser()
            parser.feed(text)
            for resource in parser.resources:
                self.assertFalse(resource.startswith(('/', '//')))
                self.assertNotIn(':', resource)
                self.assertTrue((self.output / resource).is_file(), resource)
            for forbidden in ('https://', 'http://', 'fetch(', 'XMLHttpRequest', 'WebSocket',
                              'setTimeout(', 'setInterval(', 'Math.random(', 'while (true)'):
                self.assertNotIn(forbidden, text)

    def test_png_files_have_valid_chunks_dimensions_and_scanlines(self):
        for item in self.manifest['images']:
            with self.subTest(size=item['width']):
                data = (self.output / item['path']).read_bytes()
                self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
                offset, idat, kinds = 8, bytearray(), []
                while offset < len(data):
                    size = struct.unpack('>I', data[offset:offset + 4])[0]
                    kind = data[offset + 4:offset + 8]
                    payload = data[offset + 8:offset + 8 + size]
                    checksum = struct.unpack('>I', data[offset + 8 + size:offset + 12 + size])[0]
                    self.assertEqual(checksum, binascii.crc32(kind + payload) & 0xffffffff)
                    if kind == b'IHDR':
                        header = struct.unpack('>IIBBBBB', payload)
                        self.assertEqual(header, (item['width'], item['height'], 8, 2, 0, 0, 0))
                    if kind == b'IDAT':
                        idat.extend(payload)
                    kinds.append(kind)
                    offset += 12 + size
                self.assertEqual(kinds, [b'IHDR', b'IDAT', b'IEND'])
                raw = zlib.decompress(bytes(idat))
                stride = 1 + item['width'] * 3
                self.assertEqual(len(raw), item['height'] * stride)
                self.assertTrue(all(raw[row * stride] == 0 for row in range(item['height'])))
                self.assertGreater(len(set(raw[1:stride])), 8)

    def test_javascript_counts_allocations_and_readiness_are_bounded(self):
        for name, case, rows, heap in (('sync-js.html', 'sync-js', 5000, 48 * 1024 * 1024),
                                       ('mixed.html', 'mixed', 2000, 32 * 1024 * 1024)):
            text = (self.output / name).read_text(encoding='utf-8')
            self.assertIn('var count = %d;' % rows, text)
            self.assertIn('var heapBytes = %d;' % heap, text)
            self.assertIn('var iterations = 300000;', text)
            self.assertIn('i += 4096', text)
            self.assertIn('window.p2StressRetainedBuffer = buffer;', text)
            self.assertIn("document.title = 'P2STRESS:ready:%s';" % case, text)
            parser = ResourceParser()
            parser.feed(text)
            self.assertEqual(parser.titles, ['P2STRESS:disabled:' + case])

    def test_run_matrix_has_matched_controls_and_explicit_reclaim(self):
        runs = {run['name']: run for run in self.manifest['runs']}
        self.assertEqual(len(runs), 8)
        self.assertEqual(runs['images-off']['fixture'], runs['images-on']['fixture'])
        self.assertFalse(runs['images-off']['images'])
        self.assertTrue(runs['images-on']['images'])
        self.assertFalse(runs['js-off']['javascript'])
        self.assertTrue(runs['js-on']['javascript'])
        self.assertEqual(runs['tabs-baseline']['background'], runs['tabs-pressure']['background'])
        self.assertNotIn('memory_limit_mib', runs['tabs-baseline'])
        self.assertEqual(runs['tabs-pressure']['memory_limit_mib'], 96)
        self.assertTrue(runs['tabs-pressure']['reclaim_background'])
        for run in runs.values():
            self.assertTrue((self.output / run['fixture']).is_file())

    def test_existing_output_is_never_overwritten(self):
        existing = Path(self.temp.name) / 'keep'
        existing.mkdir()
        marker = existing / 'manifest.json'
        marker.write_text('preserve')
        result = subprocess.run([sys.executable, str(GENERATOR), str(existing)],
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(marker.read_text(), 'preserve')
        self.assertIn('refusing to overwrite', result.stderr)


if __name__ == '__main__':
    unittest.main()
