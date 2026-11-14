# WiFi backend (src/wifi.c)

Emulates a camera's WiFi interface natively: vcam brings up the camera AP,
waits for a phone to associate and get a DHCP lease, then serves PTP/IP on
the AP address - the same flows as the tcp backend, plus a deterministic
AP lifecycle.

```
sudo apt install hostapd dnsmasq
```

## Usage

```
sudo build/vcam canon_1300d wifi          # Canon preset
sudo build/vcam fuji_x_h1 wifi            # Fuji preset
build/vcam canon_1300d wifi --check       # print the plan, touch nothing
build/vcam fuji_x_h1 wifi --cleanup       # revert leftovers of a SIGKILLed run
```

Vendor presets are hardcoded per model in the vendor init functions
(`cam->ap_ssid` / `cam->ap_password` / `cam->ap_gateway` in
`fuji_init_cam` / `canon_init_cam`) and can be overridden from the command
line with `--ssid`, `--password`, `--gateway` (empty password = open):

| vendor | SSID                  | security     | gateway      |
|--------|-----------------------|--------------|--------------|
| canon  | `EOST6{-464_Canon0A`  | WPA2-PSK CCMP `12345678` | 192.168.1.2 |
| fuji   | `FUJIFILM-<MODEL>-<ID>` | open       | 192.168.0.1  |

Fuji SSIDs are explicit per model (`fuji_x_h1` -> `FUJIFILM-XH1-ABCD`,
`fuji_x30` -> `FUJIFILM-X30-ABCD`, ...). The DHCP range is derived from the
gateway (`x.10 - x.100`).

## Flags

```
--wifi-dev <if>       AP interface (default: auto-pick, see below)
--channel <n>         2.4 GHz channel (default 6, fixed - no ACS)
--ssid <s>            SSID override
--password <p>        WPA2 passphrase; empty string = open network
--gateway <ip>        AP gateway IP override
--reg-domain <cc>     Set the kernel regulatory domain (GLOBAL to all radios!)
--wait-timeout <s>    Seconds to wait for association+lease (default 120, 0 = skip)
--check               Dry run: print the plan, change nothing
--cleanup             Revert leftovers of a previous crashed run
```

All other flags pass through to the vendor backend (`--fs`, `--discovery`,
`--register`, `--dump`, ...).

## How it does not screw up your main networking

- The interface carrying the default route is never touched; neither is any
  interface with an active station connection (up + carrier + operstate UP).
  ath* drivers are preferred for AP duty (Atheros AP support is complete;
  iwlwifi/AX210 AP mode is restricted).
- No `ip addr flush`, ever: exactly one address (the gateway) is added and
  removed again on teardown.
- No iptables, no NAT, no forwarding - the emulated camera network has no
  internet, like a real camera. dnsmasq binds only the AP interface; when
  the host already owns port 53 (AdGuard/unbound/system dnsmasq, detected
  via a bind probe on the gateway IP) DNS is disabled entirely
  (`port=0`) and only DHCP runs - the phone still sees "no internet",
  exactly like on a real camera AP.
- NetworkManager is not restarted: a one-interface drop-in
  (`/etc/NetworkManager/conf.d/90-vcam-wifi.conf`,
  `unmanaged-devices=interface-name:<if>`) plus a SIGHUP reload, removed on
  teardown.
- hostapd and dnsmasq are spawned as children with PR_SET_PDEATHSIG(SIGKILL):
  they cannot outlive vcam, whatever happens to it. Daemons left behind by
  older crashes are reaped at startup.
- A fixed channel is used (no ACS), so bring-up is deterministic; if the
  regdomain is `00` a warning suggests `--reg-domain`.
- The planned subnet is checked against all existing interface addresses and
  shifts to 192.168.77.0/24 on collision.

## State machine

```
reap stale -> NM unmanaged -> iface up + gateway addr -> hostapd (wait
AP-ENABLED) -> dnsmasq -> wait association -> wait DHCP lease -> PTP/IP
```

Every stage logs; every failure exits nonzero with a specific reason and the
relevant daemon log tail (e.g. channel/regdomain errors from hostapd.log).
Interrupting vcam (Ctrl-C) tears everything down; the kernel detaches the
device when the vhci-style socket closes.
# libfuji TCP regression

`regression-libfuji.c` connects to a running vcam, finds a named JPEG fixture,
fetches its thumbnail, and compares the libfuji download byte-for-byte with
the local file. It requires a separate libfuji checkout with its `fp` and
`libpict` dependencies initialized. For example, with that checkout at
`./libfuji`:

```sh
cmake -S libfuji -B /tmp/opencode/libfuji-build
cmake --build /tmp/opencode/libfuji-build
cc -Wall -Wextra -Ilibfuji/lib -Ilibfuji/libpict/src -Ilibfuji/fp/src \
  scripts/regression-libfuji.c /tmp/opencode/libfuji-build/libfuji.a \
  /tmp/opencode/libfuji-build/libpict/libpict.a /tmp/opencode/libfuji-build/fp/libfp.a \
  $(pkg-config --libs libusb-1.0) -o /tmp/opencode/regression-libfuji
mkdir -p /tmp/opencode/libfuji-card
cp bin/fuji/jpeg-compressed.jpg /tmp/opencode/libfuji-card/test.jpg
```

Start `./build/vcam fuji_x_t5 tcp --ip 127.0.0.1 --fs /tmp/opencode/libfuji-card`
in another terminal. Once it is listening, run:

```sh
timeout 30 /tmp/opencode/regression-libfuji 127.0.0.1 /tmp/opencode/libfuji-card/test.jpg
```

Stop vcam afterwards. Use a card directory containing only this fixture:
libfuji's Wi-Fi handle helper assumes sequential IDs rather than enumerating
the actual filesystem. This test does not cover modern gallery filters or
card switching.
