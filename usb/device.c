// Implements USB device control requests
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <linux/usb/ch9.h>
#include "usbthing.h"

static int send_byte(struct UsbThing *ctx, int devn, int endpoint, uint8_t b) {
	return usb_data_to_host(ctx, devn, endpoint, &b, 1);
}

// Send data with cutoff if requested by host
static int send_padding(struct UsbThing *ctx, int devn, int ep, void *data, int data_size, int req_size) {
	if (req_size > data_size) {
		usb_data_to_host(ctx, devn, ep, data, data_size);
		uint8_t pad[512] = {0};
		if ((req_size - data_size) > sizeof(pad)) {
			printf("wtf too much padding %d\n", req_size - data_size);
			abort();
		}
		usb_data_to_host(ctx, devn, ep, pad, req_size - data_size);
	} else {
		usb_data_to_host(ctx, devn, ep, data, req_size);
	}
	return 0;
}

// Dummy function
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

static int get_desc(struct UsbThing *ctx, int devn, int value, int index, int length) {
	(void)index;

	const uint16_t lang = 0x409;

	switch (value >> 8) {
	case USB_DT_STRING: {
		if (length > 0xff) {
			printf("String requested is bigger than max\n");
			abort();
		}
		if ((value & 0xff) == 0) {
			send_byte(ctx, devn, 0, 4);
			send_byte(ctx, devn, 0, USB_DT_STRING);
			send_byte(ctx, devn, 0, lang & 0xff);
			send_byte(ctx, devn, 0, lang >> 8);
			return 0;
		}

		// USB_MAX_STRING_LEN + 1
		char string[127];
		ctx->get_string_descriptor(ctx, value & 0xff, string);
		int string_size = 2 * (int)strlen(string);

		char buffer[0xff];
		int count = utf8_to_utf16le(string, (uint16_t *)buffer, sizeof(buffer));

		send_byte(ctx, devn, 0, string_size);
		send_byte(ctx, devn, 0, USB_DT_STRING);

		usb_data_to_host(ctx, devn, 0, buffer, count);
		return 0;
	}
	case USB_DT_DEVICE: {
		if (ctx->get_device_descriptor == NULL) abort();
		struct usb_device_descriptor dev;
		ctx->get_device_descriptor(ctx, devn, &dev);
		send_padding(ctx, devn, 0, &dev, sizeof(dev), length);
		return 0;
	}
	case USB_DT_DEVICE_QUALIFIER: {
		struct usb_qualifier_descriptor q;
		ctx->get_qualifier_descriptor(ctx, devn, &q);
		send_padding(ctx, devn, 0, &q, sizeof(q), length);
		return 0;
	}
	case USB_DT_CONFIG: {
		//struct usb_config_descriptor c;
		//printf("interface params %d %d\n", value & 0xff, index);
		ctx->send_config_descriptor(ctx, devn, value & 0xff, length);
		//send_padding(ctx, devn, 0, &c, sizeof(c), length);
		return 0;
	}
	case USB_DT_INTERFACE: {
		struct usb_interface_descriptor i;
		ctx->get_interface_descriptor(ctx, devn, &i, value & 0xff);
		send_padding(ctx, devn, 0, &i, sizeof(i), length);
		return 0;
	}
	default:
		printf("Unimplemented descriptor %d\n", value >> 8);
		return -1;
	}
	return 0;
}

int usb_handle_control_request(struct UsbThing *ctx, int devn, int endpoint, void *data, int length) {
	if (endpoint != 0) {
		printf("More than 1 control endpoint not supported\n");
		return -1;
	}

	struct usb_ctrlrequest *ctrl = (struct usb_ctrlrequest *)data;
	switch (ctrl->bRequest) {
	case USB_REQ_GET_DESCRIPTOR:
		return get_desc(ctx, devn, ctrl->wValue, ctrl->wIndex, ctrl->wLength);
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
	// TODO
	return 0;
}

void usbt_init(struct UsbThing *ctx) {
	memset(ctx, 0, sizeof(struct UsbThing));
	ctx->handle_control_request = usb_handle_control_request;
	ctx->get_qualifier_descriptor = usb_get_device_qualifier_descriptor;
}
