import json
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError
import insights as i

class Insights(unittest.TestCase):
    def setUp(self):
        i.CACHE.clear()

    def test_cloud_forecast_missing_values_and_bounded_packet(self):
        from datetime import datetime, timedelta
        start=datetime(2026,9,27)
        times=[(start+timedelta(hours=n)).isoformat() for n in range(96)]
        fixture = {'current': {'time':'2026-09-27T23:45'}, 'timezone_abbreviation':'BST',
                   'hourly': {'time':times,'cloud_cover':[None]*96,'weather_code':[0]*96,'is_day':[0]*96},
                   'daily': {'time':[(start+timedelta(days=n)).date().isoformat() for n in range(4)],'weather_code':[2,61,71,95]}}
        calls=[]
        def download(url, limit): calls.append(url); return json.dumps(fixture)
        data=i.weather(51.5, 0, download)
        self.assertEqual(len(data['pages']),3)
        hour,today,outlook=data['pages']
        self.assertEqual(hour['cards'][0]['label'],'00:00')
        self.assertEqual(hour['subtitle'],'28 Sep / BST')
        self.assertTrue(hour['cards'][0]['night'])
        self.assertEqual(hour['cards'][0]['cloud'],'Cloud --')
        self.assertEqual(today['note'],'Tonight cloud --')
        self.assertEqual([c['icon'] for c in outlook['cards']],[3,4,5])
        self.assertEqual(outlook['cards'][0]['label'],'Mon 28')
        self.assertLess(len(json.dumps(data).encode()),8192)
        self.assertEqual(i.weather(51.5,0,download),data)
        self.assertEqual(len(calls),1)

    def test_tonight_excludes_this_mornings_cloud(self):
        from datetime import datetime, timedelta
        start=datetime(2026,9,27)
        fixture={'current':{'time':'2026-09-27T11:45'},
                 'hourly':{'time':[(start+timedelta(hours=n)).isoformat() for n in range(96)],
                           'is_day':[int(6<=n%24<18) for n in range(96)],
                           'cloud_cover':[100 if n<6 else 20 if n<24 else 60 for n in range(96)]},
                 'daily':{'time':[(start+timedelta(days=n)).date().isoformat() for n in range(4)]}}
        data=i.weather(51,0,lambda u,n:json.dumps(fixture))
        self.assertEqual(data['pages'][1]['note'],'Tonight cloud 40%')
        self.assertFalse(data['pages'][0]['cards'][0]['night'])

    def test_weather_icons_and_incomplete_forecast(self):
        self.assertEqual([i.weather_icon(x) for x in [0,2,3,45,61,71,95,None]],[0,1,2,6,3,4,5,7])
        with self.assertRaises(ValueError):
            i.weather(0,0,lambda u,n:json.dumps({'current':{'time':'2026-09-27T12:00'},'hourly':{'time':[]},'daily':{'time':[]}}))

    def test_family_resolves_booking_and_does_not_invent_arrival(self):
        route={'callsign_icao':'EZY123', 'destination':{'iata_code':'LGW'}}
        seen=[]
        def download(url,limit):
            seen.append(url)
            if 'adsbdb' in url: return json.dumps({'response':{'flightroute':route}})
            return json.dumps({'now':time.time()*1000,'ac':[{'flight':'EZY123 ', 'lat':51.,'lon':0.,'seen_pos':3,'alt_baro':7000,'gs':210}]})
        with patch.object(i,'AIRPORT_INDEX',({'iata':'LGW','icao':'EGKK','lat':51.148,'lon':-.19},)):
            data=i.family('u2123','','LGW',download)
        self.assertTrue(seen[-1].endswith('/EZY123'))
        self.assertTrue(any(s.startswith('To LGW:') for s in data['pages'][0]['lines']))
        self.assertIn('Direct distance is not arrival time',data['pages'][0]['lines'])
        self.assertIn('Database route; verify with airline',data['pages'][1]['lines'])

    def test_missing_stale_duplicate_and_wrong_callsigns(self):
        valid={'flight':'EZY12A','lat':51.,'lon':0.,'seen_pos':2}
        for aircraft,expected in [([], 'No fresh position found'),([dict(valid,seen_pos=61)], 'No fresh position found'),
                                  ([dict(valid,flight='OTHER')], 'No fresh position found'),([valid,valid], 'Multiple matches; check callsign')]:
            i.CACHE.clear()
            with patch.object(i,'route_data',return_value={}):
                data=i.family('U2123','EZY12A','',lambda u,n:json.dumps({'now':time.time()*1000,'ac':aircraft}))
            self.assertIn(expected,data['pages'][0]['lines'])

    def test_missing_route_does_not_prevent_callsign_tracking(self):
        with patch.object(i,'route_data',side_effect=ValueError('offline')):
            data=i.family('U2123','EZY12A','',lambda u,n:json.dumps({'now':time.time()*1000,'ac':[]}))
        self.assertIn('Looking for EZY12A',data['pages'][0]['lines'])
        self.assertIn('Route provider unavailable',data['pages'][1]['lines'])

    def test_stale_feed_timestamp_and_arrival_mismatch(self):
        with patch.object(i,'route_data',return_value={'destination':{'iata_code':'GLA','icao_code':'EGPF'}}):
            with self.assertRaises(ValueError):
                i.family('EZY123','','LGW',lambda u,n:json.dumps({'now':0,'ac':[]}))
            i.CACHE.clear()
            data=i.family('EZY123','','LGW',lambda u,n:json.dumps({'now':time.time()*1000,'ac':[]}))
            self.assertIn('Arrival differs from database route',data['pages'][1]['lines'])

    def test_negative_cache_and_validation(self):
        count=[]
        def fail(): count.append(1); raise ValueError('bad')
        for _ in range(2):
            with self.assertRaises(ValueError): i.cached('x',20,fail)
        self.assertEqual(len(count),1)
        for invalid in ['../x','EZY&x=y','A','X'*11]:
            with self.assertRaises(ValueError): i.family(invalid,'','',None)

    def test_airport_index_filters_and_persists(self):
        fixture='type,name,municipality,latitude_deg,longitude_deg,iata_code,icao_code,gps_code,ident,scheduled_service\nlarge_airport,Gatwick,London,51.148,-0.19,LGW,EGKK,EGKK,EGKK,yes\nclosed,Old,London,51,0,,,,OLD,no\n'
        with tempfile.TemporaryDirectory() as directory, patch.object(i.extras,'CACHE_DIR',Path(directory)), patch.object(i,'AIRPORT_INDEX',()):
            i.load_airports(lambda u,n:fixture.encode())
            self.assertEqual(len(i.AIRPORT_INDEX),1)
            i.load_airports(lambda u,n:self.fail('should use cache'))
            self.assertEqual(i.nearby(51.148,-.19)['pages'][0]['title'],'LGW')
        self.assertAlmostEqual(i.distance(0,179.9,0,-179.9),22.239,places=2)

if __name__=='__main__': unittest.main()
