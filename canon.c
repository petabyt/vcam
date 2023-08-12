#include "ptp.h"

#include "data.h"

static struct EosInfo {
	int first_events;
	struct EventQueue {
		uint16_t code;
		uint32_t data;
	}queue[10];
	int queue_length;
}eos_info = {0};

struct EosEventUint {
	uint32_t size;
	uint32_t type;
	uint32_t code;
	uint32_t value;
};

static int ptp_eos_generic(vcamera *cam, ptpcontainer *ptp) {
	switch (ptp->code) {
	case PTP_OC_EOS_SetRemoteMode:
	case PTP_OC_EOS_SetEventMode:
		{ CHECK_PARAM_COUNT(1); }
		eos_info.first_events = 1;
		break;
	}

	ptp_response(cam,PTP_RC_OK,0);
	return 1;
}

static int ptp_eos_set_property(vcamera *cam, ptpcontainer *ptp) {
	ptp_response(cam,PTP_RC_OK,0);
	return 1;
}

static int ptp_eos_set_property_payload(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	uint32_t *dat = (uint32_t *)data;

	if (eos_info.queue_length < 10) {
		eos_info.queue[eos_info.queue_length].code = dat[1];
		eos_info.queue[eos_info.queue_length].data = dat[2];
		eos_info.queue_length++;
	}

	ptp_response(cam,PTP_RC_OK,0);
	return 1;
}

static int
ptp_eos_events(vcamera *cam, ptpcontainer *ptp) {
	CHECK_PARAM_COUNT(0);

	if (eos_info.first_events) {
		ptp_senddata(cam, ptp->code, (unsigned char *)bin_eos_events_bin, bin_eos_events_bin_len);
		eos_info.first_events = 0;
	} else {
		void *buffer = malloc(1000);
		int curr = 0;

		if (eos_info.queue_length == 0) {
			uint32_t nothing[2] = {0, 0};
			memcpy(buffer, nothing, sizeof(nothing));
			curr = sizeof(nothing);
		}

		for (int i = 0; i < eos_info.queue_length; i++) {
			struct EosEventUint *prop = (struct EosEventUint *)((uint8_t *)buffer + curr);
			prop->size = sizeof(struct EosEventUint);
			prop->type = PTP_EC_EOS_PropValueChanged;
			prop->code = eos_info.queue[i].code;
			prop->value = eos_info.queue[i].data;
			curr += prop->size;
			if (curr >= 1000) exit(1);
		}

		eos_info.queue_length = 0;
		
		ptp_senddata(cam, ptp->code, (unsigned char *)buffer, curr);
		free(buffer);
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

struct ptp_function ptp_functions_canon[] = {
		{0x9101,	ptp_eos_generic, NULL },
		{0x9102,	ptp_eos_generic, NULL },
		{0x9103,	ptp_eos_generic, NULL },
		{0x9104,	ptp_eos_generic, NULL },
		{0x9107,	ptp_eos_generic, NULL },
		{0x9105,	ptp_eos_generic, NULL },
		{0x9106,	ptp_eos_generic, NULL },
		{PTP_OC_EOS_SetDevicePropValueEx,	ptp_eos_set_property, ptp_eos_set_property_payload},
		{0x9127,	ptp_eos_generic, NULL },
		{0x910b,	ptp_eos_generic, NULL },
		{0x9108,	ptp_eos_generic, NULL },
		{0x9109,	ptp_eos_generic, NULL },
		{0x910c,	ptp_eos_generic, NULL },
		{0x910e,	ptp_eos_generic, NULL },
		{0x910f,	ptp_eos_generic, NULL },
		{0x9115,	ptp_eos_generic, NULL },
		{0x9114,	ptp_eos_generic, NULL },
		{0x9113,	ptp_eos_generic, NULL },
		{PTP_OC_EOS_GetEvent,	ptp_eos_events, NULL },
		{0x9117,	ptp_eos_generic, NULL },
		{0x9120,	ptp_eos_generic, NULL },
		{0x91f0,	ptp_eos_generic, NULL },
		{0x9118,	ptp_eos_generic, NULL },
		{0x9121,	ptp_eos_generic, NULL },
		{0x91f1,	ptp_eos_generic, NULL },
		{0x911d,	ptp_eos_generic, NULL },
		{0x910a,	ptp_eos_generic, NULL },
		{0x911b,	ptp_eos_generic, NULL },
		{0x911c,	ptp_eos_generic, NULL },
		{0x911e,	ptp_eos_generic, NULL },
		{0x911a,	ptp_eos_generic, NULL },
		{PTP_OC_EOS_GetViewFinderData,	ptp_eos_generic, NULL },
		{0x9154,	ptp_eos_generic, NULL },
		{0x9160,	ptp_eos_generic, NULL },
		{0x9155,	ptp_eos_generic, NULL },
		{0x9157,	ptp_eos_generic, NULL },
		{0x9158,	ptp_eos_generic, NULL },
		{0x9159,	ptp_eos_generic, NULL },
		{0x915a,	ptp_eos_generic, NULL },
		{0x911f,	ptp_eos_generic, NULL },
		{0x91fe,	ptp_eos_generic, NULL },
		{0x91ff,	ptp_eos_generic, NULL },
		{0x9128,	ptp_eos_generic, NULL },
		{0x9129,	ptp_eos_generic, NULL },
		{0x912d,	ptp_eos_generic, NULL },
		{0x912e,	ptp_eos_generic, NULL },
		{0x912f,	ptp_eos_generic, NULL },
		{0x912c,	ptp_eos_generic, NULL },
		{0x9130,	ptp_eos_generic, NULL },
		{0x9131,	ptp_eos_generic, NULL },
		{0x9132,	ptp_eos_generic, NULL },
		{0x9133,	ptp_eos_generic, NULL },
		{0x9134,	ptp_eos_generic, NULL },
		{0x912b,	ptp_eos_generic, NULL },
		{0x9135,	ptp_eos_generic, NULL },
		{0x9136,	ptp_eos_generic, NULL },
		{0x9137,	ptp_eos_generic, NULL },
		{0x9138,	ptp_eos_generic, NULL },
		{0x9139,	ptp_eos_generic, NULL },
		{0x913a,	ptp_eos_generic, NULL },
		{0x913b,	ptp_eos_generic, NULL },
		{0x913c,	ptp_eos_generic, NULL },
		{0x91da,	ptp_eos_generic, NULL },
		{0x91db,	ptp_eos_generic, NULL },
		{0x91dc,	ptp_eos_generic, NULL },
		{0x91dd,	ptp_eos_generic, NULL },
		{0x91de,	ptp_eos_generic, NULL },
		{0x91d8,	ptp_eos_generic, NULL },
		{0x91d9,	ptp_eos_generic, NULL },
		{0x91d7,	ptp_eos_generic, NULL },
		{0x91d5,	ptp_eos_generic, NULL },
		{0x902f,	ptp_eos_generic, NULL },
		{0x9141,	ptp_eos_generic, NULL },
		{0x9142,	ptp_eos_generic, NULL },
		{0x9143,	ptp_eos_generic, NULL },
		{0x913f,	ptp_eos_generic, NULL },
		{0x9033,	ptp_eos_generic, NULL },
		{0x9068,	ptp_eos_generic, NULL },
		{0x9069,	ptp_eos_generic, NULL },
		{0x906a,	ptp_eos_generic, NULL },
		{0x906b,	ptp_eos_generic, NULL },
		{0x906c,	ptp_eos_generic, NULL },
		{0x906d,	ptp_eos_generic, NULL },
		{0x906e,	ptp_eos_generic, NULL },
		{0x906f,	ptp_eos_generic, NULL },
		{0x913d,	ptp_eos_generic, NULL },
		{0x9180,	ptp_eos_generic, NULL },
		{0x9181,	ptp_eos_generic, NULL },
		{0x9182,	ptp_eos_generic, NULL },
		{0x9183,	ptp_eos_generic, NULL },
		{0x9184,	ptp_eos_generic, NULL },
		{0x9185,	ptp_eos_generic, NULL },
		{0x9140,	ptp_eos_generic, NULL },
		{0x9801,	ptp_eos_generic, NULL },
		{0x9802,	ptp_eos_generic, NULL },
		{0x9803,	ptp_eos_generic, NULL },
		{0x9804,	ptp_eos_generic, NULL },
		{0x9805,	ptp_eos_generic, NULL },
		{0x91c0,	ptp_eos_generic, NULL },
		{0x91c1,	ptp_eos_generic, NULL },
		{0x91c2,	ptp_eos_generic, NULL },
		{0x91c3,	ptp_eos_generic, NULL },
		{0x91c4,	ptp_eos_generic, NULL },
		{0x91c5,	ptp_eos_generic, NULL },
		{0x91c6,	ptp_eos_generic, NULL },
		{0x91c7,	ptp_eos_generic, NULL },
		{0x91c8,	ptp_eos_generic, NULL },
		{0x91c9,	ptp_eos_generic, NULL },
		{0x91ca,	ptp_eos_generic, NULL },
		{0x91cb,	ptp_eos_generic, NULL },
		{0x91cc,	ptp_eos_generic, NULL },
		{0x91ce,	ptp_eos_generic, NULL },
		{0x91cf,	ptp_eos_generic, NULL },
		{0x91d0,	ptp_eos_generic, NULL },
		{0x91d1,	ptp_eos_generic, NULL },
		{0x91d2,	ptp_eos_generic, NULL },
		{0x91e1,	ptp_eos_generic, NULL },
		{0x91e2,	ptp_eos_generic, NULL },
		{0x91e3,	ptp_eos_generic, NULL },
		{0x91e4,	ptp_eos_generic, NULL },
		{0x91e5,	ptp_eos_generic, NULL },
		{0x91e6,	ptp_eos_generic, NULL },
		{0x91e7,	ptp_eos_generic, NULL },
		{0x91e8,	ptp_eos_generic, NULL },
		{0x91e9,	ptp_eos_generic, NULL },
		{0x91ea,	ptp_eos_generic, NULL },
		{0x91eb,	ptp_eos_generic, NULL },
		{0x91ec,	ptp_eos_generic, NULL },
		{0x91ed,	ptp_eos_generic, NULL },
		{0x91ee,	ptp_eos_generic, NULL },
		{0x91ef,	ptp_eos_generic, NULL },
		{0x91f8,	ptp_eos_generic, NULL },
		{0x91f9,	ptp_eos_generic, NULL },
		{0x91f2,	ptp_eos_generic, NULL },
		{0x91f3,	ptp_eos_generic, NULL },
		{0x91f4,	ptp_eos_generic, NULL },
		{0x91f7,	ptp_eos_generic, NULL },
		{0x9122,	ptp_eos_generic, NULL },
		{0x9123,	ptp_eos_generic, NULL },
		{0x9124,	ptp_eos_generic, NULL },
		{0x91f5,	ptp_eos_generic, NULL },
		{0x91f6,	ptp_eos_generic, NULL },
		{0x9052,	ptp_eos_generic, NULL },
		{0x9053,	ptp_eos_generic, NULL },
		{0x9057,	ptp_eos_generic, NULL },
		{0x9058,	ptp_eos_generic, NULL },
		{0x9059,	ptp_eos_generic, NULL },
		{0x905a,	ptp_eos_generic, NULL },
		{0x905f,	ptp_eos_generic, NULL },
		{0x9996,	ptp_eos_generic, NULL },
};
