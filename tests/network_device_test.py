import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('network_device', Path(__file__).parents[1] / 'scripts/network_device.py')
net = importlib.util.module_from_spec(spec)
spec.loader.exec_module(net)


class NetworkDeviceTests(unittest.TestCase):
    def test_addresses(self):
        self.assertEqual(net.address('192,168.2.151'), '192.168.2.151')
        with self.assertRaises(ValueError):
            net.address('192.168.2.999')

    def test_monitor_reboot_and_reconnect(self):
        responses = [
            (b'hello', {'x-log-cursor': '10', 'x-log-boot': 'a'}),
            (b'reboot', {'x-log-cursor': '4', 'x-log-boot': 'b'}),
            (b'boot', {'x-log-cursor': '5', 'x-log-boot': 'b', 'x-log-lost': '1'}),
            OSError('disconnected'),
            (b'back', {'x-log-cursor': '9', 'x-log-boot': 'b'}),
            KeyboardInterrupt(),
        ]
        with patch.object(net, 'request', side_effect=responses) as request, patch.object(net.time, 'sleep'), patch('builtins.print'):
            with self.assertRaises(KeyboardInterrupt):
                net.monitor('192.168.2.151')
        self.assertEqual([call.args[2] for call in request.call_args_list],
                         ['/logs?since=0', '/logs?since=10', '/logs?since=0', '/logs?since=5', '/logs?since=5', '/logs?since=9'])

    def test_upload_and_rejections(self):
        image = bytearray(128)
        image[0] = 0xE9
        image[12] = 9
        image[32:36] = bytes.fromhex('3254cdab')
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'app.bin'
            path.write_bytes(image)
            info = {'device': 'echoscope', 'protocol': 1, 'token': 'test-token', 'capacity': 1024}
            with patch.object(net, 'request', side_effect=[(json.dumps(info).encode(), {}), (b'OK', {})]) as request:
                net.upload('192.168.2.151', path)
                args = request.call_args.args
                self.assertEqual(args[2], '/update')
                self.assertIn(bytes(image), args[3])
                self.assertEqual(args[4]['X-Firmware-MD5'], hashlib.md5(image).hexdigest())
                self.assertEqual(args[4]['X-Firmware-Size'], '128')
                self.assertEqual(args[4]['X-EchoScope-Token'], 'test-token')
            for change in [{'capacity': 64}, {'device': 'other'}, {'protocol': 2}]:
                with patch.object(net, 'request', return_value=(json.dumps(info | change).encode(), {})) as request:
                    with self.assertRaises((ValueError, RuntimeError)):
                        net.upload('192.168.2.151', path)
                    self.assertEqual(request.call_count, 1)
            image[32] = 0  # Bootloader/merged image must never be sent.
            path.write_bytes(image)
            with patch.object(net, 'request') as request:
                with self.assertRaises(ValueError):
                    net.upload('192.168.2.151', path)
                request.assert_not_called()


if __name__ == '__main__':
    unittest.main()
