#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "usbthing.h"

enum {
	STRINGID_MANUFACTURER = 1,
	STRINGID_PRODUCT,
	STRINGID_SERIAL,
	STRINGID_CONFIG_HS,
	STRINGID_CONFIG_LS,
	STRINGID_INTERFACE,
	STRINGID_MAX
};

struct Config {
	struct usb_config_descriptor config;
	struct usb_interface_descriptor interf0;
}config = {
	.config = {
		.bLength = sizeof(struct usb_config_descriptor),
		.bDescriptorType = USB_DT_CONFIG,
		.bNumInterfaces = 0x1,
		.wTotalLength = sizeof(struct Config),
		.bConfigurationValue = 0x1,
		.iConfiguration = 0,
		.bmAttributes = 0xc,
		.bMaxPower = 0x32,
	},
	.interf0 = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceProtocol = 0,
		.bInterfaceClass = 0,
		.bAlternateSetting = 0,
		.bInterfaceNumber = 0,
		.bInterfaceSubClass = 0,
		.bNumEndpoints = 0,
		.iInterface = 0,
	}
};

int usb_get_string(struct UsbThing *ctx, int devn, int id, char buffer[127]) {
	switch (id) {
	case STRINGID_MANUFACTURER:
		strcpy(buffer, "abcdefg");
		return 0;
	case STRINGID_PRODUCT:
		strcpy(buffer, "abcdefg");
		return 0;
	}
	return -1;
}

int usb_get_device_descriptor(struct UsbThing *ctx, int devn, struct usb_device_descriptor *dev) {
	dev->bLength = sizeof(struct usb_device_descriptor);
	dev->bDescriptorType = 1;
	dev->bcdUSB = 0x0200;
	dev->bcdDevice = 0;
	dev->bDeviceClass = 0;
	dev->bDeviceSubClass = 0;
	dev->bDeviceProtocol = 0;
	dev->bMaxPacketSize0 = 64;
	dev->idVendor = 0x1234;
	dev->idProduct = 0x5678;

	dev->iManufacturer = STRINGID_MANUFACTURER;
	dev->iProduct = STRINGID_PRODUCT;
	dev->iSerialNumber = 0;
	dev->bNumConfigurations = 1;
	return 0;
}

int usb_send_config_descriptor(struct UsbThing *ctx, int devn, int i, void *data) {
	if (i == 0) {
		memcpy(data, &config, sizeof(config));
		return sizeof(config);
	} else {
		printf("config desc\n");
		abort();
	}
	return 0;
}

static int get_config_descriptor(struct UsbThing *ctx, int devn, struct usb_config_descriptor *desc, int i) {
	memcpy(desc, &config.config, sizeof(config.config));
	return 0;
}

static int get_interface_descriptor(struct UsbThing *ctx, int devn, struct usb_interface_descriptor *desc, int i) {
	memcpy(desc, &config.interf0, sizeof(config.interf0));
	return 0;
}

void usbt_user_init(struct UsbThing *ctx) {
	ctx->get_string_descriptor = usb_get_string;
	ctx->get_total_config_descriptor = usb_send_config_descriptor;
	ctx->get_config_descriptor = get_config_descriptor;
	ctx->get_qualifier_descriptor = usbt_get_device_qualifier_descriptor;
	ctx->get_interface_descriptor = get_interface_descriptor;
	ctx->get_device_descriptor = usb_get_device_descriptor;
	ctx->get_endpoint_descriptor = NULL; // writeme
	ctx->handle_control_request = usbt_handle_control_request;
}

int main(int argc, char **argv) {
	struct UsbThing t;
	usbt_user_init(&t);
	return usbt_vhci_init(&t);
}
