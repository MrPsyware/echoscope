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
    def test_negative_cache(self):
        service.CACHE.clear()
        with patch.object(service,'download',return_value=b'{"photos":[]}') as download:
            self.assertEqual(service.photo('NONE'),(None,None,None))
            self.assertEqual(service.photo('NONE'),(None,None,None))
            self.assertEqual(download.call_count,1)
    def test_reject_invalid_image(self):
        with self.assertRaises(OSError): service.make_packet(b'not an image','A','B')

if __name__=='__main__': unittest.main()
