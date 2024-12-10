// Fake libusb-v1.0 .so spoofer for vcam
// Copyright Daniel C - GNU Lesser General Public License v2.1  
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <libusb.h>

#include <vcam.h>

struct libusb_context {

};

int libusb_init(libusb_context **ctx) {
	vcam_log("libusb_init\n");
	// No allocation is needed as per spec. Implementation is hidden.
	(*ctx) = malloc(sizeof(struct libusb_context));
	return 0;
}

void libusb_exit(libusb_context *ctx) {
	vcam_log("libusb_exit\n");
}

void libusb_set_debug(libusb_context *ctx, int level) {}
libusb_device* libusb_ref_device(libusb_device *dev) { return dev; }
void libusb_unref_device(libusb_device *dev) {}
int libusb_get_configuration(libusb_device_handle *dev_handle, int *config) {
	*config = 0;
	return 0;
}

ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list) {
	*list = malloc(sizeof(void *) * ctx->length);
	for (int i = 0; i < ctx->length; i++) {
		(*list)[i] = malloc(sizeof(libusb_device));
		(*list)[i]->cam = ctx->cams[i];
	}
	return ctx->length;
}

int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc) {
	desc->bLength = sizeof(struct libusb_device_descriptor);
	desc->bDescriptorType = 1;
	desc->bNumConfigurations = 1;

	desc->idVendor = dev->cam->vendor;
	desc->idProduct = dev->cam->product;

	return 0;
}

int libusb_get_config_descriptor(libusb_device *dev, uint8_t config_index, struct libusb_config_descriptor **config) {
	*config = (struct libusb_config_descriptor *)malloc(sizeof(struct libusb_config_descriptor));
	(*config)->bNumInterfaces = 1;

	struct libusb_interface *interface = malloc(sizeof(struct libusb_interface));
	interface->num_altsetting = 1;
	(*config)->interface = interface;

	struct libusb_interface_descriptor *altsetting = malloc(sizeof(struct libusb_interface_descriptor));
	altsetting->bInterfaceClass = LIBUSB_CLASS_IMAGE;
	interface->altsetting = altsetting;

	altsetting->bNumEndpoints = 3;
	struct libusb_endpoint_descriptor *ep = malloc(sizeof(struct libusb_endpoint_descriptor) * 3);
	altsetting->endpoint = ep;

	ep[0].bmAttributes = LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK;
	ep[0].bEndpointAddress = 0x81;

	ep[1].bmAttributes = LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK;
	ep[1].bEndpointAddress = 0x02;

	ep[2].bmAttributes = LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT;
	ep[2].bEndpointAddress = 0x82;

	return 0;
}

void libusb_free_config_descriptor(struct libusb_config_descriptor *config) {
	free(config);
	// TODO: free memory
}

int libusb_open(libusb_device *dev, libusb_device_handle **dev_handle) {
	*dev_handle = (libusb_device_handle *)malloc(sizeof(struct libusb_device_handle));
	(*dev_handle)->cam = dev->cam;
	return 0;
}

libusb_device_handle *libusb_open_quick(void) {
	libusb_device_handle *handle;
	libusb_device *dev = malloc(sizeof(libusb_device));
	libusb_open(dev, &handle);
	return handle;
}

int libusb_get_string_descriptor_ascii(libusb_device_handle *devh, uint8_t desc_idx, unsigned char *data, int length) {
	strncpy((char *)data, "vcam", length);
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

int libusb_bulk_transfer(libusb_device_handle *dev_handle, unsigned char endpoint,
		unsigned char *data, int length, int *transferred, unsigned int timeout) {
	if (endpoint == 0x2) {
		*transferred = vcam_write(dev_handle->cam, endpoint, (unsigned char *)data, length);
		return 0;
	} else if (endpoint == 0x81) {
		*transferred = vcam_read(dev_handle->cam, endpoint, (unsigned char *)data, length);
		return 0;
	}

	*transferred = 0;
	return 0;
}

int libusb_control_transfer(libusb_device_handle *dev_handle, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, unsigned char *data, uint16_t wLength, unsigned int timeout) {
	return 0;
}
