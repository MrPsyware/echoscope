import io
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import numpy as np
from PIL import Image
from skyfield.api import EarthSatellite
import extras

class Extras(unittest.TestCase):
    def test_projection(self):
        for lat, lon in [(51.5, 0), (0, 179.9), (-33.8, 151.2)]:
            p, l, mask = extras.inverse_grid(lat, lon, 100)
            self.assertAlmostEqual(math.degrees(p[210, 210]), lat)
            self.assertAlmostEqual(math.degrees(l[210, 210]), lon)
            # North edge is exactly 100 km along the same meridian.
            self.assertAlmostEqual((p[0, 210]-math.radians(lat))*6371.0088, 100)
            self.assertAlmostEqual(l[0, 210], math.radians(lon))
            self.assertFalse(mask[0, 0])

    def test_map_packet_and_tile_cache(self):
        raw = io.BytesIO(); Image.new('RGB', (256, 256), 'white').save(raw, format='PNG')
        with tempfile.TemporaryDirectory() as folder, patch.object(extras, 'CACHE_DIR', Path(folder)), patch.object(extras.time, 'sleep'):
            with patch.object(extras, 'tile', return_value=np.full((256, 256, 3), 255, dtype=np.uint8)):
                packet = extras.make_map(51.5, 0, 25, lambda *_: None)
                self.assertEqual(packet[:8], b'ECM1\xa4\x01\xa4\x01')
                self.assertEqual(len(packet), 8+420*420*2)
                self.assertEqual(packet[8:10], packet[8+(210*420+210)*2:10+(210*420+210)*2])
            from unittest.mock import Mock
            download = Mock(return_value=raw.getvalue())
            extras.tile(4, 5, 6, download); extras.tile(4, 5, 6, download)
            self.assertEqual(download.call_count, 1)

    def test_query_validation(self):
        for query in ['lat=nan&lon=0', 'lat=86&lon=0', 'lat=0&lon=181']:
            with self.assertRaises(ValueError): extras.location(query)
        with self.assertRaises(ValueError): extras.map_response(0, 0, 42, None)

    def test_station_prediction_and_stale_suppression(self):
        # Known published sample; freeze time at its epoch, never use stale elements as live data.
        sat = EarthSatellite('1 25544U 98067A   24127.82853009  .00015698  00000+0  27310-3 0  9995',
                             '2 25544  51.6393 160.4574 0003580 140.6673 205.7250 15.50957674452123', 'ISS', extras.TS)
        with patch.object(extras, 'SATS', [sat]), patch.object(extras.TS, 'now', return_value=sat.epoch):
            self.assertEqual(len(extras.available_satellites()), 1)
        with patch.object(extras, 'SATS', [sat]), patch.object(extras.TS, 'now', return_value=sat.epoch+8):
            self.assertEqual(extras.available_satellites(), [])
        with patch.object(extras, 'available_satellites', return_value=[sat]), patch.object(extras, 'datetime') as dt:
            dt.now.return_value=sat.epoch.utc_datetime()
            result=extras.satellite_response(51.5, 0)
        row=result['satellites'][0]
        self.assertGreaterEqual(row['az'], 0); self.assertLess(row['az'], 360)
        self.assertGreater(row['km'], 0); self.assertGreater(row['next_rise'], result['generated'])
        self.assertLessEqual(row['next_rise'], result['generated']+86400)

if __name__ == '__main__': unittest.main()
