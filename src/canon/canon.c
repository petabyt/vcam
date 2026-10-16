// Emulator for non-standard Canon PTP
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <vcam.h>
#include <canon.h>

void canon_register_d4_hidden(vcam *cam);
void canon_register_base_eos(vcam *cam);
void canon_register_object_ops(vcam *cam);
int canon_register_capture_props(vcam *cam);

// CaptureDestination (0xD11C): gphoto2 treats 4 as the SDRAM/remote target
#define EOS_CAPTUREDEST_SDRAM 0x4
#define EOS_MAIN_STORAGE_ID 0x00010001
#define EOS_OFC_JPEG 0x3801

#define EOS_LV_JPEG "bin/eos_liveview.jpg"
#define EOS_EVENTS_BIN "bin/eos/eos_events.bin"

// Model profiles. Models with a generated dump profile (scripts/eos_dump2c.py)
// register their own props and advertised ops; the legacy models use the
// hand-written superset in canon_register_base_eos().
struct CanonModelProfile {
	const char *name;   // CLI tag
	const char *model;  // DeviceInfo model string
	const char *version;
	const char *serial;
	uint16_t product_id;
	int (*register_model)(vcam *cam);
};

int canon_register_model_5d3(vcam *cam);
int canon_register_model_90d(vcam *cam);
int canon_register_model_200d(vcam *cam);
int canon_register_model_r(vcam *cam);
int canon_register_model_g7x3(vcam *cam);

static const struct CanonModelProfile canon_models[] = {
	{ "canon_1300d", "Canon EOS Rebel T6", "3-1.2.0", "828af56", 0x32b4, NULL },
	{ "eos_m", "Canon EOS M", "1.0.0", "xxxx", 0, NULL },
	{ "canon_5d3", "Canon EOS 5D Mark III", "3-1.2.1", "4ff1a05e8014efd", 0x323a, canon_register_model_5d3 },
	{ "canon_90d", "Canon EOS 90D", "3-1.0.0", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 0x32ea, canon_register_model_90d },
	{ "canon_200d", "Canon EOS Rebel SL2", "3-1.0.1", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 0x32cc, canon_register_model_200d },
	{ "canon_r", "Canon EOS R", "3-1.6.0", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 0x32da, canon_register_model_r },
	{ "canon_g7x3", "Canon PowerShot G7 X Mark III", "3-1.3.2", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 0x32f0, canon_register_model_g7x3 },
};

int canon_init_cam(vcam *cam, const char *name, int argc, char **argv) {
	cam->priv = malloc(sizeof(struct CanonBase));
	struct CanonBase *p = canon(cam);

	p->first_events = 0;
	p->lv_ready = 0;
	p->calls_to_liveview = 0;
	p->capture_destination = 0x2; // card (gphoto2 maps anything != 4 to card)
	memset(&p->sdram, 0, sizeof(p->sdram));
	cam->lookup_virtual_object = canon_lookup_virtual_object;

	for (int i = 0; i < argc; i++) {
		if (vcam_parse_args(cam, argc, argv, &i)) continue;
		if (!strcmp(argv[i], "--ssdp-modern")) {
			p->ssdp_modern = 1;
			continue;
		}
		// ...
	}

	const struct CanonModelProfile *profile = NULL;
	for (unsigned i = 0; i < sizeof(canon_models) / sizeof(canon_models[0]); i++) {
		if (!strcmp(name, canon_models[i].name)) {
			profile = &canon_models[i];
			break;
		}
	}
	if (profile == NULL) return -1;

	strcpy(cam->manufac, "Canon Inc.");
	cam->vendor_id = 0x4a9;
	strcpy(cam->model, profile->model);
	strcpy(cam->version, profile->version);
	strcpy(cam->serial, profile->serial);
	cam->product_id = profile->product_id;

	ptp_register_mtp_props(cam);
	ptp_register_mtp_opcodes(cam);

	if (profile->register_model != NULL) {
		// Dump-driven model: generated props + only the advertised ops
		profile->register_model(cam);
		canon_ensure_capture_props(cam);
	} else {
		// Legacy hand-written registration
		if (!strcmp(name, "canon_1300d")) canon_register_d4_hidden(cam);
		canon_register_capture_props(cam);
		canon_register_base_eos(cam);
		canon_register_object_ops(cam);
	}

	return 0;
}

// --- EOS GetEvent (0x9116) event emission -------------------------------

// PropValueChanged entry: {u32 size, u32 0xc189, u32 dpc, u32 value}
static void eos_notify_prop_changed(vcam *cam, uint32_t dpc, int when) {
	uint8_t entry[16];
	int len = 0;
	void *val = vcam_get_prop_data(cam, (int)dpc, &len);

	ptp_write_u32(entry + 0, sizeof(entry));
	ptp_write_u32(entry + 4, PTP_EC_EOS_PropValueChanged);
	ptp_write_u32(entry + 8, dpc);
	if (val != NULL && len == 4) {
		memcpy(entry + 12, val, 4);
	} else {
		memset(entry + 12, 0, 4);
	}
	ptp_inject_event_data(cam, when, PTP_EC_EOS_PropValueChanged, entry, sizeof(entry), PTP_CH_EOS);
}

// InfoCheckComplete marker, as emitted by real cameras between event groups
static void eos_notify_info_check(vcam *cam, uint32_t marker, int when) {
	uint8_t entry[12];
	ptp_write_u32(entry + 0, sizeof(entry));
	ptp_write_u32(entry + 4, PTP_EC_EOS_InfoCheckComplete);
	ptp_write_u32(entry + 8, marker);
	ptp_inject_event_data(cam, when, PTP_EC_EOS_InfoCheckComplete, entry, sizeof(entry), PTP_CH_EOS);
}

// ObjectAddedEx entry, offsets per gphoto2's PTP_cee_OA_*:
// handle@8, storage@c, ofc@10 (u16), size@0x1c, parent@0x20, name@0x28
static void eos_notify_object_added(vcam *cam, uint32_t handle, uint32_t storage,
		uint32_t parent, uint32_t ofc, uint32_t size, const char *name, int when) {
	uint32_t namelen = strlen(name) + 1;
	uint32_t entrysize = 0x28 + namelen;
	uint8_t entry[0x28 + 64];
	if (entrysize > sizeof(entry)) return;

	memset(entry, 0, sizeof(entry));
	ptp_write_u32(entry + 0, entrysize);
	ptp_write_u32(entry + 4, PTP_EC_EOS_ObjectAddedEx);
	ptp_write_u32(entry + 8, handle);
	ptp_write_u32(entry + 12, storage);
	ptp_write_u16(entry + 16, (uint16_t)ofc);
	ptp_write_u32(entry + 28, size);
	ptp_write_u32(entry + 32, parent);
	memcpy(entry + 40, name, namelen);

	ptp_inject_event_data(cam, when, PTP_EC_EOS_ObjectAddedEx, entry, (int)entrysize, PTP_CH_EOS);
}

// RequestObjectTransfer entry, offsets per gphoto2's PTP_cee_OI_*:
// handle@8, ofc@c (u16), size@0x14, name@0x1c
static void eos_notify_request_transfer(vcam *cam, uint32_t handle, uint32_t ofc,
		uint32_t size, const char *name, int when) {
	uint32_t namelen = strlen(name) + 1;
	uint32_t entrysize = 0x1c + namelen;
	uint8_t entry[0x1c + 64];
	if (entrysize > sizeof(entry)) return;

	memset(entry, 0, sizeof(entry));
	ptp_write_u32(entry + 0, entrysize);
	ptp_write_u32(entry + 4, PTP_EC_EOS_RequestObjectTransfer);
	ptp_write_u32(entry + 8, handle);
	ptp_write_u16(entry + 12, (uint16_t)ofc);
	ptp_write_u32(entry + 20, size);
	memcpy(entry + 28, name, namelen);

	ptp_inject_event_data(cam, when, PTP_EC_EOS_RequestObjectTransfer, entry, (int)entrysize, PTP_CH_EOS);
}

// OLCInfoChanged entry reporting a Button state, as parsed by gphoto2's
// EOS M / PowerShot trigger: {u32 size, u32 0xC1A5, u32 len(=size-8),
// u16 mask 0x0001 (Button), u16 unk, u16 button}
static void eos_notify_olc_button(vcam *cam, uint16_t button, int when) {
	uint8_t entry[18];
	ptp_write_u32(entry + 0, sizeof(entry));
	ptp_write_u32(entry + 4, PTP_EC_EOS_OLCInfoChanged);
	ptp_write_u32(entry + 8, sizeof(entry) - 8);
	ptp_write_u16(entry + 12, 0x0001); // Button mask
	ptp_write_u16(entry + 14, 0);
	ptp_write_u16(entry + 16, button);
	ptp_inject_event_data(cam, when, PTP_EC_EOS_OLCInfoChanged, entry, sizeof(entry), PTP_CH_EOS);
}

// --- Remote capture -----------------------------------------------------

// Creates a virtual card capture object in a DCIM/xxxGPHOT directory, mirroring
// the standard capture op in ops.c. Returns the new dirent, NULL on failure.
static struct ptp_dirent *eos_create_card_object(vcam *cam, struct ptp_dirent *src) {
	struct ptp_dirent *dir, *dcim = NULL;
	char buf[16];

	// identify the DCIM dir below root
	dir = cam->first_dirent;
	while (dir) {
		if (!strcmp(dir->name, "DCIM") && dir->parent && !dir->parent->id)
			dcim = dir;
		dir = dir->next;
	}
	if (!dcim) return NULL;

	// find or create the nnnGPHOT directory (DCIM standard, nnn = 100-999)
	int capcnt = 98;
	sprintf(buf, "%03dGPHOT", 100 + ((capcnt / 100) % 900));
	dir = cam->first_dirent;
	while (dir) {
		if (!strcmp(dir->name, buf) && dir->parent == dcim)
			break;
		dir = dir->next;
	}
	if (!dir) {
		dir = malloc(sizeof(struct ptp_dirent));
		dir->id = ++cam->ptp_objectid;
		dir->fsname = (char *)"virtual";
		dir->stbuf = dcim->stbuf; // only S_ISDIR is used
		dir->parent = dcim;
		dir->next = cam->first_dirent;
		dir->name = strdup(buf);
		cam->first_dirent = dir;
	}

	struct ptp_dirent *newcur = malloc(sizeof(struct ptp_dirent));
	newcur->id = ++cam->ptp_objectid;
	newcur->fsname = strdup(src->fsname);
	newcur->stbuf = src->stbuf;
	newcur->parent = dir;
	newcur->next = cam->first_dirent;
	newcur->name = malloc(13);
	sprintf(newcur->name, "GPH_%04d.JPG", ++capcnt);
	cam->first_dirent = newcur;

	return newcur;
}

// Full-press release: emit the capture event burst
static void eos_capture_trigger(vcam *cam) {
	struct CanonBase *p = canon(cam);

	// Find a source image to serve as the captured picture (same approach
	// as the standard capture op)
	struct ptp_dirent *src = cam->first_dirent;
	while (src) {
		if (strstr(src->name, ".jpg") || strstr(src->name, ".JPG"))
			break;
		src = src->next;
	}
	if (!src) {
		vcam_log_func(__func__, "no JPG in the store, cannot capture");
		return;
	}

	// Exposure summary keeps the client's config fresh
	eos_notify_prop_changed(cam, PTP_DPC_EOS_ISOSpeed, 0);
	eos_notify_prop_changed(cam, PTP_DPC_EOS_ShutterSpeed, 0);
	eos_notify_prop_changed(cam, PTP_DPC_EOS_Aperture, 0);
	eos_notify_prop_changed(cam, PTP_DPC_EOS_AvailableShots, 100);
	eos_notify_info_check(cam, 0x20001, 50);

	// Full-press confirmation for the EOS M / PowerShot trigger flow.
	// Must precede the object event: the button-wait loop consumes
	// everything queued before it.
	eos_notify_olc_button(cam, 4, 100);

	if (p->capture_destination == EOS_CAPTUREDEST_SDRAM) {
		// SDRAM: the object lives only in memory until it is downloaded
		// and confirmed with TransferComplete
		void *file = read_file(src);
		if (file == NULL) {
			vcam_log_func(__func__, "cannot read %s", src->fsname);
			return;
		}
		free(p->sdram.data);
		memset(&p->sdram, 0, sizeof(p->sdram));
		p->sdram.used = 1;
		p->sdram.handle = ++cam->ptp_objectid;
		p->sdram.ofc = EOS_OFC_JPEG;
		p->sdram.size = (uint32_t)src->stbuf.st_size;
		p->sdram.data = file;
		snprintf(p->sdram.name, sizeof(p->sdram.name), "GPH_%04d.JPG", (int)(p->sdram.handle % 10000));

		eos_notify_request_transfer(cam, p->sdram.handle, p->sdram.ofc, p->sdram.size, p->sdram.name, 500);
		vcam_log("CANON: SDRAM capture 0x%08x (%d bytes)", p->sdram.handle, p->sdram.size);
	} else {
		// Card: create the object in the FS view and announce it
		struct ptp_dirent *obj = eos_create_card_object(cam, src);
		if (obj == NULL) {
			vcam_log_func(__func__, "no DCIM directory, cannot store capture");
			return;
		}
		eos_notify_object_added(cam, obj->id, EOS_MAIN_STORAGE_ID, obj->parent->id,
			EOS_OFC_JPEG, (uint32_t)obj->stbuf.st_size, obj->name, 500);
		vcam_log("CANON: card capture 0x%08x (%s)", obj->id, obj->name);
	}
}

struct EosEventUint {
	uint32_t size;
	uint32_t type;
	uint32_t code;
	uint32_t value;
};

static int ptp_eos_viewfinder_data(vcam *cam, ptpcontainer *ptp) {
	struct CanonBase *p = canon(cam);
	usleep(1000 * 10);
	p->calls_to_liveview++;

	if (p->calls_to_liveview < 15) {
		ptp_response(cam, PTP_RC_CANON_NotReady, 0);
		return 1;
	}

	vcam_generic_send_file(EOS_LV_JPEG, cam, 0, ptp);

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int ptp_eos_generic(vcam *cam, ptpcontainer *ptp) {
	struct CanonBase *p = canon(cam);
	switch (ptp->code) {
	case PTP_OC_EOS_SetRemoteMode:
	case PTP_OC_EOS_SetEventMode:
		if (vcam_check_param_count(cam, ptp, 1)) return 1;
		p->first_events = 1;
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	case PTP_OC_EOS_KeepDeviceOn:
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	case PTP_OC_EOS_GetRemoteMode:
		ptp_response(cam, PTP_RC_OK, 1, 0); // untested
		return 1;
	}

	vcam_log("Error: Error generic 0x%X opcode\n", ptp->code);
	ptp_response(cam, PTP_RC_OperationNotSupported, 0);
	return 1;
}

static int ptp_eos_exec_evproc(vcam *cam, ptpcontainer *ptp) {
	vcam_log("Evproc %d %d\n", ptp->params[0], ptp->params[1]);
	// wait until payload
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}
static int ptp_eos_exec_evproc_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	int of = 0;

	char name[64];
	of += ptp_read_utf8_string(data + of, name, sizeof(name));

	printf("Name: %s\n", name);

	uint32_t length, type, val, p3, p4, temp;
	of += ptp_read_u32(data + of, &length);

	printf("length: %d\n", length);

	for (uint32_t i = 0; i < length; i++) {
		of += ptp_read_u32(data + of, &type);
		if (type == 2) {
			of += ptp_read_u32(data + of, &val);
			of += ptp_read_u32(data + of, &p3);
			of += ptp_read_u32(data + of, &p4);
			printf("- Int param: %X (%X, %X)\n", val, p3, p4);
			of += ptp_read_u32(data + of, &temp);
		} else if (type == 4) {
			of += ptp_read_u32(data + of, &val);
			printf("%x\n", val);
			of += ptp_read_u32(data + of, &temp); printf("%x\n", temp);
			of += ptp_read_u32(data + of, &temp); printf("%x\n", temp);
			of += ptp_read_u32(data + of, &temp); printf("%x\n", temp);
			of += ptp_read_u32(data + of, &temp); printf("%x\n", temp);
			of += ptp_read_u32(data + of, &temp); printf("%x\n", temp);

			of += ptp_read_utf8_string(data + of, name, sizeof(name));
			printf("- String param: %s\n", name);
		} else {
			printf("Unknown type %x\n", type);
		}

	}

	vcam_log("Evproc data phase\n");
	return 1;
}

static int ptp_eos_set_property(vcam *cam, ptpcontainer *ptp) {
	// Wait until the payload
	return 1;
}
static int ptp_eos_set_property_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	struct CanonBase *p = canon(cam);
	uint32_t *dat = (uint32_t *)data;
	uint32_t length = dat[0];
	uint32_t code = dat[1];
	uint32_t value = dat[2];

	// We don't support multi-length params
	assert(length == 0xc);

	switch (code) {
	case PTP_DPC_EOS_CaptureDestination:
		p->lv_ready = 1;
		p->capture_destination = value;
		break;
	}

	vcam_set_prop(cam, (int)code, value);
	// Prop changes are delivered through the EOS GetEvent channel
	eos_notify_prop_changed(cam, code, 0);

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int ptp_eos_remote_release(vcam *cam, ptpcontainer *ptp) {
	if (ptp->code == PTP_OC_EOS_RemoteReleaseOff) {
		vcam_log("CANON: Shutter up");
	} else if (ptp->code == PTP_OC_EOS_RemoteReleaseOn) {
		if (ptp->params[0] == 1) {
			vcam_log("CANON: Shutter half down");
			usleep(1000 * 500);
		} else if (ptp->params[0] == 2 || ptp->params[0] == 3) {
			// 2 = EOS full press, 3 = EOS M / PowerShot full press
			vcam_log("CANON: Shutter full down");
			usleep(1000 * 200);
			eos_capture_trigger(cam);
		}
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// One-shot remote release (0x910F), used by cameras without the
// RemoteReleaseOn/Off pair (e.g. EOS 5D Mark III). Response param 0 == success.
static int ptp_eos_remoterelease(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_param_count(cam, ptp, 0)) return 1;
	vcam_log("CANON: RemoteRelease (one-shot)");
	usleep(1000 * 200);
	eos_capture_trigger(cam);
	ptp_response(cam, PTP_RC_OK, 1, 0);
	return 1;
}

static int eos_pack_all_props(vcam *cam, uint8_t *buf, int *size) {
	int cnt = 0;

	// Pack in all properties and their current values
	for (int i = 0; i < cam->props->length; i++) {
		struct PtpProp *p = &cam->props->handlers[i];
		int prop_size = vcam_get_prop_size(cam, p->code);
		cnt += ptp_write_u32(buf + cnt, 12 + prop_size);
		cnt += ptp_write_u32(buf + cnt, PTP_EC_EOS_PropValueChanged);
		cnt += ptp_write_u32(buf + cnt, p->code);
		memcpy(buf + cnt, p->desc.value, prop_size);
		cnt += prop_size;
	}

	cnt += ptp_write_u32(buf + cnt, 0xc);
	cnt += ptp_write_u32(buf + cnt, PTP_EC_EOS_InfoCheckComplete);
	cnt += ptp_write_u32(buf + cnt, 0x20001);

	// Pack in all property available value lists
	for (int i = 0; i < cam->props->length; i++) {
		struct PtpProp *p = &cam->props->handlers[i];
		if (p->desc.FormFlag != PTP_EnumerationForm) continue;
		if (p->desc.avail == NULL) abort();
		int avail_size = ptp_get_prop_list_size(p->desc.avail, p->desc.DataType, p->desc.avail_cnt);
		cnt += ptp_write_u32(buf + cnt, 5 * 4 + avail_size);
		cnt += ptp_write_u32(buf + cnt, PTP_EC_EOS_AvailListChanged);
		cnt += ptp_write_u32(buf + cnt, p->code);
		cnt += ptp_write_u32(buf + cnt, 3); // type can be 1/2/3
		cnt += ptp_write_u32(buf + cnt, p->desc.avail_cnt);

		memcpy(buf + cnt, p->desc.avail, avail_size);
		cnt += avail_size;
	}

	cnt += ptp_write_u32(buf + cnt, 0xc);
	cnt += ptp_write_u32(buf + cnt, PTP_EC_EOS_InfoCheckComplete);
	cnt += ptp_write_u32(buf + cnt, 0x20001);

	// Terminating code: 8-byte {size=8, code=0} end marker, as parsed
	// cleanly by clients
	cnt += ptp_write_u32(buf + cnt, 8);
	cnt += ptp_write_u32(buf + cnt, 0x0);
	cnt += ptp_write_u32(buf + cnt, 0x0);

	(*size) = cnt;

	return 0;
}

static int vusb_ptp_eos_events(vcam *cam, ptpcontainer *ptp) {
	struct CanonBase *p = canon(cam);
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	if (p->first_events) {
		int size = 0;
		uint8_t *buffer = malloc(10000);
		eos_pack_all_props(cam, buffer, &size);

		ptp_senddata(cam, ptp->code, (unsigned char *)buffer, size);
		ptp_response(cam, PTP_RC_OK, 0);

		free(buffer);
		p->first_events = 0;
		return 1;
	}

	// Drain the EOS event channel into one response, then terminate.
	// Real cameras always send a data phase here; a response-only packet
	// confuses clients that expect the data phase (and double-frees in
	// gphoto2's event cache).
	uint8_t *buffer = malloc(10000);
	if (buffer == NULL) vcam_panic("out of memory");
	int size = 0;

	unsigned char *ev;
	int evsize;
	while (!ptp_pop_eos_event(cam, &ev, &evsize)) {
		if (size + evsize + 12 > 10000) {
			free(ev);
			break;
		}
		memcpy(buffer + size, ev, evsize);
		size += evsize;
		free(ev);
	}

	size += ptp_write_u32(buffer + size, 8);
	size += ptp_write_u32(buffer + size, 0x0);
	size += ptp_write_u32(buffer + size, 0x0);

	ptp_senddata(cam, ptp->code, buffer, size);
	ptp_response(cam, PTP_RC_OK, 0);
	free(buffer);
	return 1;
}

static int vusb_ptp_eos_getdevicepropvalue(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_param_count(cam, ptp, 1)) return 1;
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// Registers the EOS vendor opcodes advertised by a model (generated from a
// camera dump). Known codes map to their real handlers, everything else gets
// the generic stub.
int canon_register_eos_ops(vcam *cam, const uint16_t *ops, int nops) {
	for (int i = 0; i < nops; i++) {
		int code = ops[i];
		// Standard PTP range is already registered by ops.c with real
		// handlers - only vendor opcodes are (re-)registered here
		if (code < 0x9000) continue;
		switch (code) {
		case PTP_OC_EOS_GetStorageIDs:
			vcam_register_opcode(cam, code, eos_getstorageids, NULL);
			break;
		case PTP_OC_EOS_GetStorageInfo:
			vcam_register_opcode(cam, code, eos_getstorageinfo, NULL);
			break;
		case 0x9108: // GetDeviceInfoEx
			vcam_register_opcode(cam, code, eos_getdeviceinfoex, NULL);
			break;
		case PTP_OC_EOS_GetObjectInfoEx:
			vcam_register_opcode(cam, code, eos_getobjectinfoex, NULL);
			break;
		case PTP_OC_EOS_GetObject:
			vcam_register_opcode(cam, code, eos_getobject, NULL);
			break;
		case PTP_OC_EOS_GetThumbEx:
			vcam_register_opcode(cam, code, eos_getthumbex, NULL);
			break;
		case PTP_OC_EOS_GetPartialObject:
			vcam_register_opcode(cam, code, eos_getpartialobject, NULL);
			break;
		case PTP_OC_EOS_GetPartialObjectEx:
			vcam_register_opcode(cam, code, eos_getpartialobjectex, NULL);
			break;
		case PTP_OC_EOS_TransferComplete:
			vcam_register_opcode(cam, code, eos_transfercomplete, NULL);
			break;
		case PTP_OC_EOS_SetDevicePropValueEx:
			vcam_register_opcode(cam, code, ptp_eos_set_property, ptp_eos_set_property_data);
			break;
		case PTP_OC_EOS_GetEvent:
			vcam_register_opcode(cam, code, vusb_ptp_eos_events, NULL);
			break;
		case PTP_OC_EOS_RemoteReleaseOn:
		case PTP_OC_EOS_RemoteReleaseOff:
			vcam_register_opcode(cam, code, ptp_eos_remote_release, NULL);
			break;
		case PTP_OC_EOS_RemoteRelease:
			vcam_register_opcode(cam, code, ptp_eos_remoterelease, NULL);
			break;
		case PTP_OC_EOS_GetViewFinderData:
			vcam_register_opcode(cam, code, ptp_eos_viewfinder_data, NULL);
			break;
		case 0x913D: // SetRequestOLCInfoGroup
		case 0x913E: // SetRequestRollingPitchingLevel
			vcam_register_opcode(cam, code, eos_noop_ok, NULL);
			break;
		default:
			vcam_register_opcode(cam, code, ptp_eos_generic, NULL);
			break;
		}
	}
	return 0;
}

void canon_register_d4_hidden(vcam *cam) {
	vcam_register_opcode(cam, PTP_OC_EOS_ExecuteEventProc, ptp_eos_exec_evproc, ptp_eos_exec_evproc_data);
	vcam_register_opcode(cam, 0x9057,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9058,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9059,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x905a,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x905f,	ptp_eos_generic, NULL);
}

void canon_register_base_eos(vcam *cam) {
	vcam_register_opcode(cam, PTP_OC_EOS_GetStorageIDs,			ptp_eos_generic, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_GetStorageInfo,			ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9103,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9104,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9107,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9105,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9106,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_GetObjectInfoEx,		ptp_eos_generic, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_SetDevicePropValueEx,	ptp_eos_set_property, ptp_eos_set_property_data);
	vcam_register_opcode(cam, PTP_OC_EOS_GetDevicePropValue, vusb_ptp_eos_getdevicepropvalue, NULL);
	vcam_register_opcode(cam, 0x910b,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9108,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9109,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x910c,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x910e,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x910f,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9115,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9114,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_GetRemoteMode,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_GetEvent,				vusb_ptp_eos_events, NULL);
	vcam_register_opcode(cam, 0x9117,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9120,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91f0,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9118,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9121,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91f1,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_KeepDeviceOn,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x910a,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_SetUILock,				ptp_eos_generic, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_ResetUILock,			ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x911e,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x911a,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_GetViewFinderData,	ptp_eos_viewfinder_data, NULL);
	vcam_register_opcode(cam, 0x9154,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9160,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9155,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9157,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9158,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9159,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x915a,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x911f,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91fe,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91ff,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_RemoteReleaseOn,	ptp_eos_remote_release, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_RemoteReleaseOff,	ptp_eos_remote_release, NULL);
	vcam_register_opcode(cam, 0x912d,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x912e,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x912f,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x912c,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9130,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9131,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9132,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9133,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9134,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x912b,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9135,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9136,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9137,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9138,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9139,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x913a,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x913b,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x913c,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91da,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91db,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91dc,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91dd,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91de,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91d8,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91d9,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91d7,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91d5,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x902f,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9141,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9142,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9143,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x913f,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9033,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9068,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9069,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x906a,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x906b,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x906c,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x906d,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x906e,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x906f,	ptp_eos_generic, NULL);
//	vcam_register_opcode(cam, 0x913d,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9180,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9181,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9182,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9183,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9184,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9185,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9140,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91c0,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91c1,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91c2,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91c3,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91c4,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91c5,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91c6,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91c7,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91c8,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91c9,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91ca,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91cb,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91cc,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91ce,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91cf,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91d0,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91d1,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91d2,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91e1,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91e2,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91e3,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91e4,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91e5,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91e6,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91e7,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91e8,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91e9,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91ea,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91eb,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91ec,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91ed,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91ee,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91ef,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91f8,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91f9,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91f2,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91f3,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91f4,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91f7,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9122,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9123,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9124,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91f5,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x91f6,	ptp_eos_generic, NULL);
	vcam_register_opcode(cam, 0x9053,	ptp_eos_generic, NULL); // TODO: special handling for 9053
	//vcam_register_opcode(cam, PTP_OC_CHDK,	ptp_eos_generic, NULL);
	//vcam_register_opcode(cam, PTP_OC_MagicLantern, 	ptp_eos_generic, NULL);
}
