# vcam
This is a virtual camera emulator to spoof and communicate with official vendor software. It currently emulates the
responder (server) side of PTP/USB, PTP/IP, UPnP, and (eventually) Bluetooth. It also perfectly emulates the networking
too (requires a recent WiFi card) so that means it can [spoof official vendor apps](https://twitter.com/danielcdev/status/1696271427240902894) without patching.

## Roadmap
- [x] Basic PTP responder implementation (thanks Marcus Meissner)
- [x] `libusb-v1.0.so` drop-in replacement - spoof Linux PTP apps
- [x] PTP/IP packet & behavior support
- [x] Complete Fujifilm X implementation (2015-2020)
- [x] Spoof [Fujifilm Camera Connect](https://play.google.com/store/apps/details?id=com.fujifilm_dsc.app.remoteshooter&hl=en_US&gl=US)
- [x] Spoof [EOS Connect](https://play.google.com/store/apps/details?id=jp.co.canon.ic.cameraconnect&hl=en_US&gl=US) (only the setup part)
- [ ] Complete Canon EOS implementation (Digic 4+)
- [ ] Complete ISO MTP implementation
- [x] OTG raspberry pi zero device

## Why
- For regression testing - link a PTP client again the fake `libusb.so` and test functionality in CI
- For easier prototyping - vcam can be used instead of a physical camera (no need to wait for a camera to recharge to continue testing)
- [Black box testing](https://en.wikipedia.org/wiki/Black-box_testing) - test against vendor software without disassembling
- Note that this tool is very *experimental*, and the code quality reflects that.

## Compiling
Run on linux.
```
make vcam
./vcam canon_1300d
```
To compile libusb shared object:
```
make libusb.so
```

Helper targets for Canon PTP/IP spoofer:
- `make setup-canon` - setup dummy net device
- `make ip-canon` - start wireless AP on device wlp0s20f3
- `make test-canon` - starts program in a loop - will accept another connection after disconnect 

Fuji test images: https://s1.danielc.dev/filedump/fuji_sd.tar.gz

## Credits
Original Author (vusb): Marcus Meissner <marcus@jet.franken.de>  
Forked from https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb  
create_ap scripts from https://github.com/oblique/create_ap  
Licensed under the GNU Lesser General Public License v2.1  
