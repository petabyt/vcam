// Emulator for non-standard Fujifilm PTP/USB over TCP
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>

#include <vcam.h>

#include <fujiptp.h>
#include <cl_data.h>
#include "fuji.h"

// Copied from README @ https://github.com/malc0mn/ptp-ip
char fuji_device_info_x_t1[] = {0x8, 0x0, 0x0, 0x0, 0x16, 0x0, 0x0, 0x0, 0x12, 0x50, 0x4, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x2, 0x3, 0x0, 0x0, 0x0, 0x2, 0x0, 0x4, 0x0, 0x14, 0x0, 0x0, 0x0, 0xc, 0x50, 0x4, 0x0, 0x1, 0x2, 0x0, 0x9, 0x80, 0x2, 0x2, 0x0, 0x9, 0x80, 0xa, 0x80, 0x24, 0x0, 0x0, 0x0, 0x5, 0x50, 0x4, 0x0, 0x1, 0x2, 0x0, 0x2, 0x0, 0x2, 0xa, 0x0, 0x2, 0x0, 0x4, 0x0, 0x6, 0x80, 0x1, 0x80, 0x2, 0x80, 0x3, 0x80, 0x6, 0x0, 0xa, 0x80, 0xb, 0x80, 0xc, 0x80, 0x36, 0x0, 0x0, 0x0, 0x10, 0x50, 0x3, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x2, 0x13, 0x0, 0x48, 0xf4, 0x95, 0xf5, 0xe3, 0xf6, 0x30, 0xf8, 0x7d, 0xf9, 0xcb, 0xfa, 0x18, 0xfc, 0x65, 0xfd, 0xb3, 0xfe, 0x0, 0x0, 0x4d, 0x1, 0x9b, 0x2, 0xe8, 0x3, 0x35, 0x5, 0x83, 0x6, 0xd0, 0x7, 0x1d, 0x9, 0x6b, 0xa, 0xb8, 0xb, 0x26, 0x0, 0x0, 0x0, 0x1, 0xd0, 0x4, 0x0, 0x1, 0x1, 0x0, 0x2, 0x0, 0x2, 0xb, 0x0, 0x1, 0x0, 0x2, 0x0, 0x3, 0x0, 0x4, 0x0, 0x5, 0x0, 0x6, 0x0, 0x7, 0x0, 0x8, 0x0, 0x9, 0x0, 0xa, 0x0, 0xb, 0x0, 0x78, 0x0, 0x0, 0x0, 0x2a, 0xd0, 0x6, 0x0, 0x1, 0xff, 0xff, 0xff, 0xff, 0x0, 0x19, 0x0, 0x80, 0x2, 0x19, 0x0, 0x90, 0x1, 0x0, 0x80, 0x20, 0x3, 0x0, 0x80, 0x40, 0x6, 0x0, 0x80, 0x80, 0xc, 0x0, 0x80, 0x0, 0x19, 0x0, 0x80, 0x64, 0x0, 0x0, 0x40, 0xc8, 0x0, 0x0, 0x0, 0xfa, 0x0, 0x0, 0x0, 0x40, 0x1, 0x0, 0x0, 0x90, 0x1, 0x0, 0x0, 0xf4, 0x1, 0x0, 0x0, 0x80, 0x2, 0x0, 0x0, 0x20, 0x3, 0x0, 0x0, 0xe8, 0x3, 0x0, 0x0, 0xe2, 0x4, 0x0, 0x0, 0x40, 0x6, 0x0, 0x0, 0xd0, 0x7, 0x0, 0x0, 0xc4, 0x9, 0x0, 0x0, 0x80, 0xc, 0x0, 0x0, 0xa0, 0xf, 0x0, 0x0, 0x88, 0x13, 0x0, 0x0, 0x0, 0x19, 0x0, 0x0, 0x0, 0x32, 0x0, 0x40, 0x0, 0x64, 0x0, 0x40, 0x0, 0xc8, 0x0, 0x40, 0x14, 0x0, 0x0, 0x0, 0x19, 0xd0, 0x4, 0x0, 0x1, 0x1, 0x0, 0x1, 0x0, 0x2, 0x2, 0x0, 0x0, 0x0, 0x1, 0x0, 0x1e, 0x0, 0x0, 0x0, 0x7c, 0xd1, 0x6, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x4, 0x4, 0x2, 0x3, 0x1, 0x0, 0x0, 0x0, 0x0, 0x7, 0x7, 0x9, 0x10, 0x1, 0x0, 0x0, 0x0};

struct FujiPropEventSend {
	unsigned short code;
	unsigned int value;
};

enum CameraStates {
	// Initial state before ACK
	CAM_STATE_READY,
	// Idle in normal mode
	CAM_STATE_IDLE,
	// Setup phase of remote
	CAM_STATE_INIT_REMOTE,
	// Idle remote (gallery or liveview)
	CAM_STATE_IDLE_REMOTE,
};

uint8_t *fuji_get_ack_packet(vcamera *cam) {
	static struct FujiInitPacket p = {
		.length = 0x44,
		.type = PTPIP_INIT_COMMAND_ACK,
		.version = 0x0,
		.guid1 = 0x61b07008,
		.guid2 = 0x93458b0a,
		.guid3 = 0x5793e7b2,
		.guid4 = 0x50e036dd,
	};

	char *name = cam->conf->model;
	int i;
	for (i = 0; name[i] != '\0'; i++) {
		p.device_name[i * 2] = name[i];
		p.device_name[i * 2 + 1] = '\0';
	}

	p.device_name[i * 2 + 1] = '\0';

	return (uint8_t *)(&p);
}

int vcam_fuji_setup(vcamera *cam) {
	cam->function_mode = 2;
	cam->camera_state = 0;
	cam->remote_version = 0;
	cam->compress_small = 0;
	cam->no_compressed = 0;
	cam->camera_internal_state = CAM_STATE_READY;
	cam->sent_images = 0;

	// TODO: Better way to ignore folders (Fuji doesn't show them)
	cam->obj_count = ptp_get_object_count() - 1;

	vcam_log("Fuji: Found %d objects\n", cam->obj_count);

	// Check if remote mode is supported
	if (cam->conf->remote_version) {
		cam->camera_state = FUJI_FULL_ACCESS;
	} else {
		cam->camera_state = FUJI_REMOTE_ACCESS;
	}

	if (cam->conf->is_select_multiple_images) {
		vcam_log("Configuring fuji to select multiple images\n");
		cam->camera_state = FUJI_MULTIPLE_TRANSFER;
		// First dirent is DCIM
		first_dirent->next->id = 1;
	}

	return 0;
}

int fuji_is_compressed_mode(vcamera *cam) {
	return (int)(cam->compress_small);
}

static int send_partial_object(char *path, vcamera *cam, ptpcontainer *ptp) {
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		vcam_log("File %s not found\n", path);
		exit(-1);
	}

	if (fseek(file, (size_t)ptp->params[1], SEEK_SET) == -1) {
		vcam_log("fseek failure\n");
		exit(-1);
	}

	size_t max = (size_t)ptp->params[2];
	char *buffer = malloc(max);
	int read = fread(buffer, 1, max, file);

	ptp_senddata(cam, ptp->code, (unsigned char *)buffer, read);
	vcam_log("Generic sending %d\n", read);

	free(buffer);
	fclose(file);

	return 0;
}

static size_t generic_file_size(char *path) {
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		vcam_log("File %s not found", path);
		exit(-1);
	}

	fseek(file, 0, SEEK_END);
	size_t file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	fclose(file);

	return file_size;
}

int fuji_get_object_info(vcamera *cam, ptpcontainer *ptp) {
	vcam_log("Get object info for %d\n", ptp->params[0]);
	FILE *file = NULL;
	if (cam->no_compressed) {
		file = fopen(FUJI_DUMMY_OBJ_INFO, "rb");
	} else {
		file = fopen(FUJI_DUMMY_OBJ_INFO_CUT, "rb");
	}

	if (file == NULL) {
		vcam_log("File not found\n");
		exit(-1);
	}

	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *buffer = malloc(file_size);
	fread(buffer, 1, file_size, file);
	fclose(file);

	struct PtpObjectInfo *oi = (struct PtpObjectInfo *)buffer;
	if (cam->no_compressed) {
		oi->compressed_size = generic_file_size(FUJI_DUMMY_JPEG_FULL);
	} else {
		oi->compressed_size = generic_file_size(FUJI_DUMMY_JPEG_COMPRESSED);
	}

	ptp_senddata(cam, ptp->code, (unsigned char *)buffer, file_size);

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

int fuji_get_partial_object(vcamera *cam, ptpcontainer *ptp) {
	vcam_log("GetPartialObject request for %d\n", ptp->params[0]);
	if (cam->no_compressed) {
		send_partial_object(FUJI_DUMMY_JPEG_FULL, cam, ptp);
	} else {
		send_partial_object(FUJI_DUMMY_JPEG_COMPRESSED, cam, ptp);
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

int fuji_set_prop_supported(vcamera *cam, int code) {
	int codes[] = {
	    PTP_PC_FUJI_CameraState,
	    PTP_PC_FUJI_FunctionMode,
	    PTP_PC_FUJI_GetObjectVersion,
	    PTP_PC_FUJI_NoCompression,
	    PTP_PC_FUJI_CompressSmall,
		PTP_PC_FUJI_ImageGetVersion
	};

	int codes_remote_only[] = {
	    PTP_PC_FUJI_RemoteVersion,
	    PTP_PC_FUJI_RemoteGetObjectVersion,		
	};

	for (size_t i = 0; i < (sizeof(codes) / sizeof(codes[0])); i++) {
		if (codes[i] == code) return 0;
	}

	if (cam->conf->remote_version) {
		for (size_t i = 0; i < (sizeof(codes_remote_only) / sizeof(codes_remote_only[0])); i++) {
			if (codes_remote_only[i] == code) return 0;
		}
	}

	vcam_log("Request to set unknown property %X\n", code);

	return 1;
}

int fuji_set_property(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	uint32_t *uint = (uint32_t *)data;
	uint16_t *uint16 = (uint16_t *)data;

	// TODO: More asserts for len

	vcam_log("Set property %X -> %X (%d)\n", ptp->params[0], uint[0], len);

	switch (ptp->params[0]) {
	case PTP_PC_FUJI_FunctionMode:
		assert(len == 2);
		cam->function_mode = uint[0];
//		usleep(1000 * 2000); // Fuji accepts OK on the camera here
		break;
	case PTP_PC_FUJI_RemoteVersion:
		assert(len == 4);
		cam->remote_version = uint[0];
		break;
	case PTP_PC_FUJI_GetObjectVersion:
		break;
	case PTP_PC_FUJI_CompressSmall:
		cam->compress_small = uint16[0];
		break;
	case PTP_PC_FUJI_NoCompression:
		assert(len == 2);
		assert(uint16[0] == 1 || uint16[0] == 0);
		if (uint16[0] == 1) usleep(1000 * 500); // Fuji seems to take a while here
		cam->no_compressed = uint16[0];
		break;
	case PTP_PC_FUJI_RemoteGetObjectVersion:
		assert(len == 4);
		break;
	case PTP_PC_FUJI_CameraState:
		assert(len == 2);
		ptp_notify_event(cam, PTP_PC_FUJI_CameraState, uint16[0]);
		ptp_notify_event(cam, PTP_PC_FUJI_SelectedImgsMode, 1);
		ptp_notify_event(cam, PTP_PC_FUJI_ObjectCount, cam->obj_count);
		ptp_notify_event(cam, PTP_PC_FUJI_Unknown_D52F, 1);
		ptp_notify_event(cam, PTP_PC_FUJI_Unknown_D400, 1);
		ptp_notify_event(cam, PTP_PC_FUJI_ObjectCount, cam->obj_count);
		break;
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

// Helper func only for fuji_send_events
static void add_events(struct PtpFujiEvents *ev, struct FujiPropEventSend *p, size_t length) {
	for (int i = 0; i < length; i++) {
		ev->events[ev->length].code = p[i].code;
		ev->events[ev->length].value = p[i].value;
		ev->length++;
	}
}

extern struct ptp_interrupt *first_interrupt;

int fuji_send_events(vcamera *cam, ptpcontainer *ptp) {
	struct PtpFujiEvents *ev = calloc(1, 4096);

	struct GenericEvent ev_info;
	while (!ptp_pop_event(cam, &ev_info)) {
		vcam_log("Fuji: Popping event from stack\n");
		ev->events[ev->length].code = ev_info.code;
		ev->events[ev->length].value = ev_info.value;
		ev->length++;
	}

	if (cam->camera_internal_state == CAM_STATE_READY) {
		ev->events[ev->length].code = PTP_PC_FUJI_CameraState;
		ev->events[ev->length].value = cam->camera_state;
		ev->length++;

		if (cam->camera_state == FUJI_MULTIPLE_TRANSFER) {
			ev->events[ev->length].code = PTP_PC_FUJI_SelectedImgsMode;
			ev->events[ev->length].value = 1;
			ev->length++;
		} else {
			ev->events[ev->length].code = PTP_PC_FUJI_ObjectCount2;
			ev->events[ev->length].value = cam->obj_count;
			ev->length++;

			ev->events[ev->length].code = PTP_PC_FUJI_ObjectCount;
			ev->events[ev->length].value = cam->obj_count;
			ev->length++;
		}

		cam->camera_internal_state = CAM_STATE_IDLE;
	}

	// Properties sent over on init, by newer cams
	if (cam->camera_internal_state == CAM_STATE_IDLE_REMOTE) {
		struct FujiPropEventSend newer_remote_props[] = {
			{PTP_PC_FUJI_Unknown_D52F, 1},
			{PTP_PC_FUJI_Unknown_D400, 1},
		};
		add_events(ev, newer_remote_props, sizeof(newer_remote_props) / sizeof(newer_remote_props[0]));
	}

	if (cam->camera_internal_state == CAM_STATE_IDLE_REMOTE) {
		// Taken from a dump, not documented yet
		struct FujiPropEventSend remote_props[] = {
			{PTP_PC_FUJI_DeviceError, 0},
			{PTP_PC_FlashMode, 0x800a},
			{PTP_PC_CaptureDelay, 0},
			{PTP_PC_FUJI_CaptureRemaining, 0x761},
			{PTP_PC_FUJI_MovieRemainingTime, 0x19d1},
			{PTP_PC_ExposureProgramMode, 0x3},
			{PTP_PC_FUJI_BatteryLevel, 0xa},
			{PTP_PC_FUJI_Quality, 0x4},
			{PTP_PC_FUJI_ImageAspectRatio, 0xa},
			{PTP_PC_FUJI_ExposureIndex, 0x80003200},
			{PTP_PC_FUJI_MovieISO, 0x80003200},
			{PTP_PC_FUJI_ShutterSpeed2, 0xffffffff},
			{PTP_PC_FUJI_CommandDialMode, 0x0},
			{PTP_PC_FNumber, 0xa},
			{PTP_PC_ExposureBiasCompensation, 0x0},
			{PTP_PC_WhiteBalance, 0x2},
			{PTP_PC_FUJI_FilmSimulation, 0x6},
			{PTP_PC_FocusMode, 0x8001},
			{PTP_PC_FUJI_FocusMeteringMode, 0x03020604},
			{PTP_PC_FUJI_AFStatus, 0x0},
			{PTP_PC_FUJI_DriveMode, 0xa}
		};

		add_events(ev, remote_props, sizeof(remote_props) / sizeof(remote_props[0]));		
	}

	vcam_log("Sending %d events\n", ev->length);

	ptp_senddata(cam, ptp->code, (unsigned char *)ev, 2 + (6 * ev->length));
	free(ev);

	ptp_response(cam, PTP_RC_OK, 0);

	return 0;
}

int fuji_get_property(vcamera *cam, ptpcontainer *ptp) {
	vcam_log("Get property %X\n", ptp->params[0]);
	int data = -1;
	switch (ptp->params[0]) {
	case PTP_PC_FUJI_EventsList:
		return fuji_send_events(cam, ptp);
	case PTP_PC_FUJI_ObjectCount:
	case PTP_PC_FUJI_ObjectCount2:
		data = cam->obj_count;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_PC_FUJI_FunctionMode:
		data = cam->function_mode;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_PC_FUJI_CameraState:
		data = cam->camera_state;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_PC_FUJI_ImageGetVersion:
		data = cam->conf->image_get_version;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_PC_FUJI_GetObjectVersion: {
		data = cam->conf->get_object_version;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		} break;
	case PTP_PC_FUJI_RemoteGetObjectVersion:
		if (cam->conf->remote_get_object_version) {
			data = cam->conf->remote_get_object_version;
			ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		}
		break;
	case PTP_PC_FUJI_ImageGetLimitedVersion:
	case PTP_PC_FUJI_CompressionCutOff:
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 0);
		break;
	case PTP_PC_FUJI_RemoteVersion:
		if (cam->conf->remote_version) {
			data = cam->conf->remote_version;
			ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		} else {
			ptp_senddata(cam, ptp->code, (unsigned char *)&data, 0);
		}
		break;
	case PTP_PC_FUJI_StorageID:
		data = 0;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 1); // used to be 2, fuji sends 00
		break;
	default:
		vcam_log("Fuji Unknown %X\n", ptp->params[0]);
		return 1;
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 0;
}

extern int fuji_open_remote_port;
int ptp_fuji_capture(vcamera *cam, ptpcontainer *ptp) {
	vcam_log("Opening remote ports\n");
	fuji_open_remote_port++;

	if (ptp->code == PTP_OC_InitiateOpenCapture) {
		cam->camera_internal_state = CAM_STATE_IDLE_REMOTE;
	} else if (ptp->code == PTP_OC_TerminateOpenCapture) {
		cam->camera_internal_state = CAM_STATE_IDLE_REMOTE;		
	} 

	ptp_response(cam, PTP_RC_OK, 0);

	return 0;
}

static int devinfo_add_prop(char *data, int length, int code, uint8_t *payload) {
	int of = 0;
	of += ptp_write_u32(data + of, length);
	of += ptp_write_u16(data + of, code);
	memcpy(data + of, payload, length - 2 - 4);
	of += length - 2 - 4;
	return of;
}

int ptp_fuji_get_device_info(vcamera *cam, ptpcontainer *ptp) {
	// TODO: Pack data

	char *data = malloc(2048);
	int of = 0;
	of += ptp_write_u32(data + of, 8);

	uint8_t payload_5012[] = {0x4, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x2, 0x3, 0x0, 0x0, 0x0, 0x2, 0x0, 0x4, 0x0, };
	of += devinfo_add_prop(data + of, 22, PTP_PC_CaptureDelay, payload_5012);
	uint8_t payload_500c[] = {0x4, 0x0, 0x1, 0x2, 0x0, 0x9, 0x80, 0x2, 0x2, 0x0, 0x9, 0x80, 0xa, 0x80, };
	of += devinfo_add_prop(data + of, 20, PTP_PC_FlashMode, payload_500c);
	uint8_t payload_5005[] = {0x4, 0x0, 0x1, 0x2, 0x0, 0x2, 0x0, 0x2, 0xa, 0x0, 0x2, 0x0, 0x4, 0x0, 0x6, 0x80, 0x1, 0x80, 0x2, 0x80, 0x3, 0x80, 0x6, 0x0, 0xa, 0x80, 0xb, 0x80, 0xc, 0x80, };
	of += devinfo_add_prop(data + of, 36, PTP_PC_WhiteBalance, payload_5005);
	uint8_t payload_5010[] = {0x3, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x2, 0x13, 0x0, 0x48, 0xf4, 0x95, 0xf5, 0xe3, 0xf6, 0x30, 0xf8, 0x7d, 0xf9, 0xcb, 0xfa, 0x18, 0xfc, 0x65, 0xfd, 0xb3, 0xfe, 0x0, 0x0, 0x4d, 0x1, 0x9b, 0x2, 0xe8, 0x3, 0x35, 0x5, 0x83, 0x6, 0xd0, 0x7, 0x1d, 0x9, 0x6b, 0xa, 0xb8, 0xb, };
	of += devinfo_add_prop(data + of, 54, PTP_PC_ExposureBiasCompensation, payload_5010);
	uint8_t payload_d001[] = {0x4, 0x0, 0x1, 0x1, 0x0, 0x2, 0x0, 0x2, 0xb, 0x0, 0x1, 0x0, 0x2, 0x0, 0x3, 0x0, 0x4, 0x0, 0x5, 0x0, 0x6, 0x0, 0x7, 0x0, 0x8, 0x0, 0x9, 0x0, 0xa, 0x0, 0xb, 0x0, };
	of += devinfo_add_prop(data + of, 38, PTP_PC_FUJI_FilmSimulation, payload_d001);
	uint8_t payload_d02a[] = {0x6, 0x0, 0x1, 0xff, 0xff, 0xff, 0xff, 0x0, 0x19, 0x0, 0x80, 0x2, 0x19, 0x0, 0x90, 0x1, 0x0, 0x80, 0x20, 0x3, 0x0, 0x80, 0x40, 0x6, 0x0, 0x80, 0x80, 0xc, 0x0, 0x80, 0x0, 0x19, 0x0, 0x80, 0x64, 0x0, 0x0, 0x40, 0xc8, 0x0, 0x0, 0x0, 0xfa, 0x0, 0x0, 0x0, 0x40, 0x1, 0x0, 0x0, 0x90, 0x1, 0x0, 0x0, 0xf4, 0x1, 0x0, 0x0, 0x80, 0x2, 0x0, 0x0, 0x20, 0x3, 0x0, 0x0, 0xe8, 0x3, 0x0, 0x0, 0xe2, 0x4, 0x0, 0x0, 0x40, 0x6, 0x0, 0x0, 0xd0, 0x7, 0x0, 0x0, 0xc4, 0x9, 0x0, 0x0, 0x80, 0xc, 0x0, 0x0, 0xa0, 0xf, 0x0, 0x0, 0x88, 0x13, 0x0, 0x0, 0x0, 0x19, 0x0, 0x0, 0x0, 0x32, 0x0, 0x40, 0x0, 0x64, 0x0, 0x40, 0x0, 0xc8, 0x0, 0x40, };
	of += devinfo_add_prop(data + of, 120, PTP_PC_FUJI_ExposureIndex, payload_d02a);
	uint8_t payload_d019[] = {0x4, 0x0, 0x1, 0x1, 0x0, 0x1, 0x0, 0x2, 0x2, 0x0, 0x0, 0x0, 0x1, 0x0, };
	of += devinfo_add_prop(data + of, 20, PTP_PC_FUJI_RecMode, payload_d019);
	uint8_t payload_d17c[] = {0x6, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x4, 0x4, 0x2, 0x3, 0x1, 0x0, 0x0, 0x0, 0x0, 0x7, 0x7, 0x9, 0x10, 0x1, 0x0, 0x0, 0x0, };
	of += devinfo_add_prop(data + of, 30, PTP_PC_FUJI_FocusMeteringMode, payload_d17c);

	ptp_senddata(cam, ptp->code, (void *)data, of);
	ptp_response(cam, PTP_RC_OK, 0);
	return 0;
}

int ptp_fuji_liveview(int socket) {
	vcam_log("Broadcasting liveview\n");
	FILE *file = fopen(FUJI_DUMMY_LV_JPEG, "rb");
	if (file == NULL) {
		vcam_log("File %s not found\n", FUJI_DUMMY_LV_JPEG);
		exit(-1);
	}

	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *buffer = malloc(file_size);
	fread(buffer, 1, file_size, file);

	fclose(file);

	int rc = send(socket, buffer, file_size, 0);
	if (rc) return -1;

	free(buffer);

	return 0;
}

void fuji_downloaded_object(vcamera *cam) {
	// In MULTIPLE_TRANSFER mode, the camera 'deletes' the first object and replaces it with
	// the second object, once a partialtransfer or object is completely downloaded.
	if (cam->camera_state == FUJI_MULTIPLE_TRANSFER) {
		printf("Dirent %s\n", first_dirent->next->fsname);
		struct ptp_dirent *next = first_dirent->next;
		next->id = 1;
		first_dirent = next;

		if (cam->sent_images == 3) {
			vcam_log("Enough images send %d, killing connection\n", cam->sent_images);
			cam->next_cmd_kills_connection = 1;
		}

		// Then we resend the events from the start of the connection
		ptp_notify_event(cam, PTP_PC_FUJI_CameraState, cam->camera_state);
		ptp_notify_event(cam, PTP_PC_FUJI_SelectedImgsMode, 1);

		cam->sent_images++;
	}
}

struct ptp_function ptp_functions_fuji_wifi[] = {
	{PTP_OC_FUJI_GetDeviceInfo,	ptp_fuji_get_device_info, NULL },
	{0x101c,	ptp_fuji_capture, NULL },
	{0x1018,	ptp_fuji_capture, NULL },
	{0, NULL, NULL},
};
