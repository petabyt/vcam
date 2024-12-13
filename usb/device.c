// Implements some common operations of a USB device
// Copyright 2025 Daniel C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <linux/usb/ch9.h>
#include "usbthing.h"

int usbt_get_device_qualifier_descriptor(struct UsbThing *ctx, int devn, struct usb_qualifier_descriptor *q) {
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

	const char *usb_dt_strings[] = {
		"?",
		"USB_DT_DEVICE",
		"USB_DT_CONFIG",
		"USB_DT_STRING",
		"USB_DT_INTERFACE",
		"USB_DT_ENDPOINT",
		"USB_DT_DEVICE_QUALIFIER",
		"USB_DT_OTHER_SPEED_CONFIG",
		"USB_DT_INTERFACE_POWER",
		"USB_DT_OTG",
		"USB_DT_DEBUG",
		"USB_DT_INTERFACE_ASSOCIATION",
		"USB_DT_SECURITY",
		"USB_DT_KEY",
		"USB_DT_ENCRYPTION_TYPE",
		"USB_DT_BOS",
		"USB_DT_DEVICE_CAPABILITY",
		"USB_DT_WIRELESS_ENDPOINT_COMP",
		"USB_DT_WIRE_ADAPTER",
		"USB_DT_RPIPE",
		"USB_DT_CS_RADIO_CONTROL",
		"USB_DT_PIPE_USAGE",
		"USB_DT_SS_ENDPOINT_COMP",
		"USB_DT_SSP_ISOC_ENDPOINT_COMP"
	};

	assert((value >> 8) <= 0x31);
	assert((value >> 8) >= 0);
	printf("Get descriptor: %s(val: %d, len: %d)\n", usb_dt_strings[value >> 8], value & 0xff, length);

	const uint16_t lang = 0x409;

	switch (value >> 8) {
	case USB_DT_STRING: {
		if (length > 0xff) {
			printf("String requested is bigger than max\n");
			abort();
		}
		if ((value & 0xff) == 0) {
			uint8_t desc[4];
			desc[0] = 4;
			desc[1] = USB_DT_STRING;
			desc[2] = lang & 0xff;
			desc[3] = lang >> 8;
			memcpy_padding(data, desc, 4, length);
			return length;
		}

		// USB_MAX_STRING_LEN + 1
		char string[127];
		if (ctx->get_string_descriptor(ctx, devn, value & 0xff, string)) {
			printf("No string descriptor for %d\n", value & 0xff);
			abort();
		}
		int string_size = 2 * (int)strlen(string);

		char buffer[0xff];
		int count = utf8_to_utf16le(string, (uint16_t *)buffer, sizeof(buffer));

		data[0] = 2 + string_size;
		data[1] = USB_DT_STRING;
		memcpy_padding(&data[2], buffer, count, length - 2);
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
	case USB_DT_ENDPOINT: {
		abort();
		return 0;
	}
	case USB_DT_DEBUG: {
		struct usb_debug_descriptor desc;
		desc.bLength = sizeof(struct usb_debug_descriptor);
		desc.bDescriptorType = USB_DT_DEBUG;
		desc.bDebugInEndpoint = 0;
		desc.bDebugOutEndpoint = 0;
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
	usbt_dbg("Handling control request bRequest:%d\n", ctrl->bRequest);
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
	case USB_REQ_GET_STATUS: {
		((uint8_t *)out)[0] = 0x1;
		((uint8_t *)out)[1] = 0x0;
		return 2;
	}
	default:
		printf("unsupported bRequest %d\n", ctrl->bRequest);
		abort();
	}
	return 0;
}

int usbt_hub_handle_control_request(struct UsbThing *ctx, int endpoint, void *data, int length) {
	// writeme
	// is this possible with gadgetfs/usbip?
	return 0;
}

void usbt_init(struct UsbThing *ctx) {
	memset(ctx, 0, sizeof(struct UsbThing));
	ctx->handle_control_request = usbt_handle_control_request;
	ctx->get_qualifier_descriptor = usbt_get_device_qualifier_descriptor;
}
