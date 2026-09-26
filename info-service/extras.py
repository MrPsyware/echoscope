"""Optional map and orbital services. Calculations and caches stay off the knob."""
import hashlib
import io
import json
import math
import os
from pathlib import Path
import struct
import threading
import time
from collections import OrderedDict
from datetime import datetime, timezone, timedelta
from urllib.parse import parse_qs
import numpy as np
from PIL import Image
from skyfield.api import EarthSatellite, load, wgs84

CACHE_DIR = Path(os.environ.get('CACHE_DIR', '/data'))
MAPS = os.environ.get('ENABLE_MAPS', '1') == '1'
SATELLITES = os.environ.get('ENABLE_SATELLITES', '1') == '1'
TILE_URL = (os.environ.get('TILE_URL') or 'https://tile.openstreetmap.org/{z}/{x}/{y}.png')
MAP_CREDIT = os.environ.get('MAP_CREDIT', 'Copyright OpenStreetMap contributors')[:63]
NORAD = [int(x) for x in os.environ.get('SATELLITE_IDS', '25544,48274').split(',')][:8]
TS = load.timescale(builtin=True)
MAP_CACHE = OrderedDict()
MAP_PENDING = set()
MAP_LOCK = threading.Lock()
TILE_LOCK = threading.Lock()
SAT_LOCK = threading.Lock()
SATS = []
NEXT_ORBITS = 0
PASS_CACHE = OrderedDict()


def location(query):
    q = parse_qs(query)
    lat, lon = float(q['lat'][0]), float(q['lon'][0])
    if not math.isfinite(lat) or not math.isfinite(lon) or abs(lat) > 85 or abs(lon) > 180:
        raise ValueError('Coordinates must be within ±85 latitude / ±180 longitude')
    return lat, lon, q


def inverse_grid(lat, lon, radius, size=420):
    """Inverse azimuthal equidistant projection, matching firmware project()."""
    yy, xx = np.mgrid[:size, :size]
    east, north = (xx-size/2)*radius/(size/2), (size/2-yy)*radius/(size/2)
    rho = np.hypot(east, north)
    c = rho / 6371.0088
    phi = math.radians(lat)
    safe = np.where(rho == 0, 1, rho)
    latitude = np.arcsin(np.cos(c)*math.sin(phi) + north*np.sin(c)*math.cos(phi)/safe)
    longitude = math.radians(lon) + np.arctan2(east*np.sin(c), safe*math.cos(phi)*np.cos(c)-north*math.sin(phi)*np.sin(c))
    latitude[rho == 0] = phi
    longitude[rho == 0] = math.radians(lon)
    return latitude, longitude, rho <= radius


def tile(z, x, y, download):
    url = TILE_URL.format(z=z, x=x, y=y)
    if not url.startswith('https://'):
        raise ValueError('Tile source must use HTTPS')
    path = CACHE_DIR / 'tiles' / (hashlib.sha256(url.encode()).hexdigest() + '.png')
    path.parent.mkdir(parents=True, exist_ok=True)
    # Minimum 7-day retention, per the OSM tile policy. No prefetching.
    with TILE_LOCK:
        if path.exists() and time.time()-path.stat().st_mtime < 7*86400:
            raw = path.read_bytes()
        else:
            if not path.exists():
                files = list(path.parent.glob('*.png'))
                if len(files) >= 2048:
                    for old in files:
                        if time.time()-old.stat().st_mtime >= 7*86400:
                            old.unlink()
                    if len(list(path.parent.glob('*.png'))) >= 2048:
                        raise ValueError('Tile cache full; retaining required cache lifetime')
            raw = download(url, 512000)
            with Image.open(io.BytesIO(raw)) as check:
                if check.size != (256, 256):
                    raise ValueError('Expected 256px tiles')
            tmp = path.with_suffix('.tmp')
            tmp.write_bytes(raw)
            tmp.replace(path)
            time.sleep(0.15)
    with Image.open(io.BytesIO(raw)) as im:
        if im.size != (256, 256):
            raise ValueError('Invalid cached tile')
        return np.asarray(im.convert('RGB'))


def make_map(lat, lon, radius, download):
    latitude, longitude, mask = inverse_grid(lat, lon, radius)
    if np.max(np.abs(latitude[mask])) > math.radians(85):
        raise ValueError('Map crosses polar limit')
    zoom = max(0, min(16, round(math.log2(40075.0167*math.cos(math.radians(lat))*210/(256*radius)))))
    count = 2**zoom
    px = ((longitude/(2*math.pi)+0.5)*count*256).astype(np.int64)
    py = ((1-np.arcsinh(np.tan(latitude))/math.pi)/2*count*256).astype(np.int64)
    tx, ty = px//256, py//256
    keys = set(zip(tx[mask].tolist(), ty[mask].tolist()))
    if len(keys) > 16:
        raise ValueError('Viewport needs too many tiles')
    pixels = np.zeros((420, 420, 3), dtype=np.uint8)
    for x, y in keys:
        if y < 0 or y >= count:
            continue
        image = tile(zoom, x % count, y, download)
        selected = mask & (tx == x) & (ty == y)
        pixels[selected] = image[py[selected] % 256, px[selected] % 256]
    # Invert dark roads/text onto a muted green chart, keeping white land dark.
    luminance = np.dot(pixels.astype(float), [0.299, 0.587, 0.114])
    ink = (255-luminance)/255
    out = np.stack([3+ink*27, 13+ink*67, 16+ink*52], axis=-1).astype(np.uint16)
    out[~mask] = (3, 13, 16)
    rgb = ((out[:, :, 0] >> 3) << 11) | ((out[:, :, 1] >> 2) << 5) | (out[:, :, 2] >> 3)
    return struct.pack('<4sHH', b'ECM1', 420, 420) + rgb.astype('>u2').tobytes()


def map_response(lat, lon, radius, download):
    if radius not in (5, 10, 25, 50, 100):
        raise ValueError('Invalid radar range')
    key = (round(lat, 6), round(lon, 6), radius)
    with MAP_LOCK:
        cached = MAP_CACHE.get(key)
        if cached and cached[0] > time.time():
            return cached[1], cached[2]
        if key not in MAP_PENDING and not MAP_PENDING:
            MAP_PENDING.add(key)
            def work():
                try:
                    packet = make_map(*key, download)
                    result = (time.time()+7*86400, 200, packet)
                except Exception as error:
                    print(f'Map failed: {type(error).__name__}', flush=True)
                    result = (time.time()+600, 503, b'Map unavailable')
                with MAP_LOCK:
                    MAP_CACHE[key] = result
                    MAP_CACHE.move_to_end(key)
                    while len(MAP_CACHE) > 8:
                        MAP_CACHE.popitem(last=False)
                    MAP_PENDING.discard(key)
            threading.Thread(target=work, daemon=True).start()
    return 202, b'Map rendering'


def refresh_orbits(download):
    global NEXT_ORBITS, SATS
    if not SATELLITES:
        return
    with SAT_LOCK:
        if time.time() < NEXT_ORBITS:
            return
        NEXT_ORBITS = time.time()+6*3600
    try:
        path = CACHE_DIR / 'stations.json'
        if path.exists() and time.time()-path.stat().st_mtime < 6*3600:
            raw = path.read_bytes()
        else:
            # Only the stations group; never download the full catalogue.
            raw = download('https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=json', 524288)
            records = json.loads(raw)
            if not isinstance(records, list):
                raise ValueError('Invalid orbit catalogue')
            path.parent.mkdir(parents=True, exist_ok=True)
            tmp = path.with_suffix('.tmp'); tmp.write_bytes(raw); tmp.replace(path)
        satellites = [EarthSatellite.from_omm(TS, r) for r in json.loads(raw) if int(r['NORAD_CAT_ID']) in NORAD]
        with SAT_LOCK:
            SATS = satellites
    except Exception as error:
        print(f'Orbit refresh failed: {type(error).__name__}', flush=True)


def available_satellites():
    now = TS.now()
    with SAT_LOCK:
        return [sat for sat in SATS if abs(now-sat.epoch) <= 7]


def satellite_response(lat, lon):
    now = datetime.now(timezone.utc)
    t = TS.from_datetime(now)
    site = wgs84.latlon(lat, lon)
    result = []
    for sat in available_satellites():
        top = (sat-site).at(t)
        altitude, azimuth, distance = top.altaz()
        if not all(math.isfinite(v) for v in (altitude.degrees, azimuth.degrees, distance.km)):
            continue
        key = (sat.model.satnum, round(lat, 4), round(lon, 4), str(sat.epoch))
        cached = PASS_CACHE.get(key)
        if not cached or cached[0] < time.time():
            times, events = sat.find_events(site, t, TS.from_datetime(now+timedelta(hours=24)), altitude_degrees=10)
            rise = next((int(ti.utc_datetime().timestamp()) for ti, event in zip(times, events) if event == 0), 0)
            cached = (time.time()+60, rise)
            PASS_CACHE[key] = cached
            while len(PASS_CACHE) > 32:
                PASS_CACHE.popitem(last=False)
        result.append({'id': sat.model.satnum, 'name': sat.name[:31], 'az': round(azimuth.degrees, 2),
                       'el': round(altitude.degrees, 2), 'km': round(distance.km), 'next_rise': cached[1]})
    return {'generated': int(now.timestamp()), 'satellites': result, 'minimum_pass_elevation': 10,
            'note': 'Predicted positions; above horizon does not imply naked-eye visibility', 'source': 'CelesTrak / SGP4'}


def start(download):
    if SATELLITES:
        def worker():
            while True:
                refresh_orbits(download)
                time.sleep(3600)
        threading.Thread(target=worker, daemon=True).start()
