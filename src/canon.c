// Emulator for non-standard Canon PTP
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <vcam.h>
#include <canon.h>

#define EOS_LV_JPEG "bin/eos_liveview.jpg"
#define EOS_EVENTS_BIN "bin/eos_events.bin"

static struct EosInfo {
	int first_events;
	int lv_ready;
	int calls_to_liveview;
} eos_info = {0};

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
		ptp_response(cam, PTP_RC_CANON_NotReady, 0);
		return 1;
	}

	vcam_generic_send_file(EOS_LV_JPEG, cam, ptp);

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int ptp_eos_generic(vcamera *cam, ptpcontainer *ptp) {
	switch (ptp->code) {
	case PTP_OC_EOS_SetRemoteMode:
	case PTP_OC_EOS_SetEventMode:
		{ CHECK_PARAM_COUNT(1); }
		eos_info.first_events = 1;
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	vcam_log("Error: Error generic 0x%X opcode\n", ptp->code);
	ptp_response(cam, PTP_RC_OperationNotSupported, 0);
	return 1;
}

static int ptp_eos_exec_evproc(vcamera *cam, ptpcontainer *ptp) {
	vcam_log("Evproc %d %d\n", ptp->params[0], ptp->params[1]);
	// wait until payload
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}
static int ptp_eos_exec_evproc_data(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	vcam_log("Evproc data phase\n");
	return 1;
}

static int ptp_eos_set_property(vcamera *cam, ptpcontainer *ptp) {
	ptp_response(cam, PTP_RC_OK, 0);
	// Wait until the payload
	return 1;
}
static int ptp_eos_set_property_data(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
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

	ptp_notify_event(cam, code, value);

#if 0
	struct CamGenericEvent ev = ptp_pop_event(cam);
	printf("Code: %X\n", ev.code);
#endif

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int ptp_eos_remote_release(vcamera *cam, ptpcontainer *ptp) {
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

static int eos_pack_all_props(vcamera *cam, void *buf, int *size) {
	int cnt = 0;

	struct PtpPropList *list = NULL;

	printf("%X\n", cam->list->code);

	for (list = cam->list; list->next != NULL; list = list->next) {
		cnt += ptp_write_u32(buf + cnt, 16);
		cnt += ptp_write_u32(buf + cnt, PTP_EC_EOS_PropValueChanged);
		cnt += ptp_write_u32(buf + cnt, list->code);		
		memcpy(buf + cnt, list->data, list->length);
		cnt += list->length;
	}

	for (list = cam->list; list->next != NULL; list = list->next) {
		int size = list->avail_size * list->avail_cnt;
		cnt += ptp_write_u32(buf + cnt, 5 * 4 + size);
		cnt += ptp_write_u32(buf + cnt, PTP_EC_EOS_AvailListChanged);
		cnt += ptp_write_u32(buf + cnt, list->code);
		cnt += ptp_write_u32(buf + cnt, 3);
		cnt += ptp_write_u32(buf + cnt, list->avail_cnt);

		memcpy(buf + cnt, list->avail, size);
		cnt += size;
	}

	(*size) = cnt;

	return 0;
};

static int vusb_ptp_eos_events(vcamera *cam, ptpcontainer *ptp) {
	CHECK_PARAM_COUNT(0);

#if 0
	if (eos_info.first_events) {
		vcam_generic_send_file(EOS_EVENTS_BIN, cam, ptp);
		eos_info.first_events = 0;
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}
#endif

	if (eos_info.first_events) {
		int size = 0;
		char *buffer = malloc(10000);
		eos_pack_all_props(cam, buffer, &size);

		ptp_senddata(cam, ptp->code, (unsigned char *)buffer, size);
		ptp_response(cam, PTP_RC_OK, 0);

		free(buffer);

		eos_info.first_events = 0;
		return 1;
	}

#if 0
	{
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
#endif

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}


struct ptp_function ptp_functions_canon[] = {
	{PTP_OC_EOS_GetStorageIDs,			ptp_eos_generic, NULL },
	{PTP_OC_EOS_GetStorageInfo,			ptp_eos_generic, NULL },
	{0x9103,	ptp_eos_generic, NULL },
	{0x9104,	ptp_eos_generic, NULL },
	{0x9107,	ptp_eos_generic, NULL },
	{0x9105,	ptp_eos_generic, NULL },
	{0x9106,	ptp_eos_generic, NULL },
	{PTP_OC_EOS_GetObjectInfoEx,		ptp_eos_generic, NULL },
	{PTP_OC_EOS_SetDevicePropValueEx,	ptp_eos_set_property, ptp_eos_set_property_data},
	{PTP_OC_EOS_GetDevicePropValue,		ptp_eos_generic, NULL },
	{0x910b,	ptp_eos_generic, NULL },
	{0x9108,	ptp_eos_generic, NULL },
	{0x9109,	ptp_eos_generic, NULL },
	{0x910c,	ptp_eos_generic, NULL },
	{0x910e,	ptp_eos_generic, NULL },
	{0x910f,	ptp_eos_generic, NULL },
	{0x9115,	ptp_eos_generic, NULL },
	{0x9114,	ptp_eos_generic, NULL },
	{0x9113,	ptp_eos_generic, NULL },
	{PTP_OC_EOS_GetEvent,				vusb_ptp_eos_events, NULL },
	{0x9117,	ptp_eos_generic, NULL },
	{0x9120,	ptp_eos_generic, NULL },
	{0x91f0,	ptp_eos_generic, NULL },
	{0x9118,	ptp_eos_generic, NULL },
	{0x9121,	ptp_eos_generic, NULL },
	{0x91f1,	ptp_eos_generic, NULL },
	{0x911d,	ptp_eos_generic, NULL },
	{0x910a,	ptp_eos_generic, NULL },
	{PTP_OC_EOS_SetUILock,				ptp_eos_generic, NULL },
	{PTP_OC_EOS_ResetUILock,			ptp_eos_generic, NULL },
	{0x911e,	ptp_eos_generic, NULL },
	{0x911a,	ptp_eos_generic, NULL },
	{PTP_OC_EOS_GetViewFinderData,	ptp_eos_viewfinder_data, NULL },
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
	{PTP_OC_EOS_RemoteReleaseOn,	ptp_eos_remote_release, NULL },
	{PTP_OC_EOS_RemoteReleaseOff,	ptp_eos_remote_release, NULL },
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
	{PTP_OC_EOS_ExecuteEventProc,	ptp_eos_exec_evproc, ptp_eos_exec_evproc_data },
	{0x9053,	ptp_eos_generic, NULL },
	{0x9057,	ptp_eos_generic, NULL },
	{0x9058,	ptp_eos_generic, NULL },
	{0x9059,	ptp_eos_generic, NULL },
	{0x905a,	ptp_eos_generic, NULL },
	{0x905f,	ptp_eos_generic, NULL },
	{PTP_OC_CHDK,						ptp_eos_generic, NULL },
	{PTP_OC_MagicLantern, 				ptp_eos_generic, NULL },
	{0, NULL, NULL},
};
