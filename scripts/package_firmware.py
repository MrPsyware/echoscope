"""Package the pinned ESP32-S3 layout; never touches a serial device."""
from pathlib import Path
import argparse
import hashlib
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--environment', default='echoscope')
args = parser.parse_args()
if args.environment != 'echoscope':
    parser.error('Only the echoscope board/partition layout is supported for packaging')
build = root / '.pio/build' / args.environment
core = Path(os.environ.get('PLATFORMIO_CORE_DIR', root / '.tools/platformio'))
app = build / 'firmware.bin'
boot = build / 'bootloader.bin'
partitions = build / 'partitions.bin'
boot_app = core / 'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
for path in (app, boot, partitions, boot_app):
    if not path.is_file():
        sys.exit(f'Missing {path}; run make build first')
# Verify that the generated partition table really puts the application at
# 0x10000 before producing images advertised for a settings-preserving update.
import struct
partition_data = partitions.read_bytes()
app_offsets = []
for offset in range(0, len(partition_data) - 31, 32):
    magic, kind, subtype, address, size = struct.unpack_from('<HBBII', partition_data, offset)
    if magic == 0x50AA and kind == 0:
        app_offsets.append(address)
if not app_offsets or min(app_offsets) != 0x10000:
    sys.exit('Unexpected application partition offset; refusing to package')
version = (root / 'VERSION').read_text().strip()
dist = root / 'dist'
dist.mkdir(exist_ok=True)
subprocess.run([
    sys.executable, '-m', 'esptool', '--chip', 'esp32s3', 'merge_bin',
    '-o', str(dist / 'echoscope-merged.bin'),
    '0x0', str(boot), '0x8000', str(partitions), '0xe000', str(boot_app), '0x10000', str(app),
], check=True)
shutil.copyfile(app, dist / 'echoscope-app.bin')
shutil.copyfile(app, dist / f'echoscope-app-{version}.bin')
files = [dist / 'echoscope-app.bin', dist / f'echoscope-app-{version}.bin', dist / 'echoscope-merged.bin']
(dist / 'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files))
print(f'Packaged EchoScope {version} in {dist}')
