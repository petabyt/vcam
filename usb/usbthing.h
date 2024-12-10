#pragma once
#include <linux/usb/ch9.h>
#include <stdint.h>

struct UsbThing {
	// Backend private pointer
	void *priv_backend;
	// Private pointer for device/hub implementation
	void *priv_impl;

	int (*handle_control_request)(struct UsbThing *ctx, int devn, int endpoint, void *data, int length);

	int (*get_device_descriptor)(struct UsbThing *ctx, int devn, struct usb_device_descriptor *desc);
	int (*get_qualifier_descriptor)(struct UsbThing *ctx, int devn, struct usb_qualifier_descriptor *desc);
	int (*get_string_descriptor)(struct UsbThing *ctx, int id, char buffer[127]);
	int (*send_config_descriptor)(struct UsbThing *ctx, int devn, int i, int length);
	int (*get_interface_descriptor)(struct UsbThing *ctx, int devn, struct usb_interface_descriptor *desc, int i);
	int (*get_endpoint_descriptor)(struct UsbThing *ctx, int devn, struct usb_endpoint_descriptor *desc, int i);

	int n_devices;
};

// 	Init with default handlers
void usbt_init(struct UsbThing *ctx);

// Start VHCI (vhci.c) server
int usb_vhci_init(struct UsbThing *ctx);

// Start gadgetfs server over OTG port (otg.c)
int usb_gadgetfs_init(struct UsbThing *ctx);

// Entry function for ctx init for libusb fake so
void usbt_user_init(struct UsbThing *ctx);

// Default control request handler
int usb_handle_control_request(struct UsbThing *ctx, int devn, int endpoint, void *data, int length);

// Fake hub control request handler
int usb_hub_handle_control_request(struct UsbThing *ctx, int endpoint, void *data, int length);

// Device sends data to host
int usb_data_to_host(struct UsbThing *ctx, int devn, int endpoint, void *data, int length);

// Optional dummy function that returns info from device descriptor
int usb_get_device_qualifier_descriptor(struct UsbThing *ctx, int devn, struct usb_qualifier_descriptor *q);

int utf8_to_utf16le(const char *s, uint16_t *cp, unsigned int len);
