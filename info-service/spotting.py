"""Bounded SQLite sighting history and durable alert deduplication."""
import html
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
          CREATE TABLE IF NOT EXISTS alerts (key TEXT PRIMARY KEY, sent REAL);''')
    def ingest(self,state,now=None):
        now=now or time.time()
        if state.get('demo') or not state.get('screen'): return
        day=datetime.fromtimestamp(now,timezone.utc).strftime('%Y-%m-%d')
        with self.lock,self.db:
            for a in state.get('aircraft',[])[:64]:
                age=a.get('age_s'); km=a.get('distance_km')
                if not all(isinstance(x,(int,float)) and math.isfinite(x) for x in (age,km)) or not 0<=age<=20 or km<0: continue
                reasons=[label for key,label in [('watched','Watchlist'),('military','Military'),('helicopter','Helicopter')] if a.get(key)]
                if not reasons or not a.get('hex'): continue
                self.db.execute('''INSERT INTO sightings VALUES (?,?,?,?,?,?,?,?,?)
                    ON CONFLICT(day,hex) DO UPDATE SET last=excluded.last,closest=min(closest,excluded.closest),
                    registration=excluded.registration,callsign=excluded.callsign,type=excluded.type,reason=excluded.reason''',
                    (day,insights.clean(a['hex'],12),insights.clean(a.get('registration'),16),insights.clean(a.get('callsign'),16),
                     insights.clean(a.get('type'),12),', '.join(reasons),now-age,now-age,km))
            self.db.execute('DELETE FROM sightings WHERE last<?',(now-30*86400,))
            self.db.execute('DELETE FROM sightings WHERE rowid NOT IN (SELECT rowid FROM sightings ORDER BY last DESC LIMIT 10000)')
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
            return [dict(r) for r in self.db.execute(f'SELECT * FROM sightings {where} ORDER BY last DESC LIMIT ?',(*args,min(limit,500)))]
    def highlights(self):
        rows=self.rows(True,8)
        pages=[insights.page(r['registration'] or r['callsign'] or r['hex'],r['type']+' / '+r['callsign'],r['reason'],
                            f"Closest {r['closest']:.1f} km",'First '+insights.clock(r['first']),'Last '+insights.clock(r['last'])) for r in rows]
        return insights.result(pages or [insights.page("TODAY'S HIGHLIGHTS",'No interesting sightings recorded yet','Watchlist / military / helicopters','Recorded while device feed is awake','Day boundaries use UTC')],'EchoScope spotting log / UTC')
    def web(self,offset=0):
        # Paged full history, at most 500 rows per response.
        with self.lock:
            rows=[dict(r) for r in self.db.execute('SELECT * FROM sightings ORDER BY last DESC LIMIT 500 OFFSET ?',(offset,))]
        fields=['day','registration','callsign','type','reason','closest','first','last']
        out='<meta name="viewport" content="width=device-width"><title>EchoScope sightings</title><h1>Spotting log</h1><p>30 days, up to 10,000 aircraft/day records. Times UTC; closest observed distance. Watchlist, military and helicopters in the received feed.</p><table><tr>'+''.join('<th>'+x+'</th>' for x in fields)+'</tr>'
        for row in rows:
            row['closest']=f"{row['closest']:.1f} km"; row['first']=insights.clock(row['first']); row['last']=insights.clock(row['last'])
            out+='<tr>'+''.join('<td>'+html.escape(str(row[k]))+'</td>' for k in fields)+'</tr>'
        return out+'</table>'+ (f'<a href="?offset={offset+500}">Older sightings</a>' if len(rows)==500 else '')
