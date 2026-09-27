import io
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import time
import unittest
from unittest.mock import patch,Mock
from PIL import Image
import spotting
import photo_cache
import service

class Logbook(unittest.TestCase):
    def setUp(self):
        self.store=spotting.Store(':memory:'); self.addCleanup(self.store.db.close)
        self.a={'hex':'abc123','registration':'G-TEST','callsign':'EZY123','type':'A388','age_s':0,'distance_km':15,'east_km':10,'north_km':10,'watched':True}
        self.state={'screen':True,'lat':51.5,'lon':0,'aircraft':[self.a]}
        self.now=time.time()
    def test_sleeping_tracking_and_unavailable_route(self):
        self.state['screen']=False
        self.store.ingest(self.state,self.now)
        self.assertEqual(len(self.store.rows()),1)
        with patch.object(spotting.insights,'route_data',return_value={}):
            self.assertEqual(self.store.detail(1,None)['pages'][0]['lines'],['Unavailable'])
        self.a['age_s']=61
        self.store.ingest(self.state,self.now+30)
        self.assertEqual(self.store.entry(1)['last'],self.now)
    def test_track_bound_gaps_and_new_encounters(self):
        for n in range(400):
            self.a['east_km']=10+n*.04; self.store.ingest(self.state,self.now+15*n)
        row=self.store.entry(1)
        self.assertLessEqual(len(row['track']),192)
        self.assertEqual(row['track'][0][1],10)
        self.assertAlmostEqual(row['track'][-1][1],25.96)
        self.a['east_km']=26; self.store.ingest(self.state,self.now+15*399+120)
        self.assertTrue(self.store.entry(1)['track'][-1][3])
        self.a['callsign']='EZY456'; self.store.ingest(self.state,self.now+15*399+130)
        self.assertEqual(len(self.store.rows()),2)
        self.store.ingest(self.state,self.now+15*399+1200)
        self.assertEqual(len(self.store.rows()),3)
    def test_stationary_updates_are_not_a_reception_gap(self):
        for n in range(10): self.store.ingest(self.state,self.now+n*15)
        self.a['east_km']=11; self.store.ingest(self.state,self.now+150)
        self.assertFalse(self.store.entry(1)['track'][-1][3])
    def test_late_metadata_and_missing_callsign_do_not_split(self):
        self.a['callsign']=''; self.a['registration']=''; self.store.ingest(self.state,self.now)
        self.a['callsign']='EZY123'; self.a['registration']='G-TEST'; self.store.ingest(self.state,self.now+15)
        self.a['callsign']=''; self.store.ingest(self.state,self.now+30)
        self.assertEqual(len(self.store.rows()),1)
        self.assertEqual(self.store.entry(1)['callsign'],'EZY123')
        self.assertEqual(self.store.entry(1)['registration'],'G-TEST')
    def test_route_snapshot_and_packet_limit(self):
        for n in range(191):
            self.a['east_km']=10+n*.04; self.store.ingest(self.state,self.now+15*n)
        route={'origin':{'iata_code':'AMS'},'destination':{'iata_code':'LGW'},'airline':{'name':'easyJet'}}
        with patch.object(spotting.insights,'route_data',return_value=route) as source:
            first=self.store.detail(1,None); self.store.detail(1,None)
            self.assertEqual(source.call_count,1)
        self.assertLess(len(json.dumps(first)),8192)
        self.assertEqual(first['range'],25)
        self.assertIn('AMS > LGW',first['pages'][0]['lines'])
        self.assertEqual(first['track'][0][:2],[10,10])
        self.assertEqual(self.store.highlights()['pages'][0]['entry_id'],1)
    def test_migrate_old_history_once_without_inventing_track(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'history.db'; old=sqlite3.connect(path)
            old.execute('CREATE TABLE sightings(day TEXT,hex TEXT,registration TEXT,callsign TEXT,type TEXT,reason TEXT,first REAL,last REAL,closest REAL,PRIMARY KEY(day,hex))')
            old.execute('INSERT INTO sightings VALUES (?,?,?,?,?,?,?,?,?)',('2026-09-27','abc123','G-TEST','EZY123','A320','Watchlist',self.now,self.now,12))
            old.commit(); old.close()
            store=spotting.Store(path); self.assertEqual(len(store.rows()),1); self.assertEqual(store.entry(1)['track'],[]); store.db.close()
            store=spotting.Store(path); self.assertEqual(len(store.rows()),1); store.db.close()
    def test_web_does_not_inject_aircraft_strings(self):
        self.a['registration']='<unsafe>'; self.store.ingest(self.state,self.now)
        with patch.object(spotting.insights,'FLIGHTS',False): page=self.store.web_entry(1,None)
        self.assertIn('&lt;unsafe&gt;',page); self.assertNotIn('<unsafe>',page)
        self.assertIn('Observed track',page)
    def test_photo_disk_cache_expiry_integrity_and_manual_clear(self):
        with tempfile.TemporaryDirectory() as d,patch.object(photo_cache.extras,'CACHE_DIR',Path(d)):
            raw=io.BytesIO(); Image.new('RGB',(200,150),(255,0,0)).save(raw,format='PNG')
            packet=service.make_packet(raw.getvalue(),'Photographer','https://www.planespotters.net/photo/1')
            photo_cache.write('G-TEST',packet)
            self.assertEqual(photo_cache.read('G-TEST')[1],'Photographer')
            path=photo_cache.root()/'G-TEST.ecp'; os.utime(path,(self.now-photo_cache.TTL-1,)*2)
            self.assertIsNone(photo_cache.read('G-TEST'))
            photo_cache.write('G-TEST',packet); path.write_bytes(b'broken'); self.assertIsNone(photo_cache.read('G-TEST'))
            photo_cache.write('G-TEST',packet); self.assertEqual(photo_cache.clear(),1); self.assertIsNone(photo_cache.read('G-TEST'))
            with self.assertRaises(ValueError): photo_cache.read('../secret')
    def test_clear_cache_requires_token(self):
        handler=object.__new__(service.Handler); handler.reply=Mock(); handler.path='/cache/clear'
        handler.headers={'Content-Length':'11'}; handler.rfile=io.BytesIO(b'token=wrong')
        handler.do_POST(); self.assertEqual(handler.reply.call_args.args[0],403)
