import json
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch
import integration as i
import spotting

class Integration(unittest.TestCase):
    def test_commands_and_no_arbitrary_pages(self):
        self.assertEqual(i.parse_command('aircraft',b'abc123 / EZY12A'),{'aircraft':'abc123'})
        self.assertEqual(i.parse_command('screen',b'OFF'),{'screen':False})
        self.assertEqual(i.parse_command('brightness',b'42'),{'brightness':42})
        for key,value in [('brightness',b'0'),('brightness',b'101'),('screen',b'false'),('page',b'setup'),('aircraft',b'../x'),('page',b'x'*129)]:
            with self.assertRaises(ValueError): i.parse_command(key,value)
        for url in ['http://user:pass@host','http://host/path','ftp://host','http://host?x=y']:
            with self.assertRaises(ValueError): i.Device(url,'a'*32)
    def test_sightings_ignore_stale_and_preserve_closest(self):
        store=spotting.Store(':memory:'); now=time.time()
        a={'hex':'abc123','registration':'<unsafe>','callsign':'EZY1','type':'A388','distance_km':20,'age_s':2,'watched':True}
        state={'screen':True,'demo':False,'aircraft':[a]}
        store.ingest(state,now); a['distance_km']=10; store.ingest(state,now+5)
        a['age_s']=61; a['distance_km']=1; store.ingest(state,now+10)
        rows=store.rows(); self.assertEqual(len(rows),1); self.assertEqual(rows[0]['closest'],10)
        self.assertIn('&lt;unsafe&gt;',store.web()); self.assertNotIn('<unsafe>',store.web())
        self.assertEqual(len(store.highlights()['pages']),1)
        state['demo']=True; a['hex']='abc456'; a['age_s']=0; store.ingest(state,now+15)
        self.assertEqual(len(store.rows()),1)
        state['demo']=False; state['aircraft']=[]; store.ingest(state,now+31*86400)
        self.assertEqual(store.rows(),[])
    def test_alert_failure_retries_success_deduplicates_across_restart(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'log.sqlite3'; store=spotting.Store(path); device=Mock()
            bridge=i.Bridge(device,store,None)
            device.request.side_effect=OSError('offline')
            with self.assertRaises(OSError): bridge.alert('x','Hello','test')
            self.assertFalse(store.seen('x'))
            device.request.side_effect=None; bridge.alert('x','Hello','test')
            again=i.Bridge(device,spotting.Store(path),None); again.alert('x','Hello','test')
            self.assertEqual(device.request.call_count,2)
    def test_pickup_requires_fresh_unambiguous_position_and_alerts_once(self):
        bridge=i.Bridge(Mock(),spotting.Store(':memory:'),None); bridge.alert=Mock()
        state={'pickup':True,'family_flight':'U2123','family_callsign':'EZY123','family_arrival':'LGW','pickup_km':100}
        for track in [{'fresh':False},{'fresh':True,'distance_km':20,'position_time':time.time()-90},
                      {'fresh':True,'distance_km':None,'position_time':time.time()},
                      {'fresh':True,'distance_km':120,'position_time':time.time()}]:
            with patch.object(i.insights,'family',return_value={'tracking':track}): bridge.work(state)
        bridge.alert.assert_not_called()
        with patch.object(i.insights,'family',return_value={'tracking':{'fresh':True,'distance_km':80,'position_time':time.time()}}):
            bridge.work(state); bridge.work(state)
        self.assertEqual(bridge.alert.call_count,1)
        self.assertIn('80 km',bridge.alert.call_args.args[1])
    def test_discovery_dynamic_options_and_state(self):
        bridge=i.Bridge(Mock(),spotting.Store(':memory:'),None)
        bridge.client=Mock(); bridge.id='test'; bridge.base='echoscope/test'
        state={'pages':['radar','weather'],'aircraft':[],'version':'0.9.0','page':'radar','brightness':50,'screen':False,'pickup':False,'selected':''}
        bridge.sync(state)
        discovery=[(a.args[0],json.loads(a.args[1])) for a in bridge.client.publish.call_args_list if a.args[0].endswith('/config')]
        self.assertEqual(len(discovery),8)
        self.assertEqual(next(v for k,v in discovery if '/page/' in k)['options'],['radar','weather'])
        self.assertTrue(all(v['availability_topic']=='echoscope/test/availability' for k,v in discovery))
        bridge.client.reset_mock(); bridge.sync(state)
        self.assertFalse(any(a.args[0].endswith('/config') for a in bridge.client.publish.call_args_list))
        bridge.rediscover.set(); bridge.sync(state)
        self.assertTrue(any(a.args[0].endswith('/config') for a in bridge.client.publish.call_args_list))
    def test_satellite_alert_window_and_cloud_message(self):
        bridge=i.Bridge(Mock(),spotting.Store(':memory:'),None); bridge.alert=Mock()
        now=time.time(); passes=[{'norad':25544,'name':'ISS','start':now+60,'end':now+300,'direction':'NW','max_elevation':65,'cloud':20}]
        with patch.object(i.observing,'ready',return_value=True),patch.object(i.observing,'outlook',return_value={'moon_percent':50,'passes':passes}):
            bridge.work({'satellite_alerts':True,'lat':51,'lon':0})
            self.assertIn('Cloud 20%',bridge.alert.call_args.args[1])
            bridge.alert.reset_mock(); passes[0]['start']=now+300
            bridge.work({'satellite_alerts':True,'lat':51,'lon':0}); bridge.alert.assert_not_called()

if __name__=='__main__': unittest.main()
