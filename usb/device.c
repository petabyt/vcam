// Implements some common operations of a USB device
// Copyright 2025 Daniel C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <linux/usb/ch9.h>
#include "usbthing.h"

int usb_get_device_qualifier_descriptor(struct UsbThing *ctx, int devn, struct usb_qualifier_descriptor *q) {
	struct usb_device_descriptor dev;
	ctx->get_device_descriptor(ctx, devn, &dev);
	q->bLength = sizeof(struct usb_qualifier_descriptor);
	q->bDescriptorType = USB_DT_DEVICE_QUALIFIER;
	q->bDeviceClass = dev.bDeviceClass;
	q->bDeviceProtocol = dev.bDeviceProtocol;
	q->bNumConfigurations = dev.bNumConfigurations;
	q->bMaxPacketSize0 = dev.bMaxPacketSize0;
	q->bcdUSB = dev.bcdUSB;
	q->bDeviceSubClass = dev.bDeviceSubClass;
	return 0;
}

static inline void memcpy_padding(void *dest, const void *src, size_t n, size_t n_end) {
	memset(dest, 0, n_end);
	memcpy(dest, src, n);
}

static int get_desc(struct UsbThing *ctx, int devn, int value, int index, int length, uint8_t *data) {
	(void)index;

	const uint16_t lang = 0x409;

	switch (value >> 8) {
	case USB_DT_STRING: {
		if (length > 0xff) {
			printf("String requested is bigger than max\n");
			abort();
		}
		if ((value & 0xff) == 0) {
			data[0] = 4;
			data[1] = USB_DT_STRING;
			data[2] = lang & 0xff;
			data[3] = lang >> 8;
			return length;
		}

		// USB_MAX_STRING_LEN + 1
		char string[127];
		ctx->get_string_descriptor(ctx, devn, value & 0xff, string);
		int string_size = 2 * (int)strlen(string);

		char buffer[0xff];
		int count = utf8_to_utf16le(string, (uint16_t *)buffer, sizeof(buffer));

		data[0] = string_size;
		data[1] = USB_DT_STRING;
		memcpy(&data[2], buffer, count);
		return length;
	}
	case USB_DT_DEVICE: {
		if (ctx->get_device_descriptor == NULL) abort();
		struct usb_device_descriptor dev;
		ctx->get_device_descriptor(ctx, devn, &dev);
		memcpy_padding(data, &dev, sizeof(dev), length);
		return length;
	}
	case USB_DT_DEVICE_QUALIFIER: {
		struct usb_qualifier_descriptor desc;
		ctx->get_qualifier_descriptor(ctx, devn, &desc);
		memcpy_padding(data, &desc, sizeof(desc), length);
		return length;
	}
	case USB_DT_CONFIG: {
		ctx->get_total_config_descriptor(ctx, devn, value & 0xff, data);
		return length;
	}
	case USB_DT_INTERFACE: {
		struct usb_interface_descriptor desc;
		ctx->get_interface_descriptor(ctx, devn, &desc, value & 0xff);
		memcpy_padding(data, &desc, sizeof(desc), length);
		return 0;
	}
	default:
		printf("Unimplemented descriptor %d\n", value >> 8);
		return -1;
	}
	return 0;
}

int usbt_handle_control_request(struct UsbThing *ctx, int devn, int endpoint, const void *data, int length, void *out) {
	if (endpoint != 0) {
		printf("More than 1 control endpoint not supported\n");
		return -1;
	}

	const struct usb_ctrlrequest *ctrl = (const struct usb_ctrlrequest *)data;
	usbt_dbg("Handling control request %d\n", ctrl->bRequest);
	switch (ctrl->bRequest) {
	case USB_REQ_GET_DESCRIPTOR:
		if (length != 8) {
			printf("Weird request\n");
			return -1;
		}
		return get_desc(ctx, devn, ctrl->wValue, ctrl->wIndex, ctrl->wLength, out);
	case USB_REQ_SET_CONFIGURATION: {
		printf("USB_REQ_SET_CONFIGURATION NOP\n");
		return 0;
	}
	case USB_REQ_GET_INTERFACE: {
		printf("Unsupported USB_REQ_GET_INTERFACE\n");
		return -1;
	}
	case USB_REQ_SET_INTERFACE: {
		printf("Unsupported USB_REQ_SET_INTERFACE");
		return -1;
	}
	default:
		printf("Invalid bRequest %d\n", ctrl->bRequest);
		abort();
	}
	return 0;
}

int usb_hub_handle_control_request(struct UsbThing *ctx, int endpoint, void *data, int length) {
	// writeme
	// is this possible with gadgetfs/usbip?
	return 0;
}

void usbt_init(struct UsbThing *ctx) {
	memset(ctx, 0, sizeof(struct UsbThing));
	ctx->handle_control_request = usbt_handle_control_request;
	ctx->get_qualifier_descriptor = usb_get_device_qualifier_descriptor;
}
