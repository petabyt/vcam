#pragma once
#include <linux/usb/ch9.h>
#include <stdint.h>

struct UsbLib {
	void *priv_backend;
	void *priv_device;
};

int usb_vhci_init(void);

// Default control request handler
int usb_handle_control_request(struct UsbLib *ctx, int devn, int endpoint, void *data, int length);

// Fake hub control request handler
int usb_hub_handle_control_request(struct UsbLib *ctx, int endpoint, void *data, int length);

// Device sends data to host
int usb_data_to_host(struct UsbLib *ctx, int devn, int endpoint, void *data, int length);

extern int usb_get_string(struct UsbLib *ctx, int id, char buffer[127]);
extern int usb_get_device_descriptor(struct UsbLib *ctx, int devn, struct usb_device_descriptor *dev);
extern int usb_get_config_descriptor(struct UsbLib *ctx, int devn, struct usb_config_descriptor *c);
extern int usb_get_interface_descriptor(struct UsbLib *ctx, int devn, struct usb_interface_descriptor *i);
extern int usb_get_endpoint_descriptor(struct UsbLib *ctx, int devn, struct usb_endpoint_descriptor *c);

int utf8_to_utf16le(const char *s, uint16_t *cp, unsigned int len);
