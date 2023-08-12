#include <camlib.h>
#include <fujiptp.h>

static struct FujiInfo {
	int function_mode;
	int camera_state;
	int remote_version;
}fuji_info = {
	2,
	0,
	0,
};

static int fuji_set_prop_supported(int code) {
	int codes[] = {
		PTP_PC_FUJI_FunctionMode,
		PTP_PC_FUJI_RemoteVersion,
	};

	for (int i = 0; i < ((int)(sizeof(codes) / sizeof(codes[0]))); i++) {
		if (codes[i] == code) {
			return 0;
		}
	}

	return 1;
}

static int fuji_set_property(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	uint32_t *uint = (uint32_t *)data;

	switch (ptp->params[0]) {
	case PTP_PC_FUJI_FunctionMode:
		fuji_info.function_mode = uint[0];
		break;
	case PTP_PC_FUJI_RemoteVersion:
		fuji_info.remote_version = uint[0];
		break;
	}

	ptp_response (cam, PTP_RC_OK, 0);

	return 1;
}

static int fuji_send_events(vcamera *cam, ptpcontainer *ptp) {
	struct PtpFujiEvents *ev = malloc(100);
	memset(ev, 0, 100);
	int length = 2;

	if (fuji_info.camera_state == 0) {
		fuji_info.camera_state = FUJI_FULL_ACCESS;
		ev->events[ev->length].code = PTP_PC_FUJI_CameraState;
		ev->events[ev->length].value = FUJI_FULL_ACCESS;
		ev->length++;
	}

	ptp_senddata (cam, ptp->code, (unsigned char *)ev, length + (6 * ev->length));
	free(ev);

	ptp_response (cam, PTP_RC_OK, 0);

	return 0;
}

static int fuji_get_property(vcamera *cam, ptpcontainer *ptp) {
	int data = -1;
	switch (ptp->params[0]) {
	case PTP_PC_FUJI_EventsList:
		return fuji_send_events(cam, ptp);
	case PTP_PC_FUJI_ObjectCount:
		data = 1;
		ptp_senddata (cam, ptp->code, &data, 4);
		break;
	case PTP_PC_FUJI_FunctionMode:
		data = fuji_info.function_mode;
		ptp_senddata (cam, ptp->code, &data, 4);
		break;
	case PTP_PC_FUJI_CameraState:
		data = fuji_info.camera_state;
		ptp_senddata (cam, ptp->code, &data, 4);
		break;
	case PTP_PC_FUJI_ImageGetVersion:
		data = 1;
		ptp_senddata (cam, ptp->code, &data, 4);
		break;
	case PTP_PC_FUJI_ImageExploreVersion:
	case PTP_PC_FUJI_RemoteImageExploreVersion:
	case PTP_PC_FUJI_ImageGetLimitedVersion:
	case PTP_PC_FUJI_CompressionCutOff:
		break;
	case PTP_PC_FUJI_RemoteVersion:
		data = 0;
		ptp_senddata (cam, ptp->code, &data, 0);
		break;
	default:
		ptp_response (cam, PTP_RC_GeneralError, 0);
		return 1;
	}

	ptp_response (cam, PTP_RC_OK, 0);
	return 0;
}

struct ptp_function ptp_functions_fuji_x_a2[] = {
	{0x1234,	NULL, NULL },
};
