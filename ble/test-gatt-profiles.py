#!/usr/bin/env python3
"""Compare the emulated GATT profile shapes with the captured Fuji profiles."""
import importlib.util
from pathlib import Path
import sys
import unittest

from bumble.controller import Controller
from bumble.core import UUID
from bumble.device import Device, Peer
from bumble.gatt import Characteristic
from bumble.hci import Address
from bumble.host import Host
from bumble.link import LocalLink

spec = importlib.util.spec_from_file_location('fuji_ble', Path(__file__).with_name('fuji-ble.py'))
fuji = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = fuji
spec.loader.exec_module(fuji)


class ProfilesTest(unittest.IsolatedAsyncioTestCase):
    async def check_profile(self, model, secure, name=None):
        link = LocalLink()
        client_host, server_host = Host(), Host()
        client_host.controller = Controller('client', link=link)
        server_host.controller = Controller('server', link=link)
        client = Device('client', address=Address('F0:F1:F2:F3:F4:F5'), host=client_host)
        server = Device('camera', address=Address('F6:F7:F8:F9:FA:FB'), host=server_host)
        fuji.configure(server, bytes(4), secure=secure, model=model, gatt_profile=name,
                       identity={'ssid': 'test', 'password': 'testpass', 'gateway': '192.168.99.1'})
        await client.power_on()
        await server.power_on()
        await server.start_advertising()
        connection = await client.connect(server.random_address)
        try:
            peer = Peer(connection)
            await peer.discover_services()
            await peer.discover_characteristics()

            def service(u):
                return bool(peer.get_services_by_uuid(UUID(u)))

            def char(u):
                matches = peer.get_characteristics_by_uuid(UUID(u))
                self.assertLessEqual(len(matches), 1, u)
                return matches[0] if matches else None

            selected = name or fuji.default_gatt_profile(model, secure)
            minimal = selected == 'legacy-minimal'
            classic = selected == 'legacy-classic'
            expanded = not (minimal or classic)
            modern = selected in ('legacy-modern', 'legacy-modern-standby', 'legacy-modern-gfx',
                                  'secure-modern', 'secure-modern-new', 'secure-modern-gfx')
            standby = selected in ('legacy-modern-standby', 'legacy-modern-gfx',
                                   'secure-modern', 'secure-modern-new', 'secure-modern-gfx')
            gfx = selected in ('legacy-modern-gfx', 'secure-modern-gfx')
            early = selected == 'legacy-extended-early'
            self.assertEqual(service(fuji.DISCOVERY), not secure)
            self.assertEqual(service(fuji.PAIR_SERVICE), not secure)
            self.assertEqual(service(fuji.SECURE_DISCOVERY), secure)
            self.assertEqual(service(fuji.SECURE_PAIR_SERVICE), secure)
            self.assertEqual(service(fuji.NOT3_SERVICE), standby and secure)
            self.assertEqual(service(fuji.NOT3_LEGACY_SERVICE), standby and not secure)
            self.assertEqual(char(fuji.NOT3) is not None, standby)
            self.assertEqual(char(fuji.NOT6) is not None, modern)
            self.assertEqual(char(fuji.NOT8) is not None, gfx)
            self.assertEqual(char(fuji.NOT10) is not None, gfx)
            self.assertEqual(char(fuji.SECURE_NEW_PAIRING) is not None,
                             selected in ('secure-modern-new', 'secure-modern-gfx'))
            self.assertIsNotNone(char(fuji.TOKEN))
            self.assertEqual(char(fuji.IND3) is not None, not minimal)
            self.assertEqual(char(fuji.SHUTTER) is not None, not minimal)
            for uuid in (fuji.NOT5, fuji.NOT7, fuji.NOT9, fuji.GEO_INTERVAL):
                self.assertEqual(char(uuid) is not None, expanded, uuid)
            read = Characteristic.Properties.READ
            write = Characteristic.Properties.WRITE
            notify = Characteristic.Properties.NOTIFY
            self.assertEqual(char(fuji.SSID).properties, read if not expanded else read | write | (0 if early else notify))
            self.assertEqual(char(fuji.TIME_WRITE).properties, write if expanded else read | write)
            if expanded:
                for uuid in (fuji.NOT5, fuji.NOT7, fuji.NOT9, fuji.GEO_INTERVAL):
                    flags = read | write | (0 if early and uuid != fuji.NOT5 else notify)
                    self.assertEqual(char(uuid).properties, flags, uuid)
            if standby:
                self.assertEqual(char(fuji.NOT3).properties, read | write | notify)
            if gfx:
                for uuid in (fuji.NOT8, fuji.NOT10):
                    self.assertEqual(char(uuid).properties, read | write | notify)
        finally:
            await connection.disconnect()

    async def test_xh1_legacy(self):
        await self.check_profile('fuji_x_h1', False)

    async def test_xt4_legacy(self):
        await self.check_profile('fuji_x_t4', False)

    async def test_xt30_legacy(self):
        await self.check_profile('fuji_x_t30', False)

    async def test_xt5_secure(self):
        await self.check_profile('fuji_x_t5', True)

    async def test_named_profiles(self):
        for name in fuji.GATT_PROFILES:
            with self.subTest(profile=name):
                await self.check_profile(None, name.startswith('secure-'), name)

    def test_defaults_and_mismatch(self):
        self.assertEqual(fuji.default_gatt_profile('fuji_x_h1', False), 'legacy-minimal')
        for model in ('fuji_gfx100_ii', 'fuji_gfx100ii', 'gfx100ii', 'gfx'):
            self.assertEqual(fuji.default_gatt_profile(model, False), 'legacy-modern-gfx')
        self.assertEqual(fuji.default_gatt_profile('fuji_x_t5', True), 'secure-modern')
        with self.assertRaises(ValueError):
            fuji.configure(None, bytes(4), secure=False, gatt_profile='secure-modern')


if __name__ == '__main__':
    unittest.main()
