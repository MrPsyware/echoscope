"""On-demand RainViewer frames, projected once and shared by LAN displays."""
import io
import json
import math
import os
import re
import struct
import threading
import time
from collections import OrderedDict
from urllib.error import HTTPError

import numpy as np
from PIL import Image
import extras

ENABLED = os.environ.get('ENABLE_RAIN', '1') == '1'
HOST = 'https://tilecache.rainviewer.com'
MANIFEST = 'https://api.rainviewer.com/public/weather-maps.json'
HEADER = struct.Struct('<4sHHI')


def validate_manifest(raw, now):
    doc = json.loads(raw)
    if doc.get('host') != HOST:
        raise ValueError('Unexpected radar host')
    frames = []
    for f in doc.get('radar', {}).get('past', []):
        stamp, path = f.get('time'), f.get('path')
        if (type(stamp) is not int or not now-10800 <= stamp <= now+600 or
                not isinstance(path, str) or not re.fullmatch(r'/v2/radar/[A-Za-z0-9_-]{1,85}', path)):
            continue
        if frames and stamp <= frames[-1]['time']:
            continue
        frames.append({'time': stamp, 'path': path})
    if not frames:
        raise ValueError('No recent radar frames')
    return {'host': HOST, 'radar': {'past': frames[-13:]}}


def render(lat, lon, radius, frame, tile):
    latitude, longitude, mask = extras.inverse_grid(lat, lon, radius)
    zoom = max(0, min(7, math.floor(math.log2(40075.0167*max(.001, math.cos(math.radians(lat)))*210/(256*radius))+.5)))
    world = 256*(2**zoom)
    px = np.floor((longitude/(2*math.pi)+.5)*world).astype(np.int64) % world
    latitude = np.clip(latitude, math.radians(-85.05112878), math.radians(85.05112878))
    py = np.clip(np.floor((1-np.arcsinh(np.tan(latitude))/math.pi)*.5*world), 0, world-1).astype(np.int64)
    tx, ty = px//256, py//256
    keys = set(zip(tx[mask].tolist(), ty[mask].tolist()))
    if len(keys) > 16:
        raise ValueError('Radar viewport too large')
    pixels = np.zeros((420, 420, 4), dtype=np.uint8)
    yy, xx = np.mgrid[:420, :420]
    for x, y in sorted(keys):
        selected = mask & (tx == x) & (ty == y)
        suffix = f'/256/{zoom}/{x}/{y}'
        coverage = tile(HOST+'/v2/coverage/0'+suffix+'/0/0_0.png', 86400)
        rain = tile(HOST+frame['path']+suffix+'/2/1_0.png', 10800)
        sample = rain[py[selected] % 256, px[selected] % 256].copy()
        sample[:, 3] = sample[:, 3].astype(np.uint16)*210//255
        missing = coverage[py[selected] % 256, px[selected] % 256, 3] > 127
        sample[missing, :3] = (0x71, 0x87, 0x82)
        sample[missing, 3] = np.where((xx[selected][missing]+yy[selected][missing]) % 14 < 3, 150, 30)
        pixels[selected] = sample
    rgb = pixels[:, :, :3].astype(np.uint16)
    packed = ((rgb[:, :, 0] >> 3) << 11) | ((rgb[:, :, 1] >> 2) << 5) | (rgb[:, :, 2] >> 3)
    out = np.empty((420, 420, 3), dtype=np.uint8)
    out[:, :, 0] = packed >> 8
    out[:, :, 1] = packed & 255
    out[:, :, 2] = pixels[:, :, 3]
    return HEADER.pack(b'ECR1', 420, 420, frame['time']) + out.tobytes()


class Cache:
    """One bounded worker; HTTP handlers never wait for upstream or PNG work."""
    def __init__(self):
        self.lock = threading.Lock()
        self.pending = None
        self.results = OrderedDict()
        self.tiles = OrderedDict()  # used only by the single worker
        self.manifest = None
        self.manifest_until = 0
        self.next_request = 0
        self.backoff = 0
        self.backoff_code = 503

    def fetch(self, url, limit, download):
        wait = max(0, self.next_request-time.monotonic())
        if wait:
            time.sleep(wait)
        try:
            return download(url, limit)
        except HTTPError as error:
            if error.code == 429:
                self.backoff = time.monotonic()+120
            raise
        finally:
            self.next_request = time.monotonic()+1.1

    def get_manifest(self, download):
        if self.manifest is None or time.monotonic() >= self.manifest_until:
            self.manifest = validate_manifest(self.fetch(MANIFEST, 65536, download), time.time())
            self.manifest_until = time.monotonic()+300
        return self.manifest

    def tile(self, url, ttl, download):
        cached = self.tiles.get(url)
        if cached and cached[0] > time.monotonic():
            self.tiles.move_to_end(url)
            return cached[1]
        raw = self.fetch(url, 256*1024, download)
        with Image.open(io.BytesIO(raw)) as im:
            if im.format != 'PNG' or im.size != (256, 256):
                raise ValueError('Expected a 256px radar PNG')
            data = np.array(im.convert('RGBA'))
        self.tiles[url] = (time.monotonic()+ttl, data)
        self.tiles.move_to_end(url)
        while len(self.tiles) > 64:
            self.tiles.popitem(last=False)
        return data

    def response(self, key, download):
        with self.lock:
            now = time.monotonic()
            cached = self.results.get(key)
            if cached and cached[0] > now:
                self.results.move_to_end(key)
                return cached[1:]
            if now < self.backoff:
                return self.backoff_code, b'Rain provider cooling down'
            if self.pending is None:
                self.pending = key
                threading.Thread(target=self.work, args=(key, download), daemon=True).start()
        return 202, b'Preparing rain radar'

    def work(self, key, download):
        try:
            manifest = self.get_manifest(download)
            if key == 'manifest':
                result = (self.manifest_until, 200, json.dumps(manifest).encode())
            else:
                lat, lon, radius, stamp = key
                frame = next((f for f in manifest['radar']['past'] if f['time'] == stamp), None)
                if frame is None:
                    result = (time.monotonic()+30, 404, b'Radar frame expired')
                else:
                    packet = render(lat, lon, radius, frame, lambda url, ttl: self.tile(url, ttl, download))
                    result = (time.monotonic()+10800, 200, packet)
        except Exception as error:
            print(f'Rain radar failed: {type(error).__name__}: {error}', flush=True)
            self.backoff = max(self.backoff, time.monotonic()+60)
            self.backoff_code = 429 if isinstance(error, HTTPError) and error.code == 429 else 503
            result = (self.backoff, self.backoff_code, b'Rain radar unavailable')
        with self.lock:
            self.results[key] = result
            self.results.move_to_end(key)
            while len(self.results) > 16:
                self.results.popitem(last=False)
            self.pending = None


CACHE = Cache()


def response(query, download):
    lat, lon, q = extras.location(query)
    radius, stamp = int(q['range'][0]), int(q['time'][0])
    if radius not in (5, 10, 25, 50, 100) or not time.time()-10800 <= stamp <= time.time()+600:
        raise ValueError('Invalid radar range or frame')
    return CACHE.response((round(lat, 6), round(lon, 6), radius, stamp), download)
