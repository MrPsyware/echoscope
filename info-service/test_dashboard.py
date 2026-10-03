import unittest
from unittest.mock import Mock,patch
from io import BytesIO
import spotting
import dashboard
import service

class Dashboard(unittest.TestCase):
    def setUp(self):
        self.store=spotting.Store(':memory:')
        with self.store.db:
            self.store.db.executemany('INSERT INTO entries(day,hex,registration,callsign,type,reason,first,last,closest) VALUES (?,?,?,?,?,?,?,?,?)',
                [('2026-10-03',str(i),'G-TEST' if i else '<script>','TEST'+str(i),'A380' if i==0 else 'A320','Military' if i==0 else 'Watchlist',1,i+1,2) for i in range(65)])
    def tearDown(self): self.store.db.close()
    def test_search_all_history_and_literal_wildcards(self):
        page=self.store.web(q='a380',reason='Military',day='2026-10-03')
        self.assertIn('1–1 of 1 matches',page); self.assertIn('&lt;script&gt;',page)
        self.assertNotIn('<script>',page)
        self.assertIn('0–0 of 0 matches',self.store.web(q='%'))
        self.assertIn('0–0 of 0 matches',self.store.web(q="' OR 1=1 --"))
    def test_pagination_retains_filters(self):
        page=self.store.web(q='A320',reason='Watchlist')
        self.assertIn('1–50 of 64 matches',page)
        self.assertIn('q=A320&amp;reason=Watchlist',page)
        self.assertIn('51–64 of 64 matches',self.store.web(offset=50,q='A320'))
        self.assertIn('51–65 of 65 matches',self.store.web(offset=9999))
    def test_empty_and_invalid(self):
        self.assertIn('Configure ECHOSCOPE_URL',dashboard.page(None))
        with self.assertRaises(ValueError): self.store.web(day='bad')
        with self.assertRaises(ValueError): self.store.web(reason='bogus')
    def test_routes_and_cache_redirect(self):
        h=object.__new__(service.Handler); h.reply=Mock()
        with patch.object(service.integration,'STORE',self.store):
            for path in ('/','/sightings','/cache','/?q=A380'):
                h.path=path; h.do_GET(); self.assertEqual(h.reply.call_args.args[0],200)
                self.assertIn(b'<!doctype html>',h.reply.call_args.args[1])
        h.path='/cache/clear'; payload=('token='+service.CACHE_TOKEN).encode()
        h.headers={'Content-Length':str(len(payload))}; h.rfile=BytesIO(payload)
        h.send_response=Mock(); h.send_header=Mock(); h.end_headers=Mock()
        with patch.object(service.photo_cache,'clear') as clear:
            h.do_POST(); clear.assert_called_once()
        h.send_response.assert_called_once_with(303)
        h.send_header.assert_any_call('Location','/?cache=cleared#cache')
