#include "ptp.h"

#include "data.h"

static struct EosInfo {
	int first_events;
	int lv_ready;

	struct EventQueue {
		uint16_t code;
		uint32_t data;
	}queue[10];
	int queue_length;

	int calls_to_liveview;
}eos_info = {0};

struct EosEventUint {
	uint32_t size;
	uint32_t type;
	uint32_t code;
	uint32_t value;
};

static int ptp_eos_viewfinder_data(vcamera *cam, ptpcontainer *ptp) {
	usleep(1000 * 10);
	eos_info.calls_to_liveview++;

	if (eos_info.calls_to_liveview < 15) {
		ptp_response(cam,PTP_RC_CANON_NotReady,0);
		return 1;
	}

	ptp_senddata(cam, ptp->code, (unsigned char *)eos_lv_jpg, eos_lv_jpg_size);

	ptp_response(cam,PTP_RC_OK,0);
	return 1;
}

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
	//ptp_senddata(cam, ptp->code, (unsigned char *)buffer, curr);
	ptp_response(cam,PTP_RC_OK,0);
	return 1;
}

static int ptp_eos_set_property_payload(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	uint32_t *dat = (uint32_t *)data;

	switch (dat[1]) {
	case PTP_PC_EOS_CaptureDestination:
		eos_info.lv_ready = 1;
		break;
	}

	if (eos_info.queue_length < 10) {
		eos_info.queue[eos_info.queue_length].code = dat[1];
		eos_info.queue[eos_info.queue_length].data = dat[2];
		eos_info.queue_length++;
	}

	ptp_response(cam,PTP_RC_OK,0);
	return 1;
}

static int ptp_eos_events(vcamera *cam, ptpcontainer *ptp) {
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
