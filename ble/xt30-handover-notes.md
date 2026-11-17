# X-T30 BLE handover observations

Sources: `private/dump/fuji/xt30-wifi-handover.log` (BTSnoop HCI capture),
upstream libfuji `5780d54`, and furble `0cac22a`. Handles and values below
describe this capture, not a guarantee for other cameras or firmware. Device
identifiers, token, client name and Wi-Fi credentials are intentionally omitted.

The capture includes GATT discovery and a legacy token exchange. It is not a
complete specification of initial pairing, advertisement rotation, failed
authentication, disconnect/reconnect or security. ATT handles below are
camera-side handles observed in the capture; BlueZ assigns its own handles.

## Discovered services and relevant characteristics

Characteristic flags are ATT properties: read `02`, write `08`, notify `10`,
indicate `20`. Only characteristics relevant to the sequences below are listed.

| Service UUID | Handle | Characteristic UUID | Properties | Role |
|---|---:|---|---:|---|
| `91f1de68-dff6-466e-8b65-ff13b0f16fb8` | `4042` | `aba356eb-9633-4e60-b73f-f52516dbd671` | `08` | Token |
| same | `4012` | `85b9163e-62d1-49ff-a6f5-054b4630d4a1` | `08` | Client name |
| `4e941240-d01d-46b9-a5ea-67636806830b` | `4512` | `bf6dc9cf-3606-4ec9-a4c8-d77576e93ea4` | `1a` | SSID per libfuji |
| same | `4582` | `82a9f452-c5ce-4ef5-8203-3fc9a47f8171` | `1a` | Read after trigger |
| same | `4542` | `aab609c4-94dd-4d89-bc60-665d5090b828` | `1a` | Unknown |
| same | `4552` | `c95d91ae-b247-4d6d-8661-7dd5d6a0f85b` | `1a` | Geotag interval per libfuji |
| same | `4562` | `75823784-fbb7-4b71-abae-cd9a34072e3c` | `1a` | Unknown |
| `4c0020fe-f3b6-40de-acc9-77d129067b14` | `5012` | `a68e3f66-0fcc-4395-8d4c-aa980b5877fa` | `22` | Status indication |
| same | `5022` | `bd17ba04-b76b-4892-a545-b73ba1f74dae` | `22` | Unknown indication |
| same | `5032` | `f9150137-5d40-4801-a8dc-f7fc5b01da50` | `12` | Time notification per libfuji |
| same | `5042` | `ad06c7b7-f41a-46f4-a29a-712055319122` | `12` | Geotag request per libfuji |
| `6514eb81-4e8f-458d-aa2a-e691336cdfac` | `6012` | `600655e6-3637-42f1-8fb2-44efc5c63b13` | `08` | Wi-Fi trigger |
| same | `6032` | `7fcf49c6-4ff0-4777-a03d-1a79166af7a8` | `08` | Shutter control |
| `3b46ec2b-48ba-41fd-b1b8-ed860b60d22b` | `8012` | `0f36ec14-29e5-411a-a1b6-64ee8383f090` | `08` | Geotag payload |

The discovery also contains Generic Access, Generic Attribute, Device
Information, `117c4142-edd4-4c77-8696-dd18eebb770a`, lens,
`e872b11f-d526-4ae1-9bb4-89a99d48fa59`, and
`af854c2e-b214-458e-97e2-912c4ecf2cb8` services. The password UUID used by
current libfuji (`e809256a-915c-4967-92e8-53b7d4cad213`) was **not found in
this capture's discovered characteristic declarations**. Don't invent a
password read for this X-T30 trace.

## Observed interaction order

1. After discovery and a Device Information serial-number read, the client
   writes four token bytes to `4042`, then a 14-byte name to `4012`; both
   receive ATT write responses. It reads the device name, SSID, firmware, and
   other characteristics.
2. It enables indications on `5012` and `5022` (CCCD `02 00`), notifications
   on `5032` and `5042` (`01 00`), then notifications on `4512`, `4562`,
   `4542`, `4582` and `4552`. It receives `01 00` on `5032` and `5042`.
   Initial reads include `5012 = 00 80`, `5022 = 00 00`, and `4542 = 01 00`.
3. It writes `0a 00` to `4552`, which is also notified; repeated `5042`
   notifications are followed by 23-byte writes to `8012`. Other writes to
   the `7022` characteristic occur earlier; their meaning is not established.
4. It writes `01 00`, `02 00`, `01 00`, `00 00` to shutter `6032`, then
   `04 00` to Wi-Fi trigger `6012`. The camera indicates `01 80` on `5012`;
   the client reads `4582` and gets `01`. A later `5012` indication is
   `00 80`. These are observations, not a proven timing or success contract.

## Client comparison and gaps

- Current libfuji reads SSID and optionally password during `fill_props`, then
  sends a shutter release, writes `04 00` to the trigger and reads `4582`,
  treating first byte `01` as ready. This differs from older libfuji code that
  read credentials during handover and waited on `5012`.
- Furble's legacy implementation extracts a four-byte advertisement token,
  writes it followed by the client name, and subscribes to configuration
  indications and notifications. Furble is an ESP32 PlatformIO application,
  so a real client test needs supported hardware or a separate harness; its
  documented feature set is shutter/GPS, not necessarily Wi-Fi handover.
- Current libfuji and furble each have separate secure-pairing paths; this
  capture establishes neither their BlueZ bonding requirements nor secure
  status semantics.
- The trace proves neither actual Wi-Fi association nor successful PTP/IP
  transfer. Capture parsing here covers unfragmented ATT discovery and the
  listed transactions; verify any missing operations and advertisement data
  with a full Bluetooth decoder before treating the profile as complete.
