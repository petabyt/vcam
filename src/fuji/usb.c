#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
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

static void add_prop_u16(vcam *cam, int code, uint16_t value) {
	struct PtpPropDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.value = malloc(8);
	desc.DataType = PTP_TC_UINT16;
	memcpy(desc.value, &value, 2);
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
#define FUJI_USB_LV_JPEG "bin/eos_liveview.jpg"
#define FUJI_USB_STORAGE_ID 0x00010001

static int fuji_usb_load_lv(vcam *cam) {
	struct Fuji *f = fuji(cam);
	char path[512];

	snprintf(path, sizeof(path), "%s/" FUJI_USB_LV_JPEG, PWD);
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		vcam_panic("fuji: cannot open fixture %s", path);
		return -1;
	}
	fseek(file, 0, SEEK_END);
	long fsize = ftell(file);
	fseek(file, 0, SEEK_SET);
	unsigned char *buf = malloc(fsize);
	if (buf == NULL) vcam_panic("out of memory");
	if (fread(buf, 1, fsize, file) != (size_t)fsize) {
		vcam_panic("fuji: short read on %s", path);
	}
	fclose(file);

	// The fixture carries an 8-byte EOS live-view prefix; Fuji live view is
	// a plain JPEG (gphoto2: "no meta data wrapped around the jpeg data"),
	// so skip to the SOI marker
	long start = 0;
	for (long i = 0; i + 1 < fsize; i++) {
		if (buf[i] == 0xff && buf[i + 1] == 0xd8) {
			start = i;
			break;
		}
	}
	f->usb_lv_raw = buf;
	f->usb_lv_data = buf + start;
	f->usb_lv_size = (uint32_t)(fsize - start);
	return 0;
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
	add_prop_invisible_u32(cam, 0xd207, 0x0);
	add_prop_invisible_u32(cam, 0xd208, 0x0);
	add_prop_invisible_u32(cam, 0xd209, 0x0);
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

	// Properties gphoto2's camera_fuji_capture() requires (hard failures
	// when missing): ExposureTime gates its capture timeout, the 0xD208
	// control drives focus/shoot, 0xD209 AFStatus must leave 0x0001 for
	// its ready-poll loop to terminate
	add_prop_u32(cam, PTP_DPC_ExposureTime, 10000);
	add_prop_u16(cam, 0xd208, 0); // PTP_DPC_FUJI focus/shoot control
	add_prop_u16(cam, 0xd209, 2); // PTP_DPC_FUJI_AFStatus: 2 = ready to shoot

	// Live view + open-capture handling for the capture-preview flow
	if (fuji_usb_load_lv(cam))
		return -1;
	vcam_register_opcode(cam, PTP_OC_InitiateOpenCapture, fuji_usb_opencapture, NULL);
	vcam_register_opcode(cam, PTP_OC_TerminateOpenCapture, fuji_usb_terminateopencapture, NULL);
	vcam_register_opcode(cam, PTP_OC_GetObjectInfo, fuji_usb_getobjectinfo, NULL);
	vcam_register_opcode(cam, PTP_OC_GetObject, fuji_usb_getobject, NULL);
	vcam_register_opcode(cam, PTP_OC_DeleteObject, fuji_usb_deleteobject, NULL);

	// TODO: implement raw conv mode

	return 0;
}
