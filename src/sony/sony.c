// Sony PTP emulator
// Copyright Daniel C - GNU Lesser General Public License v2.1
//
// Emulates Sony Alpha bodies (SDIO remote-control protocol, mode 3) closely
// enough for libgphoto2's ptp2 camlib and other libusb1 PTP clients to
// detect, configure and remote-control them through the usb-vcam libusb
// shim. Model prop tables live in sony/models/ and are transcribed from
// libgphoto2's camera dumps (camlibs/ptp2/cameras/sony-*.txt).
#include <stdlib.h>
#include <string.h>
#include <vcam.h>
#include <sony.h>

// Events advertised by mode-3 bodies: the standard set plus the Sony vendor
// events gphoto2's capture/wait-event flows react to
static const uint16_t sony_events[] = {
	0x4002, // ObjectAdded
	0x4003, // ObjectRemoved
	0x4006, // DevicePropChanged
	0x400D, // CaptureComplete
	0xC201, // Sony ObjectAdded (param = object handle)
	0xC203, // Sony DevicePropChanged (param = propcode)
};

// Display formats from the dumps; capture formats are empty on mode-3 bodies
// (a non-NULL pointer with count 0 keeps the generic handler from
// substituting the standard default list)
static const uint16_t sony_image_formats[] = {0x3001, 0x3801, 0xb101, 0x300b, 0xb982, 0xb110};
static const uint16_t sony_no_formats[] = {0};

// Base DeviceInfo DevicePropertiesSupported: the PTP standard codes the body
// serves through the 0x9209 cache. The vendor properties (0xD2xx) and the
// controls (0xD2Cx) are revealed through SDIO_GetExtDeviceInfo (0x9202).
static const uint16_t sony_base_deviceprops[] = {
	0x5004, 0x5005, 0x5007, 0x500a, 0x500b, 0x500c, 0x5010, 0x500e, 0x5013,
};

// Control codes (0x9202 second array): listed in DeviceProps by gphoto2 but
// never answered by 0x9209 - write-only buttons/ranges
static const uint16_t sony_controls[] = {
	0xd2c1, 0xd2c2, 0xd2c3, 0xd2c4, 0xd2c7, 0xd2c8, 0xd2c9,
	0xd2cd, 0xd2ce, 0xd2cf, 0xd2d0,
};

static const struct SonyModelProfile sony_models[] = {
	{ "sony_a7m4", "ILCE-7M4", "1.00", "00000000000000001234567891234567", 0x0da7,
	  sony_base_deviceprops, sizeof(sony_base_deviceprops) / sizeof(sony_base_deviceprops[0]),
	  sony_controls, sizeof(sony_controls) / sizeof(sony_controls[0]),
	  sony_register_model_a7m4 },
	{ "sony_a7r5", "ILCE-7RM5", "2.02", "0000000000000000nnnnnnnnnnnnnnnn", 0x0e0c,
	  sony_base_deviceprops, sizeof(sony_base_deviceprops) / sizeof(sony_base_deviceprops[0]),
	  sony_controls, sizeof(sony_controls) / sizeof(sony_controls[0]),
	  sony_register_model_a7r5 },
};

int sony_init_cam(vcam *cam, const char *name, int argc, char **argv) {
	const struct SonyModelProfile *profile = NULL;
	for (unsigned i = 0; i < sizeof(sony_models) / sizeof(sony_models[0]); i++) {
		if (!strcmp(name, sony_models[i].name)) {
			profile = &sony_models[i];
			break;
		}
	}
	if (profile == NULL) return -1;

	cam->priv = calloc(1, sizeof(struct Sony));
	struct Sony *p = sony(cam);
	p->profile = profile;
	p->capture_count = 1;
	p->focus_found = 1;
	p->object_in_memory = 0;

	// Generic flags (--fs, --dump, ...)
	for (int i = 0; i < argc; i++) {
		if (vcam_parse_args(cam, argc, argv, &i)) continue;
		vcam_log("Unknown sony argument '%s'", argv[i]);
		return -1;
	}

	// Live view object is always available (PC-control mode bodies stream
	// it at 0xffffc002 as soon as they are in remote mode)
	if (sony_load_jpeg(cam, &p->lv_data, &p->lv_size, &p->lv_raw))
		return -1;

	strcpy(cam->manufac, "Sony Corporation");
	cam->vendor_id = 0x054c;
	cam->product_id = profile->product_id;
	strcpy(cam->model, profile->model);
	strcpy(cam->version, profile->version);
	strcpy(cam->serial, profile->serial);
	strcpy(cam->extension, "Sony PTP Extensions");
	cam->vendor_extension_id = PTP_VENDOR_SONY;
	cam->vendor_extension_version = 0x64;

	cam->events = sony_events;
	cam->n_events = sizeof(sony_events) / sizeof(sony_events[0]);
	cam->deviceprops = profile->deviceprops;
	cam->n_deviceprops = profile->n_deviceprops;
	cam->capture_formats = sony_no_formats; // empty Capture Formats, like the dumps
	cam->n_capture_formats = 0;
	cam->image_formats = sony_image_formats;
	cam->n_image_formats = sizeof(sony_image_formats) / sizeof(sony_image_formats[0]);

	// Model-specific device property table (replaces the standard defaults
	// for shared codes)
	profile->register_model(cam);

	// Sony SDIO opcode handlers + object/storage overrides
	sony_register_ops(cam);

	cam->lookup_virtual_object = sony_lookup_virtual_object;

	return 0;
}
