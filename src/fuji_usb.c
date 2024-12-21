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

int fuji_usb_init_cam(vcam *cam) {
	struct Fuji *f = fuji(cam);

	fuji_register_d212(cam);

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
		add_prop_u32(cam, PTP_DPC_FUJI_UnknownD040, 0x1);
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

	// TODO: implement raw conv mode

	return 0;
}
