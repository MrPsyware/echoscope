from pathlib import Path
import spotting
# Run only in the isolated CI Compose project, never against a user's logbook.
path=Path('/data/container-smoke.sqlite3')
assert not path.exists(), 'Refusing to overwrite an existing smoke-test database'
s=spotting.Store(path)
with s.db:
    s.db.executemany('INSERT INTO entries(day,hex,registration,callsign,type,reason,first,last,closest,track) VALUES (?,?,?,?,?,?,?,?,?,?)',
      [('2026-10-03',str(i),'G-TEST','TEST','A320','Watchlist',1,i,1,' '*8192) for i in range(1000)])
print('Rendering 500 of 1000 encounters with large tracks...',flush=True)
assert len(s.db.execute('SELECT * FROM entries ORDER BY last DESC LIMIT 500').fetchall())==500
page=s.web()
assert page.count('<tr>')==51
s.db.close()
path.unlink()
print('Logbook render passed',flush=True)
