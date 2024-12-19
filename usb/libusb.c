// Fake libusb-v1.0 shared library for spoofing USB devices
// Copyright Daniel C
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <libusb.h>
#include "usbthing.h"

struct libusb_context {
	struct UsbThing *usb;
};

struct libusb_device {
	int devn;
	struct UsbThing *usb;
};

struct libusb_device_handle {
	struct UsbThing *usb;
	int devn;
};

int libusb_init(libusb_context **ctx) {
	usbt_dbg("libusb_init\n");
	(*ctx) = malloc(sizeof(struct libusb_context));
	struct UsbThing *usb = malloc(sizeof(struct UsbThing));
	usbt_init(usb);
	usbt_user_init(usb);
	(*ctx)->usb = usb;
	return 0;
}

void libusb_exit(libusb_context *ctx) {
	usbt_dbg("libusb_exit\n");
}

void libusb_set_debug(libusb_context *ctx, int level) {}
libusb_device *libusb_ref_device(libusb_device *dev) { return dev; }
void libusb_unref_device(libusb_device *dev) {}
int libusb_get_configuration(libusb_device_handle *dev_handle, int *config) {
	*config = 0;
	return 0;
}

ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list) {
	*list = malloc(sizeof(void *) * ctx->usb->n_devices);
	for (int i = 0; i < ctx->usb->n_devices; i++) {
		(*list)[i] = malloc(sizeof(libusb_device));
		(*list)[i]->devn = i;
		(*list)[i]->usb = ctx->usb;
	}
	return ctx->usb->n_devices;
}

int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc) {
	struct usb_device_descriptor desc2;
	dev->usb->get_device_descriptor(dev->usb, dev->devn, &desc2);

	// libusb structs aren't packed so I have to do this
	desc->bLength = desc2.bLength;
	desc->bDescriptorType = desc2.bDescriptorType;
	desc->bcdUSB = desc2.bcdUSB;
	desc->bDeviceClass = desc2.bDeviceClass;
	desc->bDeviceSubClass = desc2.bDeviceSubClass;
	desc->bDeviceProtocol = desc2.bDeviceProtocol;
	desc->bMaxPacketSize0 = desc2.bMaxPacketSize0;
	desc->idVendor = desc2.idVendor;
	desc->idProduct = desc2.idProduct;
	desc->bcdDevice = desc2.bcdDevice;
	desc->iManufacturer = desc2.iManufacturer;
	desc->iProduct = desc2.iProduct;
	desc->iSerialNumber = desc2.iSerialNumber;
	desc->bNumConfigurations = desc2.bNumConfigurations;
	return 0;
}

int libusb_get_config_descriptor(libusb_device *dev, uint8_t config_index, struct libusb_config_descriptor **config) {
	*config = (struct libusb_config_descriptor *)malloc(sizeof(struct libusb_config_descriptor));
	(*config)->bNumInterfaces = 1;

	struct libusb_interface *interface = malloc(sizeof(struct libusb_interface));
	interface->altsetting = NULL;
	interface->num_altsetting = 0;
	(*config)->interface = interface;

	uint8_t buffer[4096];
	struct usb_config_descriptor *config_desc = (struct usb_config_descriptor *)buffer;
	dev->usb->get_total_config_descriptor(dev->usb, dev->devn, config_index, config_desc);
	(*config)->bLength = config_desc->bLength;
	(*config)->bNumInterfaces = config_desc->bNumInterfaces;
	// ...

	struct libusb_interface_descriptor *altsetting = NULL;
	struct libusb_endpoint_descriptor *endpoints = NULL;
	int n_eps = 0;

	int of = config_desc->bLength;
	while (of < config_desc->wTotalLength) {
		uint8_t bLength = buffer[of + 0];
		uint8_t bDescriptorType = buffer[of + 1];
		if (bDescriptorType == USB_DT_INTERFACE) {
			if (altsetting != NULL) {
				usbt_dbg("More than one interface not implemented right now\n");
				return -1;
			}
			struct usb_interface_descriptor *desc = (struct usb_interface_descriptor *) &buffer[of];

			altsetting = malloc(sizeof(struct libusb_interface_descriptor));
			altsetting->bLength = desc->bLength;
			altsetting->bDescriptorType = desc->bDescriptorType;
			altsetting->bInterfaceNumber = desc->bInterfaceNumber;
			altsetting->bAlternateSetting = desc->bAlternateSetting;
			altsetting->bNumEndpoints = desc->bNumEndpoints;
			altsetting->bInterfaceClass = desc->bInterfaceClass;
			altsetting->bInterfaceSubClass = desc->bInterfaceSubClass;
			altsetting->bInterfaceProtocol = desc->bInterfaceProtocol;
			altsetting->iInterface = desc->iInterface;
			altsetting->endpoint = NULL;
			interface->altsetting = altsetting;
			interface->num_altsetting++;
		} else if (bDescriptorType == USB_DT_ENDPOINT) {
			if (altsetting == NULL) {
				usbt_dbg("Please have interface descriptor before endpoint descriptor\n");
				return -1;
			}
			struct usb_endpoint_descriptor *desc = (struct usb_endpoint_descriptor *)&buffer[of];

			if (endpoints == NULL) {
				endpoints = malloc(sizeof(struct libusb_endpoint_descriptor));
			} else {
				endpoints = realloc(endpoints, sizeof(struct libusb_endpoint_descriptor) * (n_eps + 1));
			}
			struct libusb_endpoint_descriptor *ep = &endpoints[n_eps];
			ep->bLength = desc->bLength;
			ep->bDescriptorType = desc->bDescriptorType;
			ep->bEndpointAddress = desc->bEndpointAddress;
			ep->bmAttributes = desc->bmAttributes;
			ep->wMaxPacketSize = desc->wMaxPacketSize;
			ep->bInterval = desc->bInterval;
			ep->bRefresh = desc->bRefresh;
			ep->bSynchAddress = desc->bSynchAddress;
			n_eps++;
		} else {
			usbt_dbg("libusb: Unhandled descriptor type %d\n", bDescriptorType);
		}
		of += bLength;
	}

	if (altsetting) {
		if (endpoints) {
			altsetting->endpoint = endpoints;
		}
		interface->altsetting = altsetting;
	}

	return 0;
}

void libusb_free_config_descriptor(struct libusb_config_descriptor *config) {
	free((void *)config->interface->altsetting->endpoint);
	free((void *)config->interface->altsetting);
	free((void *)config->interface);
	free(config);
}

int libusb_open(libusb_device *dev, libusb_device_handle **dev_handle) {
	*dev_handle = (libusb_device_handle *)malloc(sizeof(struct libusb_device_handle));
	(*dev_handle)->usb = dev->usb;
	(*dev_handle)->devn = dev->devn;
	return 0;
}

int libusb_get_string_descriptor_ascii(libusb_device_handle *dev, uint8_t desc_idx, unsigned char *data, int length) {
	char buffer[127];
	dev->usb->get_string_descriptor(dev->usb, dev->devn, desc_idx, buffer);
	strncpy((char *)data, buffer, length);
	return 0;
}

void libusb_free_device_list(libusb_device **list, int unref_devices) {

}

int libusb_set_auto_detach_kernel_driver(libusb_device_handle *dev_handle, int enable) {
	return 0;
}

int libusb_claim_interface(libusb_device_handle *dev_handle, int interface_number) {
	return 0;
}

void libusb_close(libusb_device_handle *dev_handle) {
	free(dev_handle);
}

int libusb_release_interface(libusb_device_handle *dev_handle, int interface_number) {
	return 0;
}

int libusb_bulk_transfer(libusb_device_handle *dev, unsigned char endpoint,
		unsigned char *data, int length, int *transferred, unsigned int timeout) {
	(*transferred) = dev->usb->handle_bulk_transfer(dev->usb, dev->devn, endpoint, data, length);
	return 0;
}

int libusb_control_transfer(libusb_device_handle *dev, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, unsigned char *data, uint16_t wLength, unsigned int timeout) {
	struct usb_ctrlrequest ctrl;
	ctrl.bRequest = bRequest;
	ctrl.bRequestType = bmRequestType;
	ctrl.wIndex = wIndex;
	ctrl.wLength = wLength;
	ctrl.wValue = wValue;

	if (bmRequestType & (1 << 7)) {
		// Device to host
		return dev->usb->handle_control_request(dev->usb, dev->devn, 0, &ctrl, 8, data);
	} else {
		// Host to device
		// TODO: In current api this doesn't pass the payload
		return dev->usb->handle_control_request(dev->usb, dev->devn, 0, &ctrl, 8 + wLength, data);
	}
}
