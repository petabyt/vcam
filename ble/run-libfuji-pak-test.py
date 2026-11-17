#!/usr/bin/env python3
"""Discover the VHCI camera and run the libfuji/libpak test while scanning."""

import argparse
import asyncio
import sys

from bleak import BleakScanner

CAMERA = 'F6:F7:F8:F9:FA:FB'
LEGACY_SUBSCRIPTIONS = {
    'bf6dc9cf-3606-4ec9-a4c8-d77576e93ea4', 'a68e3f66-0fcc-4395-8d4c-aa980b5877fa',
    'bd17ba04-b76b-4892-a545-b73ba1f74dae', 'f9150137-5d40-4801-a8dc-f7fc5b01da50',
    'ad06c7b7-f41a-46f4-a29a-712055319122', '049ec406-ef75-4205-a390-08fe209c51f0',
}
SECURE_SUBSCRIPTIONS = (LEGACY_SUBSCRIPTIONS - {'049ec406-ef75-4205-a390-08fe209c51f0'}) | {
    '7170fd5a-56d9-4c19-b043-7a7047d8e1a0', '75823784-fbb7-4b71-abae-cd9a34072e3c',
    'e6692c5c-b7cd-44f4-95fc-eda07ce32560', 'aab609c4-94dd-4d89-bc60-665d5090b828',
    '2a125640-706d-4dd1-b420-c0f4ab93c361', '82a9f452-c5ce-4ef5-8203-3fc9a47f8171',
    'deef7187-3f43-4364-9e22-11a8c8a15951', 'c95d91ae-b247-4d6d-8661-7dd5d6a0f85b',
}


async def run(adapter, executable, secure):
    found = asyncio.Event()

    def detected(device, _advertisement):
        if device.address.upper() == CAMERA:
            found.set()

    async with BleakScanner(detection_callback=detected, adapter=adapter):
        await asyncio.wait_for(found.wait(), 15)
        process = await asyncio.create_subprocess_exec(
            executable, *(['--secure'] if secure else []), stdout=asyncio.subprocess.PIPE)
        output, _ = await process.communicate()
        text = output.decode(errors='replace')
        print(text, end='')
        subscriptions = {line.removeprefix('Subscribed: ') for line in text.splitlines()
                         if line.startswith('Subscribed: ')}
        expected = SECURE_SUBSCRIPTIONS if secure else LEGACY_SUBSCRIPTIONS
        if process.returncode or not expected.issubset(subscriptions):
            print('libfuji failed or missed required subscriptions', file=sys.stderr)
            return 1
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adapter', required=True, help='virtual BlueZ controller, e.g. hci1')
    parser.add_argument('--executable', default='/tmp/libfuji-pak-test')
    parser.add_argument('--secure', action='store_true', help='automatically confirm pairing with the virtual camera')
    args = parser.parse_args()
    try:
        return asyncio.run(run(args.adapter, args.executable, args.secure))
    except Exception as error:
        print(f'libfuji test failed: {type(error).__name__}: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
