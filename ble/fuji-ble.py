#!/usr/bin/env python3
"""Experimental Bumble peripheral for Fujifilm's legacy BLE handshake."""

import argparse
import asyncio
import json
import os
import re
import secrets
import signal
import struct
from dataclasses import dataclass

from bumble.att import (ATT_Error, ATT_INSUFFICIENT_AUTHORIZATION_ERROR,
                        ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
from bumble.controller import Controller
from bumble.core import AdvertisingData, ProtocolError, UUID
from bumble.device import Device, Peer
from bumble.gatt import Characteristic, CharacteristicValue, Service
from bumble.hci import Address
from bumble.host import Host
from bumble.link import LocalLink
from bumble.keys import MemoryKeyStore
from bumble.pairing import PairingConfig, PairingDelegate
from bumble.transport import open_transport

NAME = 'vcam-XT30'
DISCOVERY = '117c4142-edd4-4c77-8696-dd18eebb770a'
SECURE_DISCOVERY = 'a9d2b304-e8d6-4902-8336-352b772d7597'
SECURE_PAIR_SERVICE = '123d8f06-62a1-4935-9322-833c531ee225'
SECURE_STATUS = 'f557d96b-8284-4667-8793-b971c1deca2a'
PAIR_SERVICE = '91f1de68-dff6-466e-8b65-ff13b0f16fb8'
TOKEN = 'aba356eb-9633-4e60-b73f-f52516dbd671'
CLIENT_NAME = '85b9163e-62d1-49ff-a6f5-054b4630d4a1'
CONFIG_SERVICE = '4c0020fe-f3b6-40de-acc9-77d129067b14'
IND1 = 'a68e3f66-0fcc-4395-8d4c-aa980b5877fa'
IND2 = 'bd17ba04-b76b-4892-a545-b73ba1f74dae'
IND3 = '049ec406-ef75-4205-a390-08fe209c51f0'
TIME_NOTIFY = 'f9150137-5d40-4801-a8dc-f7fc5b01da50'
GEO_NOTIFY = 'ad06c7b7-f41a-46f4-a29a-712055319122'
NOT3_SERVICE = '804daa8e-ffeb-4ab3-8e75-6edd7303208d'
NOT3 = '7170fd5a-56d9-4c19-b043-7a7047d8e1a0'
NOT6 = 'e6692c5c-b7cd-44f4-95fc-eda07ce32560'
NETWORK_SERVICE = '4e941240-d01d-46b9-a5ea-67636806830b'
SSID = 'bf6dc9cf-3606-4ec9-a4c8-d77576e93ea4'
NOT5 = '75823784-fbb7-4b71-abae-cd9a34072e3c'
NOT7 = 'aab609c4-94dd-4d89-bc60-665d5090b828'
NOT8 = '2a125640-706d-4dd1-b420-c0f4ab93c361'
NOT9 = '82a9f452-c5ce-4ef5-8203-3fc9a47f8171'
NOT10 = 'deef7187-3f43-4364-9e22-11a8c8a15951'
GEO_INTERVAL = 'c95d91ae-b247-4d6d-8661-7dd5d6a0f85b'
TIME_SERVICE = 'e872b11f-d526-4ae1-9bb4-89a99d48fa59'
TIME_WRITE = 'b9bfd37f-ccad-4d36-a1ee-018e792b3edf'
GEO_SERVICE = '3b46ec2b-48ba-41fd-b1b8-ed860b60d22b'
GEO_WRITE = '0f36ec14-29e5-411a-a1b6-64ee8383f090'
SHUTTER_SERVICE = '6514eb81-4e8f-458d-aa2a-e691336cdfac'
SHUTTER = '7fcf49c6-4ff0-4777-a03d-1a79166af7a8'
CHR_WIFI_TRIGGER = '600655e6-3637-42f1-8fb2-44efc5c63b13'
CHR_UNKNOWN1_PASSWORD = 'e809256a-915c-4967-92e8-53b7d4cad213'


@dataclass(frozen=True)
class Geotag:
    # Coordinates are signed degrees * 1e7; altitude is a signed integer.
    # The altitude unit and timestamp timezone are not established by the capture.
    latitude: int
    longitude: int
    altitude: int
    reserved: bytes
    year: int
    month: int
    day: int
    hour: int
    minute: int
    second: int


class LegacyProfile:
    """Only the discovery and initial legacy pairing subset; not secure pairing."""

    def __init__(self, token):
        if len(token) != 4:
            raise ValueError('token must contain four bytes')
        self.token = token
        self.authenticated = set()
        self.names = {}
        self.subscriptions = {}
        self.geotags = {}
        self.geo_intervals = {}
        self.geo_tasks = {}
        self.wifi = None
        self.wifi_ssid = b'FUJIFILM-VCAM'
        self.wifi_password = b''
        self.disconnected = asyncio.Event()

    def connect(self, connection):
        self.disconnected.clear()
        print('BLE client connected', flush=True)
        def disconnected(_reason):
            self.authenticated.discard(connection)
            self.names.pop(connection, None)
            self.subscriptions.pop(connection, None)
            self.geotags.pop(connection, None)
            self.geo_intervals.pop(connection, None)
            task = self.geo_tasks.pop(connection, None)
            if task:
                task.cancel()
            self.disconnected.set()
            print('BLE client disconnected', flush=True)
        connection.on(connection.EVENT_DISCONNECTION, disconnected)

    def write_token(self, connection, value):
        self.authenticated.discard(connection)
        self.names.pop(connection, None)
        if len(value) != 4:
            print('Legacy token rejected: invalid length', flush=True)
            raise ATT_Error(ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
        if not secrets.compare_digest(value, self.token):
            print('Legacy token rejected', flush=True)
            raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
        self.authenticated.add(connection)
        print('Legacy token accepted', flush=True)

    def write_name(self, connection, value):
        if connection not in self.authenticated:
            raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
        if not 0 < len(value) <= 64:
            raise ATT_Error(ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
        self.names[connection] = bytes(value)
        print(f'Client name accepted ({len(value)} bytes)', flush=True)

    def accept_payload(self, connection, value, expected, label):
        if connection not in self.authenticated:
            raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
        if len(value) != expected:
            raise ATT_Error(ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
        # These values may contain personal data; do not log or persist them.
        print(f'{label} accepted ({len(value)} bytes)', flush=True)

    def write_geotag(self, connection, value):
        self.accept_payload(connection, value, 23, 'Geotag')
        self.geotags[connection] = Geotag(*struct.unpack('<iii4sHBBBBB', value))

    @staticmethod
    def writable(characteristic_uuid, callback):
        return Characteristic(characteristic_uuid, Characteristic.Properties.WRITE,
                              Characteristic.WRITEABLE, CharacteristicValue(write=callback))

    @staticmethod
    def subscribable(characteristic_uuid, indication, value):
        mode = Characteristic.Properties.INDICATE if indication else Characteristic.Properties.NOTIFY
        return Characteristic(characteristic_uuid, Characteristic.Properties.READ | mode,
                              Characteristic.READABLE, value)

    def install(self, device):
        device.on(device.EVENT_CONNECTION, self.connect)
        self.install_pairing(device)
        config = [
            self.subscribable(IND1, True, b'\x00\x80'),
            self.subscribable(IND2, True, b'\x00\x00'),
            self.subscribable(IND3, True, b'\x00\x00'),
            self.subscribable(TIME_NOTIFY, False, b'\x01\x00'),
            self.subscribable(GEO_NOTIFY, False, b'\x01\x00'),
            # Not present in the X-T30 capture; included for libfuji's
            # secure-path subscription list, with a provisional value.
            self.subscribable(NOT6, False, b'\x00\x00'),
        ]
        device.add_service(Service(CONFIG_SERVICE, config))
        # The X-T30 capture sends these two notifications after both CCCDs
        # are enabled. Dispatch after the CCCD response, to the same peer.
        time_chr, geo_chr = config[3], config[4]

        def subscribed(connection, characteristic, enabled, _indicate):
            active = self.subscriptions.setdefault(connection, set())
            was_ready = {time_chr, geo_chr}.issubset(active)
            if enabled:
                active.add(characteristic)
            else:
                active.discard(characteristic)
            if not was_ready and {time_chr, geo_chr}.issubset(active):
                asyncio.create_task(send_initial_notifications(connection))

        async def send_initial_notifications(connection):
            await device.notify_subscriber(connection, time_chr, b'\x01\x00')
            await device.notify_subscriber(connection, geo_chr, b'\x01\x00')

        async def request_geotags(connection, interval):
            try:
                while True:
                    await asyncio.sleep(interval)
                    if geo_chr in self.subscriptions.get(connection, set()):
                        await device.notify_subscriber(connection, geo_chr, b'\x01\x00')
            except asyncio.CancelledError:
                pass

        def read_interval(connection):
            return struct.pack('<H', self.geo_intervals.get(connection, 10))

        def write_interval(connection, value):
            if connection not in self.authenticated:
                raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
            if len(value) != 2:
                raise ATT_Error(ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
            interval = int.from_bytes(value, 'little')
            self.geo_intervals[connection] = interval
            task = self.geo_tasks.pop(connection, None)
            if task:
                task.cancel()
            # Zero disables requests provisionally; it is not covered by the capture.
            if interval:
                self.geo_tasks[connection] = asyncio.create_task(request_geotags(connection, interval))
            asyncio.create_task(device.notify_subscriber(connection, interval_chr, bytes(value)))

        interval_chr = Characteristic(
            GEO_INTERVAL, Characteristic.Properties.READ | Characteristic.Properties.WRITE | Characteristic.Properties.NOTIFY,
            Characteristic.READABLE | Characteristic.WRITEABLE,
            CharacteristicValue(read=read_interval, write=write_interval))

        for characteristic in (time_chr, geo_chr):
            characteristic.on(characteristic.EVENT_SUBSCRIPTION,
                              lambda connection, notify, indicate, char=characteristic:
                              subscribed(connection, char, notify, indicate))

        def read_password(connection):
            if connection not in self.authenticated:
                raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
            return self.wifi_password

        def read_wifi_ready(_connection):
            return bytes([int(self.wifi is not None and self.wifi.ready)])

        device.add_service(Service(NETWORK_SERVICE, [
            self.subscribable(SSID, False, self.wifi_ssid),
            Characteristic(CHR_UNKNOWN1_PASSWORD, Characteristic.Properties.READ,
                           Characteristic.READABLE, CharacteristicValue(read=read_password)),
            self.subscribable(NOT5, False, bytes(13)),
            self.subscribable(NOT7, False, b'\x01\x00'),
            self.subscribable(NOT8, False, b'\x00\x00'),
            Characteristic(NOT9, Characteristic.Properties.READ | Characteristic.Properties.NOTIFY,
                           Characteristic.READABLE, CharacteristicValue(read=read_wifi_ready)),
            self.subscribable(NOT10, False, b'\x00\x00'),
            interval_chr,
        ]))
        device.add_service(Service(NOT3_SERVICE, [self.subscribable(NOT3, False, b'\x00\x00')]))
        device.add_service(Service(TIME_SERVICE, [
            self.writable(TIME_WRITE, lambda conn, value: self.accept_payload(conn, value, 7, 'Time')),
        ]))
        device.add_service(Service(GEO_SERVICE, [
            self.writable(GEO_WRITE, self.write_geotag),
        ]))
        async def trigger_wifi(connection, value):
            if connection not in self.authenticated:
                raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
            if len(value) != 2:
                raise ATT_Error(ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
            if value != b'\x04\x00' or self.wifi is None:
                raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
            try:
                await self.wifi.start()
            except (OSError, RuntimeError, asyncio.TimeoutError) as error:
                print(f'Wi-Fi startup failed: {error}', flush=True)
                raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR) from error
            asyncio.create_task(device.indicate_subscriber(connection, config[0], b'\x01\x80'))

        def shutter_release(connection, value):
            if connection not in self.authenticated:
                raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
            if value not in (b'\x01\x00', b'\x00\x00'):
                raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR)

        # Accept the release sequence sent by libfuji before handover, but do
        # not pretend to implement shutter capture.
        device.add_service(Service(SHUTTER_SERVICE, [self.writable(CHR_WIFI_TRIGGER, trigger_wifi),
                                                    self.writable(SHUTTER, shutter_release)]))

    def install_pairing(self, device):
        device.add_service(Service(DISCOVERY, []))
        device.add_service(Service(PAIR_SERVICE, [
            self.writable(TOKEN, self.write_token),
            self.writable(CLIENT_NAME, self.write_name),
        ]))


class SecureProfile(LegacyProfile):
    # X-T5 reports 90 5d 04 00; these provisional, non-camera-specific
    # bytes deliberately do not reproduce a real device's status.
    status = b'\x00\x00\x00\x00'

    def write_name(self, connection, value):
        # X-T5 capture includes name writes before the status exchange.
        if not 0 < len(value) <= 64:
            raise ATT_Error(ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
        self.names[connection] = bytes(value)
        print(f'Client name accepted ({len(value)} bytes)', flush=True)

    def read_status(self, _connection):
        return self.status

    def write_status(self, connection, value):
        self.authenticated.discard(connection)
        if len(value) != 4:
            raise ATT_Error(ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
        if value != self.status[:3] + b'\x20':
            raise ATT_Error(ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
        self.authenticated.add(connection)
        print('Secure status acknowledged', flush=True)

    def install_pairing(self, device):
        rw = Characteristic.Properties.READ | Characteristic.Properties.WRITE
        permission = (Characteristic.READABLE | Characteristic.WRITEABLE
                      | Characteristic.READ_REQUIRES_AUTHENTICATION
                      | Characteristic.WRITE_REQUIRES_AUTHENTICATION)
        device.add_service(Service(SECURE_PAIR_SERVICE, [
            Characteristic(SECURE_STATUS, rw, permission,
                           CharacteristicValue(read=self.read_status, write=self.write_status)),
            Characteristic(CLIENT_NAME, Characteristic.Properties.WRITE,
                           Characteristic.WRITEABLE | Characteristic.WRITE_REQUIRES_AUTHENTICATION,
                           CharacteristicValue(write=self.write_name)),
        ]))


class ConfirmPairing(PairingDelegate):
    def __init__(self):
        super().__init__(io_capability=self.IoCapability.DISPLAY_OUTPUT_AND_KEYBOARD_INPUT)

    async def compare_numbers(self, number, digits):
        prompt = f'Confirm {number:0{digits}d} matches the client? [y/N] '
        answer = await asyncio.to_thread(input, prompt)
        return answer.strip().lower() in ('y', 'yes')

    async def display_number(self, number, digits):
        print(f'Bluetooth pairing passkey: {number:0{digits}d}', flush=True)

    async def get_number(self):
        answer = await asyncio.to_thread(input, 'Enter the client Bluetooth passkey (or blank to reject): ')
        return int(answer) if answer.isdecimal() and int(answer) < 1000000 else None


def configure(device, token, secure=False, delegate_factory=ConfirmPairing, wifi=None, identity=None):
    profile = SecureProfile(token) if secure else LegacyProfile(token)
    profile.wifi = wifi
    if identity:
        profile.wifi_ssid = identity['ssid'].encode()
        profile.wifi_password = identity['password'].encode()
    if secure:
        device.keystore = MemoryKeyStore()
        device.pairing_config_factory = lambda _connection: PairingConfig(
            sc=True, mitm=True, bonding=True, delegate=delegate_factory())
    profile.install(device)
    device.advertising_data = bytes(AdvertisingData([
        (AdvertisingData.FLAGS, b'\x06'),
        (AdvertisingData.COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
         bytes(UUID(SECURE_DISCOVERY if secure else DISCOVERY))),
        (AdvertisingData.MANUFACTURER_SPECIFIC_DATA,
         b'\xd8\x04\x01' + token + b'\x00' if secure else b'\xd8\x04\x02' + token),
    ]))
    device.scan_response_data = bytes(AdvertisingData([
        (AdvertisingData.COMPLETE_LOCAL_NAME, NAME.encode()),
    ]))
    return profile


async def self_test():
    token = b'\x12\x34\x56\x78'
    link = LocalLink()
    client_controller = Controller('client', link=link)
    server_controller = Controller('server', link=link)
    client_host, server_host = Host(), Host()
    client_host.controller, server_host.controller = client_controller, server_controller
    client = Device('client', address=Address('F0:F1:F2:F3:F4:F5'), host=client_host)
    server = Device(NAME, address=Address('F6:F7:F8:F9:FA:FB'), host=server_host)
    profile = configure(server, token)
    class TestWifi:
        ready = False

        async def start(self):
            self.ready = True

    profile.wifi = TestWifi()
    await client.power_on()
    await server.power_on()
    advertisements = asyncio.Queue()
    client.on(client.EVENT_ADVERTISEMENT, advertisements.put_nowait)
    await client.start_scanning()
    await server.start_advertising(auto_restart=True)
    advertisement = await asyncio.wait_for(advertisements.get(), 5)
    if UUID(DISCOVERY) not in sum(advertisement.data.get_all(
            AdvertisingData.COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS), []):
        raise RuntimeError('Fuji service missing from advertisement')
    if b'\xd8\x04\x02' + token not in advertisement.data.get_all(
            AdvertisingData.MANUFACTURER_SPECIFIC_DATA, raw=True):
        raise RuntimeError('Fuji manufacturer data missing')
    await client.stop_scanning()

    async def connect_and_pair(check_errors):
        connection = await client.connect(advertisement.address)
        peer = Peer(connection)
        await peer.discover_services()
        await peer.discover_characteristics()

        def characteristic(u):
            matches = peer.get_characteristics_by_uuid(UUID(u))
            if len(matches) != 1:
                raise RuntimeError(f'missing characteristic {u}')
            return matches[0]

        async def rejected(u, value, expected):
            try:
                await characteristic(u).write_value(value, with_response=True)
            except ProtocolError as error:
                if error.error_code != expected:
                    raise RuntimeError(f'unexpected ATT error: {error}') from error
                return
            raise RuntimeError('invalid write accepted')

        if check_errors:
            await rejected(CLIENT_NAME, b'client', ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
            await rejected(CHR_WIFI_TRIGGER, b'\x04\x00', ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
            await rejected(GEO_WRITE, bytes(23), ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
            await rejected(GEO_INTERVAL, b'\x01\x00', ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
            await rejected(TOKEN, b'bad!', ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
            await rejected(TOKEN, b'x', ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
        await characteristic(TOKEN).write_value(token, with_response=True)
        await characteristic(CLIENT_NAME).write_value(b'client', with_response=True)
        if check_errors:
            await rejected(CHR_WIFI_TRIGGER, b'\x04', ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
            await rejected(CHR_WIFI_TRIGGER, b'\x03\x00', ATT_INSUFFICIENT_AUTHORIZATION_ERROR)
        if await characteristic(SSID).read_value() != b'FUJIFILM-VCAM':
            raise RuntimeError('unexpected SSID')
        notifications = asyncio.Queue()
        for u in (SSID, IND1, IND2, TIME_NOTIFY, GEO_NOTIFY, IND3,
                  NOT3, NOT5, NOT6, NOT7, NOT8, NOT9, NOT10, GEO_INTERVAL):
            await characteristic(u).subscribe(lambda value, u=u: notifications.put_nowait((u, value)))
        received = [await asyncio.wait_for(notifications.get(), 5) for _ in range(2)]
        if received != [(TIME_NOTIFY, b'\x01\x00'), (GEO_NOTIFY, b'\x01\x00')]:
            raise RuntimeError(f'unexpected initial notifications: {received!r}')
        profile.wifi.ready = False
        if await characteristic(NOT9).read_value() != b'\x00':
            raise RuntimeError('Wi-Fi reported ready before startup')
        await characteristic(CHR_WIFI_TRIGGER).write_value(b'\x04\x00', with_response=True)
        if await characteristic(NOT9).read_value() != b'\x01':
            raise RuntimeError('Wi-Fi did not report ready after startup')
        if await asyncio.wait_for(notifications.get(), 5) != (IND1, b'\x01\x80'):
            raise RuntimeError('missing Wi-Fi status indication')
        await characteristic(TIME_WRITE).write_value(b'\xea\x07\x01\x02\x03\x04\x05', with_response=True)
        if check_errors:
            await rejected(GEO_WRITE, bytes(22), ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
            await rejected(GEO_INTERVAL, b'\x01', ATT_INVALID_ATTRIBUTE_LENGTH_ERROR)
        payload = struct.pack('<iii4sHBBBBB', -123456789, 987654321, -42, b'\x00' * 4,
                              2026, 11, 17, 12, 34, 56)
        await characteristic(GEO_WRITE).write_value(payload, with_response=True)
        if list(profile.geotags.values()) != [Geotag(-123456789, 987654321, -42, b'\x00' * 4,
                                                     2026, 11, 17, 12, 34, 56)]:
            raise RuntimeError('geotag decoded incorrectly')
        await characteristic(GEO_INTERVAL).write_value(b'\x01\x00', with_response=True)
        if await characteristic(GEO_INTERVAL).read_value() != b'\x01\x00':
            raise RuntimeError('geotag interval not saved')
        if await asyncio.wait_for(notifications.get(), 5) != (GEO_INTERVAL, b'\x01\x00'):
            raise RuntimeError('missing interval notification')
        if await asyncio.wait_for(notifications.get(), 5) != (GEO_NOTIFY, b'\x01\x00'):
            raise RuntimeError('missing periodic geotag request')
        await characteristic(GEO_INTERVAL).write_value(b'\x00\x00', with_response=True)
        if await asyncio.wait_for(notifications.get(), 5) != (GEO_INTERVAL, b'\x00\x00'):
            raise RuntimeError('missing disabled interval notification')
        try:
            await asyncio.wait_for(notifications.get(), 1.2)
            raise RuntimeError('geotag request after interval disabled')
        except asyncio.TimeoutError:
            pass
        if list(profile.names.values()) != [b'client']:
            raise RuntimeError('client name was not saved for the connection')
        await connection.disconnect()
        await asyncio.wait_for(profile.disconnected.wait(), 5)
        if profile.authenticated or profile.names or profile.geotags or profile.geo_tasks or profile.geo_intervals:
            raise RuntimeError('connection state was not cleared')

    await connect_and_pair(True)
    await connect_and_pair(False)
    print('Virtual Fuji discovery, legacy handshake, subscriptions and reconnect passed')


async def self_test_secure():
    class TestDelegate(ConfirmPairing):
        async def compare_numbers(self, number, digits):
            return True

    link = LocalLink()
    client_controller, server_controller = Controller('client', link=link), Controller('server', link=link)
    client_host, server_host = Host(), Host()
    client_host.controller, server_host.controller = client_controller, server_controller
    client = Device('client', address=Address('F0:F1:F2:F3:F4:F5'), host=client_host)
    server = Device(NAME, address=Address('F6:F7:F8:F9:FA:FB'), host=server_host)
    profile = configure(server, b'\x12\x34\x56\x78', secure=True, delegate_factory=TestDelegate)
    client.keystore = MemoryKeyStore()
    client.pairing_config_factory = lambda _connection: PairingConfig(
        sc=True, mitm=True, bonding=True, delegate=TestDelegate())
    await client.power_on()
    await server.power_on()
    await server.start_advertising(auto_restart=True)
    connection = await client.connect(server.random_address)
    peer = Peer(connection)
    await peer.discover_services()
    await peer.discover_characteristics()
    if peer.get_services_by_uuid(UUID(PAIR_SERVICE)):
        raise RuntimeError('legacy pairing service exposed in secure profile')
    status = peer.get_characteristics_by_uuid(UUID(SECURE_STATUS))[0]
    name = peer.get_characteristics_by_uuid(UUID(CLIENT_NAME))[0]
    geo = peer.get_characteristics_by_uuid(UUID(GEO_WRITE))[0]
    try:
        await status.read_value()
    except ProtocolError:
        pass
    else:
        raise RuntimeError('unencrypted secure status read was accepted')
    await connection.pair()
    if not connection.is_encrypted or not connection.authenticated:
        raise RuntimeError('pairing did not establish authenticated encryption')
    value = await status.read_value()
    await name.write_value(b'client', with_response=True)
    try:
        await geo.write_value(bytes(23), with_response=True)
    except ProtocolError as error:
        if error.error_code != ATT_INSUFFICIENT_AUTHORIZATION_ERROR:
            raise
    else:
        raise RuntimeError('geotag write accepted before status acknowledgement')
    try:
        await status.write_value(b'\xff\xff\xff\x20', with_response=True)
    except ProtocolError:
        pass
    else:
        raise RuntimeError('invalid secure status acknowledgement accepted')
    await status.write_value(value[:3] + b'\x20', with_response=True)
    await name.write_value(b'client', with_response=True)
    await geo.write_value(bytes(23), with_response=True)
    if len(profile.geotags) != 1:
        raise RuntimeError('secure geotag not saved')
    if list(profile.names.values()) != [b'client']:
        raise RuntimeError('secure client name not saved')
    await connection.disconnect()
    await asyncio.wait_for(profile.disconnected.wait(), 5)
    connection = await client.connect(server.random_address)
    await connection.encrypt()
    if not connection.is_encrypted or not connection.authenticated:
        raise RuntimeError('bonded reconnect did not restore authenticated encryption')
    peer = Peer(connection)
    await peer.discover_services()
    await peer.discover_characteristics()
    status = peer.get_characteristics_by_uuid(UUID(SECURE_STATUS))[0]
    name = peer.get_characteristics_by_uuid(UUID(CLIENT_NAME))[0]
    value = await status.read_value()
    await status.write_value(value[:3] + b'\x20', with_response=True)
    await name.write_value(b'client', with_response=True)
    await connection.disconnect()
    await asyncio.wait_for(profile.disconnected.wait(), 5)
    print('Virtual secure bonding, authenticated encryption, acknowledgement and reconnect passed')


class WifiProcess:
    def __init__(self, command):
        self.command = command
        self.process = None
        self.started = False
        self.ready_signal = asyncio.Event()
        self.lock = asyncio.Lock()

    @property
    def ready(self):
        return self.started and self.process is not None and self.process.returncode is None

    async def start(self):
        async with self.lock:
            if self.ready:
                return
            self.started = False
            self.ready_signal.clear()
            self.process = await asyncio.create_subprocess_exec(*self.command, '--sig', str(os.getpid()))
            signal_wait = asyncio.create_task(self.ready_signal.wait())
            exit_wait = asyncio.create_task(self.process.wait())
            try:
                done, _ = await asyncio.wait((signal_wait, exit_wait), timeout=30,
                                             return_when=asyncio.FIRST_COMPLETED)
                if exit_wait in done:
                    raise RuntimeError(f'vcam exited with status {self.process.returncode}')
                if signal_wait not in done:
                    raise asyncio.TimeoutError('vcam did not signal PTP/IP readiness')
                self.started = True
            except BaseException:
                await self.stop()
                raise
            finally:
                signal_wait.cancel()
                exit_wait.cancel()

    async def stop(self):
        self.started = False
        if self.process and self.process.returncode is None:
            self.process.terminate()
            try:
                await asyncio.wait_for(self.process.wait(), 10)
            except asyncio.TimeoutError:
                self.process.kill()
                await self.process.wait()
        self.process = None


async def camera_identity(args):
    overrides = []
    for flag in ('ssid', 'password', 'gateway'):
        value = getattr(args, flag)
        if value is not None:
            overrides.extend(('--' + flag, value))
    base = [args.vcam, args.model, 'wifi', *overrides]
    process = await asyncio.create_subprocess_exec(*base, '--dump-camera-json', stdout=asyncio.subprocess.PIPE)
    output, _ = await process.communicate()
    if process.returncode:
        raise RuntimeError('vcam camera JSON query failed')
    camera = json.loads(output)
    identity = camera['wifi']
    if not all(isinstance(identity.get(k), str) for k in ('ssid', 'password', 'gateway')):
        raise RuntimeError('invalid vcam Wi-Fi identity')
    if camera.get('manufacturer') != 'FUJIFILM':
        raise RuntimeError('BLE handover requires a Fujifilm model')
    pairing = camera.get('bluetooth', {}).get('pairing')
    if pairing not in ('legacy', 'secure'):
        raise RuntimeError('vcam did not provide a supported BLE pairing profile')
    return identity, pairing, base


async def serve(args, token):
    wifi = None
    identity = None
    secure = args.secure or args.pairing == 'secure'
    loop = asyncio.get_running_loop()
    if args.model:
        identity, pairing, base = await camera_identity(args)
        secure = (args.pairing or ('secure' if args.secure else pairing)) == 'secure'
        command = [*base, '--ble-handover', '--wait-timeout', '0']
        if args.wifi_dev:
            command.extend(('--wifi-dev', args.wifi_dev))
        wifi = WifiProcess(command)
        loop.add_signal_handler(signal.SIGUSR1, wifi.ready_signal.set)
    stopped = asyncio.Event()
    loop.add_signal_handler(signal.SIGTERM, stopped.set)
    async with await open_transport(args.transport) as hci:
        device = Device(NAME, address=Address(args.address), host=Host(hci.source, hci.sink))
        configure(device, token, secure=secure, wifi=wifi, identity=identity)
        await device.power_on()
        await device.start_advertising(auto_restart=True)
        print(f'Advertising experimental Fuji {"secure" if secure else "legacy"} profile on {args.transport} at {args.address}; Ctrl-C to stop', flush=True)
        try:
            stopped_task = asyncio.create_task(stopped.wait())
            await asyncio.wait((hci.source.terminated, stopped_task), return_when=asyncio.FIRST_COMPLETED)
        finally:
            if wifi:
                await wifi.stop()
            await device.stop_advertising()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--self-test', action='store_true')
    mode.add_argument('--self-test-secure', action='store_true')
    mode.add_argument('--transport', help='explicit Bumble HCI transport')
    parser.add_argument('--address', help='static random Bluetooth address for --transport')
    parser.add_argument('--secure', action='store_true', help='experimental secure bonding profile')
    parser.add_argument('--pairing', choices=('legacy', 'secure'), help='override the model pairing profile')
    parser.add_argument('--model', help='vcam Fuji model; enable Wi-Fi handover')
    parser.add_argument('--vcam', default='./build/vcam', help='vcam executable (default: ./build/vcam)')
    parser.add_argument('--ssid', help='override vcam AP SSID')
    parser.add_argument('--password', help='override vcam AP password (empty means open)')
    parser.add_argument('--gateway', help='override vcam AP gateway')
    parser.add_argument('--wifi-dev', help='dedicated AP interface passed to vcam')
    args = parser.parse_args()
    if bool(args.transport) != bool(args.address):
        parser.error('--transport and --address must be provided together')
    if args.transport == 'hci-socket':
        parser.error('specify an explicit adapter, e.g. hci-socket:1')
    if args.address and (not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', args.address)
                         or int(args.address[:2], 16) & 0xc0 != 0xc0):
        parser.error('--address must be a static random Bluetooth address (top two bits set)')
    if (args.model and not args.transport) or (not args.model and any(
            v is not None for v in (args.ssid, args.password, args.gateway, args.wifi_dev))):
        parser.error('--model requires --transport; Wi-Fi overrides require --model')
    if args.secure and args.pairing == 'legacy':
        parser.error('--secure conflicts with --pairing legacy')
    if args.pairing and not args.transport:
        parser.error('--pairing requires --transport')
    try:
        if args.secure and not args.transport:
            parser.error('--secure requires --transport; use --self-test-secure for the virtual test')
        task = self_test() if args.self_test else self_test_secure() if args.self_test_secure else serve(args, secrets.token_bytes(4))
        asyncio.run(task)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
