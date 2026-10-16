// Fake libusb-v1.0 shared library for spoofing USB devices
// Copyright Daniel C
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <vcam.h>
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

// ---------------------------------------------------------------------------
// Asynchronous transfer support
//
// libgphoto2 keeps a pool of interrupt transfers permanently submitted and
// waits inside libusb_handle_events_timeout() for them to complete, so the
// event endpoint (0x83) has to be serviceable from there. Completions are
// polled from the vcam interrupt queue (see vcam_readint).
// ---------------------------------------------------------------------------

struct pending_entry {
	struct libusb_transfer *t;
	// Valid when transfer->timeout > 0
	struct timeval deadline;
	struct pending_entry *next;
};

// Pending (submitted, not yet completed) transfers. Entries are self-contained
// through transfer->dev_handle, so the list is safe to share across contexts.
static struct pending_entry *pending_list = NULL;

static void tv_add_ms(struct timeval *tv, long ms) {
	tv->tv_sec += ms / 1000;
	tv->tv_usec += (ms % 1000) * 1000;
	if (tv->tv_usec > 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
}

// a <= b
static int tv_le(struct timeval a, struct timeval b) {
	if (a.tv_sec != b.tv_sec) return a.tv_sec < b.tv_sec;
	return a.tv_usec <= b.tv_usec;
}

// Remove the entry at *pp from the list, set the completion status on the
// transfer and fire its callback. The callback may re-submit or free the
// transfer, so the entry is unlinked first.
static void finish_entry(struct pending_entry **pp, int rc) {
	struct pending_entry *e = *pp;
	struct libusb_transfer *t = e->t;
	*pp = e->next;
	free(e);

	if (rc >= 0) {
		t->actual_length = rc;
		t->status = LIBUSB_TRANSFER_COMPLETED;
	} else {
		t->actual_length = 0;
		switch (rc) {
		case LIBUSB_ERROR_TIMEOUT:
			t->status = LIBUSB_TRANSFER_TIMED_OUT;
			break;
		case LIBUSB_ERROR_NO_DEVICE:
			t->status = LIBUSB_TRANSFER_NO_DEVICE;
			break;
		case LIBUSB_ERROR_OVERFLOW:
			t->status = LIBUSB_TRANSFER_OVERFLOW;
			break;
		default:
			t->status = LIBUSB_TRANSFER_ERROR;
			break;
		}
	}

	if (t->callback != NULL) t->callback(t);
}

// Drive pending transfers for up to `tv` (NULL = until at least one transfer
// completes, capped at 500ms). Events are polled from the interrupt queue.
static int handle_events_impl(struct libusb_context *ctx, struct timeval *tv) {
	(void)ctx;
	struct timeval deadline, now;

	gettimeofday(&deadline, NULL);
	if (tv != NULL) {
		deadline.tv_sec += tv->tv_sec;
		deadline.tv_usec += tv->tv_usec;
	} else {
		deadline.tv_usec += 500 * 1000;
	}
	if (deadline.tv_usec > 1000000) {
		deadline.tv_usec -= 1000000;
		deadline.tv_sec++;
	}

	while (1) {
		if (pending_list == NULL) return LIBUSB_SUCCESS;

		gettimeofday(&now, NULL);
		if (tv_le(now, deadline) == 0) break; // outer timeout expired

		int progressed = 0;
		struct pending_entry **pp = &pending_list;
		while (*pp != NULL) {
			struct pending_entry *e = *pp;
			struct libusb_transfer *t = e->t;

			// Per-transfer deadline (only for transfers with a finite timeout)
			if (t->timeout > 0) {
				gettimeofday(&now, NULL);
				if (tv_le(e->deadline, now)) {
					finish_entry(pp, LIBUSB_ERROR_TIMEOUT);
					progressed = 1;
					continue;
				}
			}

			int rc;
			if (t->type == LIBUSB_TRANSFER_TYPE_INTERRUPT && !(t->endpoint & LIBUSB_ENDPOINT_IN)) {
				// OUT interrupt: the responder never queues anything for us
				rc = 0;
			} else {
				// Returns byte count, GP_ERROR_TIMEOUT when nothing is due
				rc = t->dev_handle->usb->handle_bulk_transfer(t->dev_handle->usb, t->dev_handle->devn, t->endpoint, t->buffer, t->length);
			}

			if (rc == GP_ERROR_TIMEOUT) {
				pp = &e->next;
				continue;
			}

			finish_entry(pp, rc);
			progressed = 1;
		}

		// Bare handle_events() is used by gphoto2 to reap completed
		// transfers - return as soon as one round has produced results.
		if (tv == NULL && progressed) break;
		if (!progressed) usleep(1000 * 2);
	}

	return LIBUSB_SUCCESS;
}

int libusb_handle_events_timeout(libusb_context *ctx, struct timeval *tv) {
	return handle_events_impl(ctx, tv);
}

int libusb_handle_events(libusb_context *ctx) {
	return handle_events_impl(ctx, NULL);
}

struct libusb_transfer *libusb_alloc_transfer(int iso_packets) {
	if (iso_packets < 0) return NULL;
	return calloc(1, sizeof(struct libusb_transfer) + sizeof(struct libusb_iso_packet_descriptor) * (size_t)iso_packets);
}

void libusb_free_transfer(struct libusb_transfer *transfer) {
	if (transfer == NULL) return;
	if ((transfer->flags & LIBUSB_TRANSFER_FREE_BUFFER) && transfer->buffer != NULL) free(transfer->buffer);
	free(transfer);
}

int libusb_submit_transfer(struct libusb_transfer *transfer) {
	if (transfer == NULL || transfer->dev_handle == NULL) return LIBUSB_ERROR_INVALID_PARAM;
	if (transfer->type != LIBUSB_TRANSFER_TYPE_INTERRUPT && transfer->type != LIBUSB_TRANSFER_TYPE_BULK) {
		return LIBUSB_ERROR_NOT_SUPPORTED;
	}

	struct pending_entry *e = malloc(sizeof(struct pending_entry));
	if (e == NULL) return LIBUSB_ERROR_NO_MEM;
	e->t = transfer;
	if (transfer->timeout > 0) {
		gettimeofday(&e->deadline, NULL);
		tv_add_ms(&e->deadline, (long)transfer->timeout);
	} else {
		e->deadline.tv_sec = 0;
		e->deadline.tv_usec = 0;
	}
	e->next = pending_list;
	pending_list = e;

	return LIBUSB_SUCCESS;
}

// Completion is delivered synchronously: gphoto2 cancels its pool of
// interrupt transfers on port close and expects them to be gone afterwards.
int libusb_cancel_transfer(struct libusb_transfer *transfer) {
	if (transfer == NULL) return LIBUSB_ERROR_INVALID_PARAM;

	struct pending_entry **pp = &pending_list;
	while (*pp != NULL) {
		if ((*pp)->t == transfer) break;
		pp = &(*pp)->next;
	}
	if (*pp == NULL) return LIBUSB_ERROR_NOT_FOUND;

	struct pending_entry *e = *pp;
	*pp = e->next;
	free(e);

	transfer->actual_length = 0;
	transfer->status = LIBUSB_TRANSFER_CANCELLED;
	if (transfer->callback != NULL) transfer->callback(transfer);

	return LIBUSB_SUCCESS;
}

// ---------------------------------------------------------------------------
// Remaining libusb-1.0 entry points used by libgphoto2's USB port backend
// ---------------------------------------------------------------------------

uint8_t libusb_get_bus_number(libusb_device *dev) {
	(void)dev;
	return 1;
}

uint8_t libusb_get_device_address(libusb_device *dev) {
	return (uint8_t)(dev->devn + 1);
}

libusb_device *libusb_get_device(libusb_device_handle *dev_handle) {
	libusb_device *dev = malloc(sizeof(struct libusb_device));
	if (dev == NULL) return NULL;
	dev->devn = dev_handle->devn;
	dev->usb = dev_handle->usb;
	return dev;
}

int libusb_set_configuration(libusb_device_handle *dev_handle, int configuration) {
	(void)dev_handle;
	(void)configuration;
	return LIBUSB_SUCCESS;
}

int libusb_reset_device(libusb_device_handle *dev_handle) {
	(void)dev_handle;
	return LIBUSB_SUCCESS;
}

int libusb_attach_kernel_driver(libusb_device_handle *dev_handle, int interface_number) {
	(void)dev_handle;
	(void)interface_number;
	return LIBUSB_SUCCESS;
}

int libusb_set_option(libusb_context *ctx, enum libusb_option option, ...) {
	(void)ctx;
	(void)option;
	return LIBUSB_SUCCESS;
}

int libusb_wrap_sys_device(libusb_context *ctx, intptr_t sys_dev, libusb_device_handle **dev_handle) {
	(void)ctx;
	(void)sys_dev;
	(void)dev_handle;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}

const char *libusb_strerror(int errcode) {
	switch (errcode) {
	case LIBUSB_SUCCESS: return "Success";
	case LIBUSB_ERROR_IO: return "Input/Output Error";
	case LIBUSB_ERROR_INVALID_PARAM: return "Invalid parameter";
	case LIBUSB_ERROR_ACCESS: return "Access denied (insufficient permissions)";
	case LIBUSB_ERROR_NO_DEVICE: return "No such device (it may have been disconnected)";
	case LIBUSB_ERROR_NOT_FOUND: return "Entity not found";
	case LIBUSB_ERROR_BUSY: return "Resource busy";
	case LIBUSB_ERROR_TIMEOUT: return "Operation timed out";
	case LIBUSB_ERROR_OVERFLOW: return "Overflow";
	case LIBUSB_ERROR_INTERRUPTED: return "System call interrupted (perhaps due to signal)";
	case LIBUSB_ERROR_NO_MEM: return "Insufficient memory";
	case LIBUSB_ERROR_NOT_SUPPORTED: return "Operation not supported or unimplemented on this platform";
	default: return "Unknown error";
	}
}

const char *libusb_error_name(int error_code) {
	switch (error_code) {
	case LIBUSB_SUCCESS: return "LIBUSB_SUCCESS";
	case LIBUSB_ERROR_IO: return "LIBUSB_ERROR_IO";
	case LIBUSB_ERROR_INVALID_PARAM: return "LIBUSB_ERROR_INVALID_PARAM";
	case LIBUSB_ERROR_ACCESS: return "LIBUSB_ERROR_ACCESS";
	case LIBUSB_ERROR_NO_DEVICE: return "LIBUSB_ERROR_NO_DEVICE";
	case LIBUSB_ERROR_NOT_FOUND: return "LIBUSB_ERROR_NOT_FOUND";
	case LIBUSB_ERROR_BUSY: return "LIBUSB_ERROR_BUSY";
	case LIBUSB_ERROR_TIMEOUT: return "LIBUSB_ERROR_TIMEOUT";
	case LIBUSB_ERROR_OVERFLOW: return "LIBUSB_ERROR_OVERFLOW";
	case LIBUSB_ERROR_INTERRUPTED: return "LIBUSB_ERROR_INTERRUPTED";
	case LIBUSB_ERROR_NO_MEM: return "LIBUSB_ERROR_NO_MEM";
	case LIBUSB_ERROR_NOT_SUPPORTED: return "LIBUSB_ERROR_NOT_SUPPORTED";
	default: return "LIBUSB_ERROR_UNKNOWN";
	}
}

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

int libusb_interrupt_transfer(libusb_device_handle *dev, unsigned char endpoint, unsigned char *data, int length, int *transferred, unsigned int timeout) {
	if (transferred != NULL) *transferred = 0;

	if (endpoint == 0x83) {
		// Interrupt IN: poll the event queue until an event is due or the
		// timeout (0 = wait forever) expires. GP_ERROR_TIMEOUT is returned
		// by the queue when no event has triggered yet.
		struct timeval deadline, now;
		gettimeofday(&deadline, NULL);
		tv_add_ms(&deadline, (long)timeout);

		while (1) {
			int rc = dev->usb->handle_bulk_transfer(dev->usb, dev->devn, endpoint, data, length);
			if (rc >= 0) {
				if (transferred != NULL) *transferred = rc;
				return LIBUSB_SUCCESS;
			}
			if (rc != GP_ERROR_TIMEOUT) return LIBUSB_ERROR_IO;

			if (timeout > 0) {
				gettimeofday(&now, NULL);
				if (tv_le(deadline, now)) return LIBUSB_ERROR_TIMEOUT;
			}
			usleep(1000 * 2);
		}
	}

	int rc = dev->usb->handle_bulk_transfer(dev->usb, dev->devn, endpoint, data, length);
	if (rc < 0) {
		return LIBUSB_ERROR_IO;
	}
	if (transferred != NULL) *transferred = rc;
	return LIBUSB_SUCCESS;
}

int libusb_get_max_packet_size(libusb_device *dev, unsigned char endpoint) {
	struct libusb_config_descriptor *config;
	int r = libusb_get_config_descriptor(dev, 0, &config);
	if (r < 0) return r;
	int ret = LIBUSB_ERROR_NOT_FOUND;
	for (int i = 0; i < config->interface[0].altsetting[0].bNumEndpoints; i++) {
		const struct libusb_endpoint_descriptor *ep = &config->interface[0].altsetting[0].endpoint[i];
		if (ep->bEndpointAddress == endpoint) {
			ret = ep->wMaxPacketSize;
			break;
		}
	}
	libusb_free_config_descriptor(config);
	return ret;
}

int libusb_set_interface_alt_setting(libusb_device_handle *dev_handle, int interface_number, int alternate_setting) {
	(void)dev_handle; (void)interface_number; (void)alternate_setting;
	return 0;
}

int libusb_clear_halt(libusb_device_handle *dev_handle, unsigned char endpoint) {
	(void)dev_handle; (void)endpoint;
	return 0;
}

int libusb_kernel_driver_active(libusb_device_handle *dev_handle, int interface_number) {
	(void)dev_handle; (void)interface_number;
	return 0;
}

int libusb_detach_kernel_driver(libusb_device_handle *dev_handle, int interface_number) {
	(void)dev_handle; (void)interface_number;
	return 0;
}
