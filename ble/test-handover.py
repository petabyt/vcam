#!/usr/bin/env python3
"""Hardware-free BLE handover process and camera identity checks."""
import asyncio
import importlib.util
import os
from pathlib import Path
import signal
import sys
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('fuji_ble', Path(__file__).with_name('fuji-ble.py'))
fuji = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = fuji
spec.loader.exec_module(fuji)


class HandoverTest(unittest.IsolatedAsyncioTestCase):
    async def test_wifi_process_ready_stop_and_failure(self):
        loop = asyncio.get_running_loop()
        child = fuji.WifiProcess([
            sys.executable, '-c',
            'import os,signal,sys,time; os.kill(int(sys.argv[-1]), signal.SIGUSR1); time.sleep(60)',
        ])
        loop.add_signal_handler(signal.SIGUSR1, child.ready_signal.set)
        try:
            await child.start()
            self.assertTrue(child.ready)
            process = child.process
            await child.start()
            self.assertIs(process, child.process)
            await child.stop()
            self.assertFalse(child.ready)
            self.assertIsNotNone(process.returncode)
            failed = fuji.WifiProcess([sys.executable, '-c', 'import sys; sys.exit(7)'])
            with self.assertRaises(RuntimeError):
                await failed.start()
            self.assertFalse(failed.ready)
        finally:
            await child.stop()
            loop.remove_signal_handler(signal.SIGUSR1)

    async def test_camera_json_overrides(self):
        executable = os.environ.get('VCAM_TEST_BINARY')
        if not executable:
            self.skipTest('set VCAM_TEST_BINARY to a built vcam')
        args = SimpleNamespace(vcam=executable, model='fuji_x_t5', ssid='test-rig',
                               password='testpass123', gateway='192.168.99.1')
        identity, pairing, base = await fuji.camera_identity(args)
        self.assertEqual(identity, {'ssid': 'test-rig', 'password': 'testpass123',
                                    'gateway': '192.168.99.1'})
        self.assertEqual(pairing, 'secure')
        self.assertIn('--gateway', base)


if __name__ == '__main__':
    unittest.main()
