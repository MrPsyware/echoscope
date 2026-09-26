"""Free, optional flight and observing information; all upstream work stays here."""
import csv
import io
import json
import math
import os
import re
import threading
import time
from collections import OrderedDict
from datetime import datetime, timezone
from urllib.error import HTTPError
from urllib.parse import urlencode
import extras

WEATHER = os.environ.get('ENABLE_WEATHER', '1') == '1'
FLIGHTS = os.environ.get('ENABLE_FLIGHTS', '1') == '1'
AIRPORTS = os.environ.get('ENABLE_AIRPORTS', '1') == '1'
CACHE = OrderedDict()
LOCK = threading.RLock()
AIRPORT_INDEX = ()
NEXT_FLIGHT = 0.0
TOKEN = re.compile(r'[A-Z0-9]{2,10}\Z')


def cached(key, seconds, build):
    # Serialize these small lookups, coalesce callers and bound memory and retries.
    with LOCK:
        now = time.monotonic()
        old = CACHE.get(key)
        if old and old[0] > now:
            if isinstance(old[1], Exception):
                raise ValueError(str(old[1]))
            return old[1]
        try:
            value = build()
        except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
            CACHE[key] = (time.monotonic() + 30, ValueError(type(error).__name__))
            while len(CACHE) > 64:
                CACHE.popitem(last=False)
            raise
        CACHE[key] = (time.monotonic() + seconds, value)
        CACHE.move_to_end(key)
        while len(CACHE) > 64:
            CACHE.popitem(last=False)
        return value


def clean(value, size=60):
    return ' '.join(str(value or '').split())[:size]


def page(title, *lines):
    return {'title': clean(title, 32), 'lines': [clean(s) for s in lines][:7]}


def result(pages, source):
    return {'generated': int(time.time()), 'source': source, 'pages': pages}


def number(value, unit=''):
    if not isinstance(value, (int, float)) or not math.isfinite(value):
        return '--'
    return f'{value:.0f}{unit}'


def clock(value):
    return datetime.fromtimestamp(value, timezone.utc).strftime('%d %b %H:%M UTC')


def weather(lat, lon, download):
    def build():
        query = urlencode({'latitude': lat, 'longitude': lon, 'timezone': 'GMT', 'timeformat': 'unixtime',
                           'forecast_hours': 24,
                           'current': 'temperature_2m,cloud_cover,wind_speed_10m,wind_direction_10m',
                           'hourly': 'cloud_cover,cloud_cover_low,cloud_cover_mid,cloud_cover_high,precipitation_probability,is_day'})
        data = json.loads(download('https://api.open-meteo.com/v1/forecast?' + query, 65536))
        current, hourly = data['current'], data['hourly']
        pages = [page('LOCAL WEATHER', clock(current['time']),
                      'Cloud cover ' + number(current.get('cloud_cover'), '%'),
                      'Temperature ' + number(current.get('temperature_2m'), ' C'),
                      'Wind ' + number(current.get('wind_speed_10m'), ' km/h'),
                      'From ' + number(current.get('wind_direction_10m'), ' degrees'),
                      'Turn for 24h cloud forecast', 'Forecast, not a sky observation')]
        for i in range(0, min(24, len(hourly['time'])), 3):
            def value(key):
                values = hourly.get(key, [])
                return values[i] if i < len(values) else None
            pages.append(page('CLOUD FORECAST', clock(hourly['time'][i]),
                              'Cloud cover ' + number(value('cloud_cover'), '%'),
                              'Low / mid / high cloud',
                              ' / '.join(number(value(k), '%') for k in ('cloud_cover_low', 'cloud_cover_mid', 'cloud_cover_high')),
                              'Rain chance ' + number(value('precipitation_probability'), '%'),
                              'Night' if value('is_day') == 0 else 'Day' if value('is_day') == 1 else 'Day/night unavailable',
                              'Cloud cover does not predict seeing'))
        return result(pages, 'Open-Meteo / CC BY 4.0')
    return cached(('weather', lat, lon), 900, build)


def token(value, optional=False):
    value = value.strip().upper()
    if optional and not value:
        return ''
    if not TOKEN.fullmatch(value):
        raise ValueError('Use 2-10 letters or digits')
    return value


def route_data(flight, download):
    def build():
        try:
            data = json.loads(download('https://api.adsbdb.com/v0/callsign/' + flight, 32768))
        except HTTPError as error:
            if error.code == 404:
                return {}
            raise
        return data['response']['flightroute']
    return cached(('route', flight), 21600, build)


def route_page(flight, route):
    if not route:
        return page(flight, 'No route in the database', 'Try the transmitted callsign', 'Booking numbers can differ', 'No schedule or arrival estimate')
    origin, dest = route.get('origin', {}), route.get('destination', {})
    return page(flight, clean(route.get('airline', {}).get('name')),
                f"{origin.get('iata_code') or origin.get('icao_code') or '?'} > {dest.get('iata_code') or dest.get('icao_code') or '?'}",
                clean(origin.get('name'), 48), clean(dest.get('name'), 48),
                'Database route; verify with airline', 'No schedule or arrival estimate')


def route(flight, download):
    flight = token(flight)
    return result([route_page(flight, route_data(flight, download))], 'adsbdb')


def distance(lat, lon, lat2, lon2):
    p, q = math.radians(lat), math.radians(lat2)
    a = math.sin((q-p)/2)**2 + math.cos(p)*math.cos(q)*math.sin(math.radians(lon2-lon)/2)**2
    return 6371.0088 * 2 * math.asin(math.sqrt(min(1, max(0, a))))


def family(flight, callsign, arrival, download):
    flight, callsign, arrival = token(flight), token(callsign, True), token(arrival, True)
    def build():
        route_unavailable = False
        try:
            route = route_data(flight, download)
        except (OSError, ValueError, KeyError, TypeError):
            route = {}; route_unavailable = True
        lookup = callsign or route.get('callsign_icao') or flight
        lookup = token(lookup)
        global NEXT_FLIGHT
        time.sleep(max(0, NEXT_FLIGHT-time.monotonic()))
        NEXT_FLIGHT=time.monotonic()+1.1
        data = json.loads(download('https://opendata.adsb.fi/api/v2/callsign/' + lookup, 262144))
        feed_time=data.get('now')
        if not isinstance(feed_time, (int, float)) or not math.isfinite(feed_time) or abs(time.time()-feed_time/1000)>60:
            raise ValueError('Stale or untimed flight feed')
        feed_age=max(0, time.time()-feed_time/1000)
        if data.get('msg') not in (None, 'No error'):
            raise ValueError('Flight feed error')
        aircraft = data.get('ac', data.get('aircraft'))
        if not isinstance(aircraft, list):
            raise ValueError('Missing aircraft list')
        # No arbitrary choice between duplicate callsigns; fresh position required.
        fresh = [a for a in aircraft if clean(a.get('flight')).upper() == lookup
                 and isinstance(a.get('seen_pos'), (int, float)) and 0 <= a['seen_pos'] and a['seen_pos']+feed_age <= 60
                 and all(isinstance(a.get(k), (int, float)) and math.isfinite(a[k]) for k in ('lat', 'lon'))
                 and abs(a['lat']) <= 90 and abs(a['lon']) <= 180]
        pages = []
        if len(fresh) == 1:
            a = fresh[0]
            destination = next((x for x in AIRPORT_INDEX if arrival in (x['iata'], x['icao'])), None) if arrival else None
            proximity = ('To ' + arrival + ': ' + number(distance(a['lat'], a['lon'], destination['lat'], destination['lon']), ' km')) if destination else ('Arrival airport: ' + arrival if arrival else 'Set arrival airport in setup')
            pages.append(page(flight + ' / ' + lookup, clean(a.get('r')) + ' / ' + clean(a.get('t')),
                              f"{a['lat']:.3f}, {a['lon']:.3f}",
                              ('On ground' if a.get('alt_baro') == 'ground' else number(a.get('alt_baro'), ' ft')) + ' / ' + number(a.get('gs'), ' kt'),
                              proximity, 'Position ' + number(a['seen_pos']+feed_age, 's old at fetch'),
                              'Direct distance is not arrival time', 'Confirm flight and date with airline'))
        else:
            pages.append(page(flight, 'Multiple matches; check callsign' if len(fresh) > 1 else 'No fresh position found',
                              'Looking for ' + lookup, 'May be offline or out of coverage',
                              'Try actual callsign in setup', 'This does not mean landed', 'No arrival estimate available'))
        route_info=route_page(flight, route) if not route_unavailable else page(flight, 'Route provider unavailable', 'Live tracking uses the callsign')
        destination=route.get('destination', {})
        if arrival and destination and arrival not in (destination.get('iata_code'), destination.get('icao_code')):
            route_info['lines'][-1]='Arrival differs from database route'
        pages.append(route_info)
        return result(pages, 'adsb.fi / adsbdb / OurAirports')
    return cached(('family', flight, callsign, arrival), 20, build)


def nearby(lat, lon):
    airports = AIRPORT_INDEX
    if not airports:
        raise ValueError('Airport index not ready')
    closest = sorted(airports, key=lambda a: distance(lat, lon, a['lat'], a['lon']))[:5]
    return result([page(a['iata'] or a['icao'], a['name'], a['town'],
                        number(distance(lat, lon, a['lat'], a['lon']), ' km from home'),
                        'Scheduled service' if a['scheduled'] else 'Airfield',
                        f"{a['lat']:.3f}, {a['lon']:.3f}", 'Not navigation information') for a in closest], 'OurAirports / public domain')


def load_airports(download):
    global AIRPORT_INDEX
    path = extras.CACHE_DIR / 'airports-index.json'
    if path.exists():
        try:
            cached_data = json.loads(path.read_text())
            if isinstance(cached_data, list) and len(cached_data) <= 60000:
                AIRPORT_INDEX = tuple(cached_data)
            if AIRPORT_INDEX and time.time() - path.stat().st_mtime < 86400:
                return
        except (ValueError, OSError):
            pass
    raw = download('https://davidmegginson.github.io/ourairports-data/airports.csv', 24_000_000)
    rows = []
    for a in csv.DictReader(io.StringIO(raw.decode('utf-8-sig'))):
        if a['type'] not in ('large_airport', 'medium_airport', 'small_airport'):
            continue
        lat, lon = float(a['latitude_deg']), float(a['longitude_deg'])
        if not math.isfinite(lat) or not math.isfinite(lon) or abs(lat)>90 or abs(lon)>180:
            continue
        rows.append({'name': clean(a['name'], 48), 'town': clean(a['municipality'], 48), 'lat': lat, 'lon': lon,
                     'iata': clean(a['iata_code'], 4), 'icao': clean(a.get('icao_code') or a['gps_code'] or a['ident'], 8),
                     'scheduled': a['scheduled_service'] == 'yes'})
        if len(rows)>60000:
            raise ValueError('Airport index too large')
    if not rows:
        raise ValueError('Empty airport index')
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(rows, separators=(',', ':')))
    temporary.replace(path)
    AIRPORT_INDEX = tuple(rows)


def start(download):
    if not AIRPORTS:
        return
    def worker():
        while True:
            try:
                load_airports(download)
            except (OSError, ValueError, KeyError, TypeError) as error:
                print('Airport refresh failed: ' + type(error).__name__, flush=True)
            time.sleep(3600)
    threading.Thread(target=worker, daemon=True).start()
