"""Package the pinned original ESP32-S3 or Mini ESP32-C3 layout; never touches a serial device."""
from pathlib import Path
import argparse
import hashlib
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--environment', default='echoscope', choices=('echoscope', 'c3-mini'))
args = parser.parse_args()
mini = args.environment == 'c3-mini'
chip, chip_id, prefix = ('esp32c3', 5, 'echoscope-mini') if mini else ('esp32s3', 9, 'echoscope')
build = (root / 'hardware/c3-mini/.pio/build' if mini else root / '.pio/build') / args.environment
core = Path(os.environ.get('PLATFORMIO_CORE_DIR', root / '.tools/platformio'))
app = build / 'firmware.bin'
boot = build / 'bootloader.bin'
partitions = build / 'partitions.bin'
boot_app = core / 'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
for path in (app, boot, partitions, boot_app):
    if not path.is_file():
        sys.exit(f'Missing {path}; run make build first')
image = app.read_bytes()
if len(image) < 36 or image[0] != 0xE9 or int.from_bytes(image[12:14], 'little') != chip_id:
    sys.exit(f'Application is not built for {chip}; refusing to package')
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
dist = root / 'dist' / 'mini' if mini else root / 'dist'
dist.mkdir(parents=True, exist_ok=True)
subprocess.run([
    sys.executable, '-m', 'esptool', '--chip', chip, 'merge_bin',
    '-o', str(dist / f'{prefix}-merged.bin'),
    '0x0', str(boot), '0x8000', str(partitions), '0xe000', str(boot_app), '0x10000', str(app),
], check=True)
shutil.copyfile(app, dist / f'{prefix}-app.bin')
shutil.copyfile(app, dist / f'{prefix}-app-{version}.bin')
files = [dist / f'{prefix}-app.bin', dist / f'{prefix}-app-{version}.bin', dist / f'{prefix}-merged.bin']
(dist / ('MINI-SHA256SUMS' if mini else 'SHA256SUMS')).write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files))
print(f'Packaged {prefix} {version} in {dist}')
