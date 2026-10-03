"""Read-only logbook diagnostics: never create, reset or repair a database."""
import os
from pathlib import Path
import sqlite3
import sys


def check(path, output=print):
    path=Path(path).resolve()
    output(f'Database: {path}; process uid={os.getuid()} gid={os.getgid()}')
    try:
        stat=path.stat()
        output(f'Size={stat.st_size} bytes; owner={stat.st_uid}:{stat.st_gid}; mode={oct(stat.st_mode & 0o777)}')
        disk=os.statvfs(path.parent)
        output(f'Available storage={disk.f_bavail*disk.f_frsize} bytes; available inodes={disk.f_favail}')
        for suffix in ('-journal','-wal','-shm'):
            sidecar=Path(str(path)+suffix)
            if sidecar.exists(): output(f'{sidecar.name}: {sidecar.stat().st_size} bytes (preserved)')
        # mode=ro prevents accidental creation; do not use immutable, which ignores journals.
        db=sqlite3.connect(path.as_uri()+'?mode=ro',uri=True,timeout=5)
        try:
            results=[row[0] for row in db.execute('PRAGMA integrity_check(20)')]
            for result in results: output(f'integrity_check: {result}')
            if results!=['ok']: return 1
            count=db.execute('SELECT count(*) FROM entries').fetchone()[0]
            output(f'Encounters: {count}. Fresh read-only connection passed; this does not test writes or the running service connection.')
            return 0
        finally:
            db.close()
    except sqlite3.Error as error:
        output(f'SQLite {getattr(error,"sqlite_errorname","unknown")} ({getattr(error,"sqlite_errorcode","unknown")}): {error}')
    except OSError as error:
        output(f'Filesystem {type(error).__name__}: {error}')
    return 1


if __name__=='__main__':
    sys.exit(check(Path(os.environ.get('CACHE_DIR','/data'))/'sightings.sqlite3'))
