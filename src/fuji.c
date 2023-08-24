#include <stdlib.h>
#include <string.h>

#define _GPHOTO2_INTERNAL_CODE
#define _DARWIN_C_SOURCE
#include <config.h>
#include <gphoto2/gphoto2-port-library.h>
#include <gphoto2/gphoto2-port-log.h>
#include <gphoto2/gphoto2-port-result.h>
#include <gphoto2/gphoto2-port.h>
#include <libgphoto2_port/i18n.h>
#include <vcamera.h>

#include <camlib.h>
#include <fujiptp.h>

#include "fuji-versions.h"

#define FUJI_DUMMY_THUMB "bin/fuji/dummy_thumb2.jpg"
#define FUJI_DUMMY_OBJ_INFO "bin/fuji/fuji_generic_object_info2.bin"
#define FUJI_DUMMY_OBJ_INFO_CUT "bin/fuji/fuji_generic_object_info1.bin"
#define FUJI_DUMMY_JPEG_FULL "bin/fuji/jpeg-full.jpg"
#define FUJI_DUMMY_JPEG_COMPRESSED "bin/fuji/jpeg-compressed.jpg"

char fuji_device_info_x_t1[] = {0x8, 0x0, 0x0, 0x0, 0x16, 0x0, 0x0, 0x0, 0x12, 0x50, 0x4, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x2, 0x3, 0x0, 0x0, 0x0, 0x2, 0x0, 0x4, 0x0, 0x14, 0x0, 0x0, 0x0, 0xc, 0x50, 0x4, 0x0, 0x1, 0x2, 0x0, 0x9, 0x80, 0x2, 0x2, 0x0, 0x9, 0x80, 0xa, 0x80, 0x24, 0x0, 0x0, 0x0, 0x5, 0x50, 0x4, 0x0, 0x1, 0x2, 0x0, 0x2, 0x0, 0x2, 0xa, 0x0, 0x2, 0x0, 0x4, 0x0, 0x6, 0x80, 0x1, 0x80, 0x2, 0x80, 0x3, 0x80, 0x6, 0x0, 0xa, 0x80, 0xb, 0x80, 0xc, 0x80, 0x36, 0x0, 0x0, 0x0, 0x10, 0x50, 0x3, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x2, 0x13, 0x0, 0x48, 0xf4, 0x95, 0xf5, 0xe3, 0xf6, 0x30, 0xf8, 0x7d, 0xf9, 0xcb, 0xfa, 0x18, 0xfc, 0x65, 0xfd, 0xb3, 0xfe, 0x0, 0x0, 0x4d, 0x1, 0x9b, 0x2, 0xe8, 0x3, 0x35, 0x5, 0x83, 0x6, 0xd0, 0x7, 0x1d, 0x9, 0x6b, 0xa, 0xb8, 0xb, 0x26, 0x0, 0x0, 0x0, 0x1, 0xd0, 0x4, 0x0, 0x1, 0x1, 0x0, 0x2, 0x0, 0x2, 0xb, 0x0, 0x1, 0x0, 0x2, 0x0, 0x3, 0x0, 0x4, 0x0, 0x5, 0x0, 0x6, 0x0, 0x7, 0x0, 0x8, 0x0, 0x9, 0x0, 0xa, 0x0, 0xb, 0x0, 0x78, 0x0, 0x0, 0x0, 0x2a, 0xd0, 0x6, 0x0, 0x1, 0xff, 0xff, 0xff, 0xff, 0x0, 0x19, 0x0, 0x80, 0x2, 0x19, 0x0, 0x90, 0x1, 0x0, 0x80, 0x20, 0x3, 0x0, 0x80, 0x40, 0x6, 0x0, 0x80, 0x80, 0xc, 0x0, 0x80, 0x0, 0x19, 0x0, 0x80, 0x64, 0x0, 0x0, 0x40, 0xc8, 0x0, 0x0, 0x0, 0xfa, 0x0, 0x0, 0x0, 0x40, 0x1, 0x0, 0x0, 0x90, 0x1, 0x0, 0x0, 0xf4, 0x1, 0x0, 0x0, 0x80, 0x2, 0x0, 0x0, 0x20, 0x3, 0x0, 0x0, 0xe8, 0x3, 0x0, 0x0, 0xe2, 0x4, 0x0, 0x0, 0x40, 0x6, 0x0, 0x0, 0xd0, 0x7, 0x0, 0x0, 0xc4, 0x9, 0x0, 0x0, 0x80, 0xc, 0x0, 0x0, 0xa0, 0xf, 0x0, 0x0, 0x88, 0x13, 0x0, 0x0, 0x0, 0x19, 0x0, 0x0, 0x0, 0x32, 0x0, 0x40, 0x0, 0x64, 0x0, 0x40, 0x0, 0xc8, 0x0, 0x40, 0x14, 0x0, 0x0, 0x0, 0x19, 0xd0, 0x4, 0x0, 0x1, 0x1, 0x0, 0x1, 0x0, 0x2, 0x2, 0x0, 0x0, 0x0, 0x1, 0x0, 0x1e, 0x0, 0x0, 0x0, 0x7c, 0xd1, 0x6, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x4, 0x4, 0x2, 0x3, 0x1, 0x0, 0x0, 0x0, 0x0, 0x7, 0x7, 0x9, 0x10, 0x1, 0x0, 0x0, 0x0};

static struct FujiInfo {
	int function_mode;
	int camera_state;
	int remote_version;
	int obj_count;
	int compress_small;
	int no_compressed;

	uint8_t sent_first_events;

	FILE *partial_fp;
} fuji_info = {
    .function_mode = 2,
    .camera_state = FUJI_START_CAM_STATE,
    .remote_version = 0,
    .obj_count = 10,
    .compress_small = 0,
    .no_compressed = 0,
    .partial_fp = NULL,
    .sent_first_events = 0,
};

static int send_partial_object(char *path, vcamera *cam, ptpcontainer *ptp) {
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		puts("File not found");
		exit(-1);
	}

	if (fseek(file, (size_t)ptp->params[1], SEEK_SET) == -1) {
		puts("fseek failure");
		exit(-1);
	}

	size_t max = (size_t)ptp->params[2];
	char *buffer = malloc(max);
	int read = fread(buffer, 1, max, file);

	ptp_senddata(cam, ptp->code, (unsigned char *)buffer, read);
	printf("Generic sending %d\n", read);

	free(buffer);
	fclose(file);

	return 0;
}

static int generic_send_file(char *path, vcamera *cam, ptpcontainer *ptp) {
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		puts("File not found");
		exit(-1);
	}

	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *buffer = malloc(file_size);
	fread(buffer, 1, file_size, file);

	ptp_senddata(cam, ptp->code, (unsigned char *)buffer, file_size);
	free(buffer);
	printf("Generic sending %d\n", file_size);

	fclose(file);
}

static size_t generic_file_size(char *path) {
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		puts("File not found");
		exit(-1);
	}

	fseek(file, 0, SEEK_END);
	size_t file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	fclose(file);

	return file_size;
}

int fuji_get_thumb(vcamera *cam, ptpcontainer *ptp) {
	printf("Get thumb for object %d\n", ptp->params[0]);
	generic_send_file(FUJI_DUMMY_THUMB, cam, ptp);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int fuji_get_object_info(vcamera *cam, ptpcontainer *ptp) {
	printf("Get object info for %d\n", ptp->params[0]);
	FILE *file = NULL;
	if (fuji_info.no_compressed) {
		file = fopen(FUJI_DUMMY_OBJ_INFO, "rb");
	} else {
		file = fopen(FUJI_DUMMY_OBJ_INFO_CUT, "rb");
	}

	if (file == NULL) {
		puts("File not found");
		exit(-1);
	}

	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *buffer = malloc(file_size);
	fread(buffer, 1, file_size, file);
	fclose(file);

	struct PtpObjectInfo *oi = (struct PtpObjectInfo *)buffer;
	if (fuji_info.no_compressed) {
		oi->compressed_size = generic_file_size(FUJI_DUMMY_JPEG_FULL);
	} else {
		oi->compressed_size = generic_file_size(FUJI_DUMMY_JPEG_COMPRESSED);
	}

	ptp_senddata(cam, ptp->code, (unsigned char *)buffer, file_size);

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

int fuji_get_partial_object(vcamera *cam, ptpcontainer *ptp) {
	printf("GetPartialObject request for %d\n", ptp->params[0]);
	if (fuji_info.no_compressed) {
		send_partial_object(FUJI_DUMMY_JPEG_FULL, cam, ptp);
	} else {
		send_partial_object(FUJI_DUMMY_JPEG_COMPRESSED, cam, ptp);
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

int fuji_set_prop_supported(int code) {
	int codes[] = {
	    PTP_PC_FUJI_CameraState,
	    PTP_PC_FUJI_FunctionMode,
	    PTP_PC_FUJI_RemoteVersion,
	    PTP_PC_FUJI_ImageExploreVersion,
	    PTP_PC_FUJI_NoCompression,
	    PTP_PC_FUJI_CompressSmall,
	};

	for (size_t i = 0; i < (sizeof(codes) / sizeof(codes[0])); i++) {
		if (codes[i] == code) {
			return 0;
		}
	}

	printf("Request to set unknown property %X\n", code);

	return 1;
}

int fuji_set_property(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	uint32_t *uint = (uint32_t *)data;
	uint16_t *uint16 = (uint16_t *)data;

	printf("Set property %X -> %X\n", ptp->params[0], uint[0]);

	switch (ptp->params[0]) {
	case PTP_PC_FUJI_FunctionMode:
		fuji_info.function_mode = uint[0];
		break;
	case PTP_PC_FUJI_RemoteVersion:
		fuji_info.remote_version = uint[0];
		break;
	case PTP_PC_FUJI_ImageExploreVersion:
		break;
	case PTP_PC_FUJI_CompressSmall:
		fuji_info.compress_small = uint16[0];
		break;
	case PTP_PC_FUJI_NoCompression:
		fuji_info.no_compressed = uint16[0];
		break;
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

int fuji_send_events(vcamera *cam, ptpcontainer *ptp) {
	struct PtpFujiEvents *ev = malloc(200);
	memset(ev, 0, 200);

	if (fuji_info.sent_first_events == 0) {
		ev->events[ev->length].code = PTP_PC_FUJI_CameraState;
		ev->events[ev->length].value = fuji_info.camera_state;
		ev->length++;

		ev->events[ev->length].code = PTP_PC_FUJI_ObjectCount;
		ev->events[ev->length].value = fuji_info.obj_count;
		ev->length++;

		ev->events[ev->length].code = PTP_PC_FUJI_ObjectCount2;
		ev->events[ev->length].value = fuji_info.obj_count;
		ev->length++;

#ifdef FUJI_USE_REMOTE_MODE
		ev->events[ev->length].code = PTP_PC_FUJI_SelectedImgsMode;
		ev->events[ev->length].value = 1;
		ev->length++;

		ev->events[ev->length].code = PTP_PC_FUJI_Unknown_D400;
		ev->events[ev->length].value = 1;
		ev->length++;

		ev->events[ev->length].code = PTP_PC_FUJI_Unknown_D52F;
		ev->events[ev->length].value = 1;
		ev->length++;
#endif

		fuji_info.sent_first_events = 1;
	}

	printf("Sending %d events\n", ev->length);

	ptp_senddata(cam, ptp->code, (unsigned char *)ev, 2 + (6 * ev->length));
	free(ev);

	ptp_response(cam, PTP_RC_OK, 0);

	return 0;
}

int fuji_get_property(vcamera *cam, ptpcontainer *ptp) {
	printf("Get property %X\n", ptp->params[0]);
	int data = -1;
	switch (ptp->params[0]) {
	case PTP_PC_FUJI_EventsList:
		return fuji_send_events(cam, ptp);
	case PTP_PC_FUJI_ObjectCount:
		data = fuji_info.obj_count;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_PC_FUJI_FunctionMode:
		data = fuji_info.function_mode;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_PC_FUJI_CameraState:
		data = fuji_info.camera_state;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_PC_FUJI_ImageGetVersion:
		data = FUJI_IMAGE_GET_VERSION;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_PC_FUJI_ImageExploreVersion: {
		data = FUJI_IMAGE_EXPLORE_VERSION;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
	} break;
	case PTP_PC_FUJI_RemoteImageExploreVersion:
#ifdef FUJI_REMOTE_IMAGE_EXPLORE_VERSION
	{
		data = FUJI_REMOTE_IMAGE_EXPLORE_VERSION;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
	} break;
#endif
	case PTP_PC_FUJI_ImageGetLimitedVersion:
	case PTP_PC_FUJI_CompressionCutOff:
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 0);
		break;
	case PTP_PC_FUJI_RemoteVersion:
		data = FUJI_REMOTE_VERSION;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 0);
		break;
	default:
		printf("Unknown %X\n", ptp->params[0]);
		//ptp_response (cam, PTP_RC_GeneralError, 0);
		//return 1;
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 0;
}

extern int fuji_open_remote_port;
int ptp_fuji_capture(vcamera *cam, ptpcontainer *ptp) {
	//ptp_senddata (cam, ptp->code, (unsigned char *)fuji_device_info_x_t1, sizeof(fuji_device_info_x_t1));

	fuji_open_remote_port++;

	ptp_response(cam, PTP_RC_OK, 0);

	return 0;
}

int ptp_fuji_get_device_info(vcamera *cam, ptpcontainer *ptp) {
	ptp_senddata(cam, ptp->code, (unsigned char *)fuji_device_info_x_t1, sizeof(fuji_device_info_x_t1));

	ptp_response(cam, PTP_RC_OK, 0);

	return 0;
}
