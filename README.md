# vcam
This is a virtual camera emulator to spoof and communicate with official vendor software. It currently emulates the
responder (server) side of PTP/USB, PTP/IP, UPnP, and (eventually) Bluetooth. For USB, this project has a a
drop-in spoofer replacement of `libusb-v1.0.so`.

## Roadmap
- [x] LibUSB drop-in replacement
- [x] PTP/IP Implementation
- [x] Complete Fujifilm X implementation (2015-2020)
- [x] Spoof Fujifilm Camera Connect
- [ ] Complete Canon EOS implementation (EOS T6)
- [ ] Spoof EOS Connect

Note that this is an experimental regression testing tool, and the code quality reflects that. Of course, this will be improved
before it's finished.

## Building
- This repo is currently being rapidly developed alongside other projects so symlinks are being used for now instead of submodules.  
Clone in same dir as this repo: https://github.com/petabyt/camlib https://github.com/petabyt/fudge  
- `make libusb.so`
- Run on Linux only.

For Canon PTP/IP spoofer:
- `make setup-canon` - setup dummy net device
- `make ip-canon` - start wireless AP on device wlp0s20f3
- `make test-canon` - starts program in a loop - will accept another connection after disconnect 

## Credits
Original Author (vusb): Marcus Meissner <marcus@jet.franken.de>  
Forked from https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb  
create_ap scripts from https://github.com/oblique/create_ap  
Licensed under the GNU Lesser General Public License v2.1  
