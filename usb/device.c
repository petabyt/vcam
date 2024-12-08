// Implements USB device control requests
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <linux/usb/ch9.h>
#include "lib.h"

static int send_byte(struct UsbLib *ctx, int devn, int endpoint, uint8_t b) {
	return usb_data_to_host(ctx, devn, endpoint, &b, 1);
}

static int get_desc(struct UsbLib *ctx, int devn, int value, int index, int length) {
	(void)index;
	switch (value >> 8) {
	case USB_DT_STRING: {
		uint16_t lang = 0x409;
		// USB_MAX_STRING_LEN + 1
		if ((value & 0xff) == 0) {
			send_byte(ctx, devn, 0, 4);
			send_byte(ctx, devn, 0, USB_DT_STRING);
			send_byte(ctx, devn, 0, lang >> 8);
			send_byte(ctx, devn, 0, lang & 0xff);
			return 0;
		}

		char string[127];
		usb_get_string(ctx, value & 0xff, string);

		send_byte(ctx, devn, 0, strlen(string));
		send_byte(ctx, devn, 0, USB_DT_STRING);

		char buffer[127 * 2];
		int count = utf8_to_utf16le(string, (uint16_t *)buffer, length);
		usb_data_to_host(ctx, devn, 0, buffer, count);
		return 0;
	}
	default:
		printf("Unimplemented descriptor %d\n", value >> 8);
		return -1;
	}
	return 0;
}

int usb_handle_control_request(struct UsbLib *ctx, int devn, int endpoint, void *data, int length) {
	if (endpoint != 0) {
		printf("More than 1 control endpoint not supported\n");
		return -1;
	}

	struct usb_ctrlrequest *ctrl = (struct usb_ctrlrequest *)data;
	switch (ctrl->bRequest) {
	case USB_REQ_GET_DESCRIPTOR:
		return get_desc(ctx, devn, ctrl->wValue, ctrl->wIndex, ctrl->wLength);
	case USB_REQ_SET_CONFIGURATION: {
		return 0;
	}
	case USB_REQ_GET_INTERFACE: {
		return 0;
	}
	case USB_REQ_SET_INTERFACE: {
		return 0;
	}
	default:
		printf("Invalid bRequest %d\n", ctrl->bRequest);
		abort();
	}
	return 0;
}

int usb_hub_handle_control_request(struct UsbLib *ctx, int endpoint, void *data, int length) {
	// TODO
	return 0;
}
