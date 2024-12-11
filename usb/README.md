# usbthing

This is a general purpose translation layer for creating USB devices.

![canvas](canvas.png)

It currently supports 3 different backends:
- *libusb-v1.0*
  A fake so/dll drop-in replacement for libusb-v1.0 - this is easy to manage and is great for CI testing.
- *vhci*
  Creates a device on the kernel's virtual host interface - ideal for routing to VMs
- *gadgetfs*
  The linux kernel interface over DWC - used to expose a device over a physical OTG port.

## Roadmap
- [ ] VHCI
- [ ] libusb-v1.0
- [ ] gadgetfs
- [x] Emulate standard device SETUP requests
- [ ] Bulk endpoints
- [ ] Handle interrupt endpoint polling
- [ ] Virtual hub (if possible)
- [ ] Stable API/ABI
