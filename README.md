# vcam
This is a virtual camera that can spoof and communicate with official vendor_id software. It currently implements the
responder (server) side of PTP/USB, PTP/IP, and UPnP. It also perfectly emulates the device's WiFi AP and networking
(requires a recent WiFi card) That means it can spoof official vendor_id apps:

<img title="Fujifilm Camera Connect connected to spoofed X-H1-ABCD" src="bin/Screenshot_20240402-140041.png" width="300"><img src="bin/Screenshot_20240402-140506.png" width="300">

This started out as a fork of gphoto's [vcamera](https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb) regression tester.
Since then it has been heavily modified and improved to more closely replicate the PTP responders found in camera firmware.

## Roadmap
- [x] Basic PTP responder implementation (thanks Marcus Meissner)
- [x] libusb and vhci backends - see usb/
- [x] PTP/IP packet & behavior support
- [x] Complete Fujifilm X implementation (2015-2020)
- [x] Spoof [Fujifilm Camera Connect](https://play.google.com/store/apps/details?id=com.fujifilm_dsc.app.remoteshooter&hl=en_US&gl=US)
- [x] Spoof [EOS Connect](https://play.google.com/store/apps/details?id=jp.co.canon.ic.cameraconnect&hl=en_US&gl=US)
- [ ] Complete Canon EOS implementation (Digic 4+)
- [ ] Complete ISO MTP implementation

## Why
- For regression testing - link a PTP client against the fake `libusb.so` and test functionality in CI
- For easier prototyping - vcam can be used instead of a physical camera (no need to wait for a camera to recharge to continue testing)
- [Black box testing](https://en.wikipedia.org/wiki/Black-box_testing) - test against vendor_id software without disassembling

## Compiling
```
sudo apt install libusb-1.0-0-dev libexif-dev
make vcam
```

## libusb backend
The libusb API is hardly implemented, PRs improving this are welcome.
```
make libusb-vcam.so
```

## Running an access point
```
sudo apt install haveged hostapd
```
Power saving mode can make the TCP handshake way too slow:
```
# Doesn't last a reboot (I think)
sudo iw dev wlan0 set power_save off
```
See the makefile for more info on starting access points.
```
make ap-canon WIFI_DEV=wlan0
```

## Credits
Original Author (vusb): Marcus Meissner <marcus@jet.franken.de>  
Forked from https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb  
create_ap scripts from https://github.com/oblique/create_ap  
Licensed under the GNU Lesser General Public License v2.1  
