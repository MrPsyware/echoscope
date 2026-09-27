"""Darkness, Moon and sunlit station passes. Ephemeris/cache live on the server."""
import json
import math
import os
import threading
import time
from datetime import datetime, timezone, timedelta
from urllib.parse import urlencode
import numpy as np
from skyfield import almanac
from skyfield.api import load_file, wgs84
import extras
import insights

ENABLED = os.environ.get('ENABLE_STARGAZING', '1') == '1'
EPHEMERIS = None
LOCK = threading.RLock()


def start(download):
    if not ENABLED:
        return
    def worker():
        global EPHEMERIS
        path = extras.CACHE_DIR / 'de421.bsp'
        while EPHEMERIS is None:
            try:
                path.parent.mkdir(parents=True, exist_ok=True)
                if not path.exists():
                    raw = download('https://ssd.jpl.nasa.gov/ftp/eph/planets/bsp/de421.bsp', 20_000_000)
                    tmp = path.with_name('de421-download.bsp')
                    tmp.write_bytes(raw)
                    load_file(str(tmp)).close()  # Validate before replacing the cache.
                    tmp.replace(path)
                EPHEMERIS = load_file(str(path))
            except (OSError, ValueError) as error:
                print('Ephemeris unavailable: '+type(error).__name__, flush=True)
                time.sleep(300)
    threading.Thread(target=worker, daemon=True).start()


def ready():
    return ENABLED and EPHEMERIS is not None


def direction(degrees):
    return ('N','NE','E','SE','S','SW','W','NW')[int((degrees+22.5)//45)%8]


def forecast(lat, lon, download):
    def build():
        query = urlencode({'latitude':lat, 'longitude':lon, 'timezone':'UTC', 'forecast_days':3,
                           'hourly':'cloud_cover'})
        raw=json.loads(download('https://api.open-meteo.com/v1/forecast?'+query,65536))
        return [(datetime.fromisoformat(t).replace(tzinfo=timezone.utc).timestamp(), c)
                for t,c in zip(raw['hourly']['time'],raw['hourly']['cloud_cover'])
                if isinstance(c,(int,float)) and math.isfinite(c) and 0<=c<=100]
    return insights.cached(('clouds',lat,lon),900,build)


def visible_segments(elevation, sunlit, sun_elevation):
    visible=(np.asarray(elevation)>=10) & np.asarray(sunlit,dtype=bool) & (np.asarray(sun_elevation)<-6)
    starts=np.flatnonzero(visible & ~np.r_[False,visible[:-1]])
    ends=np.flatnonzero(visible & ~np.r_[visible[1:],False])
    return list(zip(starts,ends))


def outlook(lat, lon, download):
    if not ready():
        raise ValueError('Ephemeris not ready')
    def build():
        with LOCK:
            return calculate(lat,lon,download)
    return insights.cached(('observing',lat,lon),60,build)


def calculate(lat, lon, download):
    now=datetime.now(timezone.utc)
    t0=extras.TS.from_datetime(now); t1=extras.TS.from_datetime(now+timedelta(hours=24))
    place=wgs84.latlon(lat,lon); observer=EPHEMERIS['earth']+place
    def sun_alt(t):
        return observer.at(t).observe(EPHEMERIS['sun']).apparent().altaz()[0].degrees
    times,states=almanac.find_discrete(t0,t1,almanac.dark_twilight_day(EPHEMERIS,place))
    dark_now=float(sun_alt(t0)) < -18
    darkness=[]; start=now.timestamp() if dark_now else None
    for t,state in zip(times,states):
        stamp=t.utc_datetime().timestamp()
        if int(state)==0: start=stamp
        elif start is not None:
            darkness.append((start,stamp)); start=None
    if start is not None: darkness.append((start,(now+timedelta(hours=24)).timestamp()))
    moon=almanac.fraction_illuminated(EPHEMERIS,'moon',t0)*100
    try:
        clouds=forecast(lat,lon,download) if insights.WEATHER else []
    except (OSError,ValueError,KeyError,TypeError):
        clouds=[]
    def cloud_at(stamp):
        if not clouds: return None
        near=min(clouds,key=lambda x:abs(x[0]-stamp))
        return round(near[1]) if abs(near[0]-stamp)<=3600 else None
    night_cloud=[c for stamp,c in clouds if any(a<=stamp<b for a,b in darkness)]
    lines=[f'Moon illumination {moon:.0f}%', 'Astronomical darkness (Sun below -18 deg)']
    if darkness:
        for a,b in darkness[:2]:
            continues=b >= (now+timedelta(hours=24)).timestamp()
            lines.append(insights.clock(a)+(' / continues past 24h window' if continues else ' - '+datetime.fromtimestamp(b,timezone.utc).strftime('%H:%M UTC')))
    else: lines.append('No astronomical darkness in next 24h')
    lines.append('Dark-hour cloud '+(f'{sum(night_cloud)/len(night_cloud):.0f}% average' if night_cloud else 'unavailable'))
    if clouds and darkness:
        best=min(((stamp,c) for stamp,c in clouds if any(a<=stamp<b for a,b in darkness)),key=lambda x:x[1],default=None)
        if best: lines.append('Clearest: '+insights.clock(best[0])+f' / {best[1]:.0f}%')
    lines.append('Forecast is not a visibility guarantee')
    pages=[insights.page('STARGAZING / NEXT 24H',*lines)]
    passes=[]
    # Sample at 15 s so partial illumination/twilight passes are included.
    ts=extras.TS.from_datetimes([now+timedelta(seconds=i) for i in range(0,86401,15)])
    sun_elevation=sun_alt(ts)
    for sat in extras.available_satellites():
        top=(sat-place).at(ts); el,az,_=top.altaz()
        for first,last in visible_segments(el.degrees,sat.at(ts).is_sunlit(EPHEMERIS),sun_elevation):
            peak=first+int(np.argmax(el.degrees[first:last+1]))
            stamp=now.timestamp()+int(first)*15
            passes.append({'id':f'{sat.model.satnum}:{int((stamp+30)//60)}', 'norad':sat.model.satnum,
                           'name':insights.clean(sat.name,24),'start':int(stamp),'end':int(now.timestamp()+int(last)*15),
                           'direction':direction(float(az.degrees[first])), 'max_elevation':round(float(el.degrees[peak])),
                           'cloud':cloud_at(stamp)})
    passes.sort(key=lambda p:p['start']); passes=passes[:8]
    for p in passes:
        pages.append(insights.page(p['name']+' / LOOK UP',insights.clock(p['start']),
                                   f"Look {p['direction']} / peak {p['max_elevation']} deg",
                                   'Sunlit station / Sun below -6 deg',
                                   'Cloud '+(str(p['cloud'])+'%' if p['cloud'] is not None else 'unavailable'),
                                   'Above 10 deg / times approx. 15s', 'May be obscured by cloud or buildings'))
    if not passes: pages.append(insights.page('VISIBLE STATION PASSES','None predicted in next 24 hours','Requires fresh station orbit data','Geometric passes remain in Stations'))
    out=insights.result(pages,'Skyfield / JPL / CelesTrak / Open-Meteo')
    out.update({'passes':passes,'moon_percent':round(float(moon)), 'darkness':darkness})
    return out
