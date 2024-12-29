#include <stdlib.h>
#include <string.h>
#include <assert.h>
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

int fuji_usb_init_cam(vcam *cam) {
	struct Fuji *f = fuji(cam);
	f->rawconv_raf_path = "temp/raw.raf";

	mkdir("temp", 775);

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

			// FF129506 is image processor?

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

	// TODO: implement raw conv mode

	return 0;
}
