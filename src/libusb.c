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

struct _GPPortPrivateLibrary {
	int isopen;
	vcamera *vcamera;
};

struct libusb_device_handle {
	GPPort *dev;
	// Hidden impl
};

struct libusb_device {
	GPPort *dev;
};

int libusb_init(libusb_context **ctx) {
	vcam_log("vcam init\n");
	// No allocation is needed as per spec. Implementation is hidden.
	*ctx = NULL;
	return 0;
}

void libusb_exit(libusb_context *ctx) {
	vcam_log("Deinit");
}

void libusb_set_debug(libusb_context *ctx, int level) {}
libusb_device* libusb_ref_device(libusb_device *dev)	 { return dev; }
void libusb_unref_device(libusb_device *dev) {}
int libusb_get_configuration(libusb_device_handle *dev_handle, int *config) {
	*config = 0;
	return 0;
}

ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list) {
	// No value for libusb_device is necessary
	*list = malloc(sizeof(void *) * 1);
	(*list)[0] = malloc(sizeof(libusb_device));
	return 1;
}

int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc) {
	desc->bLength = sizeof(struct libusb_device_descriptor);
	desc->bDescriptorType = 1;
	desc->bNumConfigurations = 1;

	// Canon EOS
	desc->idVendor = 0x4A9;
	desc->idProduct = 0x0;

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
}

int libusb_open(libusb_device *dev, libusb_device_handle **dev_handle) {
	puts(__func__);
	*dev_handle = (libusb_device_handle *)malloc(sizeof(struct libusb_device_handle));

	struct CamConfig *conf = calloc(sizeof(struct CamConfig), 1);

	vcam_get_variant_info("canon_1300d", conf);

	GPPort *port = malloc(sizeof(GPPort));
	C_MEM(port->pl = calloc(1, sizeof(GPPortPrivateLibrary)));
	port->pl->vcamera = vcamera_new(CAM_CANON);
	port->pl->vcamera->conf = conf;

	(*dev_handle)->dev = port;

	dev->dev = port;

	if (port->pl->isopen)
		return -1;

	vcam_open(port->pl->vcamera, port->settings.usb.port);
	port->pl->isopen = 1;

	return 0;
}

libusb_device_handle *libusb_open_quick() {
	libusb_device_handle *handle;
	libusb_device *dev = malloc(sizeof(libusb_device));
	libusb_open(dev, &handle);
	return handle;
}

int libusb_get_string_descriptor_ascii(libusb_device_handle *devh, uint8_t desc_idx, unsigned char *data, int length) {
	strncpy((char *)data, "vcam", length);
}

void libusb_free_device_list(libusb_device **list, int unref_devices) {
	return;
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
	GPPort *port = dev_handle->dev;
	C_PARAMS(port && port->pl && port->pl->vcamera);

	if (endpoint == 0x2) {
		*transferred = vcam_write(port->pl->vcamera, endpoint, (unsigned char *)data, length);
		return 0;
	} else if (endpoint == 0x81) {
		*transferred = vcam_read(port->pl->vcamera, endpoint, (unsigned char *)data, length);
		return 0;
	}

	*transferred = 0;
	return 0;
}

