import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import insights as i

class AirportApproach(unittest.TestCase):
    def airport(self,n,scheduled=True,kind='large_airport'):
        return dict(ident='EG'+str(n),icao='EG'+str(n),iata='A'+str(n),name='Airport name '*4,town='Town '*9,lat=51+n*.001,lon=0,scheduled=scheduled,type=kind)
    def test_filter_before_limit_and_matching_overlay(self):
        rows=[self.airport(n,False,'small_airport') for n in range(20)]+[self.airport(30),self.airport(31,True,'medium_airport')]
        with patch.object(i,'AIRPORT_INDEX',rows),patch.object(i,'RUNWAY_INDEX',{}):
            data=i.nearby(51,0)
            self.assertEqual([p['title'] for p in data['pages']],['A30','A31'])
            self.assertEqual(len(i.nearby(51,0,'all')['pages']),9)
            self.assertEqual(len(i.nearby(51,0,'airline','large')['pages']),1)
            self.assertEqual(i.nearby(51,0,'airline','large')['pages'][0]['item'],'EG30')
    def test_runways_and_packet_bound(self):
        rows=[self.airport(n) for n in range(9)]
        runway={'ends':[-89.123456,-179.123456,-89.654321,-179.654321],'name':'12345/12345','length':15000}
        with patch.object(i,'AIRPORT_INDEX',rows),patch.object(i,'RUNWAY_INDEX',{a['ident']:[runway]*3 for a in rows}):
            data=i.nearby(51,0)
            self.assertEqual(len(data['pages'][0]['airport']['runways']),3)
            self.assertLess(len(json.dumps(data).encode()),8192)
    def test_runway_cache_and_reject_closed_or_missing_coordinates(self):
        header='airport_ident,closed,le_latitude_deg,le_longitude_deg,he_latitude_deg,he_longitude_deg,le_ident,he_ident,length_ft\n'
        rows='EGLL,0,51.4,-.5,51.4,-.4,09L,27R,12000\nEGLL,1,51.4,-.5,51.4,-.4,09R,27L,13000\nEGXX,0,,,,,01,19,1000\nEGYY,0,nan,0,0,0,01,19,1000\n'
        with tempfile.TemporaryDirectory() as d,patch.object(i.extras,'CACHE_DIR',Path(d)),patch.object(i,'RUNWAY_INDEX',{}):
            i.load_runways(lambda *args:(header+rows).encode())
            self.assertEqual(list(i.RUNWAY_INDEX),['EGLL'])
            self.assertEqual(i.RUNWAY_INDEX['EGLL'][0]['name'],'09L/27R')
            i.load_runways(lambda *args:self.fail('fresh disk cache should be used'))
    def test_empty_filtered_list(self):
        with patch.object(i,'AIRPORT_INDEX',[self.airport(0,False)]):
            self.assertEqual(i.nearby(51,0)['pages'],[])
