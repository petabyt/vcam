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

struct usb_endpoint_descriptor_min {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bEndpointAddress;
	__u8 bmAttributes;
	__le16 wMaxPacketSize;
	__u8 bInterval;
} __attribute__((packed));

struct Config {
	struct usb_config_descriptor config;
	struct usb_interface_descriptor interf0;
	struct usb_endpoint_descriptor_min ep1;
	struct usb_endpoint_descriptor_min ep81;
	struct usb_endpoint_descriptor_min ep82;
} __attribute__((packed)) config = {
	.config = {
		.bLength = USB_DT_CONFIG_SIZE,
		.bDescriptorType = USB_DT_CONFIG,
		.bNumInterfaces = 0x1,
		.wTotalLength = sizeof(struct Config),
		.bConfigurationValue = 0x1,
		.iConfiguration = 0,
		.bmAttributes = 0xc0,
		.bMaxPower = 0x32,
	},
	.interf0 = {
		.bLength = USB_DT_INTERFACE_SIZE,
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceProtocol = 1,
		.bInterfaceClass = 6,
		.bInterfaceSubClass = 1,
		.bAlternateSetting = 0,
		.bInterfaceNumber = 0,
		.bNumEndpoints = 3,
		.iInterface = 0,
	},
	.ep1 = {
		.bLength = sizeof(struct usb_endpoint_descriptor_min),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 0x81,
		.bmAttributes = 2,
		.wMaxPacketSize = 0x200,
		.bInterval = 0,
	},
	.ep81 = {
		.bLength = sizeof(struct usb_endpoint_descriptor_min),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 0x2,
		.bmAttributes = 2,
		.wMaxPacketSize = 0x200,
		.bInterval = 0,
	},
	.ep82 = {
		.bLength = sizeof(struct usb_endpoint_descriptor_min),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 0x83,
		.bmAttributes = 3,
		.wMaxPacketSize = 0x8,
		.bInterval = 10,
	},
};

struct Priv {
#define PRIV_MAX_CAMS 5
	vcam *cam[PRIV_MAX_CAMS];
};

static inline vcam *get_cam(struct UsbThing *ctx, int devn) {
	if (devn >= PRIV_MAX_CAMS) abort();
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
	case STRINGID_SERIAL:
		strcpy(buffer, "123456");
		return 0;
	}
	strcpy(buffer, "");
	return 0;
	return -1;
}

int usb_get_device_descriptor(struct UsbThing *ctx, int devn, struct usb_device_descriptor *dev) {
	dev->bLength = sizeof(struct usb_device_descriptor);
	dev->bDescriptorType = 1;
	dev->bcdUSB = 0x0200;
	dev->bcdDevice = 2;
	dev->bDeviceClass = 0;
	dev->bDeviceSubClass = 0;
	dev->bMaxPacketSize0 = 64;
	dev->idVendor = get_cam(ctx, devn)->vendor_id;
	dev->idProduct = get_cam(ctx, devn)->product_id;

	dev->iManufacturer = STRINGID_MANUFACTURER;
	dev->iProduct = STRINGID_PRODUCT;
	dev->iSerialNumber = STRINGID_SERIAL;
	dev->bNumConfigurations = 1;
	return 0;
}

static void hexdump(void *buffer, int size) {
	unsigned char *buf = (unsigned char *)buffer;
	for (int i = 0; i < size; i++) {
		printf("%02x ", buf[i]);
		if ((i + 1) % 16 == 0) {
			printf("\n");
		}
	}
	printf("\n");
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

// https://www.xmos.com/download/AN00132:-USB-Image-Device-Class(2_0_2rc1).pdf
#define STILL_IMAGE_CANCEL_REQUEST 0x64
#define STILL_IMAGE_GET_EXT_EVENT_DATA 0x65
#define STILL_IMAGE_DEV_RESET_REQ 0x66
#define STILL_IMAGE_GET_DEV_STATUS 0x67

// Extended function to handle additional MTP commands
static int handle_control(struct UsbThing *ctx, int devn, int ep, const void *data, int len, void *out) {
	const struct usb_ctrlrequest *ctrl = (const struct usb_ctrlrequest *)data;
	switch (ctrl->bRequest) {
	case MTP_REQ_CANCEL:
		return 0;
	case MTP_REQ_GET_EXT_EVENT_DATA:
		return 0;
	case STILL_IMAGE_GET_DEV_STATUS:
		ptp_write_u16((uint8_t *)out + 0, 0x0);
		ptp_write_u16((uint8_t *)out + 2, PTP_RC_OK);
		return 4;
	case STILL_IMAGE_DEV_RESET_REQ:
		usbt_dbg("STILL_IMAGE_DEV_RESET_REQ\n");
		return 0;
	}
	return usbt_handle_control_request(ctx, devn, ep, data, len, out);
}

static int get_bos_descriptor(struct UsbThing *ctx, int devn, struct usb_bos_descriptor *desc) {
	desc->bLength = sizeof(struct usb_bos_descriptor);
	desc->bDescriptorType = USB_DT_BOS;
	desc->wTotalLength = 0x16;
	desc->bNumDeviceCaps = 2;
	return 0;
}

// Force separate bulk transfers for each PTP phase
static int urb_splitter(struct UsbThing *ctx, int devn, int ep, void *data, int len) {
	int max_packet = 512;

	if (len > 512) {
		// Windows 11 seems to want this
		max_packet = len;
	}

	static uint32_t last_length = 0;
	if (last_length == 0) {
		vcam_read(get_cam(ctx, devn), ep, data, 4);
		ptp_read_u32(data, &last_length);
		int max = (int)last_length;
		if (max > max_packet) max = max_packet;
		if (max > len) max = len;
		vcam_read(get_cam(ctx, devn), ep, ((unsigned char *)data) + 4, max - 4);
		last_length -= (uint32_t)max;
		return max;
	} else {
		uint32_t max = last_length;
		if (max > max_packet) max = max_packet;
		if (max > len) max = len;
		vcam_read(get_cam(ctx, devn), ep, data, (int)max);
		last_length -= max;
		return (int)max;
	}
}

static int handle_bulk(struct UsbThing *ctx, int devn, int ep, void *data, int len) {
	if (ep == 0x2) {
		vcam_log("Passing h->d to vcam %d", len);
		return vcam_write(get_cam(ctx, devn), ep, (const unsigned char *)data, len);
	} else if (ep == 0x81) {
		int rc = urb_splitter(ctx, devn, ep, data, len);
		vcam_log("Reading bulk d->h to vcam (%d)", rc);
		return rc;
		//return vcam_read(get_cam(ctx, devn), ep, (unsigned char *)data, len);
	} else if (ep == 0x83) {
		// Don't respond to interrupt polling
		return -1;
	} else {
		vcam_log("Illegal endpoint 0x%x", ep);
		abort();
	}
	return 0;
}

static struct UsbThing *global_ctx = NULL;

void vcam_add_usbt_device(const char *name, int argc, char **argv) {
	if (global_ctx != NULL) {
		struct Priv *priv = (struct Priv *)global_ctx->priv_impl;
		if (global_ctx->n_devices >= PRIV_MAX_CAMS) abort();
		priv->cam[global_ctx->n_devices] = vcam_new(name, argc, argv);
		global_ctx->n_devices++;
	} else {
		vcam_panic("global_ctx NULL");
	}
}

void usbt_user_init(struct UsbThing *ctx) {
	global_ctx = ctx;
	// Add devices for libusb mode
	if (ctx->n_devices == 0) {
		ctx->priv_impl = malloc(sizeof(struct Priv));
		struct Priv *priv = (struct Priv *)ctx->priv_impl;
		priv->cam[0] = vcam_new("canon_1300d", 0, NULL);
		priv->cam[1] = vcam_new("fuji_x_h1", 1, (char *[]){"--rawconv"});
		ctx->n_devices = 2;
	}
	ctx->get_string_descriptor = usb_get_string;
	ctx->get_total_config_descriptor = usb_send_config_descriptor;
	ctx->get_config_descriptor = get_config_descriptor;
	ctx->get_qualifier_descriptor = usbt_get_device_qualifier_descriptor;
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
		return usbt_vhci_init(&ctx);
	}

	return 0;
}
