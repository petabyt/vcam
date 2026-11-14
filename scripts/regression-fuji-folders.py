#!/usr/bin/env python3
"""Exercise Fuji image-import folders without large external fixtures."""
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time

spec = importlib.util.spec_from_file_location("gfx_regression", Path(__file__).with_name("regression-gfx100ii.py"))
gfx = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = gfx
spec.loader.exec_module(gfx)


def string(value):
    return bytes([len(value) + 1]) + value.encode("utf-16le") + b"\0\0"


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/vcam"
    binary = str(Path(binary).resolve())
    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="vcam-folders-") as tmp:
        card = Path(tmp)
        for folder, names in (("203_FUJI", ("a.jpg", "b.jpg")), ("204_FUJI", ("c.jpg",))):
            target = card / "DCIM" / folder
            target.mkdir(parents=True)
            for name in names:
                shutil.copyfile(root / "bin/fuji/jpeg-compressed.jpg", target / name)
        server = subprocess.Popen([binary, "fuji_gfx100ii", "tcp", "--ip", "127.0.0.1", "--fs", str(card)], cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        client = gfx.FujiPtpClient("127.0.0.1", 55740, 3)
        try:
            for attempt in range(30):
                try:
                    client.connect()
                    break
                except OSError:
                    if server.poll() is not None or attempt == 29:
                        raise
                    time.sleep(0.1)
            handles = gfx.run_import_prelude(client)
            assert len(handles) == 3, handles
            folders, _ = client.command(0x9050)
            assert struct.unpack_from("<I", folders)[0] == 5
            pos = 4
            records = {}
            for _ in range(5):
                length = struct.unpack_from("<I", folders, pos)[0]
                assert length >= 6 and pos + length <= len(folders)
                records[struct.unpack_from("<H", folders, pos + 4)[0]] = folders[pos:pos + length]
                pos += length
            assert pos == len(folders)
            assert struct.unpack_from("<H", records[0xd602], 16)[0] == 2
            assert string("203_FUJI") in records[0xd602] and string("204_FUJI") in records[0xd602]
            dates, _ = client.command(0x9053, (0, 30000))
            assert struct.unpack_from("<I", dates, 4)[0] == 27
            name = string("203_FUJI")
            client.command(0x9051, data=struct.pack("<IIH", 1, 6 + len(name), 0xd602) + name)
            client.command(0x9053, (0, 30000))
            assert client.get_prop_u32(0xd620) == 2
            payload = client.get_prop(0xd621)
            assert struct.unpack_from("<I", payload)[0] == 2
            selected = set(struct.unpack_from("<II", payload, 4))
            excluded = next(handle for handle in handles if handle not in selected)
            assert client.object_info(excluded).name == "c.jpg"
            client.command(0x9051, data=struct.pack("<I", 0))
            assert client.get_prop_u32(0xd620) == 3
            # Directory IDs create gaps between the actual file handles.
            missing = next(i for i in range(min(handles), max(handles)) if i not in handles)
            for handle in (missing, 0xdeadbeef):
                for opcode, params in ((0x1008, (handle,)), (0x100a, (handle,)), (0x101b, (handle, 0, 8)), (0x9056, (handle, 0, 8))):
                    tx = client.transaction
                    client.transaction += 1
                    client._send_container(1, opcode, tx, params=params)
                    typ, code, response_tx, _ = client._parse_container(client._read_packet())
                    assert (typ, code, response_tx) == (3, 0x2019, tx), (opcode, handle, code)
            print("OK Fuji folder enumeration, selection, reset and invalid handle")
        finally:
            client.close()
            server.terminate()
            server.wait(timeout=5)

        with tempfile.TemporaryFile(mode="w+") as log:
            server = subprocess.Popen([binary, "fuji_gfx100ii", "tcp", "--ip", "127.0.0.1", "--fs", str(card), "--strict"], cwd=root, stdout=log, stderr=log)
            client = gfx.FujiPtpClient("127.0.0.1", 55740, 3)
            try:
                for attempt in range(30):
                    try:
                        client.connect()
                        break
                    except OSError:
                        if server.poll() is not None or attempt == 29:
                            raise
                        time.sleep(0.1)
                gfx.run_import_prelude(client)
                tx = client.transaction
                client._send_container(1, 0x1008, tx, params=(0xdeadbeef,))
                assert server.wait(timeout=5) != 0
                log.seek(0)
                output = log.read()
                assert "opcode 0x1008 requested invalid object ID 0xdeadbeef" in output
                print("OK strict mode exits on invalid object ID")
            finally:
                client.close()
                if server.poll() is None:
                    server.terminate()
                    server.wait(timeout=5)

        server = subprocess.Popen([binary, "fuji_x_t5", "tcp", "--ip", "127.0.0.1", "--fs", str(card)], cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        client = gfx.FujiPtpClient("127.0.0.1", 55740, 3)
        try:
            for attempt in range(30):
                try:
                    client.connect()
                    break
                except OSError:
                    if server.poll() is not None or attempt == 29:
                        raise
                    time.sleep(0.1)
            client.open_session()
            client.set_prop_u16(gfx.PTP_DPC_FUJI_COMPRESS_SMALL, 0)
            client.set_prop_u16(gfx.PTP_DPC_FUJI_ENABLE_CORRECT_FILE_SIZE, 0)
            client.get_prop(gfx.PTP_DPC_FUJI_STORAGE_ID)
            info, _ = client.command(0x9054, (gfx.GFX_STORAGE_SENTINEL,))
            assert gfx.parse_object_info(gfx.GFX_STORAGE_SENTINEL, info).name in ("a.jpg", "b.jpg", "c.jpg")
            thumb, _ = client.command(0x9055, (gfx.GFX_STORAGE_SENTINEL,))
            assert gfx.is_jpeg(thumb)
            folders, _ = client.command(0x9050)
            assert string("203_FUJI") in folders and string("204_FUJI") in folders
            client.command(0x9053, (0, 30000))
            assert client.get_prop_u32(0xd620) == 3
            name = string("203_FUJI")
            client.command(0x9051, data=struct.pack("<IIH", 1, 6 + len(name), 0xd602) + name)
            assert client.get_prop_u32(0xd620) == 2
            payload = client.get_prop(0xd621)
            assert struct.unpack_from("<I", payload)[0] == 2
            tx = client.transaction
            client.transaction += 1
            client._send_container(1, 0x1008, tx, params=(0xdeadbeef,))
            assert client._parse_container(client._read_packet())[:3] == (3, 0x2019, tx)
            print("OK X-T5 preview, filters and invalid object response")

            client.command(gfx.PTP_OC_SET_PROP, (gfx.PTP_DPC_FUJI_STORAGE_ID,), b"\x02")
            client.set_prop_u16(gfx.PTP_DPC_FUJI_COMPRESS_SMALL, 0)
            client.set_prop_u16(gfx.PTP_DPC_FUJI_ENABLE_CORRECT_FILE_SIZE, 0)
            assert client.get_prop(gfx.PTP_DPC_FUJI_STORAGE_ID) == b"\x02"
            assert client.get_prop_u32(0xd620) == 0
            assert client.get_prop(0xd621) == b"\0\0\0\0"
            for _ in range(2):
                tx = client.transaction
                client.transaction += 1
                client._send_container(1, 0x9054, tx, params=(gfx.GFX_STORAGE_SENTINEL,))
                assert client._parse_container(client._read_packet()) == (2, 0x9054, tx, b"")
                assert client._parse_container(client._read_packet()) == (3, 0x2013, tx, b"")

            client.command(gfx.PTP_OC_SET_PROP, (gfx.PTP_DPC_FUJI_STORAGE_ID,), b"\x01")
            client.set_prop_u16(gfx.PTP_DPC_FUJI_COMPRESS_SMALL, 0)
            client.set_prop_u16(gfx.PTP_DPC_FUJI_ENABLE_CORRECT_FILE_SIZE, 0)
            assert client.get_prop(gfx.PTP_DPC_FUJI_STORAGE_ID) == b"\x01"
            info, _ = client.command(0x9054, (gfx.GFX_STORAGE_SENTINEL,))
            assert gfx.parse_object_info(gfx.GFX_STORAGE_SENTINEL, info).name
            thumb, _ = client.command(0x9055, (gfx.GFX_STORAGE_SENTINEL,))
            assert gfx.is_jpeg(thumb)
            client.command(0x9050)
            client.command(0x9053, (0, 30000))
            assert client.get_prop_u32(0xd620) == 2  # Folder filter is preserved.
            print("OK X-T5 card switch and preview recovery")
        finally:
            client.close()
            server.terminate()
            server.wait(timeout=5)


if __name__ == "__main__":
    main()
