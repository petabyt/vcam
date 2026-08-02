#!/usr/bin/env python3
"""Smoke test the vcam GFX100 II Fuji TCP image-import path."""

import argparse
import socket
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


PTP_PACKET_TYPE_COMMAND = 1
PTP_PACKET_TYPE_DATA = 2
PTP_PACKET_TYPE_RESPONSE = 3

PTP_RC_OK = 0x2001

PTP_OC_OPEN_SESSION = 0x1002
PTP_OC_GET_OBJECT_INFO = 0x1008
PTP_OC_GET_THUMB = 0x100A
PTP_OC_GET_PARTIAL_OBJECT = 0x101B
PTP_OC_GET_PROP = 0x1015
PTP_OC_SET_PROP = 0x1016

PTP_OC_FUJI_GET_IMPORT_FOLDERS = 0x9050
PTP_OC_FUJI_GET_IMPORT_DATES = 0x9053
PTP_OC_FUJI_GET_EXTENSION_OBJECT_INFO = 0x9054
PTP_OC_FUJI_GET_EXTENSION_THUMB = 0x9055

PTP_DPC_FUJI_CLIENT_STATE = 0xDF01
PTP_DPC_FUJI_COMPRESS_SMALL = 0xD226
PTP_DPC_FUJI_ENABLE_CORRECT_FILE_SIZE = 0xD227
PTP_DPC_FUJI_STORAGE_ID = 0xD244
PTP_DPC_FUJI_REMOTE_PHOTO_VIEW_EX_VERSION = 0xDF28
PTP_DPC_FUJI_IMAGE_IMPORT_OBJECT_COUNT = 0xD620
PTP_DPC_FUJI_IMAGE_IMPORT_OBJECT_HANDLES = 0xD621

PTP_OF_JPEG = 0x3801
PTP_OF_RAF = 0xB103
PTP_OF_MOV = 0x300D

GFX_STORAGE_SENTINEL = 0x10000001
DEFAULT_HOST = "192.168.5.228"
DEFAULT_PORT = 55740
DEFAULT_FIXTURE_DIR = Path(__file__).resolve().parents[1] / "DCIM" / "100_FUJI"
RAF_MAGIC = b"FUJIFILMCCD-RAW "
UINT32_MAX = 0xFFFFFFFF


class SmokeError(Exception):
    pass


@dataclass
class PtpResponse:
    code: int
    transaction: int
    params: list[int]


@dataclass
class ObjectInfo:
    handle: int
    name: str
    fmt: int
    size: int
    thumb_format: int
    thumb_size: int
    image_width: int
    image_height: int


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def ptp_string(data: bytes, offset: int) -> str:
    if offset >= len(data):
        return ""
    length = data[offset]
    chars = []
    for i in range(max(0, length - 1)):
        pos = offset + 1 + i * 2
        if pos + 2 > len(data):
            break
        ch = u16(data, pos)
        if ch:
            chars.append(chr(ch))
    return "".join(chars)


def is_jpeg(data: bytes) -> bool:
    return len(data) >= 4 and data[:2] == b"\xff\xd8" and data[-2:] == b"\xff\xd9"


def is_real_raf(path: Path) -> bool:
    if path.suffix.lower() != ".raf":
        return False
    try:
        with path.open("rb") as file:
            return file.read(len(RAF_MAGIC)) == RAF_MAGIC
    except OSError:
        return False


def is_large_mov(path: Path) -> bool:
    if path.suffix.lower() != ".mov":
        return False
    try:
        return path.stat().st_size > UINT32_MAX
    except OSError:
        return False


def select_fixtures(fixture_dir: Path, expect_raf: str | None) -> tuple[str | None, str | None, list[str]]:
    try:
        entries = [path for path in fixture_dir.iterdir() if path.is_file()]
    except OSError:
        entries = []

    raf_name = None
    if expect_raf:
        expected = fixture_dir / expect_raf
        if is_real_raf(expected):
            raf_name = expected.name
    else:
        rafs = sorted(path for path in entries if is_real_raf(path))
        if rafs:
            raf_name = rafs[0].name

    large_movs = sorted(path for path in entries if is_large_mov(path))
    mov_name = large_movs[0].name if large_movs else None

    missing = []
    if raf_name is None:
        missing.append("I need a real RAF file")
    if mov_name is None:
        missing.append("I need a > 4GB mov")

    return raf_name, mov_name, missing


def parse_object_info(handle: int, payload: bytes) -> ObjectInfo:
    if len(payload) < 53:
        raise SmokeError(f"GetObjectInfo(0x{handle:x}) returned short payload: {len(payload)} bytes")
    return ObjectInfo(
        handle=handle,
        name=ptp_string(payload, 52),
        fmt=u16(payload, 4),
        size=u32(payload, 8),
        thumb_format=u16(payload, 12),
        thumb_size=u32(payload, 14),
        image_width=u32(payload, 26),
        image_height=u32(payload, 30),
    )


class FujiPtpClient:
    def __init__(self, host: str, port: int, timeout: float):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: socket.socket | None = None
        self.transaction = 1

    def connect(self) -> None:
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.sock.settimeout(self.timeout)
        init = bytearray(0x44)
        struct.pack_into("<IIIIIII", init, 0, 0x44, 1, 0, 1, 2, 3, 4)
        name = "vcam-smoke".encode("utf-16le") + b"\x00\x00"
        init[28:28 + min(len(name), 54)] = name[:54]
        self.sock.sendall(init)
        ack = self._read_packet()
        if len(ack) < 8 or u32(ack, 4) != 2:
            raise SmokeError(f"unexpected Fuji init ACK: {ack[:16].hex()}")

    def close(self) -> None:
        if not self.sock:
            return
        try:
            self.sock.sendall(struct.pack("<II", 8, 0xFFFFFFFF))
        except OSError:
            pass
        self.sock.close()
        self.sock = None

    def command(self, code: int, params: tuple[int, ...] = (), data: bytes | None = None) -> tuple[bytes, PtpResponse]:
        tx = self.transaction
        self.transaction += 1
        self._send_container(PTP_PACKET_TYPE_COMMAND, code, tx, params=params)
        if data is not None:
            self._send_container(PTP_PACKET_TYPE_DATA, code, tx, payload=data)

        first = self._parse_container(self._read_packet())
        payload = b""
        if first[0] == PTP_PACKET_TYPE_DATA:
            payload = first[3]
            response = self._parse_container(self._read_packet())
        else:
            response = first

        resp_type, resp_code, resp_tx, resp_payload = response
        if resp_type != PTP_PACKET_TYPE_RESPONSE:
            raise SmokeError(f"opcode 0x{code:04x} returned packet type {resp_type}, expected response")
        if resp_tx != tx:
            raise SmokeError(f"opcode 0x{code:04x} response transaction {resp_tx}, expected {tx}")
        params_out = [value[0] for value in struct.iter_unpack("<I", resp_payload)]
        if resp_code != PTP_RC_OK:
            raise SmokeError(f"opcode 0x{code:04x} failed with response 0x{resp_code:04x}")
        return payload, PtpResponse(resp_code, resp_tx, params_out)

    def open_session(self) -> None:
        self.command(PTP_OC_OPEN_SESSION, (1,))

    def get_prop(self, prop: int) -> bytes:
        payload, _ = self.command(PTP_OC_GET_PROP, (prop,))
        return payload

    def get_prop_u32(self, prop: int) -> int:
        payload = self.get_prop(prop)
        if len(payload) < 4:
            raise SmokeError(f"property 0x{prop:04x} returned {len(payload)} bytes, expected uint32")
        return u32(payload, 0)

    def set_prop_u16(self, prop: int, value: int) -> None:
        self.command(PTP_OC_SET_PROP, (prop,), struct.pack("<H", value))

    def set_prop_u32(self, prop: int, value: int) -> None:
        self.command(PTP_OC_SET_PROP, (prop,), struct.pack("<I", value))

    def object_info(self, handle: int) -> ObjectInfo:
        payload, _ = self.command(PTP_OC_GET_OBJECT_INFO, (handle,))
        return parse_object_info(handle, payload)

    def thumb(self, handle: int) -> bytes:
        payload, _ = self.command(PTP_OC_GET_THUMB, (handle,))
        return payload

    def partial_object(self, handle: int, offset: int, size: int) -> tuple[bytes, list[int]]:
        payload, response = self.command(PTP_OC_GET_PARTIAL_OBJECT, (handle, offset, size))
        return payload, response.params

    def _send_container(
        self,
        packet_type: int,
        code: int,
        transaction: int,
        params: tuple[int, ...] = (),
        payload: bytes = b"",
    ) -> None:
        if not self.sock:
            raise SmokeError("not connected")
        if payload and params:
            raise SmokeError("internal error: container cannot have both params and payload")
        if payload:
            packet = struct.pack("<IHHI", 12 + len(payload), packet_type, code, transaction) + payload
        else:
            packet = struct.pack("<IHHI", 12 + 4 * len(params), packet_type, code, transaction)
            packet += b"".join(struct.pack("<I", param) for param in params)
        self.sock.sendall(packet)

    def _read_packet(self) -> bytes:
        header = self._recv_exact(4)
        length = u32(header, 0)
        if length < 8 or length > 128 * 1024 * 1024:
            raise SmokeError(f"invalid packet length {length}")
        return header + self._recv_exact(length - 4)

    def _recv_exact(self, size: int) -> bytes:
        if not self.sock:
            raise SmokeError("not connected")
        chunks = bytearray()
        while len(chunks) < size:
            chunk = self.sock.recv(size - len(chunks))
            if not chunk:
                raise SmokeError("socket closed while reading")
            chunks.extend(chunk)
        return bytes(chunks)

    @staticmethod
    def _parse_container(packet: bytes) -> tuple[int, int, int, bytes]:
        if len(packet) < 12:
            raise SmokeError(f"short PTP container: {len(packet)} bytes")
        return u16(packet, 4), u16(packet, 6), u32(packet, 8), packet[12:]


def run_import_prelude(client: FujiPtpClient) -> list[int]:
    client.open_session()
    client.set_prop_u16(PTP_DPC_FUJI_CLIENT_STATE, 20)
    remote_photo_version = client.get_prop_u32(PTP_DPC_FUJI_REMOTE_PHOTO_VIEW_EX_VERSION)
    if remote_photo_version != 3:
        raise SmokeError(f"DF28 version {remote_photo_version}, expected 3")
    client.set_prop_u32(PTP_DPC_FUJI_REMOTE_PHOTO_VIEW_EX_VERSION, 3)
    client.set_prop_u16(PTP_DPC_FUJI_COMPRESS_SMALL, 0)
    client.set_prop_u16(PTP_DPC_FUJI_ENABLE_CORRECT_FILE_SIZE, 0)
    client.get_prop(PTP_DPC_FUJI_STORAGE_ID)

    current_info_payload, _ = client.command(PTP_OC_FUJI_GET_EXTENSION_OBJECT_INFO, (GFX_STORAGE_SENTINEL,))
    current = parse_object_info(GFX_STORAGE_SENTINEL, current_info_payload)
    if current.thumb_size == 0:
        raise SmokeError(f"current-object prelude selected {current.name or '<unnamed>'} without thumbnail")
    current_thumb, _ = client.command(PTP_OC_FUJI_GET_EXTENSION_THUMB, (GFX_STORAGE_SENTINEL,))
    if not is_jpeg(current_thumb):
        raise SmokeError("0x9055 current-object thumbnail is not a complete JPEG")

    client.command(PTP_OC_FUJI_GET_IMPORT_FOLDERS)
    client.command(PTP_OC_FUJI_GET_IMPORT_DATES, (0, 0x7530))

    count_payload = client.get_prop(PTP_DPC_FUJI_IMAGE_IMPORT_OBJECT_COUNT)
    if len(count_payload) < 4:
        raise SmokeError("D620 returned a short object-count payload")
    expected_count = u32(count_payload, 0)

    handles_payload = client.get_prop(PTP_DPC_FUJI_IMAGE_IMPORT_OBJECT_HANDLES)
    if len(handles_payload) < 4:
        raise SmokeError("D621 returned a short handle-list payload")
    handle_count = u32(handles_payload, 0)
    handles = [value[0] for value in struct.iter_unpack("<I", handles_payload[4:])]
    if expected_count != handle_count or handle_count != len(handles):
        raise SmokeError(f"D620/D621 mismatch: count={expected_count}, list_count={handle_count}, handles={len(handles)}")
    return handles


def find_object(client: FujiPtpClient, handles: list[int], name: str) -> ObjectInfo:
    for handle in handles:
        info = client.object_info(handle)
        if info.name == name:
            return info
    listed = ", ".join(client.object_info(handle).name or f"0x{handle:x}" for handle in handles)
    raise SmokeError(f"did not find expected object {name!r}; listed: {listed}")


def smoke(args: argparse.Namespace) -> None:
    client = FujiPtpClient(args.host, args.port, args.timeout)
    client.connect()
    try:
        handles = run_import_prelude(client)
        print(f"OK import prelude: {len(handles)} downloadable handles")

        raf = find_object(client, handles, args.expect_raf)
        if raf.fmt != PTP_OF_RAF:
            raise SmokeError(f"{raf.name} format 0x{raf.fmt:04x}, expected RAF 0x{PTP_OF_RAF:04x}")
        if raf.thumb_size == 0:
            raise SmokeError(f"{raf.name} reports no thumbnail")
        print(f"OK RAF info: handle=0x{raf.handle:x} thumb={raf.thumb_size} dims={raf.image_width}x{raf.image_height}")

        thumb = client.thumb(raf.handle)
        if len(thumb) != raf.thumb_size:
            raise SmokeError(f"RAF thumbnail length {len(thumb)}, ObjectInfo said {raf.thumb_size}")
        if not is_jpeg(thumb):
            raise SmokeError("RAF thumbnail is not a complete JPEG")
        print(f"OK RAF thumb: {len(thumb)} JPEG bytes")

        prefix, response_params = client.partial_object(raf.handle, 0, len(RAF_MAGIC))
        if prefix != RAF_MAGIC:
            raise SmokeError(f"RAF partial prefix {prefix.hex()}, expected {RAF_MAGIC.hex()}")
        if response_params != [len(RAF_MAGIC)]:
            raise SmokeError(f"GetPartialObject OK params {response_params}, expected [{len(RAF_MAGIC)}]")
        print("OK RAF partial: full-object path returns RAF bytes")

        mov = find_object(client, handles, args.expect_mov)
        if mov.fmt != PTP_OF_MOV:
            raise SmokeError(f"{mov.name} format 0x{mov.fmt:04x}, expected MOV 0x{PTP_OF_MOV:04x}")
        if mov.size != UINT32_MAX:
            raise SmokeError(f"{mov.name} MOV size 0x{mov.size:08x}, expected saturated 0xffffffff")
        print(f"OK MOV info: {mov.name} size saturated to 0xffffffff")
    finally:
        client.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Smoke test vcam's GFX100 II Fuji TCP import path.")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"vcam host/IP, default {DEFAULT_HOST}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"vcam Fuji command port, default {DEFAULT_PORT}")
    parser.add_argument("--timeout", type=float, default=5.0, help="socket timeout in seconds")
    parser.add_argument("--fixture-dir", type=Path, default=DEFAULT_FIXTURE_DIR, help=f"media fixture directory, default {DEFAULT_FIXTURE_DIR}")
    parser.add_argument("--expect-raf", help="specific RAF fixture name; defaults to the first real RAF in fixture-dir")
    args = parser.parse_args()

    args.expect_raf, args.expect_mov, missing = select_fixtures(args.fixture_dir, args.expect_raf)
    if missing:
        for message in missing:
            print(message, file=sys.stderr)
        return 1

    try:
        smoke(args)
    except (OSError, SmokeError, struct.error) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
