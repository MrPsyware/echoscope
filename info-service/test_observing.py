import json
import unittest
from unittest.mock import patch
import observing as o

class Observing(unittest.TestCase):
    def test_visible_segments_require_all_three_conditions(self):
        self.assertEqual(o.visible_segments([20,20,20,9,30,30,30,float('nan')],
                                           [True,True,False,True,True,True,True,True],
                                           [-10,-10,-10,-10,-5,-6,-7,-10]),[(0,1),(6,6)])
        self.assertEqual(o.visible_segments([10],[True],[-6.01]),[(0,0)])
        self.assertEqual(o.visible_segments([0],[True],[-20]),[])
    def test_compass_wrap(self):
        self.assertEqual([o.direction(v) for v in [0,45,90,180,270,359]],['N','NE','E','S','W','N'])
    def test_forecast_filters_invalid_cloud_and_caches(self):
        o.insights.CACHE.clear(); calls=[]
        def download(url,limit):
            calls.append(url)
            return json.dumps({'hourly':{'time':['2026-09-27T20:00','2026-09-27T21:00','2026-09-27T22:00'], 'cloud_cover':[25,None,101]}})
        data=o.forecast(51.5,0,download)
        self.assertEqual(len(data),1); self.assertEqual(data[0][1],25)
        o.forecast(51.5,0,download); self.assertEqual(len(calls),1)
        self.assertIn('timezone=UTC',calls[0])
    def test_no_half_ready_capability(self):
        with patch.object(o,'EPHEMERIS',None):
            self.assertFalse(o.ready())
            with self.assertRaises(ValueError): o.outlook(51,0,None)
