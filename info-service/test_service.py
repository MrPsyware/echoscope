import io
import struct
import unittest
from unittest.mock import patch
from PIL import Image
import service

class Photos(unittest.TestCase):
    def test_packet_size_colour_and_attribution(self):
        raw=io.BytesIO(); Image.new('RGB',(400,200),(255,0,0)).save(raw,format='PNG')
        packet=service.make_packet(raw.getvalue(),'A Photographer','https://www.planespotters.net/photo/1')
        magic,w,h,credit,link=service.HEADER.unpack_from(packet)
        self.assertEqual((magic,w,h),(b'ECP1',200,100))
        self.assertEqual(len(packet),392+200*100*2)
        self.assertEqual(packet[392:394],b'\xf8\x00')
        self.assertEqual(credit.split(b'\0')[0],b'A Photographer')
    def test_urls(self):
        for url in ['http://t.plnspttrs.net/a','https://evil.test/a','https://t.plnspttrs.net@evil.test/a','https://t.plnspttrs.net:8443/a']:
            with self.assertRaises(ValueError): service.safe_url(url,{'t.plnspttrs.net'})
    def test_capability_discovery_and_disabled_features(self):
        from unittest.mock import Mock
        import os
        handler=object.__new__(service.Handler)
        handler.reply=Mock()
        handler.path='/health'
        with patch.dict(os.environ, {'ENABLE_PHOTOS':'0'}), patch.object(service.extras,'MAPS',False), patch.object(service.extras,'available_satellites',return_value=[]), patch.object(service.insights,'WEATHER',False), patch.object(service.insights,'FLIGHTS',False), patch.object(service.insights,'AIRPORTS',False):
            handler.do_GET()
        payload=service.json.loads(handler.reply.call_args.args[1])
        self.assertEqual(payload['capabilities'], {'photos':False,'maps':False,'satellites':False,'weather':False,'flights':False,'airports':False})
        for path,flag in [('/v1/weather?lat=51&lon=0','WEATHER'),('/v1/family?flight=U2123','FLIGHTS'),('/v1/airports?lat=51&lon=0','AIRPORTS')]:
            handler.path=path
            with patch.object(service.insights,flag,False): handler.do_GET()
            self.assertEqual(handler.reply.call_args.args[0],404)
        handler.path='/v1/photo/G-UZHO'
        with patch.dict(os.environ, {'ENABLE_PHOTOS':'0'}): handler.do_GET()
        self.assertEqual(handler.reply.call_args.args[0],404)
        handler.path='/v1/map?lat=51&lon=0&range=25'
        with patch.object(service.extras,'MAPS',False): handler.do_GET()
        self.assertEqual(handler.reply.call_args.args[0],404)

    def test_negative_cache(self):
        service.CACHE.clear()
        with patch.object(service,'download',return_value=b'{"photos":[]}') as download:
            self.assertEqual(service.photo('NONE'),(None,None,None))
            self.assertEqual(service.photo('NONE'),(None,None,None))
            self.assertEqual(download.call_count,1)
    def test_reject_invalid_image(self):
        with self.assertRaises(OSError): service.make_packet(b'not an image','A','B')

if __name__=='__main__': unittest.main()
