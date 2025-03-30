// Fujifilm PTP over TCP reimplementation
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <vcam.h>
#include <fujiptp.h>
#include <cl_data.h>
#include "fuji.h"

int fuji_usb_init_cam(vcam *cam);

vcam *vcam_fuji_new(const char *name, const char *arg) {
	vcam *cam = vcam_init_standard();
	const char *args[] = {arg};
	int rc = fuji_init_cam(cam, name, 1, args);
	if (rc) return NULL;
	return cam;
}

int fuji_init_cam(vcam *cam, const char *name, int argc, const char **argv) {
	cam->priv = calloc(1, sizeof(struct Fuji));
	struct Fuji *f = fuji(cam);
	if (!strcmp(name, "fuji_x_a2")) {
		strcpy(cam->model, "X-A2");
		f->image_get_version = 1;
		f->get_object_version = 2;
		f->remote_version = 0;
		cam->product_id = 0x2c6;
	} else if (!strcmp(name, "fuji_x_t20")) {
		strcpy(cam->model, "X-T20");
		f->image_get_version = 3;
		f->get_object_version = 4;
		f->remote_version = 0x00020004;
		f->remote_get_object_version = 2;
	} else if (!strcmp(name, "fuji_x_t2")) {
		strcpy(cam->model, "X-T2");
		f->image_get_version = 3;
		f->get_object_version = 4;
		f->remote_version = 0x0002000a;
		f->remote_get_object_version = 2;
	} else if (!strcmp(name, "fuji_x_s10")) {
		strcpy(cam->model, "X-S10");
		f->image_get_version = 3;
		f->get_object_version = 4;
		f->remote_version = 0x0002000a; // fuji sets to 2000b
		f->remote_get_object_version = 4;
	} else if (!strcmp(name, "fuji_x_t4")) {
		strcpy(cam->model, "X-T4");
		f->image_get_version = 4;
		f->get_object_version = 5;
		f->remote_version = 0x0002000a; // fuji sets to 2000c
		f->remote_get_object_version = 5;
		// PTP_DPC_FUJI_ImageGetLimitedVersion = 1
		// PTP_DPC_FUJI_Unknown_D52F = 1
	} else if (!strcmp(name, "fuji_x_h1")) {
		strcpy(cam->model, "X-H1");
		f->image_get_version = 3; // fuji sets to 4
		f->get_object_version = 4;
		f->remote_version = 0x00020006; // fuji sets to 2000C
		f->remote_get_object_version = 4;
		cam->product_id = 0x2d7;
		static char *path = "bin/fuji/backups/FUJIFILM_X-H1_20240327_2218.DAT";
		f->settings_file_path = path;
	} else if (!strcmp(name, "fuji_x_dev")) {
		strcpy(cam->model, "X-DEV");
		f->image_get_version = 3;
		f->get_object_version = 4;
		f->remote_version = 0x00020006;
		f->remote_get_object_version = 4;
	} else if (!strcmp(name, "fuji_x_f10")) {
		strcpy(cam->model, "X-F10");
		f->image_get_version = 3;
		f->get_object_version = 4;
		f->remote_version = 0x00020004; // fuji sets to 2000C
		f->remote_get_object_version = 2;
	} else if (!strcmp(name, "fuji_x30")) {
		strcpy(cam->model, "X30");
		f->image_get_version = 3;
		f->get_object_version = 3; // 2016 fuji sets to 4
		f->remote_version = 0x00020002; // 2024 fuji sets to 2000C
		f->remote_get_object_version = 1;
	} else if (!strcmp(name, "fuji_x_t30")) {
		strcpy(cam->model, "X-T30");
		f->image_get_version = 0xdead;
		f->get_object_version = 0xdead;
		f->remote_version = 0xdead;
		f->remote_get_object_version = 0xdead;
	} else {
		return -1;
	}

	cam->vendor_id = 0x4cb;
	strcpy(cam->manufac, "FUJIFILM");
	strcpy(cam->extension, "fujifilm.co.jp: 1.0; ");
	strcpy(cam->version, "1.30");
	strcpy(cam->serial, "xxxxxxxxxxxxxxxxxxxxxxxx");

	f->transport = FUJI_FEATURE_WIRELESS_COMM;
	for (int i = 0; i < argc; i++) {
		if (vcam_parse_args(cam, argc, argv, &i)) continue;
		if (!strcmp(argv[i], "--usb")) {
			f->transport = FUJI_FEATURE_USB_CARD_READER;
		} else if (!strcmp(argv[i], "--rawconv")) {
			f->transport = FUJI_FEATURE_RAW_CONV;
		} else if (!strcmp(argv[i], "--select-img")) {
			f->is_select_multiple_images = 1;
		} else if (!strcmp(argv[i], "--discovery")) {
			f->do_discovery = 1;
			f->transport = FUJI_FEATURE_AUTOSAVE;
		} else if (!strcmp(argv[i], "--register")) {
			f->do_register = 1;
			f->transport = FUJI_FEATURE_AUTOSAVE;
		} else if (!strcmp(argv[i], "--tether")) {
			f->do_tether = 1;
			f->transport = FUJI_FEATURE_WIRELESS_TETHER;
		} else {
			vcam_log("Unknown option %s", argv[i]);
			return -1;
		}
	}

	if (f->transport == FUJI_FEATURE_WIRELESS_COMM || f->transport == FUJI_FEATURE_WIRELESS_TETHER || f->transport == FUJI_FEATURE_AUTOSAVE) {
		fuji_register_opcodes(cam);
		return vcam_fuji_setup(cam);
	} else {
		return fuji_usb_init_cam(cam);
	}
}

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

uint8_t *fuji_get_ack_packet(vcam *cam) {
	#warning "TODO make not static"
	static struct FujiInitPacket p = {
		.length = 0x44,
		.type = PTPIP_INIT_COMMAND_ACK,
		.version = 0x0,
		.guid1 = 0x61b07008,
		.guid2 = 0x93458b0a,
		.guid3 = 0x5793e7b2,
		.guid4 = 0x50e036dd,
	};

	char *name = cam->model;
	int i;
	for (i = 0; name[i] != '\0'; i++) {
		p.device_name[i * 2] = name[i];
		p.device_name[i * 2 + 1] = '\0';
	}

	p.device_name[i * 2 + 1] = '\0';

	return (uint8_t *)(&p);
}

int vcam_fuji_setup(vcam *cam) {
	struct Fuji *f = fuji(cam);
	f->client_state = 2;
	f->camera_state = 0;
	f->min_remote_version = 0;
	f->compress_small = 0;
	f->no_compressed = 0;
	f->internal_state = CAM_STATE_READY;
	f->sent_images = 0;

	// TODO: Better way to ignore folders (Fuji doesn't show them)
	f->obj_count = ptp_get_object_count(cam) - 1;

	vcam_log("Fuji: Found %d objects", f->obj_count);

	// Check if remote mode is supported
	if (f->remote_version) {
		f->camera_state = FUJI_REMOTE_ACCESS;
	} else {
		f->camera_state = FUJI_FULL_ACCESS;
	}

	if (f->do_discovery) {
		f->camera_state = FUJI_PC_AUTO_SAVE;
	}

	if (f->is_select_multiple_images) {
		vcam_log("Configuring fuji to select multiple images");
		f->camera_state = FUJI_MULTIPLE_TRANSFER;
		// ID 0 is DCIM, set to 1, which is first jpeg
		cam->first_dirent->next->id = 1;
	}

	// Common startup events for all cameras
	ptp_notify_event(cam, PTP_DPC_FUJI_CameraState, f->camera_state);

	if (f->camera_state == FUJI_MULTIPLE_TRANSFER) {
		ptp_notify_event(cam, PTP_DPC_FUJI_SelectedImgsMode, 1);
	}

	ptp_notify_event(cam, PTP_DPC_FUJI_ObjectCount, f->obj_count);

	if (f->remote_version) {
		ptp_notify_event(cam, PTP_DPC_FUJI_ObjectCount2, f->obj_count);
		ptp_notify_event(cam, PTP_DPC_FUJI_Unknown_D52F, 1);
		ptp_notify_event(cam, PTP_DPC_FUJI_Unknown_D400, 1);
		ptp_notify_event(cam, PTP_DPC_FUJI_SelectedImgsMode, 1);
	}

	return 0;
}

int fuji_is_compressed_mode(vcam *cam) {
	struct Fuji *f = fuji(cam);
	return (int)(f->compress_small);
}

int ptp_fuji_setdevicepropvalue_write(vcam *cam, ptpcontainer *ptp) {
	int code = ptp->params[0];
	struct Fuji *f = fuji(cam);
	int codes[] = {
		PTP_DPC_FUJI_CameraState,
		PTP_DPC_FUJI_ClientState,
		PTP_DPC_FUJI_GetObjectVersion,
		PTP_DPC_FUJI_EnableCorrectFileSize,
		PTP_DPC_FUJI_CompressSmall,
		PTP_DPC_FUJI_ImageGetVersion,
		PTP_DPC_FUJI_GeoTagVersion,
		PTP_DPC_FUJI_AutoSaveVersion,
		PTP_DPC_FUJI_AutoSaveDatabaseStatus,
	};

	int codes_remote_only[] = {
	    PTP_DPC_FUJI_RemoteVersion,
	    PTP_DPC_FUJI_RemoteGetObjectVersion,		
	};

	for (size_t i = 0; i < (sizeof(codes) / sizeof(codes[0])); i++) {
		if (codes[i] == code) return 1;
	}

	if (f->remote_version) {
		for (size_t i = 0; i < (sizeof(codes_remote_only) / sizeof(codes_remote_only[0])); i++) {
			if (codes_remote_only[i] == code) return 1;
		}
	}

	vcam_log("Request to set unknown property %X", code);
	ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);

	return 1;
}

int ptp_fuji_setdevicepropvalue_write_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	struct Fuji *f = fuji(cam);
	uint32_t *uint = (uint32_t *)data;
	uint16_t *uint16 = (uint16_t *)data;

	vcam_log("Fuji Set property %X -> %X (size %d)", ptp->params[0], uint[0], len);

	switch (ptp->params[0]) {
	case PTP_DPC_FUJI_ClientState:
		assert(len == 2);
		f->client_state = uint[0];
		//usleep(1000 * 1000 * 3);
		break;
	case PTP_DPC_FUJI_RemoteVersion:
		assert(len == 4);
		f->min_remote_version = uint[0];
		break;
	case PTP_DPC_FUJI_GetObjectVersion:
		break;
	case PTP_DPC_FUJI_CompressSmall:
		f->compress_small = uint16[0];
		break;
	case PTP_DPC_FUJI_EnableCorrectFileSize:
		assert(len == 2);
		assert(uint16[0] == 1 || uint16[0] == 0);
		f->no_compressed = uint16[0];
		//usleep(1000 * 5000); // Fuji seems to take a while here
		break;
	case PTP_DPC_FUJI_RemoteGetObjectVersion:
		assert(len == 4);
		break;
	case PTP_DPC_FUJI_CameraState:
		assert(len == 2);
		ptp_notify_event(cam, PTP_DPC_FUJI_CameraState, uint16[0]);
		ptp_notify_event(cam, PTP_DPC_FUJI_SelectedImgsMode, 1);
		ptp_notify_event(cam, PTP_DPC_FUJI_ObjectCount, f->obj_count);
		ptp_notify_event(cam, PTP_DPC_FUJI_Unknown_D52F, 1);
		ptp_notify_event(cam, PTP_DPC_FUJI_Unknown_D400, 1);
		ptp_notify_event(cam, PTP_DPC_FUJI_ObjectCount, f->obj_count);
		break;
	case PTP_DPC_FUJI_GeoTagVersion:
		break;
	case PTP_DPC_FUJI_AutoSaveVersion:
		break;
	case PTP_DPC_FUJI_AutoSaveDatabaseStatus:
		break;
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

int fuji_send_events(vcam *cam, ptpcontainer *ptp) {
	struct PtpFujiEvents *ev = calloc(1, 4096);

	// Pop all events and pack into fuji event structure
	struct GenericEvent ev_info;
	while (!ptp_pop_event(cam, &ev_info)) {
		ev->events[ev->length].code = ev_info.code;
		ev->events[ev->length].value = ev_info.value;
		ev->length++;
	}

	vcam_log("Sending %d events", ev->length);
	for (int i = 0; i < ev->length; i++) {
		vcam_log("%02x -> %x", ev->events[i].code, ev->events[i].value);
	}

	ptp_senddata(cam, ptp->code, (unsigned char *)ev, 2 + (6 * ev->length));
	free(ev);

	ptp_response(cam, PTP_RC_OK, 0);

	return 0;
}

int d212_getvalue(vcam *cam, struct PtpPropDesc *desc, int *optional_length) {
	uint8_t *buf = desc->value;
	struct PtpFujiEvents *ev = (struct PtpFujiEvents *)buf;

	// Pop all events and pack into fuji event structure
	struct GenericEvent ev_info;
	ev->length = 0;
	while (!ptp_pop_event(cam, &ev_info)) {
		ev->events[ev->length].code = ev_info.code;
		ev->events[ev->length].value = ev_info.value;
		ev->length++;
	}

	(*optional_length) = 2 + (ev->length * 6);

	return 0;
}

void fuji_register_d212(vcam *cam) {
	struct PtpPropDesc desc = {0};
	desc.DataType = PTP_TC_STRING; // Nonstandard
	desc.GetSet = PTP_AC_Read;
	desc.value = malloc(512);
	vcam_register_prop_handlers(cam, PTP_DPC_FUJI_EventsList, &desc, d212_getvalue, NULL);
}

int ptp_fuji_getdevicepropvalue_write(vcam *cam, ptpcontainer *ptp) {
	struct Fuji *f = fuji(cam);
	vcam_log("Get property %X", ptp->params[0]);
	int data = -1;
	switch (ptp->params[0]) {
	case PTP_DPC_FUJI_EventsList:
		return fuji_send_events(cam, ptp);
	case PTP_DPC_FUJI_ObjectCount:
	case PTP_DPC_FUJI_ObjectCount2:
		data = f->obj_count;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_DPC_FUJI_ClientState:
		data = f->client_state;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_DPC_FUJI_CameraState:
		data = f->camera_state;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_DPC_FUJI_ImageGetVersion:
		data = f->image_get_version;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_DPC_FUJI_GetObjectVersion: {
		data = f->get_object_version;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		} break;
	case PTP_DPC_FUJI_RemoteGetObjectVersion:
		if (f->remote_get_object_version) {
			data = f->remote_get_object_version;
			ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		}
		break;
//	case PTP_DPC_FUJI_ImageGetLimitedVersion:
	case PTP_DPC_FUJI_CompressionCutOff:
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 0);
		break;
	case PTP_DPC_FUJI_RemoteVersion:
		if (f->remote_version) {
			data = f->remote_version;
			ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		} else {
			ptp_senddata(cam, ptp->code, (unsigned char *)&data, 0);
		}
		break;
	case PTP_DPC_FUJI_StorageID:
		data = 0;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 1);
		break;
	case PTP_DPC_FUJI_Unknown_D52F:
		data = 0;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_DPC_FUJI_AutoSaveVersion:
		data = 1;
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;		
	default:
		vcam_log("WARN: Fuji Unknown %X", ptp->params[0]);
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 0);
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	vcam_log("Sending prop %02x == %x", ptp->params[0], data);

	ptp_response(cam, PTP_RC_OK, 0);
	return 0;
}

void fuji_accept_remote_ports(void); // fuji-server.c

int ptp_fuji_capture(vcam *cam, ptpcontainer *ptp) {
	struct Fuji *f = fuji(cam);
	if (ptp->code == PTP_OC_InitiateOpenCapture) {
		f->internal_state = CAM_STATE_IDLE_REMOTE;
		vcam_log("Opening remote ports"); // BUG: It does this twice
		fuji_accept_remote_ports();
	} else if (ptp->code == PTP_OC_TerminateOpenCapture) {
		f->internal_state = CAM_STATE_IDLE_REMOTE;
		vcam_log("One time sending all remote props");
		ptp_notify_event(cam, PTP_DPC_FUJI_DeviceError, 0);
		ptp_notify_event(cam, PTP_DPC_FlashMode, 0x800a);
		ptp_notify_event(cam, PTP_DPC_CaptureDelay, 0);
		ptp_notify_event(cam, PTP_DPC_FUJI_CaptureRemaining, 0x761);
		ptp_notify_event(cam, PTP_DPC_FUJI_MovieRemainingTime, 0x19d1);
		ptp_notify_event(cam, PTP_DPC_ExposureProgramMode, 0x3);
		ptp_notify_event(cam, PTP_DPC_FUJI_BatteryLevel, 0xa);
		ptp_notify_event(cam, PTP_DPC_FUJI_Quality, 0x4);
		ptp_notify_event(cam, PTP_DPC_FUJI_ImageAspectRatio, 0xa);
		ptp_notify_event(cam, PTP_DPC_FUJI_ExposureIndex, 0x80003200);
		ptp_notify_event(cam, PTP_DPC_FUJI_MovieISO, 0x80003200);
		ptp_notify_event(cam, PTP_DPC_FUJI_ShutterSpeed2, 0xffffffff);
		ptp_notify_event(cam, PTP_DPC_FUJI_CommandDialMode, 0x0);
		ptp_notify_event(cam, PTP_DPC_FNumber, 0xa);
		ptp_notify_event(cam, PTP_DPC_ExposureBiasCompensation, 0x0);
		ptp_notify_event(cam, PTP_DPC_WhiteBalance, 0x2);
		ptp_notify_event(cam, PTP_DPC_FUJI_FilmSimulation, 0x6);
		ptp_notify_event(cam, PTP_DPC_FocusMode, 0x8001);
		ptp_notify_event(cam, PTP_DPC_FUJI_FocusMeteringMode, 0x03020604);
		ptp_notify_event(cam, PTP_DPC_FUJI_AFStatus, 0x0);
		ptp_notify_event(cam, PTP_DPC_FUJI_Unknown1, 0xa);
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 0;
}

void cam_fuji_register_remote_props(vcam *cam) {
	// PTP_DPC_FUJI_FilmSimulation
	// ...
}

static int devinfo_add_prop(char *data, int length, int code, uint8_t *payload) {
	int of = 0;
	of += ptp_write_u32(data + of, length);
	of += ptp_write_u16(data + of, code);
	memcpy(data + of, payload, length - 2 - 4);
	of += length - 2 - 4;
	return of;
}

int ptp_fuji_get_device_info(vcam *cam, ptpcontainer *ptp) {
	char *data = malloc(2048);
	int of = 0;
	of += ptp_write_u32(data + of, 8);

	// Send all valid values of each property
	uint8_t payload_5012[] = {0x4, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x2, 0x3, 0x0, 0x0, 0x0, 0x2, 0x0, 0x4, 0x0, };
	of += devinfo_add_prop(data + of, 22, PTP_DPC_CaptureDelay, payload_5012);
	uint8_t payload_500c[] = {0x4, 0x0, 0x1, 0x2, 0x0, 0x9, 0x80, 0x2, 0x2, 0x0, 0x9, 0x80, 0xa, 0x80, };
	of += devinfo_add_prop(data + of, 20, PTP_DPC_FlashMode, payload_500c);
	uint8_t payload_5005[] = {0x4, 0x0, 0x1, 0x2, 0x0, 0x2, 0x0, 0x2, 0xa, 0x0, 0x2, 0x0, 0x4, 0x0, 0x6, 0x80, 0x1, 0x80, 0x2, 0x80, 0x3, 0x80, 0x6, 0x0, 0xa, 0x80, 0xb, 0x80, 0xc, 0x80, };
	of += devinfo_add_prop(data + of, 36, PTP_DPC_WhiteBalance, payload_5005);
	uint8_t payload_5010[] = {0x3, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x2, 0x13, 0x0, 0x48, 0xf4, 0x95, 0xf5, 0xe3, 0xf6, 0x30, 0xf8, 0x7d, 0xf9, 0xcb, 0xfa, 0x18, 0xfc, 0x65, 0xfd, 0xb3, 0xfe, 0x0, 0x0, 0x4d, 0x1, 0x9b, 0x2, 0xe8, 0x3, 0x35, 0x5, 0x83, 0x6, 0xd0, 0x7, 0x1d, 0x9, 0x6b, 0xa, 0xb8, 0xb, };
	of += devinfo_add_prop(data + of, 54, PTP_DPC_ExposureBiasCompensation, payload_5010);
	uint8_t payload_d001[] = {0x4, 0x0, 0x1, 0x1, 0x0, 0x2, 0x0, 0x2, 0xb, 0x0, 0x1, 0x0, 0x2, 0x0, 0x3, 0x0, 0x4, 0x0, 0x5, 0x0, 0x6, 0x0, 0x7, 0x0, 0x8, 0x0, 0x9, 0x0, 0xa, 0x0, 0xb, 0x0, };
	of += devinfo_add_prop(data + of, 38, PTP_DPC_FUJI_FilmSimulation, payload_d001);
	uint8_t payload_d02a[] = {0x6, 0x0, 0x1, 0xff, 0xff, 0xff, 0xff, 0x0, 0x19, 0x0, 0x80, 0x2, 0x19, 0x0, 0x90, 0x1, 0x0, 0x80, 0x20, 0x3, 0x0, 0x80, 0x40, 0x6, 0x0, 0x80, 0x80, 0xc, 0x0, 0x80, 0x0, 0x19, 0x0, 0x80, 0x64, 0x0, 0x0, 0x40, 0xc8, 0x0, 0x0, 0x0, 0xfa, 0x0, 0x0, 0x0, 0x40, 0x1, 0x0, 0x0, 0x90, 0x1, 0x0, 0x0, 0xf4, 0x1, 0x0, 0x0, 0x80, 0x2, 0x0, 0x0, 0x20, 0x3, 0x0, 0x0, 0xe8, 0x3, 0x0, 0x0, 0xe2, 0x4, 0x0, 0x0, 0x40, 0x6, 0x0, 0x0, 0xd0, 0x7, 0x0, 0x0, 0xc4, 0x9, 0x0, 0x0, 0x80, 0xc, 0x0, 0x0, 0xa0, 0xf, 0x0, 0x0, 0x88, 0x13, 0x0, 0x0, 0x0, 0x19, 0x0, 0x0, 0x0, 0x32, 0x0, 0x40, 0x0, 0x64, 0x0, 0x40, 0x0, 0xc8, 0x0, 0x40, };
	of += devinfo_add_prop(data + of, 120, PTP_DPC_FUJI_ExposureIndex, payload_d02a);
	uint8_t payload_d019[] = {0x4, 0x0, 0x1, 0x1, 0x0, 0x1, 0x0, 0x2, 0x2, 0x0, 0x0, 0x0, 0x1, 0x0, };
	of += devinfo_add_prop(data + of, 20, PTP_DPC_FUJI_RecMode, payload_d019);
	uint8_t payload_d17c[] = {0x6, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x4, 0x4, 0x2, 0x3, 0x1, 0x0, 0x0, 0x0, 0x0, 0x7, 0x7, 0x9, 0x10, 0x1, 0x0, 0x0, 0x0, };
	of += devinfo_add_prop(data + of, 30, PTP_DPC_FUJI_FocusMeteringMode, payload_d17c);

	ptp_senddata(cam, ptp->code, (void *)data, of);
	ptp_response(cam, PTP_RC_OK, 0);
	free(data);
	return 0;
}

int ptp_fuji_liveview(int socket) {
	vcam_log("Broadcasting liveview");
	FILE *file = fopen(FUJI_DUMMY_LV_JPEG, "rb");
	if (file == NULL) {
		vcam_log("File %s not found", FUJI_DUMMY_LV_JPEG);
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

int ptp_fuji_getpartialobject_write(vcam *cam, ptpcontainer *ptp) {
	int rc = ptp_getpartialobject_write(cam, ptp);

	// Once the end of the file is read, the cam seems to switch object IDs
	// From what I can tell, this can be triggered by cam's size being lower or request size being lower (TODO: just the latter)
	if (ptp->params[2] != 0x100000) {
		fuji_downloaded_object(cam);
	}

	return rc;
}

void fuji_downloaded_object(vcam *cam) {
	struct Fuji *f = fuji(cam);
	// In MULTIPLE_TRANSFER mode, the camera 'deletes' the first object and replaces it with
	// the second object, once a partialtransfer or object is completely downloaded.
	// It seems we get this notification once GetPartialObject calls reach the end of an object.
	if (f->camera_state == FUJI_MULTIPLE_TRANSFER) {
		vcam_log("Dirent %s", cam->first_dirent->next->fsname);
		struct ptp_dirent *next = cam->first_dirent->next;
		next->id = 1;
		cam->first_dirent = next;

		if (f->sent_images == 3) {
			vcam_log("Enough images send %d, killing connection", f->sent_images);
			cam->next_cmd_kills_connection = 1;
		}

		// Then we resend the events from the start of the connection
		ptp_notify_event(cam, PTP_DPC_FUJI_CameraState, f->camera_state);
		ptp_notify_event(cam, PTP_DPC_FUJI_SelectedImgsMode, 1);

		f->sent_images++;
	}
}

int ptp_fuji_discovery_getthumb_write(vcam *cam, ptpcontainer *ptp) {
	vcam_log("%s: Returning nothing for discovery mode", __func__);
	ptp_response(cam, PTP_RC_NoThumbnailPresent, 0);
	return 1;
}

void fuji_register_opcodes(vcam *cam) {
	vcam_register_opcode(cam, PTP_OC_FUJI_GetDeviceInfo, ptp_fuji_get_device_info, NULL);
	vcam_register_opcode(cam, 0x101c, ptp_fuji_capture, NULL);
	vcam_register_opcode(cam, 0x1018, ptp_fuji_capture, NULL);

	// We have completely custom implementations of these, override standard implementations
	vcam_register_opcode(cam, PTP_OC_SetDevicePropValue, ptp_fuji_setdevicepropvalue_write, ptp_fuji_setdevicepropvalue_write_data);
	vcam_register_opcode(cam, PTP_OC_GetDevicePropValue, ptp_fuji_getdevicepropvalue_write, 	NULL);
	vcam_register_opcode(cam, PTP_OC_GetPartialObject, ptp_fuji_getpartialobject_write, NULL);

	struct Fuji *f = fuji(cam);
	if (f->do_discovery) {
		vcam_register_opcode(cam, PTP_OC_GetThumb, ptp_fuji_discovery_getthumb_write, NULL);
	}
}
