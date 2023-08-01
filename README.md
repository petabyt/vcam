This is a patched up version of the [gphoto virtual USB camera](https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb) modified
to be a drop-in replacement for libusb.so, allowing USB communication with a fake camera and virtual filesystem. Only compatible with libusb v1.0.
This is a very crude hack for now, but it will eventually be improved when it's used for regression testing.
