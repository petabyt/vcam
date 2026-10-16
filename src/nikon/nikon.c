// Nikon PTP emulator
// Copyright Daniel C - GNU Lesser General Public License v2.1
//
// Emulates Nikon DSLR / Z-series bodies closely enough for libgphoto2's
// ptp2 camlib (and other libusb1 PTP clients) to detect, configure and
// remote-control them through the usb-vcam libusb shim. Model prop tables
// live in nikon/models/ and are transcribed from libgphoto2's camera dumps
// (camlibs/ptp2/cameras/nikon-*.txt).
#include <stdlib.h>
#include <string.h>
#include <vcam.h>
#include <nikon.h>

// Events advertised by D7500/Z-series bodies. The capture-critical vendor
// events (ObjectAddedInSDRAM, CaptureCompleteRecInSdram) are what gphoto2's
// camera_nikon_capture() waits for.
static const uint16_t nikon_events[] = {
	0x4002, // ObjectAdded
	0x4003, // ObjectRemoved
	0x4006, // DevicePropChanged
	0x400D, // CaptureComplete
	PTP_EC_Nikon_ObjectAddedInSDRAM,
	PTP_EC_Nikon_CaptureCompleteRecInSdram,
	0xC10C, // LiveViewStateChanged
};

// Format lists from the D7500/Z dumps: JPEG + Undefined for capture,
// plus the display-only formats gphoto2 shows in --summary
static const uint16_t nikon_capture_formats[] = {0x3801, 0x3000};
static const uint16_t nikon_image_formats[] = {0x3801, 0x3000, 0x3001, 0x3802, 0x3002, 0x300B, 0xb982};

// DeviceInfo DevicePropertiesSupported as real bodies report it: the PTP
// standard properties plus the MTP trio. The Nikon vendor properties
// (0xd015..) are deliberately absent - gphoto2 fetches those through
// GetVendorPropCodes (0x90CA) during init and appends them itself.
static const uint16_t nikon_base_deviceprops[] = {
	0x5001, 0x5003, 0x5004, 0x5005, 0x5007, 0x5008, 0x500a, 0x500b,
	0x500c, 0x500d, 0x500e, 0x500f, 0x5010, 0x5011, 0x5013, 0x5018,
	0x501c, 0x501e, 0x501f,
	0xd303, 0xd406, 0xd407,
};

// Codes that stay out of the 0x90CA/0x9439 vendor lists (they are part of
// the base DeviceInfo list)
int nikon_is_base_deviceprop(uint16_t code) {
	return code == 0xd303 || code == 0xd406 || code == 0xd407;
}

static const struct NikonModelProfile nikon_models[] = {
	// Storage geometry transcribed from the libgphoto2 dumps
	{ "nikon_d7500", "D7500", "V1.00", "0000000000000000000000000nnnnnnn", 0x0440, 0x64,
	  127831900160ull, 127816564736ull, 16813, 0, nikon_register_model_d7500 },
	{ "nikon_z6_2", "Z 6_2", "V1.00", "00000000000000000000000006001924", 0x044c, 0x6a,
	  60196651008ull, 59647983616ull, 16251, 1, nikon_register_model_z6_2 },
};

int nikon_init_cam(vcam *cam, const char *name, int argc, char **argv) {
	const struct NikonModelProfile *profile = NULL;
	for (unsigned i = 0; i < sizeof(nikon_models) / sizeof(nikon_models[0]); i++) {
		if (!strcmp(name, nikon_models[i].name)) {
			profile = &nikon_models[i];
			break;
		}
	}
	if (profile == NULL) return -1;

	cam->priv = calloc(1, sizeof(struct Nikon));
	struct Nikon *p = nikon(cam);
	p->profile = profile;
	p->capture_count = 1;
	p->liveview_status = 0;

	// Generic flags (--fs, --dump, ...)
	for (int i = 0; i < argc; i++) {
		if (vcam_parse_args(cam, argc, argv, &i)) continue;
		vcam_log("Unknown nikon argument '%s'", argv[i]);
		return -1;
	}

	strcpy(cam->manufac, "Nikon Corporation");
	cam->vendor_id = 0x04b0;
	cam->product_id = profile->product_id;
	strcpy(cam->model, profile->model);
	strcpy(cam->version, profile->version);
	strcpy(cam->serial, profile->serial);
	strcpy(cam->extension, "Microsoft.com/DeviceServices: 1.0");
	cam->vendor_extension_id = PTP_VENDOR_NIKON;
	cam->vendor_extension_version = profile->extver;

	cam->events = nikon_events;
	cam->n_events = sizeof(nikon_events) / sizeof(nikon_events[0]);
	cam->deviceprops = nikon_base_deviceprops;
	cam->n_deviceprops = sizeof(nikon_base_deviceprops) / sizeof(nikon_base_deviceprops[0]);
	cam->capture_formats = nikon_capture_formats;
	cam->n_capture_formats = sizeof(nikon_capture_formats) / sizeof(nikon_capture_formats[0]);
	cam->image_formats = nikon_image_formats;
	cam->n_image_formats = sizeof(nikon_image_formats) / sizeof(nikon_image_formats[0]);

	// Model-specific device property table (replaces the standard defaults
	// for shared codes like BatteryLevel/ImageSize)
	profile->register_model(cam);

	nikon_register_ops(cam, profile->vendorcodes);

	cam->lookup_virtual_object = nikon_lookup_virtual_object;

	return 0;
}
