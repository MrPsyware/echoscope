"""Bounded SQLite sighting history and durable alert deduplication."""
import html
import json
import re
from urllib.parse import quote
import math
import sqlite3
import threading
import time
from datetime import datetime, timezone
import extras
import insights

class Store:
    def __init__(self, path=None):
        self.lock=threading.RLock()
        path=path or extras.CACHE_DIR/'sightings.sqlite3'
        if str(path)!=':memory:': path.parent.mkdir(parents=True,exist_ok=True)
        self.db=sqlite3.connect(str(path),check_same_thread=False)
        self.db.row_factory=sqlite3.Row
        self.db.executescript('''CREATE TABLE IF NOT EXISTS sightings (
          day TEXT, hex TEXT, registration TEXT, callsign TEXT, type TEXT,
          reason TEXT, first REAL, last REAL, closest REAL, PRIMARY KEY(day,hex));
          CREATE TABLE IF NOT EXISTS alerts (key TEXT PRIMARY KEY, sent REAL);
          CREATE TABLE IF NOT EXISTS entries (
            id INTEGER PRIMARY KEY AUTOINCREMENT, day TEXT, hex TEXT, registration TEXT,
            callsign TEXT, type TEXT, reason TEXT, first REAL, last REAL, closest REAL,
            lat REAL, lon REAL, track TEXT DEFAULT '[]', route TEXT, route_time REAL);
          CREATE INDEX IF NOT EXISTS entry_lookup ON entries(hex,last);
          CREATE TABLE IF NOT EXISTS migrations (name TEXT PRIMARY KEY);
          ''')
        with self.db:
            if not self.db.execute("SELECT 1 FROM migrations WHERE name='encounters'").fetchone():
                self.db.execute('INSERT INTO entries(day,hex,registration,callsign,type,reason,first,last,closest) SELECT day,hex,registration,callsign,type,reason,first,last,closest FROM sightings')
                self.db.execute("INSERT INTO migrations VALUES ('encounters')")
                self.db.execute('DELETE FROM sightings')
    def ingest(self,state,now=None):
        now=now or time.time()
        if state.get('demo'): return
        day=datetime.fromtimestamp(now,timezone.utc).strftime('%Y-%m-%d')
        with self.lock,self.db:
            for a in state.get('aircraft',[])[:64]:
                age=a.get('age_s'); km=a.get('distance_km')
                if not all(isinstance(x,(int,float)) and math.isfinite(x) for x in (age,km)) or not 0<=age<=20 or km<0: continue
                reasons=[label for key,label in [('watched','Watchlist'),('military','Military'),('helicopter','Helicopter')] if a.get(key)]
                if not reasons or not a.get('hex'): continue
                lat,lon=state.get('lat'),state.get('lon')
                if not all(isinstance(v,(int,float)) and math.isfinite(v) for v in (lat,lon)): lat=lon=None
                call=insights.clean(a.get('callsign'),16); hexid=insights.clean(a['hex'],12)
                old=self.db.execute('SELECT * FROM entries WHERE hex=? ORDER BY last DESC LIMIT 1',(hexid,)).fetchone()
                continuous=old and now-old['last']<900 and old['day']==day and (not call or not old['callsign'] or old['callsign']==call) and old['lat']==lat and old['lon']==lon
                if not continuous:
                    cursor=self.db.execute('INSERT INTO entries(day,hex,registration,callsign,type,reason,first,last,closest,lat,lon) VALUES (?,?,?,?,?,?,?,?,?,?,?)',
                        (day,hexid,insights.clean(a.get('registration'),16),call,insights.clean(a.get('type'),12),', '.join(reasons),now-age,now-age,km,lat,lon))
                    entry_id=cursor.lastrowid; points=[]
                else:
                    entry_id=old['id']; points=json.loads(old['track'])
                east,north=a.get('east_km'),a.get('north_km'); stamp=now-age
                if lat is not None and all(isinstance(v,(int,float)) and math.isfinite(v) and abs(v)<=150 for v in (east,north)):
                    # Keep gaps explicit and bound long encounters by decimating the entire path.
                    if not points or (stamp-points[-1][0]>=10 and math.hypot(east-points[-1][1],north-points[-1][2])>=.02):
                        gap=bool(points and old and stamp-old['last']>60)
                        if len(points)>=192:
                            gap=gap or points[-1][3]
                            sampled=[]
                            for n in range(0,len(points),2):
                                point=list(points[n]); point[3]=any(p[3] for p in points[max(0,n-1):n+1]); sampled.append(point)
                            points=sampled
                        points.append([round(stamp,1),round(east,3),round(north,3),gap])
                self.db.execute("UPDATE entries SET last=max(last,?),closest=min(closest,?),track=?,callsign=coalesce(nullif(?,''),callsign),registration=coalesce(nullif(?,''),registration),type=coalesce(nullif(?,''),type) WHERE id=?",
                    (stamp,km,json.dumps(points,separators=(',',':')),call,insights.clean(a.get('registration'),16),insights.clean(a.get('type'),12),entry_id))
            self.db.execute('DELETE FROM entries WHERE last<?',(now-30*86400,))
            self.db.execute('DELETE FROM entries WHERE id NOT IN (SELECT id FROM entries ORDER BY last DESC LIMIT 10000)')
            self.db.execute('DELETE FROM alerts WHERE sent<?',(now-2*86400,))
    def seen(self,key,max_age=172800):
        with self.lock: return self.db.execute('SELECT 1 FROM alerts WHERE key=? AND sent>?',(key,time.time()-max_age)).fetchone() is not None
    def mark(self,key):
        with self.lock,self.db:
            self.db.execute('INSERT OR REPLACE INTO alerts VALUES (?,?)',(key,time.time()))
            self.db.execute('DELETE FROM alerts WHERE sent<?',(time.time()-2*86400,))
    def rows(self,today=False,limit=500):
        with self.lock:
            where='WHERE day=?' if today else ''
            args=(datetime.now(timezone.utc).strftime('%Y-%m-%d'),) if today else ()
            return [dict(r) for r in self.db.execute(f'SELECT * FROM entries {where} ORDER BY last DESC LIMIT ?',(*args,min(limit,500)))]
    def entry(self,entry_id):
        with self.lock:
            row=self.db.execute('SELECT * FROM entries WHERE id=?',(int(entry_id),)).fetchone()
            if row is None: raise KeyError('Entry not found')
            out=dict(row); out['track']=json.loads(out['track']); out['route']=json.loads(out['route']) if out['route'] else None
            return out
    def capture_route(self,download,entry_id=None):
        with self.lock:
            row=self.db.execute("SELECT * FROM entries WHERE route IS NULL AND callsign!='' " + ('AND id=? ' if entry_id else '') + "ORDER BY last DESC LIMIT 1", (entry_id,) if entry_id else ()).fetchone()
        if row is None or not re.fullmatch('[A-Z0-9]{2,10}',row['callsign']): return
        route=insights.route_data(row['callsign'],download)
        with self.lock,self.db:
            self.db.execute('UPDATE entries SET route=?,route_time=? WHERE id=? AND route IS NULL',(json.dumps(route),time.time(),row['id']))
    def detail(self,entry_id,download):
        try:
            if insights.FLIGHTS: self.capture_route(download,entry_id)
        except (OSError,ValueError,KeyError,TypeError): pass
        row=self.entry(entry_id)
        route=insights.route_page(row['callsign'],row['route']) if row['route'] is not None else insights.page('FLIGHT ROUTE','Unavailable')
        if row['route']:
            route['lines']=route['lines'][:5]+['Database route; not a filed flight plan']
            if row['route_time']: route['lines'].append('Looked up '+insights.clock(row['route_time']))
        radius=self.track_range(row)
        result=insights.result([route],'EchoScope / adsbdb')
        result.update({'id':row['id'],'registration':row['registration'],'lat':row['lat'],'lon':row['lon'],'range':radius,
                       'track':[[p[1],p[2],p[3]] for p in row['track']], 'first':int(row['first']),'last':int(row['last'])})
        return result
    @staticmethod
    def track_range(row):
        extent=max((math.hypot(p[1],p[2]) for p in row['track']),default=0)
        return next((n for n in (5,10,25,50,100) if n>=extent*1.05),100)
    def web_entry(self,entry_id,download,photo=None):
        detail=self.detail(entry_id,download); row=self.entry(entry_id); esc=html.escape
        out='<meta name="viewport" content="width=device-width"><style>body{font:17px system-ui;background:#030d10;color:#e8f8f4;max-width:750px;margin:30px auto;padding:20px}a{color:#68f3ae}img{max-width:100%}svg{max-width:466px;width:100%}</style><a href="/sightings">Logbook</a><h1>'+esc(row['registration'] or row['hex'])+'</h1><p>'+esc(row['callsign']+' / '+row['type'])+'</p>'
        if photo and row['registration']:
            try:
                packet,credit,link=photo(row['registration'])
                if packet: out+='<img src="/image/'+quote(row['registration'])+'" alt="'+esc(row['registration'])+'"><p>Copyright '+esc(credit)+' / <a href="'+esc(link,quote=True)+'">Planespotters.net original</a></p>'
            except (OSError,ValueError,KeyError,TypeError): pass
        out+='<p>'+esc(row['reason'])+' / closest '+str(round(row['closest'],1))+' km</p><p>'+insights.clock(row['first'])+' to '+insights.clock(row['last'])+'</p><h2>Flight route</h2>'
        out+=''.join('<p>'+esc(line)+'</p>' for line in detail['pages'][0]['lines'])
        radius=detail['range']; out+='<h2>Observed track</h2><p>North up; green start / orange last seen. Gaps in reception are not joined.</p><svg viewBox="0 0 466 466" xmlns="http://www.w3.org/2000/svg"><defs><clipPath id="scope"><circle cx="233" cy="233" r="210"/></clipPath></defs><g clip-path="url(#scope)"><image id="map" x="23" y="23" width="420" height="420"/>'
        for r in (52,105,157,210): out+=f'<circle cx="233" cy="233" r="{r}" fill="none" stroke="#143b36"/>'
        out+='<path d="M23 233H443 M233 23V443" stroke="#143b36"/>'
        def xy(p): return (233+p[1]/radius*210,233-p[2]/radius*210)
        for a,b in zip(row['track'],row['track'][1:]):
            if b[3]: continue
            x,y=xy(a); xx,yy=xy(b); out+=f'<path d="M{x:.1f} {y:.1f} L{xx:.1f} {yy:.1f}" fill="none" stroke="#f2bb70" stroke-width="2"/>'
        if row['track']:
            for point,color in [(row['track'][0],'#68f3ae'),(row['track'][-1],'#f2bb70')]:
                x,y=xy(point); out+=f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{color}"/>'
        out+='</g></svg><p>'+str(radius)+' km / '+str(len(row['track']))+' recorded points. '+esc(extras.MAP_CREDIT if extras.MAPS else 'Radar grid')+'</p>'
        if not row['track']: out+='<p>No recorded path for this entry. Older entries cannot recover past positions.</p>'
        if extras.MAPS and row['lat'] is not None:
            out+="<script>let tries=0;async function map(){try{let r=await fetch('/sighting-map/"+str(entry_id)+"');if(r.status===202&&tries++<12){setTimeout(map,2000);return}if(r.ok&&r.headers.get('Content-Type')==='image/png')document.getElementById('map').setAttribute('href',URL.createObjectURL(await r.blob()))}catch(e){}}map()</script>"
        return out
    def highlights(self):
        rows=self.rows(False,8)
        pages=[]
        for r in rows:
            p=insights.page(r['registration'] or r['callsign'] or r['hex'],r['type']+' / '+r['callsign'],r['reason'],
                            f"Closest {r['closest']:.1f} km",'First '+insights.clock(r['first']),'Last '+insights.clock(r['last']))
            p.update({'entry_id':r['id'],'registration':r['registration'],'item':str(r['id'])}); pages.append(p)
        return insights.result(pages or [insights.page('LOGBOOK','No interesting sightings recorded yet','Watchlist / military / helicopters','Recorded while device feed is awake')],'EchoScope logbook / UTC')
    def web(self,offset=0):
        # Paged full history, at most 500 rows per response.
        with self.lock:
            rows=[dict(r) for r in self.db.execute('SELECT * FROM entries ORDER BY last DESC LIMIT 500 OFFSET ?',(offset,))]
        fields=['day','registration','callsign','type','reason','closest','first','last']
        out='<meta name="viewport" content="width=device-width"><title>EchoScope sightings</title><h1>Spotting log</h1><p>30 days, up to 10,000 encounters. Times UTC; closest observed distance. Watchlist, military and helicopters in the received feed.</p><table><tr>'+''.join('<th>'+x+'</th>' for x in fields)+'</tr>'
        for row in rows:
            row['closest']=f"{row['closest']:.1f} km"; row['first']=insights.clock(row['first']); row['last']=insights.clock(row['last'])
            out+='<tr>'+''.join('<td>'+('<a href="/sighting/'+str(row['id'])+'">'+html.escape(str(row[k]))+'</a>' if k in ('day','registration') else html.escape(str(row[k])))+'</td>' for k in fields)+'</tr>'
        return out+'</table>'+ (f'<a href="?offset={offset+500}">Older sightings</a>' if len(rows)==500 else '')
