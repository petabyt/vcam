# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project overview

**vcam** is a PTP (Picture Transfer Protocol) responder written in C that emulates digital
cameras in software. It reimplements the proprietary vendor extensions found in real camera
firmware — filesystem access, remote capture, event/property handling — and replicates the
USB and Wi-Fi stacks well enough to spoof official client software (Fujifilm Camera Connect,
Canon EOS Connect, Windows 11 MTP).

Purpose:

- Regression testing of camera client software in CI, without physical hardware
- Easier prototyping (no waiting for a camera to recharge/reconnect)
- Black-box testing against vendor software without disassembling it
- A readable reference implementation of proprietary vendor functionality

Started as a fork of gphoto's
[vcamera](https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb)
regression tester (originally by Marcus Meissner); since heavily modified.
Licensed under **LGPL v2.1** — keep license headers intact where present.

## Build

Requires: CMake ≥ 3.14, Ninja (recommended), pkg-config, **libusb-1.0** and **libexif**
development packages.

```sh
sudo apt install libusb-1.0-0-dev libexif-dev   # Ubuntu/Debian
cmake -G Ninja -B build && cmake --build build
```

Or simply `make` (wraps the cmake calls). `make clean` removes `build/`.

Build targets (see `CMakeLists.txt`):

- `vcam` — main CLI executable (`build/vcam`)
- `usb-vcam` — shared library: a drop-in libusb-1.0 replacement for client-side testing

Compiler flags are strict: `-Wall -Wshadow -Wcast-qual -Wpedantic
-Werror=incompatible-pointer-types -Wstrict-aliasing=3`. Fix warnings; do not add
`#pragma` or per-file suppressions without reason.

## Running

```
vcam <model> <backend> [flags]
```

- Models: `canon_1300d`, `fuji_x_t4`, `fuji_x_h1`, `fuji_x30`, `fuji_gfx100ii` (aliases
  `gfx100ii`, `gfx`) — resolved in `src/canon/canon.c` and `src/fuji/fuji.c`
- Backends:
  - `tcp` — PTP/IP over a TCP socket (no root needed)
  - `vhci` — exposes the device on the kernel's virtual USB host controller (needs root:
    `sudo modprobe vhci-hcd && sudo ./vcam canon_1300d vhci`)
  - `otg` — gadgetfs, to expose the device on a physical USB OTG port
- Flags: `--local-ip` (bind to this machine's IP), `--fs <path>` (scan a folder as the
  PTP "SD card"), `--sig <pid>` (signal a parent process once PTP/IP is listening),
  `--dump` (write raw comm bytes to a file named `COMM_DUMP`), `--ssdp-modern`
  (canon models: NOTIFY alive messages carry the Canon-style `Server:` header
  instead of the default gssdp-style one)

The gfx100ii model auto-populates image-import fixtures under `/tmp/vcam-gfx100ii`
(see `src/fuji/fuji.c`).

### Wi-Fi / access point emulation

`make ap-fuji` / `make ap-canon` (with `model=fuji_x_h1` etc.) spin up a dummy network
interface and run `scripts/create_ap` to broadcast camera SSIDs. Requires `hostapd`,
`dnsmasq`, `iptables`, `iproute2`, `iw` and root; `make reset` removes the dummy
interfaces. See `Makefile` and `scripts/README.md`.

## Testing

There is no formal unit-test suite. Useful checks:

- `scripts/smoke-gfx100ii.py` — Python smoke test that drives the Fuji TCP
  image-import path over a raw PTP/IP socket
- Running vcam with `--dump` and inspecting the `COMM_DUMP` byte dump
- Building with the strict warning set enabled (it is by default) and fixing regressions

## Architecture

```
src/            Core, device-independent code
  vcamera.c     vcam lifecycle, opcode/property dispatch tables
  ops.c         Standard PTP opcode handlers (session, object transfer, ...)
  props.c       Standard PTP device property handlers
  packet.c      PTP packet framing (USB bulk and PTP/IP)
  ptpip.c       PTP/IP protocol layer
  socket.c      TCP socket helpers (incl. IPv6)
  pack.c / data.c / cl_data.h   Serialization & PTP constant tables
  main.c        CLI entry point (model + backend + flags)
  usbthing.c    Bridge between the vcam core and the src/usb/ USB backends
  ext/          Optional extras (FUSE filesystem view)
  notes/        Reverse-engineering notes (do not "clean up")
src/canon/      Canon EOS implementation (canon.c, props.c, ssdp.c)
src/fuji/       Fujifilm implementation: PTP ops (fuji.c), TCP server (server.c),
                SSDP discovery (ssdp.c), USB handling (usb.c), FS helpers (fs.c)
src/usb/        USB device emulation backends (usbthing): device.c, usbstring.c,
                vhci.c (kernel virtual HCD), libusb.c (fake libusb-1.0 .so),
                otg.c (gadgetfs, not compiled), dummy.c (manual demo)
scripts/        create_ap Wi-Fi helpers, wlanstart.sh, smoke test, misc tools
upnp/           Standalone SSDP/UPnP experiment; upnp/gssdp-vcam is a vendored,
                patched libgssdp fork built with meson — NOT built by the root CMake
bin/            Test fixtures (sample FS trees) and screenshots
```

### Request flow

1. A transport (TCP socket or usbthing backend) delivers bytes to the packet layer.
2. `packet.c` reassembles PTP containers into `struct ptpcontainer`.
3. `vcamera.c` dispatches to the registered opcode handler (`ops.c` for standard PTP,
   `src/canon/` or `src/fuji/` for vendor extensions) and property handler.
4. Handlers reply via `ptp_response()` / data-phase helpers and may queue USB
   interrupt events.

Device-specific state lives in `cam->priv` (vendor code) and `cam->hw_priv`
(transport); the shared `struct vcam` (see `src/vcam.h`) holds everything else.

## Code style

Formatting is defined by `.clang-format`:

- Indentation: **tabs, width 8**; `ColumnLimit: 0`; braces attached
  (`BreakBeforeBraces: Attach`); no single-line functions

Conventions used throughout the codebase:

- C11, no external frameworks; plain structs and `snake_case` functions
- Use `vcam_log()` / `vcam_log_func()` for logging — do **not** append `\n` (the
  helpers add it; several old call sites with `\n` are being cleaned up — see
  `ops.diff`)
- Use `vcam_panic()` for unrecoverable errors
- Keep handlers consistent with the existing pattern in `ops.c`:
  validate session (`vcam_check_session`), validate params
  (`vcam_check_param_count`), then act, then respond
- Prefer fixed-width types (`uint32_t`) for protocol fields; PTP is little-endian
- File-scope statics are fine; avoid globals that break running two camera instances

## Gotchas

- `upnp/gssdp-vcam` is a vendored third-party source
  (libgssdp). Never reformat or "modernize" it; it is built separately with meson.
- `src/notes/` contains raw reverse-engineering observations (e.g. Windows 11
  transfer quirks) — treat as documentation, not dead code.
- `--dump` writes the dump to a file literally named `COMM_DUMP` in the CWD.
- Some opcodes are intentionally "dummy" stubs (e.g. Canon) to keep real client
  software happy — match that behavior rather than returning errors.
