#!/usr/bin/env python3
"""LAN firmware upload and bounded application-log monitoring (standard library)."""
import argparse
import hashlib
import http.client
import ipaddress
import json
import os
from pathlib import Path
import sys
import time


def address(value):
    # Accept the comma-separated address accidentally used in terminal examples.
    return str(ipaddress.IPv4Address(value.strip().replace(',', '.')))


def request(host, method, path, body=None, headers=None):
    conn = http.client.HTTPConnection(host, timeout=180)
    try:
        conn.request(method, path, body=body, headers=headers or {})
        response = conn.getresponse()
        data = response.read(65537)
        if len(data) > 65536:
            raise RuntimeError('Unexpected oversized response')
        if response.status != 200:
            raise RuntimeError(f'HTTP {response.status}: {data.decode(errors="replace")}')
        return data, dict((k.lower(), v) for k, v in response.getheaders())
    finally:
        conn.close()


def upload(host, image):
    data = Path(image).read_bytes()
    if len(data) < 36 or data[0] != 0xE9 or data[32:36] != bytes.fromhex("3254cdab"):
        raise ValueError('Expected an ESP32 application image, not a merged flash image')
    # ESP32-S3 chip ID in the extended image header; merged images start with a bootloader.
    if int.from_bytes(data[12:14], 'little') != 9:
        raise ValueError('The image is not built for ESP32-S3')
    raw, _ = request(host, 'GET', '/maintenance')
    info = json.loads(raw)
    if info.get('device') != 'echoscope' or info.get('protocol') != 1:
        raise RuntimeError('The target is not a compatible EchoScope device')
    if len(data) > info.get('capacity', 0):
        raise ValueError('Image exceeds the device OTA partition')
    boundary = 'EchoScopeFirmwareBoundary'
    prefix = (f'--{boundary}\r\nContent-Disposition: form-data; name="firmware"; '
              'filename="echoscope-app.bin"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()
    body = prefix + data + f'\r\n--{boundary}--\r\n'.encode()
    print(f'Uploading {len(data):,} bytes to {host}…', flush=True)
    result, _ = request(host, 'POST', '/update', body, {
        'Content-Type': f'multipart/form-data; boundary={boundary}',
        'X-EchoScope-Token': info['token'],
        'X-Firmware-Size': str(len(data)),
        'X-Firmware-MD5': hashlib.md5(data).hexdigest(),
    })
    print(result.decode(errors='replace'))


def monitor(host):
    cursor = '0'
    boot = None
    failed = False
    print(f'EchoScope application logs from {host}; Ctrl+C to stop.', flush=True)
    while True:
        try:
            data, headers = request(host, 'GET', '/logs?since=' + cursor)
            if 'x-log-cursor' not in headers:
                raise RuntimeError('Target does not provide EchoScope network logs')
            current_boot = headers.get('x-log-boot')
            if boot is not None and current_boot != boot:
                print('\n[monitor] Device restarted; replaying available logs.', flush=True)
                boot, cursor = current_boot, '0'
                continue
            boot = current_boot
            if headers.get('x-log-lost') == '1':
                print('\n[monitor] Older log data expired; resuming available logs.', flush=True)
            cursor = headers['x-log-cursor']
            if failed:
                print('\n[monitor] Reconnected.', flush=True)
            failed = False
            print(data.decode(errors='replace'), end='', flush=True)
            time.sleep(0.1 if len(data) >= 1024 else 1)
        except (OSError, RuntimeError, http.client.HTTPException) as error:
            if not failed:
                print(f'\n[monitor] {error}; retrying…', flush=True)
            failed = True
            time.sleep(3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['upload', 'monitor'])
    parser.add_argument('--ip', default=os.environ.get('IP', ''))
    parser.add_argument('--image', default='dist/echoscope-app.bin')
    args = parser.parse_args()
    try:
        host = address(args.ip)
        if args.action == 'upload':
            upload(host, args.image)
        else:
            monitor(host)
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, RuntimeError, KeyError, http.client.HTTPException) as error:
        print(f'Error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
