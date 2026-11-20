#!/usr/bin/env python3
"""Expose a BlueZ virtual controller linked to the Bumble Fuji peripheral."""

import asyncio
import argparse
import importlib.util
from pathlib import Path
import secrets
import signal
import sys

from bumble.controller import Controller
from bumble.core import PhysicalTransport
from bumble.device import Device
from bumble.hci import Address
from bumble.host import Host
from bumble.link import LocalLink
from bumble.transport import open_transport


class VhciLink(LocalLink):
    # Bumble e1a21f5's LocalLink uses random_address for all LE ACL packets,
    # even when BlueZ connects using its public address. Use the address that
    # was actually used for this connection instead.
    def send_acl_data(self, sender_controller, destination_address, transport, data):
        if transport != PhysicalTransport.LE:
            return super().send_acl_data(sender_controller, destination_address, transport, data)
        target = self.find_le_controller(destination_address)
        connection = sender_controller.le_connections.get(destination_address)
        if target is not None and connection is not None:
            asyncio.get_running_loop().call_soon(
                target.on_link_acl_data, connection.self_address, transport, data)


async def main(secure, handover, handover_vcam):
    spec = importlib.util.spec_from_file_location('fuji_ble', Path(__file__).with_name('fuji-ble.py'))
    fuji = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fuji)
    async with await open_transport('vhci') as hci:
        link = VhciLink()
        bluez_controller = Controller('bluez', host_source=hci.source, host_sink=hci.sink,
                                      link=link, public_address='02:56:43:41:4D:01')
        camera_controller = Controller('camera', link=link)
        host = Host()
        host.controller = camera_controller
        camera = Device(fuji.NAME, address=Address('F6:F7:F8:F9:FA:FB'), host=host)
        class TestPairing(fuji.ConfirmPairing):
            async def compare_numbers(self, number, digits):
                print('Virtual test: automatically confirming pairing', flush=True)
                return True

        wifi = None
        identity = None
        if handover:
            # No network changes: the fake child signals readiness then waits.
            command = ([handover_vcam, 'fuji_x_t5', 'tcp', '--ip', '127.0.0.1', '--ble-handover']
                       if handover_vcam else [sys.executable, '-c',
                       'import os,signal,sys,time; os.kill(int(sys.argv[-1]), signal.SIGUSR1); time.sleep(300)'])
            wifi = fuji.WifiProcess(command)
            asyncio.get_running_loop().add_signal_handler(signal.SIGUSR1, wifi.ready_signal.set)
            identity = {'ssid': 'VCAM-HANDOVER-TEST', 'password': 'testpass123', 'gateway': '192.168.99.1'}
        fuji.configure(camera, secrets.token_bytes(4), secure=secure,
                       delegate_factory=TestPairing, wifi=wifi, identity=identity)
        await camera.power_on()
        await camera.start_advertising(auto_restart=True)
        print('Virtual BlueZ controller 02:56:43:41:4D:01; camera F6:F7:F8:F9:FA:FB', flush=True)
        try:
            await hci.source.terminated
        finally:
            if wifi:
                await wifi.stop()
        # Keep the controller alive until the transport closes.
        _ = bluez_controller


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--secure', action='store_true', help='secure virtual pairing; confirmations are automatic')
    parser.add_argument('--handover', action='store_true', help='simulate Wi-Fi readiness without touching networking')
    parser.add_argument('--handover-vcam', help='with --handover, start this vcam in loopback TCP mode')
    args = parser.parse_args()
    if args.handover_vcam and not args.handover:
        parser.error('--handover-vcam requires --handover')
    try:
        asyncio.run(main(args.secure, args.handover, args.handover_vcam))
    except KeyboardInterrupt:
        pass
