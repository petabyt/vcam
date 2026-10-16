// Nikon PTP vendor opcode handlers
// Copyright Daniel C - GNU Lesser General Public License v2.1
//
// Implements the Nikon vendor protocol as understood by libgphoto2's ptp2
// camlib (camlibs/ptp2/library.c camera_nikon_capture and friends):
//
//   0x90C0 Capture                    (SDRAM capture, no AF parameter)
//   0x90C1 AfDrive
//   0x90C2 ChangeCameraMode           (0 = user, 1 = PC remote)
//   0x90C3 DelImageSDRAM
//   0x90C4 GetLargeThumb
//   0x90C7 GetEvent                   ({u16 count, (u16 code, u32 param)*})
//   0x90C8 DeviceReady
//   0x90CA GetVendorPropCodes         ({u32 count, u16 code*})
//   0x90CB AfCaptureSDRAM
//   0x9200 GetPreviewImg
//   0x9201/0x9202/0x9203 Live view
//   0x9207 InitiateCaptureRecInMedia  ((af, sdram), also 1-arg D3x form)
//   0x920C TerminateCapture
//   0x941C GetEventEx                 ({u16 count, (u16 code, u16 nparam, u32 params[nparam])*})
//   0x9421 GetObjectSize              (64-bit object size)
//   0x9431 GetPartialObjectEx         (64-bit offsets)
//   0x9439 GetVendorCodes             (Z-series extra props, param 0xd)
//
// Events are queued on the PTP_CH_NIKON channel and served by both the
// GetEvent/GetEventEx opcodes and the USB interrupt endpoint - the first
// consumer wins, which mirrors how real cameras hand events between their
// interrupt pipe and the 0x90C7 queue.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vcam.h>
#include <nikon.h>

// Nikon vendor opcodes not present in src/ptp.h
#define PTP_OC_NIKON_AfDrive			0x90C1
#define PTP_OC_NIKON_ChangeCameraMode		0x90C2
#define PTP_OC_NIKON_DelImageSDRAM		0x90C3
#define PTP_OC_NIKON_GetLargeThumb		0x90C4
#define PTP_OC_NIKON_DeviceReady		0x90C8
#define PTP_OC_NIKON_GetVendorPropCodes		0x90CA
#define PTP_OC_NIKON_GetPreviewImg		0x9200
#define PTP_OC_NIKON_GetLiveViewImg		0x9203
#define PTP_OC_NIKON_InitiateCaptureRecInMedia	0x9207
#define PTP_OC_NIKON_TerminateCapture		0x920C
#define PTP_OC_NIKON_GetEventEx			0x941C
#define PTP_OC_NIKON_GetObjectSize		0x9421
#define PTP_OC_NIKON_GetPartialObjectEx		0x9431
#define PTP_OC_NIKON_GetVendorCodes		0x9439

// Nikon vendor response codes
#define PTP_RC_NIKON_InvalidStatus		0xA004
#define PTP_RC_NIKON_NotLiveView		0xA00B

// Nikon device properties this emulator drives dynamically
#define PTP_DPC_NIKON_RecordingMedia		0xD10B
#define PTP_DPC_NIKON_LiveViewStatus		0xD1A2
#define PTP_DPC_NIKON_LiveViewProhibitCondition	0xD1A4

#define NIKON_MAX_EVENTS 64

static void nikon_notify(vcam *cam, int when, uint16_t code, uint32_t param) {
	ptp_inject_interrupt_channel(cam, when, code, 1, param, 0, PTP_CH_NIKON);
}

// Load the capture/live view JPEG fixture. The shared bin/eos_liveview.jpg
// carries an 8-byte EOS live-view prefix; skip to the JPEG SOI marker so
// Nikon payloads are plain JPEGs. *raw receives the malloc base to free.
static int nikon_load_jpeg(vcam *cam, unsigned char **data, uint32_t *size, unsigned char **raw) {
	char path[512];
	snprintf(path, sizeof(path), "%s/" NIKON_LV_JPEG, PWD);

	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		vcam_panic("nikon: cannot open fixture %s", path);
		return -1;
	}
	fseek(file, 0, SEEK_END);
	long fsize = ftell(file);
	fseek(file, 0, SEEK_SET);
	unsigned char *buf = malloc(fsize);
	if (buf == NULL) vcam_panic("out of memory");
	if (fread(buf, 1, fsize, file) != (size_t)fsize) {
		vcam_panic("nikon: short read on %s", path);
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

void nikon_free_sdram(vcam *cam) {
	struct Nikon *p = nikon(cam);
	if (p->sdram.used) {
		free(p->sdram.raw);
		memset(&p->sdram, 0, sizeof(p->sdram));
	}
}

// Queue the SDRAM capture events the way gphoto2's camera_nikon_capture
// expects them: ObjectAddedInSDRAM (carries the handle) followed by
// CaptureCompleteRecInSdram.
static void nikon_notify_sdram_capture(vcam *cam) {
	nikon_notify(cam, 100, PTP_EC_Nikon_ObjectAddedInSDRAM, NIKON_SDRAM_HANDLE);
	nikon_notify(cam, 250, PTP_EC_Nikon_CaptureCompleteRecInSdram, 0);
}

static int nikon_capture_sdram(vcam *cam) {
	struct Nikon *p = nikon(cam);
	unsigned char *data;
	unsigned char *raw;
	uint32_t size;

	if (nikon_load_jpeg(cam, &data, &size, &raw)) {
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}

	// Real bodies keep the newest SDRAM image; drop a pending one
	nikon_free_sdram(cam);
	p->sdram.used = 1;
	p->sdram.handle = NIKON_SDRAM_HANDLE;
	p->sdram.ofc = PTP_OF_JPEG;
	p->sdram.size = size;
	p->sdram.raw = raw;
	p->sdram.data = data;
	snprintf(p->sdram.name, sizeof(p->sdram.name), "DSC_%04u.JPG",
		 (unsigned)((p->capture_count++) % 10000));

	nikon_notify_sdram_capture(cam);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// Card capture: create a virtual /DCIM/100NIKON/DSC_xxxx.JPG object backed
// by the first JPEG in the FS tree (same trick as the generic
// InitiateCapture handler) and announce it with ObjectAdded/CaptureComplete.
static int nikon_capture_card(vcam *cam) {
	struct Nikon *p = nikon(cam);
	struct ptp_dirent *cur, *dir, *dcim = NULL;

	cur = cam->first_dirent;
	while (cur) {
		if (strstr(cur->name, ".jpg") || strstr(cur->name, ".JPG"))
			break;
		cur = cur->next;
	}
	if (!cur) {
		vcam_log_func(__func__, "no JPG file in the store, can not capture");
		ptp_response(cam, PTP_RC_StoreNotAvailable, 0);
		return 1;
	}

	dir = cam->first_dirent;
	while (dir) {
		if (!strcmp(dir->name, "DCIM") && dir->parent && !dir->parent->id)
			dcim = dir;
		dir = dir->next;
	}
	if (!dcim) {
		vcam_log_func(__func__, "no DCIM root directory");
		ptp_response(cam, PTP_RC_StoreNotAvailable, 0);
		return 1;
	}

	dir = cam->first_dirent;
	while (dir) {
		if (!strcmp(dir->name, "100NIKON") && (dir->parent == dcim))
			break;
		dir = dir->next;
	}
	if (!dir) {
		dir = malloc(sizeof(struct ptp_dirent));
		dir->id = ++cam->ptp_objectid;
		dir->fsname = "virtual";
		dir->stbuf = dcim->stbuf; /* only the S_ISDIR flag is used */
		dir->parent = dcim;
		dir->next = cam->first_dirent;
		dir->name = strdup("100NIKON");
		cam->first_dirent = dir;
	}

	struct ptp_dirent *newcur = malloc(sizeof(struct ptp_dirent));
	newcur->id = ++cam->ptp_objectid;
	newcur->fsname = strdup(cur->fsname);
	newcur->stbuf = cur->stbuf;
	newcur->parent = dir;
	newcur->next = cam->first_dirent;
	newcur->name = malloc(32);
	snprintf(newcur->name, 32, "DSC_%04u.JPG", (unsigned)((p->capture_count++) % 10000));
	cam->first_dirent = newcur;

	nikon_notify(cam, 100, PTP_EC_ObjectAdded, newcur->id);
	nikon_notify(cam, 250, PTP_EC_CaptureComplete, 0);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int nikon_do_capture(vcam *cam, int sdram) {
	if (sdram)
		return nikon_capture_sdram(cam);
	return nikon_capture_card(cam);
}

// --- Vendor opcodes ------------------------------------------------------

// 0x90C0 Capture (legacy SDRAM capture, single 0xffffffff param)
static int nikon_capture_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;
	return nikon_do_capture(cam, 1);
}

// 0x90CB AfCaptureSDRAM
static int nikon_afcapture_sdram_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;
	return nikon_do_capture(cam, 1);
}

// 0x9207 InitiateCaptureRecInMedia: (af, sdram) or the 1-arg D3x form
static int nikon_capture2_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;

	int sdram;
	if (ptp->nparams == 2) {
		sdram = (ptp->params[1] == 1);
	} else if (ptp->nparams == 1) {
		sdram = 1; // 1-arg form always targets SDRAM
	} else {
		vcam_log_func(__func__, "params should be 1 or 2, but is %d", ptp->nparams);
		ptp_response(cam, PTP_RC_ParameterNotSupported, 0);
		return 1;
	}
	return nikon_do_capture(cam, sdram);
}

// 0x920C TerminateCapture (ends bulb exposures)
static int nikon_terminatecapture_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 2)) return 1;
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x90C1 AfDrive
static int nikon_afdrive_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x90C2 ChangeCameraMode (0 = user, 1 = PC remote control)
static int nikon_changecameramode_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	if ((ptp->params[0] != 0) && (ptp->params[0] != 1)) {
		vcam_log_func(__func__, "controlmode must be 0 or 1, is %d", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	nikon(cam)->controlmode = (int)ptp->params[0];
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x90C8 DeviceReady - gphoto2 polls this in nikon_wait_busy() after
// triggering a capture (up to 1000s for bulb exposures)
static int nikon_deviceready_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x90C3 DelImageSDRAM (0 = all)
static int nikon_delimagesdram_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;
	nikon_free_sdram(cam);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// Serialize popped events in the 0x90C7 ({u16 code, u32 param}) or
// 0x941C ({u16 count, u16 pad, (u16 code, u16 nparam, u32 params[nparam])})
// wire format - see ptp_unpack_Nikon_EC / ptp_unpack_Nikon_EC_EX in
// libgphoto2: GetEvent records start at offset 2, GetEventEx at offset 4
static int nikon_pop_events(vcam *cam, unsigned char *data, int use_ex) {
	int x = use_ex ? 4 : 2; // u16 count at offset 0, filled at the end
	int n = 0;
	struct GenericEvent ev;

	while (n < NIKON_MAX_EVENTS && ptp_pop_nikon_event(cam, &ev) == 0) {
		x += put_16bit_le(data + x, ev.code);
		if (use_ex) {
			// CaptureComplete-style events carry no parameter on
			// real bodies; everything else reports one
			uint16_t nparam = (ev.code == PTP_EC_CaptureComplete ||
					   ev.code == PTP_EC_Nikon_CaptureCompleteRecInSdram) ? 0 : 1;
			x += put_16bit_le(data + x, nparam);
			if (nparam)
				x += put_32bit_le(data + x, ev.value);
		} else {
			x += put_32bit_le(data + x, ev.value);
		}
		n++;
	}
	put_16bit_le(data, n);
	return x;
}

// 0x90C7 GetEvent
static int nikon_getevent_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	unsigned char *data = malloc(2 + NIKON_MAX_EVENTS * 6);
	int x = nikon_pop_events(cam, data, 0);
	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x941C GetEventEx
static int nikon_geteventex_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	unsigned char *data = malloc(4 + NIKON_MAX_EVENTS * 8);
	int x = nikon_pop_events(cam, data, 1);
	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x90CA GetVendorPropCodes - gphoto2 appends the answer to its cached
// DeviceInfo.DeviceProperties list during camera_init
static int nikon_getvendorpropcodes_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	unsigned char *data = malloc(4 + 2 * cam->props->length);
	int x = 0, n = 0;
	for (int i = 0; i < cam->props->length; i++) {
		uint16_t code = cam->props->handlers[i].code;
		if (code >= 0xd000 && !nikon_is_base_deviceprop(code))
			n++;
	}
	x += put_32bit_le(data + x, n);
	for (int i = 0; i < cam->props->length; i++) {
		uint16_t code = cam->props->handlers[i].code;
		if (code >= 0xd000 && !nikon_is_base_deviceprop(code))
			x += put_16bit_le(data + x, code);
	}
	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x9439 GetVendorCodes (param 0xd = device properties) - newer Z bodies
// expose props that are missing from the static DeviceInfo list
static int nikon_getvendorcodes_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	if (ptp->params[0] != 0xd) {
		vcam_log_func(__func__, "unknown vendor code type 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_ParameterNotSupported, 0);
		return 1;
	}

	unsigned char *data = malloc(4 + 4 * cam->props->length);
	int x = 0, n = 0;
	for (int i = 0; i < cam->props->length; i++) {
		uint16_t code = cam->props->handlers[i].code;
		if (code >= 0xd000 && !nikon_is_base_deviceprop(code))
			n++;
	}
	x += put_32bit_le(data + x, n);
	for (int i = 0; i < cam->props->length; i++) {
		uint16_t code = cam->props->handlers[i].code;
		if (code >= 0xd000 && !nikon_is_base_deviceprop(code))
			x += put_32bit_le(data + x, code);
	}
	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// --- Live view -----------------------------------------------------------

// 0x9201 StartLiveView
static int nikon_start_liveview_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	struct Nikon *p = nikon(cam);
	p->liveview_status = 1;
	uint8_t status = 1;
	vcam_set_prop_data(cam, PTP_DPC_NIKON_LiveViewStatus, &status, 1);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x9202 EndLiveView
static int nikon_end_liveview_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	struct Nikon *p = nikon(cam);
	p->liveview_status = 0;
	uint8_t status = 0;
	vcam_set_prop_data(cam, PTP_DPC_NIKON_LiveViewStatus, &status, 1);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x9203 GetLiveViewImg: 128-byte Nikon header followed by a JPEG (the
// client scans for the SOI marker, the header content itself is ignored)
static int nikon_getliveviewimg_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	struct Nikon *p = nikon(cam);
	if (!p->liveview_status) {
		ptp_response(cam, PTP_RC_NIKON_NotLiveView, 0);
		return 1;
	}

	unsigned char *jpeg;
	unsigned char *raw;
	uint32_t size;
	if (nikon_load_jpeg(cam, &jpeg, &size, &raw)) {
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}

	unsigned char *data = malloc(NIKON_LV_HEADER_SIZE + size);
	memset(data, 0, NIKON_LV_HEADER_SIZE);
	put_32bit_le(data, size); /* payload size at offset 0 */
	memcpy(data + NIKON_LV_HEADER_SIZE, jpeg, size);
	free(raw);

	ptp_senddata(cam, ptp->code, data, NIKON_LV_HEADER_SIZE + size);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x9200 GetPreviewImg (declared but unused by gphoto2)
static int nikon_getpreviewimg_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	unsigned char *jpeg;
	unsigned char *raw;
	uint32_t size;
	if (nikon_load_jpeg(cam, &jpeg, &size, &raw)) {
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}
	ptp_senddata(cam, ptp->code, jpeg, size);
	free(raw);
	ptp_response(cam, PTP_RC_OK, 1, NIKON_SDRAM_HANDLE);
	return 1;
}

// --- Object access -------------------------------------------------------

static int nikon_sdram_object_info(vcam *cam, ptpcontainer *ptp) {
	struct Nikon *p = nikon(cam);
	unsigned char *data = malloc(2000);
	int x = 0;
	time_t xtime = time(NULL);
	struct tm *tm = gmtime(&xtime);
	char xdate[40];

	sprintf(xdate, "%04d%02d%02dT%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1,
		tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	x += put_32bit_le(data + x, 0);			/* StorageID (SDRAM) */
	x += put_16bit_le(data + x, p->sdram.ofc);	/* ObjectFormatCode */
	x += put_16bit_le(data + x, 0);			/* ProtectionStatus */
	x += put_32bit_le(data + x, p->sdram.size);	/* ObjectCompressedSize */
	x += put_16bit_le(data + x, 0x3808);		/* ThumbFormat (EXIF JPEG) */
	x += put_32bit_le(data + x, p->sdram.size);	/* ThumbCompressedSize */
	x += put_32bit_le(data + x, 0);			/* ThumbPixWidth */
	x += put_32bit_le(data + x, 0);			/* ThumbPixHeight */
	x += put_32bit_le(data + x, 0);			/* ImagePixWidth */
	x += put_32bit_le(data + x, 0);			/* ImagePixHeight */
	x += put_32bit_le(data + x, 0);			/* ImageBitDepth */
	x += put_32bit_le(data + x, 0);			/* ParentObject - SDRAM marker */
	x += put_16bit_le(data + x, 0);			/* AssociationType: Undefined */
	x += put_32bit_le(data + x, 0);
	x += put_32bit_le(data + x, 0);			/* SequenceNumber */
	x += put_string(data + x, p->sdram.name);	/* Filename */
	x += put_string(data + x, xdate);		/* CreationDate */
	x += put_string(data + x, xdate);		/* ModificationDate */
	x += put_string(data + x, "Orientation: 1");	/* Keywords string */

	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int nikon_getobjectinfo(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	if (ptp->params[0] == NIKON_SDRAM_HANDLE && nikon(cam)->sdram.used)
		return nikon_sdram_object_info(cam, ptp);

	return ptp_getobjectinfo_write(cam, ptp);
}

int nikon_getobject(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct Nikon *p = nikon(cam);
	if (ptp->params[0] == NIKON_SDRAM_HANDLE && p->sdram.used) {
		ptp_senddata(cam, ptp->code, p->sdram.data, p->sdram.size);
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	return ptp_getobject_write(cam, ptp);
}

int nikon_getthumb(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct Nikon *p = nikon(cam);
	if (ptp->params[0] == NIKON_SDRAM_HANDLE && p->sdram.used) {
		ptp_senddata(cam, ptp->code, p->sdram.data, p->sdram.size);
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	return ptp_getthumb_write(cam, ptp);
}

// 0x90C4 GetLargeThumb
static int nikon_getlargethumb_op(vcam *cam, ptpcontainer *ptp) {
	return nikon_getthumb(cam, ptp);
}

int nikon_deleteobject(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;

	if (ptp->nparams >= 1 && (ptp->params[0] & 0xffff0000) == 0xffff0000) {
		// SDRAM object - gphoto2 deletes it right after download
		if (nikon(cam)->sdram.used && ptp->params[0] == NIKON_SDRAM_HANDLE) {
			nikon_free_sdram(cam);
			ptp_response(cam, PTP_RC_OK, 0);
			return 1;
		}
		vcam_log_func(__func__, "invalid sdram object id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	return ptp_deleteobject_write(cam, ptp);
}

int nikon_lookup_virtual_object(vcam *cam, uint32_t handle, unsigned char **data, uint32_t *size) {
	struct Nikon *p = nikon(cam);
	if (handle == NIKON_SDRAM_HANDLE && p->sdram.used) {
		*data = p->sdram.data;
		*size = p->sdram.size;
		return 0;
	}
	return -1;
}

// 0x9421 GetObjectSize - 64-bit object size used when GetObjectInfo reports
// 0xffffffff (>4GB files)
static int nikon_getobjectsize_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	uint64_t size = 0;
	struct Nikon *p = nikon(cam);
	if (ptp->params[0] == NIKON_SDRAM_HANDLE && p->sdram.used) {
		size = p->sdram.size;
	} else {
		struct ptp_dirent *cur = cam->first_dirent;
		while (cur) {
			if (cur->id == ptp->params[0])
				break;
			cur = cur->next;
		}
		if (!cur) {
			ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
			return 1;
		}
		size = (uint64_t)cur->stbuf.st_size;
	}

	unsigned char data[8];
	put_64bit_le(data, size);
	ptp_senddata(cam, ptp->code, data, 8);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// 0x9431 GetPartialObjectEx - 64-bit offsets for huge (>4GB) downloads.
// Params: handle, offset_lo, offset_hi, maxlen_lo, maxlen_hi.
// Response params: transferred length as a 64-bit value split in two.
static int nikon_getpartialobjectex_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 5)) return 1;

	uint64_t offset = (uint64_t)ptp->params[1] | ((uint64_t)ptp->params[2] << 32);
	uint64_t maxreq = (uint64_t)ptp->params[3] | ((uint64_t)ptp->params[4] << 32);
	if (maxreq > 10u * 1024u * 1024u)
		maxreq = 10u * 1024u * 1024u; // gphoto2 chunks 10MB

	unsigned char *src = NULL;
	uint64_t objsize = 0;
	struct Nikon *p = nikon(cam);
	int from_sdram = 0;
	struct ptp_dirent *dirent = NULL;

	if (ptp->params[0] == NIKON_SDRAM_HANDLE && p->sdram.used) {
		src = p->sdram.data;
		objsize = p->sdram.size;
		from_sdram = 1;
	} else {
		struct ptp_dirent *cur = cam->first_dirent;
		while (cur) {
			if (cur->id == ptp->params[0])
				break;
			cur = cur->next;
		}
		if (!cur) {
			ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
			return 1;
		}
		objsize = (uint64_t)cur->stbuf.st_size;
		dirent = cur;
	}

	if (offset >= objsize) {
		ptp_senddata(cam, ptp->code, NULL, 0);
		ptp_response(cam, PTP_RC_OK, 2, 0, 0);
		return 1;
	}
	uint64_t avail = objsize - offset;
	uint64_t tocopy = maxreq < avail ? maxreq : avail;

	unsigned char *buffer = malloc(tocopy);
	if (from_sdram) {
		memcpy(buffer, src + offset, tocopy);
	} else {
		FILE *file = fopen(dirent->fsname, "rb");
		if (file == NULL || fseek(file, (long)offset, SEEK_SET) != 0 ||
		    fread(buffer, 1, tocopy, file) != tocopy) {
			if (file) fclose(file);
			free(buffer);
			ptp_response(cam, PTP_RC_GeneralError, 0);
			return 1;
		}
		fclose(file);
	}

	ptp_senddata(cam, ptp->code, buffer, (int)tocopy);
	free(buffer);
	ptp_response(cam, PTP_RC_OK, 2, (uint32_t)(tocopy & 0xffffffff), (uint32_t)(tocopy >> 32));
	return 1;
}

// --- Storage / property overrides ----------------------------------------

// 0x1005 GetStorageInfo - report a Nikon-style storage ("Slot 1" SD card)
static int nikon_getstorageinfo_op(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	if (ptp->params[0] != NIKON_MAIN_STORAGE_ID) {
		vcam_log_func(__func__, "invalid storage id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidStorageId, 0);
		return 1;
	}

	char label[160];
	snprintf(label, sizeof(label), "NIKON %s [Slot 1]", cam->model);

	unsigned char *data = malloc(400);
	int x = 0;
	x += put_16bit_le(data + x, 4);		/* StorageType: Removable RAM */
	x += put_16bit_le(data + x, 3);		/* FileSystemType: DCF layout */
	x += put_16bit_le(data + x, 2);		/* AccessCapability: R/O with deletion */
	x += put_64bit_le(data + x, nikon(cam)->profile->storage_capacity);   /* MaxCapacity */
	x += put_64bit_le(data + x, nikon(cam)->profile->storage_free);       /* FreeSpaceInBytes */
	x += put_32bit_le(data + x, nikon(cam)->profile->storage_images);     /* FreeSpaceInImages */
	x += put_string(data + x, "");		/* StorageDescription: None */
	x += put_string(data + x, label);	/* VolumeLabel */

	ptp_senddata(cam, 0x1005, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int nikon_setdevicepropvalue_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	struct PtpPropDesc *desc = vcam_get_prop_desc(cam, (int)ptp->params[0]);
	int announce = (desc != NULL && desc->GetSet == PTP_AC_ReadWrite && cam->session != 0);

	int rc = ptp_setdevicepropvalue_write_data(cam, ptp, data, len);

	// Announce the change like a real body does - gphoto2 invalidates its
	// propdesc cache when the DevicePropChanged event arrives
	if (announce && len > 0)
		nikon_notify(cam, 300, PTP_EC_DevicePropChanged, ptp->params[0]);
	return rc;
}

// --- Registration --------------------------------------------------------

void nikon_register_ops(vcam *cam, int want_vendorcodes) {
	// Nikon vendor operations
	vcam_register_opcode(cam, PTP_OC_NIKON_Capture, nikon_capture_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_AfDrive, nikon_afdrive_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_ChangeCameraMode, nikon_changecameramode_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_DelImageSDRAM, nikon_delimagesdram_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_GetLargeThumb, nikon_getlargethumb_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_GetEvent, nikon_getevent_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_DeviceReady, nikon_deviceready_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_GetVendorPropCodes, nikon_getvendorpropcodes_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_AfCaptureSDRAM, nikon_afcapture_sdram_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_GetPreviewImg, nikon_getpreviewimg_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_StartLiveView, nikon_start_liveview_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_EndLiveView, nikon_end_liveview_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_GetLiveViewImg, nikon_getliveviewimg_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_InitiateCaptureRecInMedia, nikon_capture2_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_TerminateCapture, nikon_terminatecapture_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_GetEventEx, nikon_geteventex_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_GetObjectSize, nikon_getobjectsize_op, NULL);
	vcam_register_opcode(cam, PTP_OC_NIKON_GetPartialObjectEx, nikon_getpartialobjectex_op, NULL);
	if (want_vendorcodes)
		vcam_register_opcode(cam, PTP_OC_NIKON_GetVendorCodes, nikon_getvendorcodes_op, NULL);

	// Overrides of the standard handlers (SDRAM object serving, Nikon
	// storage description, DevicePropChanged announcements)
	vcam_register_opcode(cam, 0x1005, nikon_getstorageinfo_op, NULL);
	vcam_register_opcode(cam, 0x1008, nikon_getobjectinfo, NULL);
	vcam_register_opcode(cam, 0x1009, nikon_getobject, NULL);
	vcam_register_opcode(cam, 0x100A, nikon_getthumb, NULL);
	vcam_register_opcode(cam, 0x100B, nikon_deleteobject, NULL);
	vcam_register_opcode(cam, 0x1016, ptp_setdevicepropvalue_write, nikon_setdevicepropvalue_data);
}
