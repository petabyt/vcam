# vcam
This is a virtual camera emulator to spoof and communicate with official vendor software. It currently emulates the
responder (server) side of PTP/USB, PTP/IP, UPnP, and (eventually) Bluetooth. It also perfectly emulates the networking
too (requires a good WiFi card) so that means it can [spoof official vendor apps](https://twitter.com/danielcdev/status/1696271427240902894) without patching.

## Roadmap
- [x] Basic PTP responder implementation (thanks Marcus Meissner)
- [x] `libusb-v1.0.so` drop-in replacement - spoof Linux PTP apps
- [x] PTP/IP packet support
- [x] Complete Fujifilm X implementation (2015-2020)
- [x] Spoof [Fujifilm Camera Connect](https://play.google.com/store/apps/details?id=com.fujifilm_dsc.app.remoteshooter&hl=en_US&gl=US)
- [x] Spoof [EOS Connect](https://play.google.com/store/apps/details?id=jp.co.canon.ic.cameraconnect&hl=en_US&gl=US) (only the setup part)
- [ ] Complete Canon EOS implementation (Digic 4+)

## Why
- For regression testing - link a PTP library again the fake libusb and test functionality in CI
- For rapid prototyping - vcam can be used instead of a physical camera for maintaining PTP code.
- Spoofing official clients helps me better understand what they are doing - this has helped me improve my PTP app without decompiling anything,
and removes a lot of the guesswork.
- Note that this tool is very *experimental*, and the code quality reflects that.

## Building
- `make libusb.so`
- Run on Linux only.

Hard symlinks:
- `ln ../../fudge/lib/fujiptp.h fujiptp.h`

For Canon PTP/IP spoofer:
- `make setup-canon` - setup dummy net device
- `make ip-canon` - start wireless AP on device wlp0s20f3
- `make test-canon` - starts program in a loop - will accept another connection after disconnect 

Fuji test images: https://s1.danielc.dev/filedump/fuji_sd.tar.gz

## Credits
Original Author (vusb): Marcus Meissner <marcus@jet.franken.de>  
Forked from https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb  
create_ap scripts from https://github.com/oblique/create_ap  
Licensed under the GNU Lesser General Public License v2.1  
