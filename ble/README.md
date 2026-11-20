# Experimental Fujifilm BLE peripheral

`fuji-ble.py` is a standalone Python/Bumble experiment. It does not affect the
normal vcam build and does not use BlueZ's D-Bus GATT server.

The current profile advertises a Fujifilm discovery UUID and manufacturer
data with a four-byte legacy token. It exposes the legacy token and client-name
write characteristics and a captured subset of the other GATT characteristics.
The legacy path sends the captured initial time
and geotag notifications after both subscriptions, and accepts their associated
writes. Without `--model`, the SSID is a placeholder and no AP is started. Several
characteristic values are provisional. Token validation, name length, notification timing, and
connection-state handling are provisional; this is not Bluetooth bonding or
meaningful security. The token is sent in the clear in advertisements.

GATT differences are based on the X-H1 and X-T4 firmware dumps and the X-T30
legacy and X-T5 secure captures. `--model fuji_x_h1` with legacy pairing defaults
to `legacy-minimal`; GFX100 II with legacy pairing defaults to `legacy-modern-gfx`;
other legacy models default to `legacy-extended`. Secure pairing defaults to
`secure-modern`. These defaults are provisional, not exact model databases. The secure
discovery service is present but its additional captured characteristics are
not implemented. The secure token and the writable network settings are
advertised with captured properties, but writes return ATT Request Not Supported
until their semantics are known; they do not modify the AP. `NOT8_CHR_UUID` and
`NOT10_CHR_UUID` are exposed only in the approximate GFX variants.
The minimal and classic profiles lack `NOT9_CHR_UUID`, which the current
libfuji handoff expects; handoff on those profiles has not been established.

Use `--gatt-profile NAME` to explicitly select an **approximate** profile and
its pairing mode. The names describe the implemented subset, not an exact
firmware database or a promise that every characteristic is present:

| Profile | Reference family / distinguishing implemented features |
|---|---|
| `legacy-minimal` | v1.0: no shutter or expanded Wi-Fi group |
| `legacy-classic` | v1.1: shutter and IND3; no expanded Wi-Fi group |
| `legacy-extended-early` | v2.0: expanded group with reduced notification properties |
| `legacy-extended` | v2.1: expanded Wi-Fi group |
| `legacy-modern` | v3.0: adds NOT6 |
| `legacy-modern-standby` | v3.1: adds NOT3 in its legacy service |
| `legacy-modern-gfx` | v3.1gfx: adds NOT8 and NOT10 |
| `secure-modern` | v4.0: secure pairing and secure NOT3 service |
| `secure-modern-new` | v4.1: adds a provisional pairing characteristic |
| `secure-modern-gfx` | v4.1gfx: adds NOT8 and NOT10 |

`--gatt-profile` takes precedence over the model's default pairing mode;
conflicting explicit `--pairing`/`--secure` options are rejected. Existing
`--pairing` and `--secure` options still select the default profile for that
mode. The implementation does not automatically infer a firmware version.
See `gatt-reference/README.md` for the source profiles and limitations.

Geotag writes are decoded as signed little-endian latitude/longitude in degrees
times 10 million, signed altitude, four reserved bytes, and a seven-byte time.
The most recent value is available as `profile.geotags[connection]` in memory
and is discarded on disconnect; it is not logged, persisted, or attached to
images. The altitude unit and timestamp timezone are not established. The
geotag interval characteristic accepts a little-endian number of seconds and
notifies its new value; while nonzero it requests updates from a subscribed
client at that interval. Zero disabling requests is provisional. No recurring
requests are scheduled until the client writes the interval.

Furble implements the request/write exchange, but has not been tested against
this emulator. The tested libfuji revision subscribes to geotag requests but
does not handle them or send geotags, and the experimental libpak patch does
not yet dispatch characteristic-change callbacks. Thus this does not make
libfuji geotagging work end to end.

An opt-in experimental secure profile uses real LE Secure Connections bonding
and authenticated encryption. It advertises a synthetic identity and uses
provisional status bytes; libfuji's secure connection has been tested over
BlueZ/VHCI, but not with furble or physical hardware. Bonds are held **only in memory**, and the advertising
identity changes on restart. Shutter capture is not implemented. The optional
Wi-Fi handoff below is experimental. See `xt30-handover-notes.md` for capture observations.

## Experimental Wi-Fi handoff

Build vcam and run BLE on its own dedicated HCI adapter, for example:

```sh
sudo /tmp/bumble-venv/bin/python ble/fuji-ble.py --transport hci-socket:1 --address F6:F7:F8:F9:FA:FB --model fuji_x_t5 --wifi-dev wlan1 --ssid TestRig --password testpass123 --gateway 192.168.99.1
```

Choose the Bluetooth and Wi-Fi interfaces carefully; neither should be an
adapter needed for another connection. The Wi-Fi backend requires hostapd,
dnsmasq and suitable privileges, and changes network configuration (see
`scripts/README.md`). `--vcam` selects a different executable; the default is
`./build/vcam`. Overrides are optional; an empty `--password ''` selects an open
network. The password is visible in process arguments. X-T5 defaults to a
synthetic `vcam12345` WPA2 password; other Fuji defaults remain open.

The program reads `--dump-camera-json` for the effective Wi-Fi identity and
default BLE pairing profile. X-T5 defaults to secure pairing; other Fuji models
currently default to legacy. These firmware-dependent choices are provisional;
`--pairing legacy` or `--pairing secure` overrides them. Secure pairing requires
interactive confirmation. JSON and BLE credentials must match the backend; do
not print JSON in public logs.
For an open network the password characteristic is omitted: the X-T30 capture
does not contain it, and libfuji treats its presence as a secured/hidden AP.

On `CHR_WIFI_TRIGGER` value `04 00`, BLE starts vcam with `--ble-handover` and
`--wait-timeout 0`, waits for its PTP/IP listener-ready signal, and then reports
ready through `NOT9_CHR_UUID` and indicates `CHR_IND1_UUID`. BLE terminates vcam
on normal shutdown. Handover mode rejects a `PTP_DPC_FUJI_ClientState_DF01`
other than `FUJI_MODE_REMOTE_IMG_VIEW_XAPP` and gates extension object info
until that mode is selected. The backend refuses to silently shift the gateway
subnet on collision. Abrupt process death may require the Wi-Fi backend's
`--cleanup` procedure. AP startup and Android handoff have not been verified on
hardware; listener readiness does not independently verify DHCP. The real
libfuji handoff has been tested through BlueZ/VHCI with simulated Wi-Fi and a
loopback vcam TCP listener, not an actual Wi-Fi association.

`ble/test-handover.py` checks child readiness, failure and cleanup without radio
hardware. Set `VCAM_TEST_BINARY` to a built vcam to also test JSON overrides.

## Hardware-free test

Requires Python 3.11+ and Bumble. The following pins the upstream revision
used for development:

```sh
python3 -m venv /tmp/bumble-venv
/tmp/bumble-venv/bin/pip install 'bumble @ git+https://github.com/google/bumble.git@e1a21f5'
/tmp/bumble-venv/bin/python ble/fuji-ble.py --self-test
/tmp/bumble-venv/bin/python ble/fuji-ble.py --self-test-secure
/tmp/bumble-venv/bin/python ble/test-gatt-profiles.py
```

The self-test uses two in-process Bumble controllers and a virtual link. It
checks the advertisement, connects, verifies bad token and name-before-token
rejection, completes token/name writes and all listed subscriptions, checks
the initial notifications and associated writes, disconnects, and reconnects.
It requires no Bluetooth adapter or system D-Bus. It tests a
Bumble client, not libfuji or furble.

The secure test uses two virtual Bumble devices and automatically confirms the
comparison **in the test only**. It verifies that unpaired status reads fail,
performs Secure Connections pairing and authenticated encryption, checks the
status acknowledgement, and reconnects using the in-memory bond.

## BlueZ test with a virtual controller

`vhci-bridge.py` creates a Bumble virtual controller attached to Linux
`/dev/vhci`, linked to a Bumble camera in the same process. It does not use
the physical Bluetooth controller. It requires access to `/dev/vhci`, a system
D-Bus, and a running BlueZ daemon. Install Bumble and Bleak in the same venv:

```sh
/tmp/bumble-venv/bin/pip install 'bleak>=2.1,<3'
/tmp/bumble-venv/bin/python ble/vhci-bridge.py
```

In another terminal, identify the **new virtual controller** by its address
`02:56:43:41:4D:01` using `bluetoothctl list`. Replace `hci1` below with its
actual index; don't power on or otherwise alter an unrelated adapter:

```sh
busctl --system set-property org.bluez /org/bluez/hci1 org.bluez.Adapter1 Powered b true
/tmp/bumble-venv/bin/python ble/bluez-test.py --adapter hci1 --address F6:F7:F8:F9:FA:FB
```

The virtual controller disappears when the bridge exits. If D-Bus external
authentication fails in a user namespace, Bleak supports setting
`BLEAK_DBUS_AUTH_UID` to the UID as seen by the system bus (for example,
`BLEAK_DBUS_AUTH_UID=1000`); don't assume the container UID is the host UID.
This test has passed discovery, token/name writes, subscriptions, and initial
notifications through a real BlueZ daemon, but it does not test a physical
radio or libfuji/libpak.

## Experimental libfuji/libpak test

`libpak-bluez-experimental.patch` is a **partial local experiment**, not a
general-purpose libpak BlueZ implementation. It was tested against libpak
`dbd2fff` with libfuji `5780d54`. It fixes build signatures and enough device
enumeration, connection, bonding, GATT reads/writes and subscriptions for libfuji's
legacy and secure connection calls. In particular, asynchronous characteristic-change
callbacks and event dispatch are still missing: time and geotag notifications
are not handled by libfuji during this test. Its error handling and general multi-device behavior need further
work before upstream use.

Build in a separate checkout, not over an existing modified libpak tree. This
requires a C compiler, CMake, Ninja, pkg-config, and the dbus-1, BlueZ and
libusb development packages:

```sh
git clone https://github.com/petabyt/libpak.git /tmp/libpak-vcam
git -C /tmp/libpak-vcam checkout dbd2fff
git -C /tmp/libpak-vcam apply "$PWD/ble/libpak-bluez-experimental.patch"
cmake -S /tmp/libpak-vcam -B /tmp/libpak-vcam-build -G Ninja -DLINUX=ON -DPAK_QUICKJS=OFF -DPAK_WASM=OFF -DPAK_INCLUDE_CLI=OFF
cmake --build /tmp/libpak-vcam-build
git clone https://github.com/petabyt/libfuji.git /tmp/libfuji-vcam
git -C /tmp/libfuji-vcam checkout 5780d54
git -C /tmp/libfuji-vcam -c url.https://github.com/.insteadOf=git@github.com: submodule update --init --recursive
cc -g -ffunction-sections -fdata-sections -I/tmp/libpak-vcam/include -I/tmp/libfuji-vcam/lib -I/tmp/libfuji-vcam/libpict/src -I/tmp/libfuji-vcam/fp/src $(pkg-config --cflags dbus-1) ble/libfuji-pak-test.c /tmp/libfuji-vcam/lib/bluetooth.c -Wl,--gc-sections -L/tmp/libpak-vcam-build -Wl,-rpath,/tmp/libpak-vcam-build -lpak $(pkg-config --libs dbus-1) -o /tmp/libfuji-pak-test
```

The test harness is deliberately restricted to the VHCI controller and camera
addresses printed by `vhci-bridge.py`. Start that bridge and power on its
virtual controller as above, then run the following with the correct virtual
adapter index. The runner keeps scanning while the test executes, because BlueZ
may discard a recently discovered device after scanning stops:

```sh
/tmp/bumble-venv/bin/python ble/run-libfuji-pak-test.py --adapter hci1
```

For secure mode start `vhci-bridge.py --secure` instead and add `--secure` to
the runner. This test registers a temporary BlueZ agent that **automatically
confirms** pairing for the test camera address; use it only in an isolated
virtual test environment, never with real devices. It does not register as the
system default agent. BlueZ may retain the virtual test bond after the bridge
exits, while the emulator loses its keys; remove only that virtual device's bond
if starting a fresh bridge prevents pairing.

It invokes the real libfuji `fuji_connect_bluetooth`, not a replacement protocol
client. The runner checks the six legacy or 11 captured secure subscription UUIDs as well
as the return code: libfuji currently ignores some subscription failures.
The test runtime stubs UI/session persistence, and this is not a reconnect or
full libpak frontend test.

To additionally exercise libfuji's real `fuji_bluetooth_connect_to_wifi` and
the PTP function-mode selection without changing network configuration, stop the
normal bridge and start a fresh one as follows (replace the vcam path):

```sh
/tmp/bumble-venv/bin/python ble/vhci-bridge.py --secure --handover --handover-vcam "$PWD/build/vcam"
# Power on the virtual controller as above, then in another terminal:
/tmp/bumble-venv/bin/python ble/run-libfuji-pak-test.py --adapter hci1 --secure --handover --ptp-loopback
```

The bridge uses synthetic test credentials and starts vcam on `127.0.0.1`, not
an AP. The harness verifies the credentials passed to its stub Wi-Fi callback;
the runner checks that the spawned PTP server rejects regular image-view mode
and accepts X-App mode. Use `--handover` without `--handover-vcam` and omit
`--ptp-loopback` to simulate the child as well. Automatic pairing confirmation
is restricted to this virtual test; do not use it with real devices. An old
test bond may have to be removed from the virtual adapter after bridge restart.

## Hardware test with a BlueZ client

This mode has **not been hardware tested**. Bumble is the Bluetooth host for
the peripheral: it cannot register this service with BlueZ or share its HCI
controller with BlueZ. Use a dedicated LE-capable peripheral controller and a
different BlueZ controller for the client, either on a second machine or a
separate adapter on the same machine. The emulator does not power down adapters
or stop system services itself.

On the peripheral machine, install the dependencies as above. Identify the
dedicated controller carefully with `bluetoothctl list` or `hciconfig -a`.
Replace `hci1` and the static random address below with your own choices. The
kernel HCI user channel requires the controller to be DOWN and may require
root or `CAP_NET_ADMIN`:

```sh
sudo hciconfig hci1 down
sudo /tmp/bumble-venv/bin/python ble/fuji-ble.py --transport hci-socket:1 --address F6:F7:F8:F9:FA:FB
```

Add `--secure` to try the experimental secure profile instead. It requires an
interactive terminal to approve numeric comparison or enter a passkey; never
approve a code that does not match the client. The existing `bluez-test.py`
only tests legacy pairing and cannot test this profile. Stop with Ctrl-C. Bond
keys are discarded on exit; the other device may need its test bond removed
manually before a new pairing. Do not remove unrelated bonds.

Bumble takes exclusive control and may reset the controller. BlueZ can race
to reclaim it: if startup reports device busy, arrange for BlueZ not to manage
that dedicated adapter before retrying. Do not stop the system Bluetooth daemon
if it is needed by your client or other devices. Ctrl-C releases the transport;
restore the peripheral adapter's power state manually if needed. Each run
generates a new token, so a saved pairing cannot survive a process restart.

On the client machine, use a **different**, powered-on BlueZ adapter and a
running `bluetoothd`. Install Python 3 and Bleak, then run (replace `hci0` and
the address with your client adapter and the emulator's address):

```sh
python3 -m venv /tmp/bluez-test-venv
/tmp/bluez-test-venv/bin/pip install 'bleak>=2.1,<3'
/tmp/bluez-test-venv/bin/python ble/bluez-test.py --adapter hci0 --address F6:F7:F8:F9:FA:FB
```

The test scans for the specified address and Fuji advertisement, extracts the
public token, connects, writes it and a test client name, checks subscriptions,
initial notifications and their associated writes, then disconnects.
The emulator logs acceptance without logging the token or client name. Avoid
using the address of a real camera: the test writes to the selected device.
This checks the preliminary exchange only, not pairing in the BlueZ bonding
sense, libfuji/furble compatibility, shutter or Wi-Fi handoff.
