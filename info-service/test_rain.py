import io
import json
import struct
import threading
import time
import unittest
from unittest.mock import Mock, patch
import numpy as np
from PIL import Image
import rain


class Rain(unittest.TestCase):
    def manifest(self):
        return json.dumps({'host': rain.HOST, 'radar': {'past': [{'time': int(time.time())-600, 'path': '/v2/radar/hash'}]}}).encode()

    def test_manifest_host_paths_and_freshness(self):
        parsed = rain.validate_manifest(self.manifest(), time.time())
        self.assertEqual(len(parsed['radar']['past']), 1)
        for host, path, stamp in [('http://evil.test', '/v2/radar/hash', time.time()),
                                  (rain.HOST, '/v2/radar/../bad', int(time.time())),
                                  (rain.HOST, '/v2/radar/hash', int(time.time())-20000)]:
            with self.assertRaises(ValueError):
                rain.validate_manifest(json.dumps({'host': host, 'radar': {'past': [{'time': stamp, 'path': path}]}}), time.time())

    def test_pixels_and_packet_match_firmware(self):
        wet = np.full((256, 256, 4), (255, 68, 0, 255), dtype=np.uint8)
        coverage = np.zeros((256, 256, 4), dtype=np.uint8)
        def tile(url, ttl): return coverage if '/coverage/' in url else wet
        packet = rain.render(51.5, 0, 25, {'time': 123, 'path': '/v2/radar/hash'}, tile)
        self.assertEqual(len(packet), 12+420*420*3)
        self.assertEqual(rain.HEADER.unpack(packet[:12]), (b'ECR1', 420, 420, 123))
        self.assertEqual(packet[12:15], b'\0\0\0')
        centre = 12+(210*420+210)*3
        self.assertEqual(packet[centre:centre+3], struct.pack('>H', (31<<11)|(17<<5))+bytes([210]))
        coverage[:, :, 3] = 255
        packet = rain.render(51.5, 0, 25, {'time': 123, 'path': '/v2/radar/hash'}, tile)
        self.assertEqual(packet[centre+2], 150)  # x+y divisible by 14

    def test_wrap_and_zoom_limit(self):
        seen = []
        def tile(url, ttl):
            seen.append(url)
            return np.zeros((256, 256, 4), dtype=np.uint8)
        for lat, lon in [(0, 179.99), (85, -179.99), (-85, 0)]:
            rain.render(lat, lon, 100, {'time': 123, 'path': '/v2/radar/hash'}, tile)
        for url in seen:
            parts = url.split('/'); z, x, y = map(int, parts[-5:-2])
            self.assertLessEqual(z, 7)
            self.assertTrue(0 <= x < 2**z and 0 <= y < 2**z)

    def test_tile_cache_and_dimensions(self):
        raw = io.BytesIO(); Image.new('RGBA', (256, 256)).save(raw, 'PNG')
        cache = rain.Cache(); download = Mock(return_value=raw.getvalue())
        cache.tile('tile', 100, download); cache.tile('tile', 100, download)
        self.assertEqual(download.call_count, 1)
        raw = io.BytesIO(); Image.new('RGBA', (512, 256)).save(raw, 'PNG')
        cache.next_request = 0
        with self.assertRaises(ValueError): cache.tile('other', 100, lambda *_: raw.getvalue())

    def test_worker_deduplicates_and_reuses_result(self):
        cache = rain.Cache(); started = threading.Event(); release = threading.Event()
        def download(*_): started.set(); release.wait(3); return self.manifest()
        self.assertEqual(cache.response('manifest', download)[0], 202)
        self.assertTrue(started.wait(2))
        self.assertEqual(cache.response('manifest', download)[0], 202)
        release.set()
        for _ in range(100):
            if cache.pending is None: break
            time.sleep(.01)
        self.assertEqual(cache.response('manifest', Mock(side_effect=AssertionError))[0], 200)

    def test_failure_backoff(self):
        cache = rain.Cache()
        cache.work('manifest', Mock(side_effect=OSError('offline')))
        self.assertEqual(cache.response('manifest', None)[0], 503)
        self.assertEqual(cache.response((0, 0, 25, int(time.time())), None)[0], 503)

    def test_rate_limit_propagates_to_knob(self):
        from urllib.error import HTTPError
        cache=rain.Cache()
        cache.work('manifest', Mock(side_effect=HTTPError(rain.MANIFEST,429,'limited',{},None)))
        self.assertEqual(cache.response('manifest',None)[0],429)
        self.assertEqual(cache.response((0,0,25,int(time.time())),None)[0],429)
        self.assertGreater(cache.backoff,time.monotonic()+110)

    def test_invalid_queries(self):
        for q in ['lat=nan&lon=0', 'lat=0&lon=0&range=7&time=123', 'lat=0&lon=0&range=25&time=123']:
            with self.assertRaises((ValueError, KeyError)): rain.response(q, None)
