"""Remove only generated build products; retain packages, credentials and backups."""
from pathlib import Path
import shutil

root = Path(__file__).resolve().parent.parent
for relative in ('.pio/build', '.tools/tests', 'dist'):
    directory = root / relative
    if directory.is_dir():
        shutil.rmtree(directory)
        print(f'Removed {relative}')
