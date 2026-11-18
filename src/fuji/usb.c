#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <vcam.h>
#include <fujiptp.h>
#include <cl_data.h>
#include "fuji.h"

static void add_prop_u32(vcam *cam, int code, uint32_t value) {
	struct PtpPropDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.value = malloc(8);
	desc.DataType = PTP_TC_UINT32;
	memcpy(desc.value, &value, 4);
	vcam_register_prop(cam, code, &desc);
}

static void add_prop_invisible_u32(vcam *cam, int code, uint32_t value) {
	struct PtpPropDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.value = malloc(8);
	desc.DataType = PTP_TC_UINT32;
	desc.GetSet = PTP_AC_Invisible;
	memcpy(desc.value, &value, 4);
	vcam_register_prop(cam, code, &desc);
}

// Plain settable u16 property with a proper factory default, so both
// GetDevicePropDesc (0x1014) and GetDevicePropValue (0x1015) work
static void add_prop_settable_u16(vcam *cam, int code, uint16_t value) {
	struct PtpPropDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.DataType = PTP_TC_UINT16;
	desc.GetSet = PTP_AC_ReadWrite;
	desc.factory_default_value = malloc(8);
	desc.value = malloc(8);
	ptp_write_u16(desc.factory_default_value, value);
	ptp_write_u16(desc.value, value);
	vcam_register_prop(cam, code, &desc);
}

static int fake_dev_info(vcam *cam, ptpcontainer *ptp) {
	vcam_generic_send_file("bin/fuji/xh1_device_info.bin", cam, 12, ptp);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int prop_d21c_getvalue(vcam *cam, struct PtpPropDesc *desc, int *optional_length) {
	ptp_write_u32(desc->value, 0x3);
	return 0;
}
int prop_d21c_setvalue(vcam *cam, struct PtpPropDesc *desc, const void *data) {
	uint16_t x;
	ptp_read_u16(data, &x);
	vcam_log("New value for d21c: %x", x);
	return 0;
}

int prop_d18c_getvalue(vcam *cam, struct PtpPropDesc *desc, int *optional_length) {
	int of = 0x0;
	uint8_t *d = desc->value;
	of += ptp_write_u16(d + of, 0x1d);
	of += ptp_write_string(d + of, "FF129506");
	memset(d + of, 0x0, 0x201 - of); of = 0x201;
	of += ptp_write_u32(d + of, 0x2);
	of += ptp_write_u32(d + of, 0x7);
	of += ptp_write_u32(d + of, 0x7);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x64);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x2);
	of += ptp_write_u32(d + of, 0x1);
	of += ptp_write_u32(d + of, 0x1);
	of += ptp_write_u32(d + of, 0x1);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0xffffffd8);
	of += ptp_write_u32(d + of, 0x8000);
	of += ptp_write_u32(d + of, 0x2);
	of += ptp_write_u32(d + of, 0x1);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x1);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x0);
	of += ptp_write_u32(d + of, 0x2);
	of += ptp_write_u32(d + of, 0x1);
	(*optional_length) = of;
	return 0;
}

// Live view object served at 0x80000001 for the gphoto2 capture-preview
// flow: InitiateOpenCapture (0x101C) -> GetObjectInfo/GetObject(0x80000001)
// -> DeleteObject, then TerminateOpenCapture (0x101D) on session exit.
#define FUJI_USB_LV_HANDLE 0x80000001
#include <liveview.h>
#define FUJI_USB_STORAGE_ID 0x00010001

static int fuji_usb_load_lv(vcam *cam) {
	struct Fuji *f = fuji(cam);
	unsigned char *data;
	uint32_t size;
	if (vcam_liveview_jpeg(VCAM_LV_WIDTH, VCAM_LV_HEIGHT, VCAM_LV_QUALITY, &data, &size)) return -1;
	free(f->usb_lv_raw);
	f->usb_lv_raw = data;
	f->usb_lv_data = data;
	f->usb_lv_size = size;
	return 0;
}

// ---------------------------------------------------------------------------
// Capture: PTP_DPC_FUJI_CameraAction (0xD208) + 0xD209 AFStatus
//
// libgphoto2 drives capture through the 0xD208 action control: it writes an
// action code and then calls InitiateCapture to execute it ("After setting
// usually it does initiatecapture to trigger this mode operation",
// camlibs/ptp2/config.c fuji_action). Action codes observed on real bodies:
//
//	0x0304 regular capture (SDK_Shoot) - the power-on default
//	0x0200 autofocus (SDK_ShootS1), 0x0600 one-push AF, 0xa000 AF start
//	0x9100 X-Pro2 AF, 0x9300 webcam AF (followed by polling 0xD209)
//	0x0004/0x0006/0x0001/0x0005 cancel AF (S1/webcam/X-Pro2 stop codes)
//	0x0500 bulb start, 0x000c bulb end
//
// While AF runs 0xD209 AFStatus reads 1 (busy), 2 (ready) afterwards.
// gphoto2's camera_fuji_capture() polls it until it leaves 1.
// ---------------------------------------------------------------------------
#define FUJI_USB_ACTION_AF			0x0200
#define FUJI_USB_ACTION_CANCEL_S1		0x0004
#define FUJI_USB_ACTION_CANCEL_AF		0x0006
#define FUJI_USB_ACTION_SHOOT			0x0304
#define FUJI_USB_ACTION_BULB_ON			0x0500
#define FUJI_USB_ACTION_BULB_OFF		0x000c
#define FUJI_USB_ACTION_ONE_PUSH_AF		0x0600
#define FUJI_USB_ACTION_AF_START		0xa000
#define FUJI_USB_ACTION_XPRO2_STOP		0x0001
#define FUJI_USB_ACTION_WEBCAM_STOP		0x0005
#define FUJI_USB_ACTION_XPRO2_AF		0x9100
#define FUJI_USB_ACTION_WEBCAM_AF		0x9300

// AFStatus values
#define FUJI_USB_AF_BUSY			0x0001
#define FUJI_USB_AF_READY			0x0002

// How long the AF busy window lasts after a focus action is executed
#define FUJI_USB_AF_MS 400

static long fuji_usb_ms_since(struct timeval *start) {
	struct timeval now;
	gettimeofday(&now, NULL);
	return (now.tv_sec - start->tv_sec) * 1000 + (now.tv_usec - start->tv_usec) / 1000;
}

static void fuji_usb_start_af(vcam *cam, struct Fuji *f, int inject_event) {
	struct PtpPropDesc *desc = vcam_get_prop_desc(cam, PTP_DPC_FUJI_AFStatus);

	f->usb_af_active = 1;
	gettimeofday(&f->usb_af_start, NULL);
	if (desc != NULL) ptp_write_u16(desc->value, FUJI_USB_AF_BUSY);
	if (inject_event) {
		// fuji-style event: property code as event code, new value as
		// parameter (same convention as the wifi event channel)
		ptp_inject_interrupt(cam, FUJI_USB_AF_MS, PTP_DPC_FUJI_AFStatus, 1, FUJI_USB_AF_READY, 0);
	}
}

static void fuji_usb_cancel_af(struct Fuji *f) {
	f->usb_af_active = 0;
}

static int prop_d208_setvalue(vcam *cam, struct PtpPropDesc *desc, const void *data) {
	struct Fuji *f = fuji(cam);
	uint16_t action;

	ptp_read_u16(data, &action);
	ptp_write_u16(desc->value, action);
	f->usb_action = action;
	vcam_log("fuji usb: camera action %04x", action);

	switch (action) {
	case FUJI_USB_ACTION_AF:
	case FUJI_USB_ACTION_ONE_PUSH_AF:
	case FUJI_USB_ACTION_AF_START:
	case FUJI_USB_ACTION_XPRO2_AF:
	case FUJI_USB_ACTION_WEBCAM_AF:
		fuji_usb_start_af(cam, f, 0);
		break;
	case FUJI_USB_ACTION_CANCEL_S1:
	case FUJI_USB_ACTION_CANCEL_AF:
	case FUJI_USB_ACTION_XPRO2_STOP:
	case FUJI_USB_ACTION_WEBCAM_STOP:
		fuji_usb_cancel_af(f);
		break;
	}
	return 0;
}

static int prop_d209_getvalue(vcam *cam, struct PtpPropDesc *desc, int *optional_length) {
	struct Fuji *f = fuji(cam);
	(void)optional_length;

	uint16_t status = FUJI_USB_AF_READY;
	if (f->usb_af_active) {
		if (fuji_usb_ms_since(&f->usb_af_start) < FUJI_USB_AF_MS) {
			status = FUJI_USB_AF_BUSY;
		} else {
			f->usb_af_active = 0;
		}
	}
	ptp_write_u16(desc->value, status);
	return 0;
}

// InitiateCapture executes the action last written to 0xD208. Focus actions
// only drive AF - no image is created - so gphoto2's camera_fuji_capture()
// flow (0x0200 focus + InitiateCapture, poll AFStatus, 0x0304 shoot +
// InitiateCapture) produces exactly one object per shoot. Unset and unknown
// actions fall through to the plain capture.
static int fuji_usb_initiatecapture(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 2)) return 1;

	struct Fuji *f = fuji(cam);
	switch (f->usb_action) {
	case FUJI_USB_ACTION_AF:
	case FUJI_USB_ACTION_ONE_PUSH_AF:
	case FUJI_USB_ACTION_AF_START:
	case FUJI_USB_ACTION_XPRO2_AF:
	case FUJI_USB_ACTION_WEBCAM_AF:
		fuji_usb_start_af(cam, f, 1);
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	case FUJI_USB_ACTION_CANCEL_S1:
	case FUJI_USB_ACTION_CANCEL_AF:
	case FUJI_USB_ACTION_XPRO2_STOP:
	case FUJI_USB_ACTION_WEBCAM_STOP:
		fuji_usb_cancel_af(f);
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	case FUJI_USB_ACTION_BULB_ON:
		// exposure opens - nothing to emulate until the closing
		// InitiateCapture (0x000c falls through to the generic capture)
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	default:
		// 0x0304 shoot, 0x000c bulb end and everything unknown
		return ptp_initiatecapture_write(cam, ptp);
	}
}

static int fuji_usb_opencapture(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 2)) return 1;

	fuji(cam)->usb_liveview = 1;
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int fuji_usb_terminateopencapture(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	fuji(cam)->usb_liveview = 0;
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int fuji_usb_getobjectinfo(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct Fuji *f = fuji(cam);
	if (ptp->params[0] != FUJI_USB_LV_HANDLE || !f->usb_liveview)
		return ptp_getobjectinfo_write(cam, ptp);
	if (fuji_usb_load_lv(cam)) {
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}

	unsigned char *data = malloc(2000);
	int x = 0;
	time_t xtime = time(NULL);
	struct tm *tm = gmtime(&xtime);
	char xdate[40];
	sprintf(xdate, "%04d%02d%02dT%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1,
		tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	x += put_32bit_le(data + x, FUJI_USB_STORAGE_ID);
	x += put_16bit_le(data + x, PTP_OF_JPEG);
	x += put_16bit_le(data + x, 0);			/* ProtectionStatus */
	x += put_32bit_le(data + x, f->usb_lv_size);	/* ObjectCompressedSize */
	x += put_16bit_le(data + x, 0);			/* ThumbFormat */
	x += put_32bit_le(data + x, 0);			/* ThumbCompressedSize */
	x += put_32bit_le(data + x, 0);			/* ThumbPixWidth */
	x += put_32bit_le(data + x, 0);			/* ThumbPixHeight */
	x += put_32bit_le(data + x, 0);			/* ImagePixWidth */
	x += put_32bit_le(data + x, 0);			/* ImagePixHeight */
	x += put_32bit_le(data + x, 0);			/* ImageBitDepth */
	x += put_32bit_le(data + x, 0);			/* ParentObject - RAM marker */
	x += put_16bit_le(data + x, 0);			/* AssociationType */
	x += put_32bit_le(data + x, 0);
	x += put_32bit_le(data + x, 0);			/* SequenceNumber */
	x += put_string(data + x, "liveview.jpg");
	x += put_string(data + x, xdate);
	x += put_string(data + x, xdate);
	x += put_string(data + x, "Orientation: 1");

	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int fuji_usb_getobject(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct Fuji *f = fuji(cam);
	if (ptp->params[0] != FUJI_USB_LV_HANDLE || !f->usb_liveview)
		return ptp_getobject_write(cam, ptp);

	ptp_senddata(cam, ptp->code, f->usb_lv_data, f->usb_lv_size);
	/* Keep ObjectInfo and GetObject consistent; prepare the next frame afterward. */
	if (fuji_usb_load_lv(cam)) vcam_log("Failed to generate Fuji live view frame");
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int fuji_usb_deleteobject(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;

	if (ptp->nparams >= 1 && ptp->params[0] == FUJI_USB_LV_HANDLE) {
		// gphoto2 deletes the live view object after every fetched frame
		fuji(cam)->usb_liveview = 0;
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	return ptp_deleteobject_write(cam, ptp);
}

int fuji_usb_init_cam(vcam *cam) {
	struct Fuji *f = fuji(cam);

	ptp_register_mtp_props(cam);
	ptp_register_mtp_opcodes(cam);

	fuji_register_d212(cam);

	add_prop_invisible_u32(cam, 0xd200, 0x0);
	add_prop_invisible_u32(cam, 0xd201, 0x0);
	add_prop_invisible_u32(cam, 0xd202, 0x0);
	add_prop_invisible_u32(cam, 0xd203, 0x0);
	add_prop_invisible_u32(cam, 0xd204, 0x0);
	add_prop_invisible_u32(cam, 0xd205, 0x0);
	add_prop_invisible_u32(cam, 0xd206, 0x0);
	add_prop_invisible_u32(cam, 0xd20a, 0x0);
	add_prop_invisible_u32(cam, 0xd20c, 0x0);
	add_prop_invisible_u32(cam, 0xd20d, 0x0);
	add_prop_invisible_u32(cam, 0xd20e, 0x0);
	add_prop_invisible_u32(cam, 0xd20f, 0x0);
	add_prop_invisible_u32(cam, 0xd210, 0x0);
	add_prop_invisible_u32(cam, 0xd211, 0x0);
	add_prop_invisible_u32(cam, 0xd213, 0x0);
	add_prop_invisible_u32(cam, 0xd214, 0x0);
	add_prop_invisible_u32(cam, 0xd215, 0x0);
	add_prop_invisible_u32(cam, 0xd216, 0x0);
	add_prop_invisible_u32(cam, 0xd217, 0x0);
	add_prop_invisible_u32(cam, 0xd218, 0x0);
	add_prop_invisible_u32(cam, 0xd219, 0x0);
	add_prop_invisible_u32(cam, 0xd21a, 0x0);
	add_prop_invisible_u32(cam, 0xd21b, 0x0);

	add_prop_invisible_u32(cam, 0xd001, 0x0);
	add_prop_invisible_u32(cam, 0xd002, 0x0);
	add_prop_invisible_u32(cam, 0xd003, 0x0);
	add_prop_invisible_u32(cam, 0xd004, 0x0);
	add_prop_invisible_u32(cam, 0xd005, 0x0);
	add_prop_invisible_u32(cam, 0xd007, 0x0);
	add_prop_invisible_u32(cam, 0xd008, 0x0);
	add_prop_invisible_u32(cam, 0xd009, 0x0);
	add_prop_invisible_u32(cam, 0xd00a, 0x0);
	add_prop_invisible_u32(cam, 0xd00b, 0x0);
	add_prop_invisible_u32(cam, 0xd00c, 0x0);
	add_prop_invisible_u32(cam, 0xd00d, 0x0);
	add_prop_invisible_u32(cam, 0xd00e, 0x0);
	add_prop_invisible_u32(cam, 0xd00f, 0x0);
	add_prop_invisible_u32(cam, 0xd010, 0x0);
	add_prop_invisible_u32(cam, 0xd011, 0x0);
	add_prop_invisible_u32(cam, 0xd012, 0x0);
	add_prop_invisible_u32(cam, 0xd013, 0x0);
	add_prop_invisible_u32(cam, 0xd014, 0x0);
	add_prop_invisible_u32(cam, 0xd015, 0x0);
	add_prop_invisible_u32(cam, 0xd016, 0x0);
	add_prop_invisible_u32(cam, 0xd017, 0x0);
	add_prop_invisible_u32(cam, 0xd018, 0x0);
	add_prop_invisible_u32(cam, 0xd019, 0x0);
	add_prop_invisible_u32(cam, 0xd01a, 0x0);
	add_prop_invisible_u32(cam, 0xd01b, 0x0);
	add_prop_invisible_u32(cam, 0xd01c, 0x0);

	// PTP_DPC_FUJI_PriorityMode (0xD207): 1 = camera UI has priority,
	// 2 = USB control (gphoto2 prepare_capture/unprepare_capture)
	add_prop_settable_u16(cam, PTP_DPC_FUJI_PriorityMode, 1);
	// PTP_DPC_FUJI_CameraAction (0xD208): shoot control, see the
	// fuji_usb_action table above for the codes
	{
		struct PtpPropDesc desc;
		memset(&desc, 0, sizeof(desc));
		desc.DataType = PTP_TC_UINT16;
		desc.GetSet = PTP_AC_ReadWrite;
		desc.factory_default_value = malloc(8);
		desc.value = malloc(8);
		ptp_write_u16(desc.factory_default_value, FUJI_USB_ACTION_SHOOT);
		ptp_write_u16(desc.value, FUJI_USB_ACTION_SHOOT);
		vcam_register_prop_handlers(cam, PTP_DPC_FUJI_CameraAction, &desc, NULL, prop_d208_setvalue);
		uint16_t actions[] = {0x0104, 0x0200, 0x0004, 0x0304, 0x0500, 0x000c,
				      0xa000, 0x0006, 0x9000, 0x0002, 0x9100, 0x0001, 0x9300, 0x0005};
		vcam_set_prop_avail(cam, PTP_DPC_FUJI_CameraAction, actions, (int)(sizeof(actions) / sizeof(actions[0])));
	}
	// PTP_DPC_FUJI_AFStatus (0xD209): 1 = AF busy, 2 = ready. Time-driven
	// while an AF action is executing, see prop_d209_getvalue
	{
		struct PtpPropDesc desc;
		memset(&desc, 0, sizeof(desc));
		desc.DataType = PTP_TC_UINT16;
		desc.GetSet = PTP_AC_Read;
		desc.factory_default_value = malloc(8);
		desc.value = malloc(8);
		ptp_write_u16(desc.factory_default_value, FUJI_USB_AF_READY);
		ptp_write_u16(desc.value, FUJI_USB_AF_READY);
		vcam_register_prop_handlers(cam, PTP_DPC_FUJI_AFStatus, &desc, prop_d209_getvalue, NULL);
	}
	// PTP_DPC_FUJI_LensZoomPosCaps (0xD38C): "PC Mode" - set to 1 by
	// gphoto2's prepare_capture (timelapse does d38c -> 1, d207 -> 2)
	add_prop_settable_u16(cam, PTP_DPC_FUJI_LensZoomPosCaps, 0);

	if (f->transport == FUJI_FEATURE_RAW_CONV) {
		add_prop_u32(cam, PTP_DPC_FUJI_USBMode, 6);
		{
			uint8_t *d = malloc(100);

			ptp_write_string(d, "FF129506,FA129506");
			struct PtpPropDesc desc = {0};
			desc.value = d;
			desc.DataType = PTP_TC_STRING;
			vcam_register_prop(cam, PTP_DPC_FUJI_IOPCode, &desc);
		}

		//add_prop_u32(cam, PTP_DPC_FUJI_UnknownD040, 0x1);
		//add_prop_u16(cam, PTP_DPC_FUJI_UnknownD21C, 0x3);

		{
			struct PtpPropDesc desc = {0};
			desc.value = malloc(8);
			desc.GetSet = PTP_AC_Read;
			desc.DataType = PTP_TC_UINT16;
			vcam_register_prop_handlers(cam, 0xd21c, &desc, prop_d21c_getvalue, prop_d21c_setvalue);
		}

		//vcam_register_opcode(cam, PTP_OC_GetDeviceInfo, fake_dev_info, NULL);

		vcam_fuji_register_rawconv_fs(cam);
	} else if (f->transport == FUJI_FEATURE_USB_TETHER_SHOOT) {
		add_prop_u32(cam, PTP_DPC_FUJI_USBMode, 5);
	}

	{
		// No name reported in Fuji X-H1
		struct PtpPropDesc desc = {0};
		desc.value = malloc(3);
		uint8_t str[3] = {1, 0, 0};
		memcpy(desc.value, str, sizeof(str));
		desc.DataType = PTP_TC_STRING;
		vcam_register_prop(cam, PTP_DPC_FUJI_DeviceName, &desc);
	}
	add_prop_u32(cam, PTP_DPC_FUJI_BatteryInfo1, 0xa);
	{
		struct PtpPropDesc desc = {0};
		desc.value = malloc(50);
		ptp_write_string(desc.value, "49,0,0");
		desc.DataType = PTP_TC_STRING;
		vcam_register_prop(cam, PTP_DPC_FUJI_BatteryInfo2, &desc);
	}

	// ExposureTime is required by gphoto2's camera_fuji_capture() - it
	// gates the capture timeout (hard failure when missing)
	add_prop_u32(cam, PTP_DPC_ExposureTime, 10000);
	// PTP_DPC_FocusMode: read by gphoto2's autofocusdrive (0x0001 switches
	// its AF-drive codes to the manual-focus set, 2 = automatic)
	add_prop_settable_u16(cam, PTP_DPC_FocusMode, 2);
	{
		uint16_t modes[] = {1, 2};
		vcam_set_prop_avail(cam, PTP_DPC_FocusMode, modes, 2);
	}

	// Live view + open-capture handling for the capture-preview flow
	if (fuji_usb_load_lv(cam))
		return -1;
	vcam_register_opcode(cam, PTP_OC_InitiateOpenCapture, fuji_usb_opencapture, NULL);
	vcam_register_opcode(cam, PTP_OC_TerminateOpenCapture, fuji_usb_terminateopencapture, NULL);
	// InitiateCapture gated on the 0xD208 action control
	vcam_register_opcode(cam, PTP_OC_InitiateCapture, fuji_usb_initiatecapture, NULL);
	// Real Fuji USB bodies do not announce captured objects on the
	// interrupt pipe; clients discover them via the GetObjectHandles diff
	cam->capture_no_objectadded = 1;

	// Real Fuji bodies put the interrupt pair on EP 1 (bin/fuji/fujicard.txt)
	cam->usb_ep_profile = (f->transport == FUJI_FEATURE_RAW_CONV) ? VCAM_EP_FUJI_RAW : VCAM_EP_FUJI_CARD;
	vcam_register_opcode(cam, PTP_OC_GetObjectInfo, fuji_usb_getobjectinfo, NULL);
	vcam_register_opcode(cam, PTP_OC_GetObject, fuji_usb_getobject, NULL);
	vcam_register_opcode(cam, PTP_OC_DeleteObject, fuji_usb_deleteobject, NULL);

	// TODO: implement raw conv mode

	return 0;
}
