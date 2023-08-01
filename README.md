This is a patched up version of the [gphoto virtual USB camera](https://github.com/gphoto/libgphoto2/tree/master/libgphoto2_port/vusb) modified
to be a drop-in replacement for libusb.so, allowing USB communication with a fake camera and virtual filesystem. Only compatible with libusb v1.0.
This is a very crude hack for now, but it will eventually be improved when it's used for regression testing.

A basic test program based on [camlib](https://github.com/petabyt/camlib) is provided, and should output:
```
$ ./main
Initializing USB...
Vendor ID: 123, Product ID: 123
Endpoint IN addr: 0x81
Endpoint OUT addr: 0x2
Endpoint INT addr: 0x82
send_bulk_packets 0x1002 (PTP_OC_OpenSession)
send_bulk_packet: Sent 16 bytes
recieve_bulk_packets: Read 12 bytes
recieve_bulk_packets: Return code: 0x2001
send_bulk_packets 0x1001 (PTP_OC_GetDeviceInfo)
send_bulk_packet: Sent 12 bytes
recieve_bulk_packets: Read 176 bytes
recieve_bulk_packets: Recieved extra packet 0 bytes
recieve_bulk_packets: Return code: 0x2001
{
    "ops_supported": [4097, 4098, 4099, 4100, 4101, 4102, 4103, 4104, 4105, 4106, 4107, 4110, 4116, 4117, 4118, 39321],
    "events_supported": [16386, 16387, 16390, 16394, 16397],
    "props_supported": [20481, 20483, 20487, 20496, 20493, 20497],
    "manufacturer": "GP",
    "extensions": "G-V: 1.0;",
    "model": "VC",
    "device_version": "2.5.11",
    "serial_number": "0.1"
}
send_bulk_packets 0x1004 (PTP_OC_GetStorageIDs)
send_bulk_packet: Sent 12 bytes
recieve_bulk_packets: Read 32 bytes
recieve_bulk_packets: Recieved extra packet 0 bytes
recieve_bulk_packets: Return code: 0x2001
send_bulk_packets 0x1007 (PTP_OC_GetObjectHandles)
send_bulk_packet: Sent 24 bytes
recieve_bulk_packets: Read 40 bytes
recieve_bulk_packets: Recieved extra packet 0 bytes
recieve_bulk_packets: Return code: 0x2001
send_bulk_packets 0x1008 (PTP_OC_GetObjectInfo)
send_bulk_packet: Sent 16 bytes
recieve_bulk_packets: Read 178 bytes
recieve_bulk_packets: Recieved extra packet 0 bytes
recieve_bulk_packets: Return code: 0x2001
Filename: BLAH.JPG
File size: 0
send_bulk_packets 0x1008 (PTP_OC_GetObjectInfo)
send_bulk_packet: Sent 16 bytes
recieve_bulk_packets: Read 176 bytes
recieve_bulk_packets: Recieved extra packet 0 bytes
recieve_bulk_packets: Return code: 0x2001
Filename: 101_EOS
File size: 4096
send_bulk_packets 0x1008 (PTP_OC_GetObjectInfo)
send_bulk_packet: Sent 16 bytes
recieve_bulk_packets: Read 170 bytes
recieve_bulk_packets: Recieved extra packet 0 bytes
recieve_bulk_packets: Return code: 0x2001
Filename: DCIM
File size: 4096
send_bulk_packets 0x1003 (PTP_OC_CloseSession)
send_bulk_packet: Sent 12 bytes
recieve_bulk_packets: Read 12 bytes
recieve_bulk_packets: Return code: 0x2001
```
