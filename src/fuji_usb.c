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

static int ptp_fuji_getobjecthandles_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;

	unsigned char data[128];
	int of = 0;

	if (ptp->params[0] == 0xffffffff & ptp->params[1] == 0x0 && ptp->params[2] == 0x0) {
		of += ptp_write_u32(data + of, 0x0);
	}

	ptp_senddata(cam, ptp->code, data, of);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

#if 0
0x5001 0x5003 0x5005 0x500a 0x500b 0x500c 0x500e 0x500f 0x5011 0x5012 0x5015 0x5018 0x5019 0x501c

0xd001
0xd002
0xd003
0xd004
0xd005
0xd007
0xd008
0xd009
0xd00a
0xd00b
0xd00c
0xd00d
0xd00e
0xd00f
0xd010
0xd011
0xd012
0xd013
0xd014
0xd015
0xd016
0xd017
0xd018
0xd019
0xd01a
0xd01b
0xd01c

0xd100
0xd101
0xd102
0xd103
0xd104
0xd105
0xd106
0xd107
0xd108
0xd109
0xd10a
0xd10b
0xd10c
0xd10d
0xd10e
0xd10f
0xd110
0xd111
0xd112
0xd113
0xd114
0xd115
0xd116
0xd117
0xd118
0xd119
0xd11a
0xd11b
0xd11c
0xd11d
0xd11e
0xd11f
0xd120
0xd121
0xd122
0xd123
0xd124
0xd125
0xd126
0xd127
0xd128
0xd129
0xd12a
0xd12b
0xd12c
0xd12d
0xd12e
0xd12f
0xd130
0xd131
0xd132
0xd133
0xd134
0xd135
0xd136
0xd137
0xd138
0xd139
0xd13a
0xd13b
0xd13c
0xd13d
0xd13e
0xd13f
0xd140
0xd141
0xd142
0xd143
0xd144
0xd145
0xd146
0xd147
0xd148
0xd149
0xd14a
0xd14b
0xd14c
0xd14d
0xd14e
0xd14f
0xd150
0xd151
0xd152
0xd153
0xd154
0xd155
0xd157
0xd158
0xd159
0xd15a
0xd15b
0xd15c
0xd15d
0xd15e
0xd15f
0xd160
0xd161

0xd200
0xd201
0xd202
0xd203
0xd204
0xd205
0xd206
0xd207
0xd208
0xd209
0xd20a
0xd20b
0xd20c
0xd20d
0xd20e
0xd20f
0xd210
0xd211
0xd212
0xd213
0xd214
0xd215
0xd216
0xd217
0xd218
0xd219
0xd21a
0xd21b
// MTP range
0xd406
0xd407
#endif

static int fake_dev_info(vcam *cam, ptpcontainer *ptp) {
	vcam_generic_send_file("bin/fuji/xh1_device_info.bin", cam, 12, ptp);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
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
			ptp_write_string(d, "FF129506,F129506");
			struct PtpPropDesc desc = {0};
			desc.value = d;
			desc.DataType = PTP_TC_STRING;
			vcam_register_prop(cam, PTP_DPC_FUJI_IOPCode, &desc);
		}
		//add_prop_u32(cam, PTP_DPC_FUJI_UnknownD040, 0x1);
		add_prop_u16(cam, PTP_DPC_FUJI_UnknownD21C, 0x1);
		vcam_register_opcode(cam, 0x1007, ptp_fuji_getobjecthandles_write, NULL);
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

	vcam_register_opcode(cam, PTP_OC_GetDeviceInfo, fake_dev_info, NULL);

	// TODO: implement raw conv mode

	return 0;
}
