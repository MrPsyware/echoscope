"""Server-rendered logbook UI; works without JavaScript or external assets."""
from html import escape as esc
from urllib.parse import urlencode
from datetime import datetime, timezone

STYLE='''
:root{color-scheme:dark;--bg:#030d10;--panel:#0b1c20;--line:#24413f;--text:#e8f8f4;--muted:#94b3ac;--green:#68f3ae}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px/1.55 system-ui,sans-serif}a{color:var(--green);text-decoration:none}a:hover{text-decoration:underline}a:focus-visible,button:focus-visible,input:focus-visible,select:focus-visible,summary:focus-visible{outline:2px solid var(--green);outline-offset:4px}main{max-width:1180px;margin:auto;padding:32px 24px}header{display:flex;align-items:center;justify-content:space-between;gap:20px}.brand{letter-spacing:.15em;font-weight:750;font-size:20px}.brand:before{content:'◉';color:var(--green);margin-right:12px}nav{display:flex;gap:20px}h1{font-size:clamp(32px,5vw,52px);letter-spacing:-.035em;margin:32px 0 4px}h2{font-size:20px;margin:0}p{color:var(--muted);margin:8px 0 20px}.eyebrow{color:var(--green);font-size:12px;letter-spacing:.14em;text-transform:uppercase}.stats{display:flex;gap:28px;flex-wrap:wrap;border-block:1px solid var(--line);padding:18px 0;margin:26px 0}.stats strong{font-size:25px;color:var(--text);display:block}.stats span{color:var(--muted);font-size:13px}.filters{display:flex;align-items:end;gap:12px;flex-wrap:wrap;margin:24px 0}label{display:block;color:var(--muted);font-size:13px}label:first-child{flex:1;min-width:220px}input,select,button{font:inherit;border:1px solid var(--line);border-radius:8px;padding:11px 13px;background:var(--panel);color:var(--text);margin-top:6px}input,select{width:100%}button{cursor:pointer;background:var(--green);color:#082017;font-weight:650}button:hover{filter:brightness(1.1)}.table{overflow:auto;border:1px solid var(--line);border-radius:12px}table{width:100%;border-collapse:collapse;text-align:left;white-space:nowrap}th{font-size:11px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);background:var(--panel)}td,th{padding:15px 18px;border-bottom:1px solid var(--line)}tbody tr:last-child td{border:0}tbody tr:hover{background:#10272a}td small{display:block;color:var(--muted)}.badge{display:inline-block;border:1px solid #31534a;border-radius:20px;padding:3px 9px;font-size:12px;color:#b0dec8}.pager{display:flex;justify-content:space-between;align-items:center;gap:16px;margin:18px 0 36px;color:var(--muted);font-size:14px}.empty{padding:42px 24px;text-align:center;border:1px dashed var(--line);border-radius:12px}.utility{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:20px;margin:28px 0}summary{cursor:pointer;font-weight:650}footer{border-top:1px solid var(--line);padding-top:22px;color:var(--muted);font-size:12px}footer a{color:var(--muted)}.notice{padding:12px;border:1px solid var(--green);border-radius:8px;color:var(--green)}@media(max-width:600px){main{padding:20px 14px}header{align-items:start;flex-direction:column;gap:12px}.brand{font-size:16px}nav{font-size:13px;gap:12px}.filters label{width:100%}.filters button{flex:1}td,th{padding:12px}h1{margin-top:24px}.stats{gap:20px}}
'''

def page(store,offset=0,q='',reason='',day='',token='',notice=''):
    q=q.strip()[:80]
    if reason not in ('','Watchlist','Military','Helicopter'): raise ValueError('Invalid category')
    if day:
        datetime.strptime(day,'%Y-%m-%d')
    offset=max(0,min(10000,offset)); size=50
    conditions=[]; args=[]
    if q:
        conditions.append("(registration LIKE ? ESCAPE '\\' OR callsign LIKE ? ESCAPE '\\' OR type LIKE ? ESCAPE '\\' OR hex LIKE ? ESCAPE '\\')")
        value='%'+q.replace('\\','\\\\').replace('%','\\%').replace('_','\\_')+'%'; args.extend([value]*4)
    if reason: conditions.append('reason LIKE ?'); args.append('%'+reason+'%')
    if day: conditions.append('day=?'); args.append(day)
    where=' WHERE '+' AND '.join(conditions) if conditions else ''
    rows=[]; total=matches=today=0
    if store:
        with store.lock:
            total=store.db.execute('SELECT count(*) FROM entries').fetchone()[0]
            today=store.db.execute('SELECT count(*) FROM entries WHERE day=?',(datetime.now(timezone.utc).strftime('%Y-%m-%d'),)).fetchone()[0]
            matches=store.db.execute('SELECT count(*) FROM entries'+where,args).fetchone()[0]
            offset=min(offset, max(0,(matches-1)//size*size))
            rows=store.db.execute('SELECT id,day,hex,registration,callsign,type,reason,first,last,closest FROM entries'+where+' ORDER BY last DESC,id DESC LIMIT ? OFFSET ?',(*args,size,offset)).fetchall()
    out='<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>EchoScope · Spotting log</title><style>'+STYLE+'</style></head><body><main>'
    out+='<header><a class="brand" href="/">ECHOSCOPE</a><nav><a href="#logbook">Logbook</a><a href="#cache">Photo cache</a><a href="https://github.com/MrPsyware/echoscope">Project ↗</a></nav></header>'
    out+='<h1>Your sky, recorded.</h1><p>EchoScope is a miniature radar station for the aircraft overhead. Explore the interesting aircraft your scope has spotted, with photos, routes and recorded flight paths.</p>'
    if notice: out+='<p class="notice" role="status">'+esc(notice)+'</p>'
    out+=f'<div class="stats"><span><strong>{total:,}</strong>Saved encounters</span><span><strong>{today:,}</strong>Today · UTC</span><span><strong>30 days</strong>History · up to 10,000 encounters</span></div>'
    out+='<section id="logbook"><div class="eyebrow">Watchlist · Military · Helicopters</div><h2>Spotting log</h2><form class="filters" method="get" action="/"><label>Search aircraft<input type="search" name="q" maxlength="80" placeholder="Registration, callsign, type or hex" value="'+esc(q,quote=True)+'"></label><label>Category<select name="reason">'
    for value,label in [('','All interesting aircraft'),('Watchlist','Watchlist'),('Military','Military'),('Helicopter','Helicopters')]:
        out+='<option value="'+value+'"'+(' selected' if value==reason else '')+'>'+label+'</option>'
    out+='</select></label><label>Date · UTC<input type="date" name="day" value="'+esc(day,quote=True)+'"></label><button type="submit">Search</button><a href="/#logbook">Reset</a></form>'
    if rows:
        out+='<div class="table" tabindex="0" role="region" aria-label="Sightings; scroll horizontally for more columns"><table><thead><tr><th scope="col">Aircraft</th><th scope="col">Flight / type</th><th scope="col">Spotted for</th><th scope="col">Closest</th><th scope="col">Last seen · UTC</th></tr></thead><tbody>'
        for r in rows:
            seen=datetime.fromtimestamp(r['last'],timezone.utc).strftime('%d %b %Y · %H:%M')
            out+=f'<tr><td><a href="/sighting/{r["id"]}"><strong>'+esc(r['registration'] or r['hex'])+'</strong></a><small>'+esc(r['hex'])+'</small></td><td>'+esc(r['callsign'] or '—')+'<small>'+esc(r['type'] or 'Unknown type')+'</small></td><td><span class="badge">'+esc(r['reason'])+f'</span></td><td>{r["closest"]:.1f} km</td><td>{seen}</td></tr>'
        out+='</tbody></table></div>'
    else:
        out+='<div class="empty"><h2>'+('No matching sightings' if store and total else 'Your logbook starts here')+'</h2><p>'+('Try a different search, date or category.' if store and total else 'Interesting aircraft will appear as your scope spots them.' if store else 'Configure ECHOSCOPE_URL in the information server to connect your scope and start recording.')+'</p></div>'
    def link(n,label): return '<a href="/?'+esc(urlencode(dict(q=q,reason=reason,day=day,offset=n)),quote=True)+'#logbook">'+label+'</a>'
    out+='<div class="pager">'+(link(offset-size,'← Newer') if offset else '<span></span>')+f'<span>{offset+1 if matches else 0}–{min(offset+size,matches)} of {matches:,} matches</span>'+(link(offset+size,'Older →') if offset+size<matches else '<span></span>')+'</div></section>'
    out+='<details class="utility" id="cache"><summary>Photo cache</summary><p>Photos are cached for one week, up to 256 images. Clearing the cache downloads photos again when viewed. Your sightings, maps and flight paths are kept.</p><form action="/cache/clear" method="post"><input type="hidden" name="token" value="'+esc(token,quote=True)+'"><button type="submit">Clear cached photos</button></form></details>'
    out+='<footer>Recorded encounters, not a complete air traffic history. Times are UTC. Routes describe database routes, not filed flight plans.<p>Aircraft: <a href="https://adsb.fi/">adsb.fi</a> · Routes: <a href="https://www.adsbdb.com/">adsbdb</a> · Photos: <a href="https://planespotters.net/">Planespotters.net</a> (individual credits on each photo) · Airports: OurAirports · Weather: Open-Meteo (CC BY 4.0) · Maps: © <a href="https://www.openstreetmap.org/copyright">OpenStreetMap contributors</a> · Orbits: CelesTrak / SGP4</p></footer></main></body></html>'
    return out
