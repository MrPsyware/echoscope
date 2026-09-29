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


def weather_icon(code):
    if code in (0, 1): return 0
    if code == 2: return 1
    if code == 3: return 2
    if code in (45, 48): return 6
    if code in (71, 73, 75, 77, 85, 86): return 4
    if code in (95, 96, 97, 99): return 5
    if code in (51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82): return 3
    return 7


def weather(lat, lon, download):
    def build():
        query = urlencode({'latitude': lat, 'longitude': lon, 'timezone': 'auto',
                           'forecast_days': 4,
                           'current': 'temperature_2m,weather_code',
                           'hourly': 'temperature_2m,weather_code,cloud_cover,precipitation_probability,wind_speed_10m,is_day',
                           'daily': 'weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,wind_speed_10m_max'})
        data = json.loads(download('https://api.open-meteo.com/v1/forecast?' + query, 65536))
        current, hourly, daily = data['current'], data['hourly'], data['daily']
        now = datetime.fromisoformat(current['time'])
        times = [datetime.fromisoformat(t) for t in hourly['time']]
        future = next((n for n,t in enumerate(times) if t > now), None)
        if future is None or len(daily['time']) < 4:
            raise ValueError('Incomplete forecast')
        def value(group, key, n):
            values = group.get(key, [])
            return values[n] if n < len(values) else None
        def mean(values):
            valid = [x for x in values if isinstance(x, (int,float)) and math.isfinite(x)]
            return sum(valid)/len(valid) if valid else None
        descriptions = ['Clear', 'Partly cloudy', 'Overcast', 'Rain', 'Snow', 'Thunderstorms', 'Fog', 'Unavailable']
        def card(label, code, temperature, cloud, rain, wind, night=False):
            icon = weather_icon(code)
            return {'label': label, 'icon': icon, 'night': night, 'condition': descriptions[icon],
                    'temperature': temperature, 'cloud': 'Cloud ' + number(cloud, '%'),
                    'rain': 'Rain ' + number(rain, '%'), 'wind': 'Wind ' + number(wind, ' km/h')}
        n = future
        hour = card(times[n].strftime('%H:%M'), value(hourly,'weather_code',n),
                    number(value(hourly,'temperature_2m',n),' C'), value(hourly,'cloud_cover',n),
                    value(hourly,'precipitation_probability',n),value(hourly,'wind_speed_10m',n),value(hourly,'is_day',n)==0)
        days=[]
        for n, day in enumerate(daily['time'][:4]):
            cloud = mean([value(hourly,'cloud_cover',j) for j,t in enumerate(times) if t.date().isoformat()==day])
            days.append(card(datetime.fromisoformat(day).strftime('%a %d'),value(daily,'weather_code',n),
                             number(value(daily,'temperature_2m_min',n))+' / '+number(value(daily,'temperature_2m_max',n),' C'),
                             cloud,value(daily,'precipitation_probability_max',n),value(daily,'wind_speed_10m_max',n)))
        # Upcoming night, noon-to-noon, excludes this morning's remaining darkness.
        from datetime import timedelta
        noon = now.replace(hour=12,minute=0,second=0)
        night_cloud = mean([value(hourly,'cloud_cover',j) for j,t in enumerate(times)
                            if noon<=t<noon+timedelta(days=1) and value(hourly,'is_day',j)==0])
        for day in days:
            day['cloud']=day['cloud'].replace('Cloud ', 'Avg cloud ', 1)
        zone = clean(data.get('timezone_abbreviation') or data.get('timezone') or 'Local', 24)
        def weather_page(title, cards, note, valid=None):
            lines=[]
            for c in cards:
                lines.extend([c['label']+' '+c['condition'],c['temperature']+' / '+c['cloud']+' / '+c['rain']])
            p=page(title, *lines, note)
            p.update({'layout':'weather','subtitle':(valid or now).strftime('%d %b')+' / '+zone,'cards':cards,'note':note})
            return p
        return result([weather_page('NEXT HOUR',[hour],'Forecast / local time',times[future]),
                       weather_page('TODAY',[days[0]],'Tonight cloud '+number(night_cloud,'%')),
                       weather_page('NEXT 3 DAYS',days[1:],'Cloud: daily mean / rain: peak chance')], 'Open-Meteo / CC BY 4.0')
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
        return page(flight, 'Unavailable')
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
        tracking = {"fresh": False, "callsign": lookup, "arrival": arrival}
        if len(fresh) == 1:
            a = fresh[0]
            destination = next((x for x in AIRPORT_INDEX if arrival in (x['iata'], x['icao'])), None) if arrival else None
            tracking.update({'fresh': True, 'hex': clean(a.get('hex')), 'registration': clean(a.get('r')), 'type': clean(a.get('t')), 'position_time': feed_time/1000-a['seen_pos'], 'distance_km': distance(a['lat'], a['lon'], destination['lat'], destination['lon']) if destination else None})
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
        out = result(pages, 'adsb.fi / adsbdb / OurAirports')
        for p in out['pages']: p['item']=flight
        out['tracking'] = tracking
        return out
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


def overlay_ready():
    return AIRPORTS and bool(AIRPORT_INDEX) and all('type' in a for a in AIRPORT_INDEX)


def airport_overlay(lat, lon, radius, mode='airline', size='any'):
    if radius not in (5, 10, 25, 50, 100) or mode not in ('airline', 'all') or size not in ('any', 'medium', 'large'):
        raise ValueError('Invalid airport overlay filter')
    if not overlay_ready():
        raise ValueError('Airport overlay unavailable')
    rank={'large_airport':0, 'medium_airport':1, 'small_airport':2}
    selected=[]
    for a in AIRPORT_INDEX:
        level=rank.get(a['type'],3)
        if level>2 or (mode=='airline' and not a['scheduled']) or (size=='medium' and level>1) or (size=='large' and level>0): continue
        km=distance(lat,lon,a['lat'],a['lon'])
        if km<=radius: selected.append((not a['scheduled'],level,km,a['icao'],a))
    selected.sort(key=lambda row:row[:4])
    return {'airports':[{'code':a['iata'] or a['icao'], 'lat':round(a['lat'],6), 'lon':round(a['lon'],6),
                         'size':level, 'scheduled':a['scheduled']} for _,level,_,_,a in selected[:32]],
            'source':'OurAirports', 'range':radius}


def load_airports(download):
    global AIRPORT_INDEX
    path = extras.CACHE_DIR / 'airports-index.json'
    if path.exists():
        try:
            cached_data = json.loads(path.read_text())
            if isinstance(cached_data, list) and len(cached_data) <= 60000:
                AIRPORT_INDEX = tuple(cached_data)
            if AIRPORT_INDEX and all('type' in a for a in AIRPORT_INDEX) and time.time() - path.stat().st_mtime < 86400:
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
        rows.append({'type':a['type'], 'name': clean(a['name'], 48), 'town': clean(a['municipality'], 48), 'lat': lat, 'lon': lon,
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
