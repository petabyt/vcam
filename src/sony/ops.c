// Sony PTP vendor opcode handlers
// Copyright Daniel C - GNU Lesser General Public License v2.1
//
// Implements the Sony "SDIO" remote-control protocol (mode 3, "For 2020
// Models or Later") as understood by libgphoto2's ptp2 camlib:
//
//   0x9201 SDIO_Connect               ((phase,0,0) x3 during init, data-in)
//   0x9202 SDIO_GetExtDeviceInfo      ((0x12c,1), data-in: u16 version +
//                                      u16-array props + u16-array controls)
//   0x9205 SDIO_SetExtDevicePropValue (1 param propcode, data-out DPV)
//   0x9207 SDIO_ControlDevice         (1 param propcode, data-out DPV;
//                                      0xD2C1/0xD2C2 shutter press/release)
//   0x9209 SDIO_GetAllExtDevicePropInfo (data-in: u32 count, u32 0, then a
//                                      stream of packed Sony DPD records)
//
// gphoto2 reads *all* Sony properties (including standard PTP codes) through
// the 0x9209 cache and writes them through 0x9205; the shutter is driven
// through 0x9207 controls (2 = press, 1 = release). Captures land in SDRAM
// at handle 0xffffc001, live view JPEG at 0xffffc002 (4-byte LE offset to
// the SOI), both gated by 0xD215 ObjectInMemory. Events go over the classic
// USB interrupt endpoint: 0xC203 (DevicePropChanged, param = propcode) and
// 0xC201 (ObjectAdded, param = handle).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vcam.h>
#include <sony.h>

#define PTP_EC_Sony_ObjectAdded		0xC201
#define PTP_EC_Sony_DevicePropChanged	0xC203

// Standard props registered by vcam_init_standard that Sony bodies do not
// report (BatteryLevel is 0xD218, ImageSize is 0xD203 on Sony)
static int sony_is_standard_only_prop(uint16_t code) {
	return code == PTP_DPC_BatteryLevel || code == PTP_DPC_ImageSize;
}

void sony_notify(vcam *cam, int when, uint16_t code, uint32_t param) {
	ptp_inject_interrupt(cam, when, code, 1, param, 0);
}

// --- JPEG fixture ---------------------------------------------------------

// Load the capture/live view JPEG fixture. The shared bin/eos_liveview.jpg
// carries an 8-byte EOS live-view prefix; skip to the JPEG SOI marker so
// Sony payloads are plain JPEGs. *raw receives the malloc base to free.
int sony_load_jpeg(vcam *cam, unsigned char **data, uint32_t *size, unsigned char **raw) {
	char path[512];
	snprintf(path, sizeof(path), "%s/" SONY_LV_JPEG, PWD);

	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		vcam_panic("sony: cannot open fixture %s", path);
		return -1;
	}
	fseek(file, 0, SEEK_END);
	long fsize = ftell(file);
	fseek(file, 0, SEEK_SET);
	unsigned char *buf = malloc(fsize);
	if (buf == NULL) vcam_panic("out of memory");
	if (fread(buf, 1, fsize, file) != (size_t)fsize) {
		vcam_panic("sony: short read on %s", path);
	}
	fclose(file);

	long start = 0;
	for (long i = 0; i + 1 < fsize; i++) {
		if (buf[i] == 0xff && buf[i + 1] == 0xd8) {
			start = i;
			break;
		}
	}
	*raw = buf;
	*data = buf + start;
	*size = (uint32_t)(fsize - start);
	return 0;
}

void sony_free_sdram(vcam *cam) {
	struct Sony *p = sony(cam);
	if (p->sdram.used) {
		free(p->sdram.raw);
		memset(&p->sdram, 0, sizeof(p->sdram));
	}
}

// --- Property value plumbing ---------------------------------------------

static void sony_set_prop_u8(vcam *cam, uint16_t code, uint8_t value) {
	vcam_set_prop_data(cam, code, &value, 1);
}

static void sony_set_prop_u16(vcam *cam, uint16_t code, uint16_t value) {
	vcam_set_prop_data(cam, code, &value, 2);
}

// --- Sony DPD serialization (0x9209) --------------------------------------

static int sony_dpv_size(uint16_t type) {
	switch (type) {
	case PTP_TC_INT8: case PTP_TC_UINT8: return 1;
	case PTP_TC_INT16: case PTP_TC_UINT16: return 2;
	case PTP_TC_INT32: case PTP_TC_UINT32: return 4;
	case PTP_TC_INT64: case PTP_TC_UINT64: return 8;
	default: return 0;
	}
}

static int sony_pack_dpv(uint8_t *d, uint16_t type, const void *value) {
	int size = sony_dpv_size(type);
	memcpy(d, value, size);
	return size;
}

// Pack one Sony DPD record: u16 code, u16 datatype, u8 getset, u8 isenabled,
// default value, current value, u8 formflag, form payload
static int sony_pack_dpd(uint8_t *d, struct PtpPropDesc *desc) {
	int x = 0;
	int width = sony_dpv_size(desc->DataType);

	x += ptp_write_u16(d + x, desc->DevicePropertyCode);
	x += ptp_write_u16(d + x, desc->DataType);
	x += ptp_write_u8(d + x, desc->GetSet);
	x += ptp_write_u8(d + x, 1); /* IsEnabled: 1 = enabled */
	if (desc->factory_default_value) {
		x += sony_pack_dpv(d + x, desc->DataType, desc->factory_default_value);
	} else {
		memset(d + x, 0, width);
		x += width;
	}
	if (desc->value) {
		x += sony_pack_dpv(d + x, desc->DataType, desc->value);
	} else {
		memset(d + x, 0, width);
		x += width;
	}

	x += ptp_write_u8(d + x, desc->FormFlag);
	switch (desc->FormFlag) {
	case PTP_RangeForm:
		x += sony_pack_dpv(d + x, desc->DataType, desc->form_min);
		x += sony_pack_dpv(d + x, desc->DataType, desc->form_max);
		x += sony_pack_dpv(d + x, desc->DataType, desc->form_step);
		break;
	case PTP_EnumerationForm:
		x += ptp_write_u16(d + x, desc->avail_cnt);
		for (int i = 0; i < desc->avail_cnt; i++)
			x += sony_pack_dpv(d + x, desc->DataType, (uint8_t *)desc->avail + i * width);
		break;
	default:
		break;
	}
	return x;
}

// 0x9209 SDIO_GetAllExtDevicePropInfo: u32 count, u32 0, then DPD records.
// gphoto2 re-fetches this in a tight loop during capture ("avoid caching"),
// so values must be serialized live from the property store.
static int sony_getalldevicepropinfo_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	unsigned char *data = malloc(64 * 1024);
	int x = 0;
	uint32_t count = 0;

	x += 8; /* header: u32 count (filled below), u32 0 */
	put_32bit_le(data + 4, 0);

	for (int i = 0; i < cam->props->length; i++) {
		struct PtpPropDesc *desc = &cam->props->handlers[i].desc;
		if (sony_is_standard_only_prop(desc->DevicePropertyCode))
			continue;
		if (sony_dpv_size(desc->DataType) == 0)
			continue; // no string/array props on Sony bodies
		x += sony_pack_dpd(data + x, desc);
		count++;
	}
	put_32bit_le(data, count);

	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// --- Init handshake -------------------------------------------------------

// 0x9201 SDIO_Connect: gphoto2 drives phases 1, 2 and 3 during init and
// discards the data phase contents
static int sony_sdioconnect_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 3)) return 1;

	unsigned char data[8];
	memset(data, 0, sizeof(data));
	ptp_senddata(cam, ptp->code, data, sizeof(data));
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x9202 SDIO_GetExtDeviceInfo: protocol negotiation. The response version
// word selects mode 2 (0x0c8) or mode 3 (0x12c); a zero-length reply would
// make gphoto2 fall back to mode 2, so the arrays must be non-empty.
static int sony_getextdeviceinfo_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 2)) return 1;

	struct Sony *p = sony(cam);
	unsigned char *data = malloc(16 + 2 * cam->props->length);
	int x = 0;
	uint16_t n1 = 0, n2 = (uint16_t)p->profile->n_controls;

	// Count vendor properties (0xD2xx settings; controls live in the second
	// array and 0xD25A PriorityMode stays out of the advertised lists)
	for (int i = 0; i < cam->props->length; i++) {
		uint16_t code = cam->props->handlers[i].code;
		if (code >= 0xd200 && code < PTP_DPC_SONY_ShutterHalfRelease && code != PTP_DPC_SONY_PriorityMode)
			n1++;
	}

	x += ptp_write_u16(data + x, SONY_PROTOCOL_3_00);
	x += ptp_write_u32(data + x, n1);
	for (int i = 0; i < cam->props->length; i++) {
		uint16_t code = cam->props->handlers[i].code;
		if (code >= 0xd200 && code < PTP_DPC_SONY_ShutterHalfRelease && code != PTP_DPC_SONY_PriorityMode)
			x += ptp_write_u16(data + x, code);
	}
	x += ptp_write_u32(data + x, n2);
	for (int i = 0; i < p->profile->n_controls; i++)
		x += ptp_write_u16(data + x, p->profile->controls[i]);

	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// --- Property and control writes -----------------------------------------

// 0x9205 SDIO_SetExtDevicePropValue: 1 param propcode, data-out is a
// standard-packed DPV. gphoto2 routes ALL Sony property writes here.
static int sony_setdevicepropvalue_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;
	/* wait for the data phase */
	return 1;
}

static int sony_setdevicepropvalue_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	uint16_t propcode = (uint16_t)ptp->params[0];
	struct PtpPropDesc *desc = vcam_get_prop_desc(cam, propcode);
	int width = desc ? sony_dpv_size(desc->DataType) : 0;

	if (desc && len >= (unsigned int)width && width > 0) {
		vcam_set_prop_data(cam, propcode, data, width);
	} else {
		// Empty writes happen when gphoto2 menu rows carry no datatype
		// (e.g. CaptureTarget) - real bodies see the same traffic
		vcam_log_func(__func__, "ignoring short write to prop 0x%04x (%u bytes)", propcode, len);
	}

	// Real bodies announce property changes with 0xC203 (param = propcode);
	// gphoto2 maps it onto the standard DevicePropChanged handling
	if (cam->session)
		sony_notify(cam, 300, PTP_EC_Sony_DevicePropChanged, propcode);

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// Complete a capture: materialize the SDRAM object, set ObjectInMemory to
// 0x8001 and announce the new object. Called from the release control.
static void sony_complete_capture(vcam *cam) {
	struct Sony *p = sony(cam);
	unsigned char *data;
	unsigned char *raw;
	uint32_t size;

	if (p->sdram.used) {
		// A previous image is still pending; real bodies keep exactly one
		sony_free_sdram(cam);
	}
	if (sony_load_jpeg(cam, &data, &size, &raw)) {
		return;
	}
	p->sdram.used = 1;
	p->sdram.handle = SONY_OBJECT_HANDLE;
	p->sdram.ofc = PTP_OF_JPEG;
	p->sdram.size = size;
	p->sdram.raw = raw;
	p->sdram.data = data;
	snprintf(p->sdram.name, sizeof(p->sdram.name), "DSC_%04u.JPG",
		 (unsigned)((p->capture_count++) % 10000));

	sony_set_prop_u16(cam, PTP_DPC_SONY_ObjectInMemory, 0x8001);
	sony_set_prop_u8(cam, PTP_DPC_SONY_FocusFound, 1);
	sony_notify(cam, 100, PTP_EC_Sony_ObjectAdded, SONY_OBJECT_HANDLE);
}

// 0x9207 SDIO_ControlDevice ("SetControlDeviceB"): button-style controls.
// The shutter is driven through 0xD2C1 (half-press) / 0xD2C2 (full-press)
// with u16 values: 2 = press, 1 = release.
static int sony_controldevice_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;
	/* wait for the data phase */
	return 1;
}

static int sony_controldevice_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct Sony *p = sony(cam);
	uint16_t propcode = (uint16_t)ptp->params[0];

	if ((propcode == PTP_DPC_SONY_ShutterHalfRelease || propcode == PTP_DPC_SONY_ShutterRelease) &&
	    len >= 2) {
		uint16_t value = get_16bit_le(data);
		if (propcode == PTP_DPC_SONY_ShutterHalfRelease) {
			p->half_pressed = (value == 2);
			vcam_log_func(__func__, "half-press %s", value == 2 ? "on" : "off");
		} else {
			if (value == 2) {
				p->full_pressed = 1;
				// Focus achieved right after the full press (gphoto2 waits
				// for 0xD213 == 2/3 or the matching 0xC203 event)
				sony_set_prop_u8(cam, PTP_DPC_SONY_FocusFound, 2);
				sony_notify(cam, 100, PTP_EC_Sony_DevicePropChanged, PTP_DPC_SONY_FocusFound);
				vcam_log_func(__func__, "full-press, focus found");
			} else {
				if (p->full_pressed)
					sony_complete_capture(cam);
				p->full_pressed = 0;
				vcam_log_func(__func__, "release");
			}
		}
	} else {
		vcam_log_func(__func__, "control 0x%04x (%u bytes)", propcode, len);
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// --- Storage --------------------------------------------------------------

// 0x1004 GetStorageIDs - PC-control mode bodies report no storages (the
// a7m4/a7r5 dumps show an empty "Storage Devices Summary")
static int sony_getstorageids_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	unsigned char data[4];
	put_32bit_le(data, 0);
	ptp_senddata(cam, ptp->code, data, 4);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// --- Virtual SDRAM / live view objects ------------------------------------

static int sony_virtual_object_info(vcam *cam, ptpcontainer *ptp, uint32_t handle) {
	struct Sony *p = sony(cam);
	uint32_t size;
	const char *name;

	if (handle == SONY_OBJECT_HANDLE && p->sdram.used) {
		size = p->sdram.size;
		name = p->sdram.name;
	} else if (handle == SONY_LIVEVIEW_HANDLE && p->lv_data != NULL) {
		size = SONY_LV_HEADER_SIZE + p->lv_size;
		name = "livestream.jpg";
	} else {
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	unsigned char *data = malloc(2000);
	int x = 0;
	time_t xtime = time(NULL);
	struct tm *tm = gmtime(&xtime);
	char xdate[40];

	sprintf(xdate, "%04d%02d%02dT%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1,
		tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	x += put_32bit_le(data + x, 0);			/* StorageID (RAM) */
	x += put_16bit_le(data + x, PTP_OF_JPEG);	/* ObjectFormatCode */
	x += put_16bit_le(data + x, 0);			/* ProtectionStatus */
	x += put_32bit_le(data + x, size);		/* ObjectCompressedSize */
	x += put_16bit_le(data + x, 0x3808);		/* ThumbFormat (EXIF JPEG) */
	x += put_32bit_le(data + x, size);		/* ThumbCompressedSize */
	x += put_32bit_le(data + x, 0);			/* ThumbPixWidth */
	x += put_32bit_le(data + x, 0);			/* ThumbPixHeight */
	x += put_32bit_le(data + x, 0);			/* ImagePixWidth */
	x += put_32bit_le(data + x, 0);			/* ImagePixHeight */
	x += put_32bit_le(data + x, 0);			/* ImageBitDepth */
	x += put_32bit_le(data + x, 0);			/* ParentObject - RAM marker */
	x += put_16bit_le(data + x, 0);			/* AssociationType: Undefined */
	x += put_32bit_le(data + x, 0);
	x += put_32bit_le(data + x, 0);			/* SequenceNumber */
	x += put_string(data + x, name);		/* Filename */
	x += put_string(data + x, xdate);		/* CreationDate */
	x += put_string(data + x, xdate);		/* ModificationDate */
	x += put_string(data + x, "Orientation: 1");	/* Keywords string */

	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int sony_getobjectinfo(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	uint32_t handle = ptp->params[0];
	if (handle == SONY_OBJECT_HANDLE || handle == SONY_LIVEVIEW_HANDLE)
		return sony_virtual_object_info(cam, ptp, handle);

	return ptp_getobjectinfo_write(cam, ptp);
}

int sony_getobject(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct Sony *p = sony(cam);
	uint32_t handle = ptp->params[0];

	if (handle == SONY_OBJECT_HANDLE && p->sdram.used) {
		ptp_senddata(cam, ptp->code, p->sdram.data, p->sdram.size);
		// Fetching the RAM object clears ObjectInMemory (real bodies do
		// this too - gphoto2's stale-image cleanup loop relies on it)
		sony_free_sdram(cam);
		sony_set_prop_u16(cam, PTP_DPC_SONY_ObjectInMemory, 0);
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}
	if (handle == SONY_LIVEVIEW_HANDLE && p->lv_data != NULL) {
		unsigned char *data = malloc(SONY_LV_HEADER_SIZE + p->lv_size);
		put_32bit_le(data, SONY_LV_HEADER_SIZE); /* offset to the JPEG SOI */
		memcpy(data + SONY_LV_HEADER_SIZE, p->lv_data, p->lv_size);
		ptp_senddata(cam, ptp->code, data, SONY_LV_HEADER_SIZE + p->lv_size);
		free(data);
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	return ptp_getobject_write(cam, ptp);
}

int sony_lookup_virtual_object(vcam *cam, uint32_t handle, unsigned char **data, uint32_t *size) {
	struct Sony *p = sony(cam);
	if (handle == SONY_OBJECT_HANDLE && p->sdram.used) {
		*data = p->sdram.data;
		*size = p->sdram.size;
		return 0;
	}
	if (handle == SONY_LIVEVIEW_HANDLE && p->lv_data != NULL) {
		*data = p->lv_data;
		*size = p->lv_size;
		return 0;
	}
	return -1;
}

// --- Registration ---------------------------------------------------------

// Real mode-3 bodies do not advertise DeleteObject ("PTP_OC_DeleteObject is
// not supported", SDRAM objects just vanish on download) nor the generic
// InitiateCapture (capture goes through the 0x9207 controls only)
static int sony_not_supported_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	ptp_response(cam, PTP_RC_OperationNotSupported, 0);
	return 1;
}

void sony_register_ops(vcam *cam) {
	// Sony SDIO vendor operations
	vcam_register_opcode(cam, PTP_OC_SONY_SDIO_Connect, sony_sdioconnect_op, NULL);
	vcam_register_opcode(cam, PTP_OC_SONY_SDIO_GetExtDeviceInfo, sony_getextdeviceinfo_op, NULL);
	vcam_register_opcode(cam, PTP_OC_SONY_SDIO_GetAllExtDevicePropInfo, sony_getalldevicepropinfo_op, NULL);
	vcam_register_opcode(cam, PTP_OC_SONY_SDIO_SetExtDevicePropValue, sony_setdevicepropvalue_write, sony_setdevicepropvalue_data);
	vcam_register_opcode(cam, PTP_OC_SONY_SDIO_ControlDevice, sony_controldevice_write, sony_controldevice_data);

	// Overrides of the standard handlers
	vcam_register_opcode(cam, 0x1004, sony_getstorageids_op, NULL);
	vcam_register_opcode(cam, 0x1008, sony_getobjectinfo, NULL);
	vcam_register_opcode(cam, 0x1009, sony_getobject, NULL);

	// Ops real mode-3 bodies do not advertise - answer with
	// OperationNotSupported so they still show up as unsupported
	vcam_register_opcode(cam, 0x100B, sony_not_supported_op, NULL); // DeleteObject
	vcam_register_opcode(cam, 0x100E, sony_not_supported_op, NULL); // InitiateCapture
}
