// Emulator for non-standard Canon PTP
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <gphoto.h>
#include <canon.h>

char *extern_manufacturer_info = CANON_MANUFACT;
char *extern_model_name = CANON_MODEL;
char *extern_device_version = CANON_DEV_VER;
char *extern_serial_no = CANON_SERIAL_NO;

extern unsigned char bin_eos_events_bin[];
extern unsigned int bin_eos_events_bin_len;
extern unsigned char eos_lv_jpg[];
extern unsigned int eos_lv_jpg_size;

static struct EosInfo {
	int first_events;
	int lv_ready;

	struct EventQueue {
		uint16_t code;
		uint32_t data;
	}queue[10]; // TODO: We need to do more than 10
	int queue_length;

	int calls_to_liveview;
} eos_info = {0};

struct EosEventUint {
	uint32_t size;
	uint32_t type;
	uint32_t code;
	uint32_t value;
};

int vcam_vendor_setup(vcamera *cam) {
	return 0;
}

int ptp_eos_viewfinder_data(vcamera *cam, ptpcontainer *ptp) {
	usleep(1000 * 10);
	eos_info.calls_to_liveview++;

	if (eos_info.calls_to_liveview < 15) {
		ptp_response(cam, PTP_RC_CANON_NotReady, 0);
		return 1;
	}

	ptp_senddata(cam, ptp->code, (unsigned char *)eos_lv_jpg, eos_lv_jpg_size);

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_eos_generic(vcamera *cam, ptpcontainer *ptp) {
	switch (ptp->code) {
	case PTP_OC_EOS_SetRemoteMode:
	case PTP_OC_EOS_SetEventMode: {
		CHECK_PARAM_COUNT(1);
	}
		eos_info.first_events = 1;
		break;
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_eos_set_property(vcamera *cam, ptpcontainer *ptp) {
	//ptp_senddata(cam, ptp->code, (unsigned char *)buffer, curr);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_eos_set_property_payload(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	uint32_t *dat = (uint32_t *)data;
	uint32_t length = dat[0];
	uint32_t code = dat[1];
	uint32_t value = dat[2];

	// We don't support multi-length params
	assert(length == 0xc);

	switch (code) {
	case PTP_PC_EOS_CaptureDestination:
		eos_info.lv_ready = 1;
		break;
	}

	if (eos_info.queue_length < 10) {
		eos_info.queue[eos_info.queue_length].code = dat[1];
		eos_info.queue[eos_info.queue_length].data = dat[2];
		eos_info.queue_length++;
	}

	ptp_notify_change(code, value);

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_eos_remote_release(vcamera *cam, ptpcontainer *ptp) {
	if (ptp->code == PTP_OC_EOS_RemoteReleaseOff) {
		vcam_log("CANON: Shutter up\n");
	} else if (ptp->code == PTP_OC_EOS_RemoteReleaseOn) {
		if (ptp->params[0] == 1) {
			vcam_log("CANON: Shutter half down\n");
			usleep(1000 * 2000);
		} else if (ptp->params[0] == 2) {
			vcam_log("CANON: Shutter full down\n");
			usleep(1000 * 200);
		}
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int vusb_ptp_eos_events(vcamera *cam, ptpcontainer *ptp) {
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
			if (curr >= 1000)
				exit(1);
		}

		eos_info.queue_length = 0;

		ptp_senddata(cam, ptp->code, (unsigned char *)buffer, curr);
		free(buffer);
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}
