#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <usbthing.h>
#include <vcam.h>

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
	struct usb_endpoint_descriptor ep1;
	struct usb_endpoint_descriptor ep81;
	struct usb_endpoint_descriptor ep82;
	struct usb_bos_descriptor bos;
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
	},
	.ep1 = {
		.bLength = 7,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 0x1,
		.bmAttributes = 2,
		.wMaxPacketSize = 0x200,
		.bInterval = 1,
	},
	.ep81 = {
		.bLength = 7,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 0x81,
		.bmAttributes = 2,
		.wMaxPacketSize = 0x200,
		.bInterval = 1,
	},
	.ep82 = {
		.bLength = 7,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 0x82,
		.bmAttributes = 3,
		.wMaxPacketSize = 0x18,
		.bInterval = 4,
	},
	.bos = {
		.bLength = 5,
		.bDescriptorType = USB_DT_BOS,
		.wTotalLength = 0x16,
		.bNumDeviceCaps = 2,
	}
};

struct Priv {
	vcam *cam[5];
};

static inline vcam *get_cam(struct UsbThing *ctx, int devn) {
	if (devn > 5) abort();
	return ((struct Priv *)ctx->priv_impl)->cam[devn];
}

int usb_get_string(struct UsbThing *ctx, int devn, int id, char buffer[127]) {
	switch (id) {
	case STRINGID_MANUFACTURER:
		strcpy(buffer, get_cam(ctx, devn)->manufac);
		return 0;
	case STRINGID_PRODUCT:
		strcpy(buffer, get_cam(ctx, devn)->model);
		return 0;
	}
	return -1;
}

int usb_get_device_descriptor(struct UsbThing *ctx, int devn, struct usb_device_descriptor *dev) {
	dev->bLength = sizeof(struct usb_device_descriptor);
	dev->bDescriptorType = 1;
	dev->bcdUSB = 0x0200;
	dev->bDeviceClass = 0;
	dev->bDeviceSubClass = 0;
	dev->bMaxPacketSize0 = 64;
	dev->idVendor = get_cam(ctx, devn)->vendor;
	dev->idProduct = get_cam(ctx, devn)->product;

	dev->iManufacturer = STRINGID_MANUFACTURER;
	dev->iProduct = STRINGID_PRODUCT;
	dev->iSerialNumber = STRINGID_SERIAL;
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

int get_interface_descriptor(struct UsbThing *ctx, int devn, struct usb_interface_descriptor *desc, int i) {
	memcpy(desc, &config.interf0, sizeof(config.interf0));
	return 0;
}

// Extended function to handle additional MTP commands
static int handle_control(struct UsbThing *ctx, int devn, int ep, const void *data, int len, void *out) {
	const struct usb_ctrlrequest *ctrl = (const struct usb_ctrlrequest *)data;
	switch (ctrl->bRequest) {
	case MTP_REQ_CANCEL:
		return 0;
	case MTP_REQ_GET_EXT_EVENT_DATA:
		return 0;
	}
	return usbt_handle_control_request(ctx, devn, ep, data, len, out);
}

static int handle_bulk(struct UsbThing *ctx, int devn, int ep, void *data, int len) {
	if (ep == 0x1) {
		return vcam_write(get_cam(ctx, devn), ep, (const unsigned char *)data, len);
	} else if (ep == 0x81) {
		vcam_read(get_cam(ctx, devn), ep, (unsigned char *)data, len);
	} else if (ep == 0x82) {
		printf("int not implemented\n");
		abort();
	}
	return 0;
}

void usbt_user_init(struct UsbThing *ctx) {
	ctx->priv_backend = malloc(sizeof(struct Priv));
	ctx->get_string_descriptor = usb_get_string;
	ctx->get_total_config_descriptor = usb_send_config_descriptor;
	ctx->get_config_descriptor = get_config_descriptor;
	ctx->get_qualifier_descriptor = usb_get_device_qualifier_descriptor;
	ctx->get_interface_descriptor = get_interface_descriptor;
	ctx->get_device_descriptor = usb_get_device_descriptor;

	ctx->handle_control_request = handle_control;
	ctx->handle_bulk_transfer = handle_bulk;
}

// TODO: This function will eventually be able to accept more than one camera
// Or have an argument parser that accepts multiple cameras
int vcam_start_usbthing(vcam *cam, enum CamBackendType backend) {
	struct UsbThing ctx;

	struct Priv priv;
	usbt_init(&ctx);
	priv.cam[0] = cam;
	ctx.priv_impl = (void *)&priv;
	ctx.n_devices = 1;

	usbt_user_init(&ctx);
	if (backend == VCAM_VHCI) {
		return usb_vhci_init(&ctx);
	}

	return 0;
}
