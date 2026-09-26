"""EchoScope LAN photo adapter. No photographs are stored on disk."""
import html
import os
import extras
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

USER_AGENT = 'EchoScope/0.6.0 (+https://github.com/MrPsyware/echoscope)'
REG = re.compile(r'[A-Z0-9][A-Z0-9-]{0,14}\Z')
CACHE = OrderedDict()
LOCK = threading.Lock()
GATE = threading.BoundedSemaphore(2)
SAT_QUERY_LOCK = threading.Lock()
NEXT_LOOKUP = 0.0
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
        CACHE[registration] = (time.monotonic() + 300, *result)
        CACHE.move_to_end(registration)
        while len(CACHE) > 32:
            CACHE.popitem(last=False)
        return result

class Handler(BaseHTTPRequestHandler):
    def reply(self, code, body, mime):
        self.send_response(code)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == '/health':
            body = {'service': 'echoscope-photos', 'name': 'EchoScope Info Server', 'protocol': 1,
                    'capabilities': {'photos': os.environ.get('ENABLE_PHOTOS', '1') == '1',
                                     'maps': extras.MAPS, 'satellites': bool(extras.available_satellites())},
                    'map_credit': extras.MAP_CREDIT}
            return self.reply(200, json.dumps(body).encode(), 'application/json')
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
            return self.reply(200, b'<h1>EchoScope Info Server</h1><p>Photos, maps and station predictions. Map data: &copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap contributors</a>. Orbits: CelesTrak / SGP4.</p><p>Service ready. Set this server URL in EchoScope setup.</p><p>Photo credits and original links: /photo/REGISTRATION</p>', 'text/html; charset=utf-8')
        binary = path.startswith('/v1/photo/')
        if not binary and not path.startswith('/photo/'):
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
    ThreadingHTTPServer(('0.0.0.0', 8086), Handler).serve_forever()
