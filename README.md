# vcam
This is a virtual camera emulator to spoof and communicate with official vendor software. It currently emulates the
responder (server) side of PTP/USB, PTP/IP, UPnP, and (eventually) Bluetooth. For USB, this project has a a
drop-in spoofer replacement of `libusb-v1.0.so`.

## Roadmap
- [x] LibUSB drop-in replacement
- [x] PTP/IP Implementation
- [x] Spoof Fujifilm Camera Connect
- [x] Complete Fujifilm implementation
- [ ] Complete Canon EOS implementation
- [ ] Spoof EOS Connect

## Building
- symlinks: https://github.com/petabyt/camlib https://github.com/petabyt/fudge
- `make libusb.so`
- Run on Linux only.

## Credits
Original Author (vcam): Marcus Meissner <marcus@jet.franken.de>  
Forked from https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb  
create_ap scripts from https://github.com/oblique/create_ap  
