import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch, Mock
import insights
import service

class AirportOverlay(unittest.TestCase):
    def airport(self, code, kind='large_airport', scheduled=True, lon=0.01):
        return dict(iata=code,icao=code,type=kind,scheduled=scheduled,lat=51.5,lon=lon)
    def test_filters_range_priority_and_packet_bound(self):
        rows=[self.airport('SMALL','small_airport'),self.airport('PRIVATE','large_airport',False),
              self.airport('MID','medium_airport'),self.airport('BIG'),self.airport('FAR',lon=10)]
        with patch.object(insights,'AIRPORT_INDEX',rows),patch.object(insights,'AIRPORTS',True):
            codes=lambda **kw:[a['code'] for a in insights.airport_overlay(51.5,0,25,**kw)['airports']]
            self.assertEqual(codes(),['BIG','MID','SMALL'])
            self.assertEqual(codes(size='medium'),['BIG','MID'])
            self.assertEqual(codes(mode='all',size='large'),['BIG','PRIVATE'])
            self.assertEqual(codes(mode='all'),['BIG','MID','SMALL','PRIVATE'])
            for kw in [dict(radius=0),dict(radius=25,mode='bad'),dict(radius=25,size='small')]:
                with self.assertRaises(ValueError): insights.airport_overlay(51.5,0,**kw)
        rows=[self.airport(str(n),lon=n/1000) for n in range(100)]
        with patch.object(insights,'AIRPORT_INDEX',rows),patch.object(insights,'AIRPORTS',True):
            data=insights.airport_overlay(51.5,0,100)
            self.assertEqual(len(data['airports']),32)
            self.assertLess(len(json.dumps(data)),8192)
    def test_dateline_and_empty_result(self):
        a=self.airport('EDGE',lon=-179.99);a['lat']=0
        with patch.object(insights,'AIRPORT_INDEX',[a]),patch.object(insights,'AIRPORTS',True):
            self.assertEqual(len(insights.airport_overlay(0,179.99,5)['airports']),1)
            self.assertEqual(insights.airport_overlay(51,0,5)['airports'],[])
    def test_legacy_cache_upgrades_before_advertising(self):
        fixture=b'type,name,municipality,latitude_deg,longitude_deg,iata_code,icao_code,gps_code,ident,scheduled_service\nlarge_airport,Heathrow,London,51.47,-0.46,LHR,EGLL,EGLL,EGLL,yes\nclosed_airport,Closed,London,51,0,,,,C,no\n'
        with tempfile.TemporaryDirectory() as d,patch.object(insights.extras,'CACHE_DIR',Path(d)),patch.object(insights,'AIRPORT_INDEX',()),patch.object(insights,'AIRPORTS',True):
            old=self.airport('OLD');del old['type']
            (Path(d)/'airports-index.json').write_text(json.dumps([old]))
            def download(*args):
                self.assertFalse(insights.overlay_ready())
                return fixture
            insights.load_airports(download)
            self.assertTrue(insights.overlay_ready())
            self.assertEqual(insights.AIRPORT_INDEX[0]['type'],'large_airport')
            self.assertEqual(len(insights.AIRPORT_INDEX),1)
            insights.load_airports(lambda *args:self.fail('fresh cache must be reused'))
    def test_endpoint_and_capability(self):
        handler=object.__new__(service.Handler);handler.reply=Mock()
        with patch.object(insights,'AIRPORT_INDEX',[self.airport('LHR')]),patch.object(insights,'AIRPORTS',True):
            handler.path='/health';handler.do_GET()
            self.assertTrue(json.loads(handler.reply.call_args.args[1])['capabilities']['airport_overlay'])
            handler.path='/v1/airport-overlay?lat=51.5&lon=0&range=25';handler.do_GET()
            self.assertEqual(handler.reply.call_args.args[0],200)
            handler.path='/v1/airport-overlay?lat=nan&lon=0&range=25';handler.do_GET()
            self.assertEqual(handler.reply.call_args.args[0],400)
        with patch.object(insights,'AIRPORTS',False):
            handler.do_GET();self.assertEqual(handler.reply.call_args.args[0],404)
