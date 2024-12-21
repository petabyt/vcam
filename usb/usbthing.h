#pragma once
#include <linux/usb/ch9.h>
#include <stdint.h>

#define usbt_dbg(...) printf(__VA_ARGS__)

struct UsbThing {
	/// @brief Private pointer for backend
	void *priv_backend;
	/// @brief Private pointer for device implementation
	void *priv_impl;

	/// @brief Called by backend to handle control requests/setup packets
	/// @param out Response buffer - is at least 65535 bytes long.
	/// @param out_length Is set to zero by caller
	/// @returns bytes written, -1 for error
	int (*handle_control_request)(struct UsbThing *ctx, int devn, int endpoint, const void *data, int length, void *out);
	/// @brief Handle bulk transfers IN/OUT
	/// @param data Buffer for reading/writing, will be at least the size of maxPacketSize for this endpoint
	/// @returns bytes read/written, -1 for no response
	int (*handle_bulk_transfer)(struct UsbThing *ctx, int devn, int endpoint, void *data, int length);

	/// @returns nonzero for error
	int (*get_device_descriptor)(struct UsbThing *ctx, int devn, struct usb_device_descriptor *desc);
	/// @returns nonzero for error
	int (*get_qualifier_descriptor)(struct UsbThing *ctx, int devn, struct usb_qualifier_descriptor *desc);
	/// @returns nonzero for error
	int (*get_string_descriptor)(struct UsbThing *ctx, int devn, int id, char buffer[127]);
	/// @brief Get just the config descriptor
	/// @returns nonzero for error
	int (*get_config_descriptor)(struct UsbThing *ctx, int devn, struct usb_config_descriptor *desc, int i);
	/// @brief Write `length` bytes of the total config descriptor to `data`
	/// @returns number of bytes written
	int (*get_total_config_descriptor)(struct UsbThing *ctx, int devn, int i, void *data);
	/// @returns nonzero for error
	int (*get_interface_descriptor)(struct UsbThing *ctx, int devn, struct usb_interface_descriptor *desc, int i);
	/// @returns nonzero for error
	int (*get_endpoint_descriptor)(struct UsbThing *ctx, int devn, struct usb_endpoint_descriptor *desc, int i);

	int n_devices;
};

// 	Init with default handlers
void usbt_init(struct UsbThing *ctx);

// Start VHCI (vhci.c) server
int usbt_vhci_init(struct UsbThing *ctx);

// Start gadgetfs server over OTG port (otg.c)
int usbt_gadgetfs_init(struct UsbThing *ctx);

/// @brief Entry function for ctx init for libusb fake so
extern void usbt_user_init(struct UsbThing *ctx);

/// @brief Default control request handler, will handle all standard control requests and state logic
int usbt_handle_control_request(struct UsbThing *ctx, int devn, int endpoint, const void *data, int length, void *out);

/// @brief Fake hub control request handler, can be used in place of usbt_handle_control_request
/// @todo Finish
int usbt_hub_handle_control_request(struct UsbThing *ctx, int endpoint, void *data, int length);

/// @brief Optional dummy implementation for get_qualifier_descriptor
int usbt_get_device_qualifier_descriptor(struct UsbThing *ctx, int devn, struct usb_qualifier_descriptor *q);

// usbstring.c
int utf8_to_utf16le(const char *s, uint16_t *cp, unsigned int len);
