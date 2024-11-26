# vcam
This is a virtual camera that can spoof and communicate with official vendor software. It currently implements the
responder (server) side of PTP/USB, PTP/IP, and UPnP. It also perfectly emulates the device's WiFi AP and networking
(requires a recent WiFi card) That means it can spoof official vendor apps:

<img title="Fujifilm Camera Connect connected to spoofed X-H1-ABCD" src="bin/Screenshot_20240402-140041.png" width="300"><img src="bin/Screenshot_20240402-140506.png" width="300">

This started out as a fork of gphoto's [vcamera](https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb) regression tester.
Since then it has been heavily modified and improved to more closely replicate the PTP responders found in camera firmware.

## Roadmap
- [x] Basic PTP responder (vcamera) implementation (thanks Marcus Meissner)
- [x] `libusb-v1.0.so` drop-in replacement - spoof Linux PTP apps
- [x] PTP/IP packet & behavior support
- [x] Complete Fujifilm X implementation (2015-2020)
- [x] Spoof [Fujifilm Camera Connect](https://play.google.com/store/apps/details?id=com.fujifilm_dsc.app.remoteshooter&hl=en_US&gl=US)
- [x] Spoof [EOS Connect](https://play.google.com/store/apps/details?id=jp.co.canon.ic.cameraconnect&hl=en_US&gl=US)
- [ ] Complete Canon EOS implementation (Digic 4+)
- [ ] Complete ISO MTP implementation
- [x] OTG raspberry pi zero device - works in explorer.exe

## Why
- For regression testing - link a PTP client against the fake `libusb.so` and test functionality in CI
- For easier prototyping - vcam can be used instead of a physical camera (no need to wait for a camera to recharge to continue testing)
- [Black box testing](https://en.wikipedia.org/wiki/Black-box_testing) - test against vendor software without disassembling

## Compiling
This will only run on Linux. It can work on WSL2 if you can route the IP through the Windows firewall.
```
make vcam
./vcam fuji_x_a2
```

## Compiling fake libusb.so
By default, will target Canon 1300D
```
sudo apt install libusb-1.0-dev # Needed for libusb.h
make libusb.so
```

## Running WiFi access point
On target Raspberry Pi, install the depencencies needed to run the access point:
```
sudo apt install haveged hostapd
```
Power saving mode can make the TCP handshake way too slow:
```
# Doesn't last a reboot (I think)
sudo iw dev wlan0 set power_save off
```
`pi.mak` gets ssh login info from `~/.config/secret.mak`
```
DEVICE_PORT := 22
DEVICE_PASSWORD := xxx
DEVICE_USERNAME := pi
DEVICE_IP := 192.168.1.123
endif

AP_PASS := password for ap, don't define for no password
```

### Compiling vcam for armhf/Raspi Zero
```
sudo dpkg --add-architecture armhf && sudo apt update
sudo apt install gcc-arm-linux-gnueabi libexif-dev:armhf

make TARGET=pi vcam # Send binary over scp
make TARGET=pi run # run vcam over ssh
```

Sample X-A2 JPEG images: https://s1.danielc.dev/filedump/fuji_sd.tar.gz -> export to /home/pi/fuji_sdcard

## Credits
Original Author (vusb): Marcus Meissner <marcus@jet.franken.de>  
Forked from https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb  
create_ap scripts from https://github.com/oblique/create_ap  
Licensed under the GNU Lesser General Public License v2.1  
