#!/usr/bin/env python3
"""Hardware-independent tests for the BlueZ smoke-test client."""

import asyncio
import importlib.util
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('bluez_test', Path(__file__).with_name('bluez-test.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


async def test():
    target = SimpleNamespace(address='F6:F7:F8:F9:FA:FB')
    other = SimpleNamespace(address='F6:F7:F8:F9:FA:FC')
    advertisement = SimpleNamespace(manufacturer_data={0x04d8: b'\x02\x12\x34\x56\x78'},
                                    service_uuids=[module.DISCOVERY.upper()])
    writes = []

    async def scan(match, timeout, adapter):
        assert timeout == 2 and adapter == 'hci2'
        assert not match(other, advertisement)
        assert match(target, advertisement)
        return target

    class Client:
        def __init__(self, device, adapter, timeout):
            assert device == target and adapter == 'hci2' and timeout == 2

        async def __aenter__(self):
            return self

        async def __aexit__(self, *_args):
            pass

        async def write_gatt_char(self, uuid, value, response):
            writes.append((uuid, value, response))

        async def read_gatt_char(self, _uuid):
            return b'FUJIFILM-VCAM'

        async def start_notify(self, uuid, callback):
            if uuid == module.TIME_NOTIFY:
                self.time_callback = callback
            if uuid == module.GEO_NOTIFY:
                self.time_callback(None, b'\x01\x00')
                callback(None, b'\x01\x00')

    with patch.object(module.BleakScanner, 'find_device_by_filter', scan), patch.object(module, 'BleakClient', Client):
        await module.test('hci2', 2, target.address.lower())
    assert writes == [(module.TOKEN, b'\x12\x34\x56\x78', True),
                      (module.CLIENT_NAME, b'vcam-bluez-test', True),
                      (module.TIME_WRITE, b'\xea\x07\x01\x02\x03\x04\x05', True),
                      (module.GEO_WRITE, bytes(23), True)]


if __name__ == '__main__':
    asyncio.run(test())
