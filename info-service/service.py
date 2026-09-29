"""EchoScope LAN information server with bounded, attributed thumbnail caching."""
import html
import os
import extras
import insights
import observing
import integration
import photo_cache
import secrets
import io
import json
import re
import struct
import threading
import time
from collections import OrderedDict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, quote
from urllib.request import Request, build_opener, HTTPRedirectHandler
from urllib.error import HTTPError
from PIL import Image

USER_AGENT = 'EchoScope/0.12.0 (+https://github.com/MrPsyware/echoscope)'
REG = re.compile(r'[A-Z0-9][A-Z0-9-]{0,14}\Z')
CACHE = OrderedDict()
LOCK = threading.Lock()
GATE = threading.BoundedSemaphore(2)
SAT_QUERY_LOCK = threading.Lock()
NEXT_LOOKUP = 0.0
CACHE_TOKEN=secrets.token_hex(24)
WIDTH, HEIGHT = 200, 150
HEADER = struct.Struct('<4sHH128s256s')
Image.MAX_IMAGE_PIXELS = 2_000_000

class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None

OPENER = build_opener(NoRedirect)

def download(url, limit):
    with OPENER.open(Request(url, headers={'User-Agent': USER_AGENT}), timeout=6) as response:
        body = response.read(limit + 1)
        if len(body) > limit:
            raise ValueError('Upstream response too large')
        return body

def safe_url(value, hosts):
    if not isinstance(value, str):
        raise ValueError('Missing upstream URL')
    parsed = urlsplit(value)
    if parsed.scheme != 'https' or parsed.hostname not in hosts or parsed.username or parsed.password or parsed.port not in (None, 443):
        raise ValueError('Unexpected upstream URL')
    return value

def field(text, size):
    return text.encode('utf-8')[:size-1].decode('utf-8', 'ignore').encode('utf-8').ljust(size, b'\0')

def make_packet(raw, photographer, link):
    with Image.open(io.BytesIO(raw)) as source:
        if source.format not in ('JPEG', 'PNG', 'WEBP'):
            raise ValueError('Unsupported photo format')
        image = source.convert('RGB')
        image.thumbnail((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
        width, height = image.size
        pixels = bytearray()
        for r, g, b in image.get_flattened_data():
            pixels.extend(struct.pack('>H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
    return HEADER.pack(b'ECP1', width, height, field(photographer, 128), field(link, 256)) + pixels

def photo(registration):
    global NEXT_LOOKUP
    with LOCK:
        cached = CACHE.get(registration)
        if cached and cached[0] > time.monotonic():
            CACHE.move_to_end(registration)
            return cached[1:]
        saved=photo_cache.read(registration)
        if saved:
            CACHE[registration]=(time.monotonic()+300,*saved)
            while len(CACHE)>32: CACHE.popitem(last=False)
            return saved
        # Serialize upstream lookups and allow at most one new lookup per second.
        time.sleep(max(0, NEXT_LOOKUP - time.monotonic()))
        NEXT_LOOKUP = time.monotonic() + 1
        data = json.loads(download('https://api.planespotters.net/pub/photos/reg/' + quote(registration), 65536))
        photos = data.get('photos')
        if not isinstance(photos, list):
            raise ValueError('Photo provider returned an error')
        if not photos:
            result = (None, None, None)
        else:
            item = photos[0]
            url = safe_url(item['thumbnail']['src'], {'t.plnspttrs.net', 'cdn.planespotters.net'})
            link = safe_url(item['link'], {'www.planespotters.net'})
            credit = item['photographer']
            if not isinstance(credit, str) or not credit.strip() or len(credit.encode('utf-8')) >= 128 or len(link.encode('utf-8')) >= 256:
                raise ValueError('Photo attribution is missing or too long')
            result = (make_packet(download(url, 512000), credit, link), credit, link)
        if result[0] is not None:
            try: photo_cache.write(registration,result[0])
            except OSError: print('Photo disk cache unavailable; serving uncached',flush=True)
        CACHE[registration] = (time.monotonic() + 300, *result)
        CACHE.move_to_end(registration)
        while len(CACHE) > 32:
            CACHE.popitem(last=False)
        return result

def packet_png(packet,offset,width,height):
    import numpy as np
    rgb=np.frombuffer(packet[offset:],dtype='>u2').reshape(height,width)
    pixels=np.stack([((rgb>>11)&31)*255//31,((rgb>>5)&63)*255//63,(rgb&31)*255//31],axis=-1).astype('uint8')
    out=io.BytesIO(); Image.fromarray(pixels).save(out,format='PNG'); return out.getvalue()

class Handler(BaseHTTPRequestHandler):
    def log_request(self, code='-', size='-'):
        # Keep flight numbers and home coordinates out of normal access logs.
        self.log_message('"%s %s" %s %s', self.command, urlsplit(self.path).path, code, size)

    def reply(self, code, body, mime):
        self.send_response(code)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if urlsplit(self.path).path!='/cache/clear': return self.reply(404,b'Not found','text/plain')
        from urllib.parse import parse_qs
        try: size=int(self.headers.get('Content-Length','0'))
        except ValueError: return self.reply(400,b'Invalid request','text/plain')
        if not 0<size<=256: return self.reply(400,b'Invalid request','text/plain')
        try: token=parse_qs(self.rfile.read(size).decode()).get('token',[''])[0]
        except UnicodeError: return self.reply(400,b'Invalid form','text/plain')
        if not secrets.compare_digest(token,CACHE_TOKEN): return self.reply(403,b'Reload cache controls','text/plain')
        with LOCK:
            photo_cache.clear(); CACHE.clear()
        return self.reply(200,b'Photo cache cleared. Photos will be downloaded again when viewed.','text/plain')

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == '/cache':
            body='<meta name="viewport" content="width=device-width"><h1>Photo cache</h1><p>Thumbnails are cached for one week, up to 256 photos. Clearing photos preserves maps, orbital data and your logbook.</p><form method="post" action="/cache/clear"><input type="hidden" name="token" value="'+CACHE_TOKEN+'"><button>Clear photo cache</button></form>'
            return self.reply(200,body.encode(),'text/html; charset=utf-8')
        if path == '/health':
            body = {'service': 'echoscope-photos', 'name': 'EchoScope Info Server', 'protocol': 1,
                    'capabilities': {'photos': os.environ.get('ENABLE_PHOTOS', '1') == '1',
                                     'maps': extras.MAPS, 'satellites': bool(extras.available_satellites()),
                                     'stargazing': observing.ready(), 'highlights': integration.ready(),
                                     'weather': insights.WEATHER, 'flights': insights.FLIGHTS,
                                     'airport_overlay': insights.overlay_ready(),
                                     'airports': insights.AIRPORTS and bool(insights.AIRPORT_INDEX)},
                    'map_credit': extras.MAP_CREDIT}
            return self.reply(200, json.dumps(body).encode(), 'application/json')
        if path.startswith(('/v1/logbook/','/v1/logmap/','/sighting/','/sighting-map/')):
            if integration.STORE is None: return self.reply(404,b'Logbook unavailable','text/plain')
            if not GATE.acquire(blocking=False): return self.reply(503,b'Busy','text/plain')
            try:
                entry_id=int(path.rsplit('/',1)[-1]); row=integration.STORE.entry(entry_id)
                if path.startswith('/v1/logbook/'):
                    body=integration.STORE.detail(entry_id,download)
                    return self.reply(200,json.dumps(body,allow_nan=False).encode(),'application/json')
                if path.startswith(('/v1/logmap/','/sighting-map/')):
                    if not extras.MAPS or row['lat'] is None: return self.reply(404,b'Map unavailable','text/plain')
                    radius=integration.STORE.track_range(row)
                    code,packet=extras.map_response(row['lat'],row['lon'],radius,download)
                    if path.startswith('/v1/logmap/') or code!=200: return self.reply(code,packet,'application/octet-stream')
                    return self.reply(200,packet_png(packet,8,420,420),'image/png')
                body=integration.STORE.web_entry(entry_id,download,photo if os.environ.get('ENABLE_PHOTOS','1')=='1' else None)
                return self.reply(200,body.encode(),'text/html; charset=utf-8')
            except (ValueError,KeyError): return self.reply(404,b'Log entry unavailable','text/plain')
            except (OSError,TypeError): return self.reply(502,b'Log entry temporarily unavailable','text/plain')
            finally: GATE.release()
        if path == '/sightings':
            if integration.STORE is None: return self.reply(404,b'Configure device integration first','text/plain')
            from urllib.parse import parse_qs
            try: offset=max(0,min(10000,int(parse_qs(urlsplit(self.path).query).get('offset',['0'])[0])))
            except ValueError: return self.reply(400,b'Invalid offset','text/plain')
            return self.reply(200,integration.STORE.web(offset).encode(),'text/html; charset=utf-8')
        if path == '/v1/highlights':
            if not integration.ready(): return self.reply(404,b'Device integration unavailable','text/plain')
            return self.reply(200,json.dumps(integration.STORE.highlights()).encode(),'application/json')
        if path == '/v1/stargazing':
            if not observing.ready(): return self.reply(404,b'Astronomy starting or disabled','text/plain')
            if not GATE.acquire(blocking=False): return self.reply(503,b'Busy','text/plain')
            try:
                lat,lon,_=extras.location(urlsplit(self.path).query)
                body=observing.outlook(lat,lon,download)
                return self.reply(200,json.dumps(body,allow_nan=False).encode(),'application/json')
            except (OSError,ValueError,KeyError,TypeError):
                return self.reply(502,b'Observing data unavailable','text/plain')
            finally: GATE.release()
        if path == '/v1/airport-overlay':
            if not insights.overlay_ready(): return self.reply(404,b'Airport overlay unavailable','text/plain')
            if not GATE.acquire(blocking=False): return self.reply(503,b'Busy','text/plain')
            try:
                lat,lon,query=extras.location(urlsplit(self.path).query)
                body=insights.airport_overlay(lat,lon,int(query['range'][0]),query.get('mode',['airline'])[0],query.get('size',['any'])[0])
                return self.reply(200,json.dumps(body,allow_nan=False).encode(),'application/json')
            except (ValueError,KeyError,TypeError):
                return self.reply(400,b'Invalid airport overlay query','text/plain')
            finally: GATE.release()
        if path in ('/v1/weather', '/v1/family', '/v1/route', '/v1/airports'):
            enabled = insights.WEATHER if path.endswith('weather') else insights.AIRPORTS and bool(insights.AIRPORT_INDEX) if path.endswith('airports') else insights.FLIGHTS
            if not enabled:
                return self.reply(404, b'Capability unavailable', 'text/plain')
            if not GATE.acquire(blocking=False):
                return self.reply(503, b'Busy', 'text/plain')
            try:
                from urllib.parse import parse_qs
                query = parse_qs(urlsplit(self.path).query)
                if path.endswith('weather') or path.endswith('airports'):
                    lat, lon, query = extras.location(urlsplit(self.path).query)
                    body = insights.weather(lat, lon, download) if path.endswith('weather') else insights.nearby(lat, lon)
                elif path.endswith('route'):
                    body = insights.route(query.get('flight', [''])[0], download)
                else:
                    body = insights.family(query.get('flight', [''])[0], query.get('callsign', [''])[0], query.get('arrival', [''])[0], download)
                return self.reply(200, json.dumps(body, allow_nan=False).encode(), 'application/json')
            except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
                print(f'Info lookup {path} failed: {type(error).__name__}: {error}', flush=True)
                return self.reply(502, b'Information unavailable; try again shortly', 'text/plain')
            finally:
                GATE.release()
        if path in ('/v1/map', '/v1/satellites'):
            if (path.endswith('map') and not extras.MAPS) or (path.endswith('satellites') and not extras.available_satellites()):
                return self.reply(404, b'Capability unavailable', 'text/plain')
            if not GATE.acquire(blocking=False):
                return self.reply(503, b'Busy', 'text/plain')
            try:
                lat, lon, query = extras.location(urlsplit(self.path).query)
                if path.endswith('map'):
                    code, body = extras.map_response(lat, lon, int(query['range'][0]), download)
                    return self.reply(code, body, 'application/octet-stream')
                with SAT_QUERY_LOCK:
                    body = json.dumps(extras.satellite_response(lat, lon)).encode()
                return self.reply(200, body, 'application/json')
            except (ValueError, KeyError, OSError, TypeError):
                return self.reply(400, b'Invalid query or unavailable data', 'text/plain')
            finally:
                GATE.release()
        if path == '/':
            return self.reply(200, b'<h1>EchoScope Info Server</h1><p>Photos, maps, station predictions, weather and flight information. Weather: <a href="https://open-meteo.com/">Open-Meteo</a> (CC BY 4.0). Flights: <a href="https://adsb.fi/">adsb.fi</a> and <a href="https://www.adsbdb.com/">adsbdb</a>. Airports: <a href="https://ourairports.com/data/">OurAirports</a>. Map data: &copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap contributors</a>. Orbits: CelesTrak / SGP4.</p><p><a href="/sightings">Spotting history</a> / <a href="/cache">Photo cache controls</a>. Service ready. Set this server URL in EchoScope setup.</p><p>Photo credits and original links: /photo/REGISTRATION</p>', 'text/html; charset=utf-8')
        png=path.startswith("/image/")
        binary = path.startswith('/v1/photo/')
        if not binary and not png and not path.startswith('/photo/'):
            return self.reply(404, b'Not found', 'text/plain')
        if os.environ.get('ENABLE_PHOTOS', '1') != '1':
            return self.reply(404, b'Photos disabled', 'text/plain')
        reg = path.rsplit('/', 1)[-1].upper()
        if not REG.fullmatch(reg):
            return self.reply(400, b'Invalid registration', 'text/plain')
        if not GATE.acquire(blocking=False):
            return self.reply(503, b'Busy; try again shortly', 'text/plain')
        try:
            packet, credit, link = photo(reg)
            if packet is None:
                return self.reply(404, b'No photo available', 'text/plain')
            if png:
                _,w,h=struct.unpack_from('<4sHH',packet)
                return self.reply(200,packet_png(packet,392,w,h),'image/png')
            if binary:
                return self.reply(200, packet, 'application/x-echoscope-rgb565')
            page = f'<h1>{html.escape(reg)}</h1><p>Photo: &copy; {html.escape(credit)} / Planespotters.net</p><p><a href="{html.escape(link, quote=True)}">View original photo</a></p>'
            self.reply(200, page.encode(), 'text/html; charset=utf-8')
        except (HTTPError, OSError, ValueError, KeyError, TypeError) as error:
            print(f'Photo lookup failed: {type(error).__name__}', flush=True)
            self.reply(502, b'Photo provider unavailable', 'text/plain')
        finally:
            GATE.release()

if __name__ == '__main__':
    extras.start(download)
    insights.start(download)
    observing.start(download)
    integration.start(download)
    ThreadingHTTPServer(('0.0.0.0', 8086), Handler).serve_forever()
