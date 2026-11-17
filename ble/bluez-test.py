#!/usr/bin/env python3
"""Scan and test the experimental Fuji handshake using BlueZ via Bleak."""

import argparse
import asyncio

from bleak import BleakClient, BleakScanner

DISCOVERY = '117c4142-edd4-4c77-8696-dd18eebb770a'
TOKEN = 'aba356eb-9633-4e60-b73f-f52516dbd671'
CLIENT_NAME = '85b9163e-62d1-49ff-a6f5-054b4630d4a1'
SSID = 'bf6dc9cf-3606-4ec9-a4c8-d77576e93ea4'
IND1 = 'a68e3f66-0fcc-4395-8d4c-aa980b5877fa'
IND2 = 'bd17ba04-b76b-4892-a545-b73ba1f74dae'
IND3 = '049ec406-ef75-4205-a390-08fe209c51f0'
TIME_NOTIFY = 'f9150137-5d40-4801-a8dc-f7fc5b01da50'
GEO_NOTIFY = 'ad06c7b7-f41a-46f4-a29a-712055319122'
TIME_WRITE = 'b9bfd37f-ccad-4d36-a1ee-018e792b3edf'
GEO_WRITE = '0f36ec14-29e5-411a-a1b6-64ee8383f090'
ADDITIONAL_SUBSCRIPTIONS = (
    '7170fd5a-56d9-4c19-b043-7a7047d8e1a0',
    '75823784-fbb7-4b71-abae-cd9a34072e3c',
    'e6692c5c-b7cd-44f4-95fc-eda07ce32560',
    'aab609c4-94dd-4d89-bc60-665d5090b828',
    '2a125640-706d-4dd1-b420-c0f4ab93c361',
    '82a9f452-c5ce-4ef5-8203-3fc9a47f8171',
    'deef7187-3f43-4364-9e22-11a8c8a15951',
    'c95d91ae-b247-4d6d-8661-7dd5d6a0f85b',
)


async def test(adapter, timeout, address):
    print(f'Scanning with BlueZ adapter {adapter}...', flush=True)
    token = None

    def matches(device, advertisement):
        nonlocal token
        data = advertisement.manufacturer_data.get(0x04d8, b'')
        if (len(data) == 5 and data[0] == 2
                and DISCOVERY in [u.lower() for u in advertisement.service_uuids]
                and device.address.lower() == address.lower()):
            token = data[1:]
            return True
        return False

    result = await BleakScanner.find_device_by_filter(
        matches, timeout=timeout, adapter=adapter)
    if result is None:
        raise RuntimeError('No matching legacy Fuji advertisement found')
    device = result
    print(f'Found {device.address}; connecting...', flush=True)
    async with BleakClient(device, adapter=adapter, timeout=timeout) as client:
        await client.write_gatt_char(TOKEN, token, response=True)
        await client.write_gatt_char(CLIENT_NAME, b'vcam-bluez-test', response=True)
        ssid = bytes(await client.read_gatt_char(SSID))
        if not ssid:
            raise RuntimeError('empty SSID')
        notifications = asyncio.Queue()
        for characteristic in (SSID, IND1, IND2, TIME_NOTIFY, GEO_NOTIFY, IND3, *ADDITIONAL_SUBSCRIPTIONS):
            await client.start_notify(characteristic,
                                      lambda _sender, value, u=characteristic:
                                      notifications.put_nowait((u, bytes(value))))
        received = [await asyncio.wait_for(notifications.get(), timeout) for _ in range(2)]
        if received != [(TIME_NOTIFY, b'\x01\x00'), (GEO_NOTIFY, b'\x01\x00')]:
            raise RuntimeError(f'unexpected initial notifications: {received!r}')
        await client.write_gatt_char(TIME_WRITE, b'\xea\x07\x01\x02\x03\x04\x05', response=True)
        await client.write_gatt_char(GEO_WRITE, bytes(23), response=True)
        print('Token, name, subscriptions and initial notifications succeeded', flush=True)
    print('Disconnected successfully')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adapter', required=True, help='BlueZ adapter, e.g. hci1')
    parser.add_argument('--address', required=True, help='Bluetooth address of the emulator')
    parser.add_argument('--timeout', type=float, default=15)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    try:
        asyncio.run(test(args.adapter, args.timeout, args.address))
    except (Exception, KeyboardInterrupt) as error:
        parser.exit(1, f'BlueZ test failed: {type(error).__name__}: {error}\n')


if __name__ == '__main__':
    main()
