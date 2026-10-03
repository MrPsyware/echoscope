import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch
import db_check
import service
import spotting


class DatabaseDiagnostics(unittest.TestCase):
    def test_missing_does_not_create_database(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'missing.sqlite3'
            self.assertEqual(db_check.check(path,lambda _:None),1)
            self.assertFalse(path.exists())

    def test_valid_and_invalid_files_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'sightings.sqlite3'
            store=spotting.Store(path); store.db.close()
            before=path.read_bytes()
            self.assertEqual(db_check.check(path,lambda _:None),0)
            self.assertEqual(path.read_bytes(),before)
            path.write_bytes(b'not a SQLite database')
            messages=[]
            self.assertEqual(db_check.check(path,messages.append),1)
            self.assertTrue(any('SQLITE_NOTADB' in line for line in messages))
            self.assertEqual(path.read_bytes(),b'not a SQLite database')

    def test_sqlite_failure_returns_503_and_detailed_log(self):
        error=sqlite3.OperationalError('disk I/O error')
        error.sqlite_errorname='SQLITE_IOERR_READ'; error.sqlite_errorcode=266
        store=Mock()
        for method in ('web','entry','highlights'):
            getattr(store,method).side_effect=error
        handler=object.__new__(service.Handler); handler.reply=Mock()
        for path in ('/sightings','/v1/highlights','/sighting/1','/v1/logbook/1','/v1/logmap/1','/sighting-map/1'):
            handler.path=path
            with patch.object(service.integration,'STORE',store), patch.object(service.integration,'ready',return_value=True), self.assertLogs('echoscope.storage',level='ERROR') as logs:
                handler.do_GET()
            self.assertEqual(handler.reply.call_args.args[0],503)
            self.assertIn(b'not been reset',handler.reply.call_args.args[1])
            self.assertIn('SQLITE_IOERR_READ (266)',logs.output[0])
        self.assertTrue(service.GATE.acquire(blocking=False))
        service.GATE.release()
