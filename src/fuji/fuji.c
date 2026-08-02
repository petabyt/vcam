// Fujifilm PTP over TCP reimplementation
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <time.h>
#include <sys/socket.h>
#include <vcam.h>
#include <fujiptp.h>
#include <cl_data.h>
#include <data.h>
#include "fuji.h"

#define FUJI_GFX_CHUNK_SIZE 0x00C00020u
#define FUJI_GFX_LARGE_JPEG_SIZE (FUJI_GFX_CHUNK_SIZE + (1024u * 1024u))
#define FUJI_GFX_FIXTURE_DIR "/tmp/vcam-gfx100ii"
#define FUJI_GFX_LARGE_JPEG_PATH FUJI_GFX_FIXTURE_DIR "/DSCF0001.JPG"

#define FUJI_IMPORT_STEP_CLIENT_STATE	0x0001u
#define FUJI_IMPORT_STEP_DF28_GET	0x0002u
#define FUJI_IMPORT_STEP_DF28_SET	0x0004u
#define FUJI_IMPORT_STEP_D226_SET	0x0008u
#define FUJI_IMPORT_STEP_D227_SET	0x0010u
#define FUJI_IMPORT_STEP_D244_GET	0x0020u
#define FUJI_IMPORT_STEP_CURRENT_INFO	0x0040u
#define FUJI_IMPORT_STEP_CURRENT_THUMB	0x0080u
#define FUJI_IMPORT_STEP_FOLDERS	0x0100u
#define FUJI_IMPORT_STEP_DATES		0x0200u

#define FUJI_IMPORT_BOOTSTRAP_READY (FUJI_IMPORT_STEP_CLIENT_STATE | FUJI_IMPORT_STEP_DF28_GET | FUJI_IMPORT_STEP_DF28_SET | FUJI_IMPORT_STEP_D226_SET | FUJI_IMPORT_STEP_D227_SET | FUJI_IMPORT_STEP_D244_GET)
#define FUJI_IMPORT_ENUM_READY (FUJI_IMPORT_BOOTSTRAP_READY | FUJI_IMPORT_STEP_CURRENT_INFO | FUJI_IMPORT_STEP_CURRENT_THUMB | FUJI_IMPORT_STEP_FOLDERS | FUJI_IMPORT_STEP_DATES)

int fuji_usb_init_cam(vcam *cam);
int ptp_fuji_getpartialobject_write(vcam *cam, ptpcontainer *ptp);
static void fuji_setup_gfx100ii_fixtures(vcam *cam);

void fuji_reset_image_import_state(vcam *cam) {
	struct Fuji *f = fuji(cam);
	if (!f) {
		return;
	}
	f->image_import_preflight_seen = 0;
	f->image_import_folder_query_seen = 0;
	f->image_import_date_query_seen = 0;
	f->image_import_steps = 0;
	f->image_import_current_handle = 0;
}

int fuji_init_cam(vcam *cam, const char *name, int argc, char **argv) {
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
	} else if (!strcmp(name, "fuji_gfx100_ii") || !strcmp(name, "fuji_gfx100ii") || !strcmp(name, "gfx100ii") || !strcmp(name, "gfx")) {
		strcpy(cam->model, "GFX100 II");
		f->image_get_version = 4;
		f->get_object_version = 5;
		f->remote_version = 0x0002000c;
		f->remote_get_object_version = 5;
		f->is_gfx100ii = 1;
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
	fuji_reset_image_import_state(cam);

	if (f->is_gfx100ii) {
		fuji_setup_gfx100ii_fixtures(cam);
	}

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

static void fuji_mark_image_import_step(vcam *cam, unsigned int step) {
	struct Fuji *f = fuji(cam);
	if (!f->is_gfx100ii) {
		return;
	}
	f->image_import_steps |= step;
	f->image_import_preflight_seen = 1;
}

static int fuji_image_import_has_steps(vcam *cam, unsigned int steps) {
	struct Fuji *f = fuji(cam);
	return (f->image_import_steps & steps) == steps;
}

static int fuji_image_import_reject(vcam *cam, ptpcontainer *ptp, const char *reason) {
	vcam_log("Fuji GFX image-import gate rejected opcode 0x%04x: %s", ptp->code, reason);
	ptp_response(cam, PTP_RC_AccessDenied, 0);
	return 1;
}

static int fuji_name_has_ext(const char *name, const char *ext) {
	const char *dot = strrchr(name, '.');
	return dot && !strcasecmp(dot, ext);
}

static int fuji_is_large_jpeg(struct ptp_dirent *cur) {
	return cur && fuji_name_has_ext(cur->name, ".jpg") && cur->stbuf.st_size > FUJI_GFX_CHUNK_SIZE;
}

static int fuji_has_large_jpeg(vcam *cam) {
	for (struct ptp_dirent *cur = cam->first_dirent; cur; cur = cur->next) {
		if (fuji_is_large_jpeg(cur)) {
			return 1;
		}
	}
	return 0;
}

static struct ptp_dirent *fuji_find_fixture_parent(vcam *cam) {
	for (struct ptp_dirent *cur = cam->first_dirent; cur; cur = cur->next) {
		if (cur->name && !strcmp(cur->name, "DCIM")) {
			return cur;
		}
	}
	return cam->first_dirent;
}

static int fuji_write_com_payload(FILE *file, size_t size) {
	char payload[4096];
	memset(payload, 'V', sizeof(payload));
	while (size) {
		size_t chunk = size < sizeof(payload) ? size : sizeof(payload);
		if (fwrite(payload, 1, chunk, file) != chunk) {
			return -1;
		}
		size -= chunk;
	}
	return 0;
}

static int fuji_create_large_jpeg_fixture(void) {
	struct stat st;
	if (stat(FUJI_GFX_LARGE_JPEG_PATH, &st) == 0 && st.st_size > FUJI_GFX_CHUNK_SIZE) {
		return 0;
	}

	mkdir(FUJI_GFX_FIXTURE_DIR, 0700);

	char base_path[512];
	snprintf(base_path, sizeof(base_path), "%s/%s", PWD, FUJI_DUMMY_JPEG_COMPRESSED);
	FILE *base = fopen(base_path, "rb");
	if (!base) {
		vcam_log("%s: could not open %s", __func__, base_path);
		return -1;
	}

	fseek(base, 0, SEEK_END);
	long base_size = ftell(base);
	fseek(base, 0, SEEK_SET);
	if (base_size < 4) {
		fclose(base);
		return -1;
	}

	unsigned char *base_data = malloc((size_t)base_size);
	if (!base_data) {
		fclose(base);
		return -1;
	}
	if (fread(base_data, 1, (size_t)base_size, base) != (size_t)base_size) {
		free(base_data);
		fclose(base);
		return -1;
	}
	fclose(base);

	if (base_data[0] != 0xff || base_data[1] != 0xd8 || base_data[base_size - 2] != 0xff || base_data[base_size - 1] != 0xd9) {
		vcam_log("%s: source JPEG does not have expected SOI/EOI markers", __func__);
		free(base_data);
		return -1;
	}

	FILE *out = fopen(FUJI_GFX_LARGE_JPEG_PATH, "wb");
	if (!out) {
		free(base_data);
		return -1;
	}

	size_t current = (size_t)base_size - 2;
	int rc = 0;
	if (fwrite(base_data, 1, current, out) != current) {
		rc = -1;
	}
	while (!rc && current + 2 < FUJI_GFX_LARGE_JPEG_SIZE) {
		size_t payload_size = FUJI_GFX_LARGE_JPEG_SIZE - current - 2;
		if (payload_size > 65533) {
			payload_size = 65533;
		}

		unsigned char header[4] = {
			0xff,
			0xfe,
			(unsigned char)(((payload_size + 2) >> 8) & 0xff),
			(unsigned char)((payload_size + 2) & 0xff),
		};
		if (fwrite(header, 1, sizeof(header), out) != sizeof(header) || fuji_write_com_payload(out, payload_size)) {
			rc = -1;
			break;
		}
		current += sizeof(header) + payload_size;
	}
	if (!rc) {
		unsigned char eoi[2] = {0xff, 0xd9};
		if (fwrite(eoi, 1, sizeof(eoi), out) != sizeof(eoi)) {
			rc = -1;
		}
	}

	free(base_data);
	fclose(out);
	return rc;
}

static void fuji_add_virtual_file(vcam *cam, const char *name, const char *path) {
	struct ptp_dirent *cur = calloc(1, sizeof(*cur));
	if (!cur) {
		return;
	}

	cur->name = strdup(name);
	cur->fsname = strdup(path);
	cur->parent = fuji_find_fixture_parent(cam);
	if (cam->ptp_objectid == 0) {
		cam->ptp_objectid = 1;
	}
	cur->id = cam->ptp_objectid++;
	if (stat(cur->fsname, &cur->stbuf) == -1) {
		free_dirent(cur);
		return;
	}

	cur->next = cam->first_dirent;
	cam->first_dirent = cur;
	vcam_log("Fuji GFX fixture object %s handle 0x%08x size %ld", cur->name, cur->id, cur->stbuf.st_size);
}

static void fuji_setup_gfx100ii_fixtures(vcam *cam) {
	if (strcmp(cam->vcamera_filesystem, PWD "/bin/card")) {
		vcam_log("Fuji GFX: explicit filesystem selected, skipping synthetic large JPEG fixture");
		return;
	}
	if (fuji_has_large_jpeg(cam)) {
		return;
	}
	if (fuji_create_large_jpeg_fixture()) {
		vcam_log("Fuji GFX fixture generation failed; continuing with scanned filesystem");
		return;
	}
	fuji_add_virtual_file(cam, "DSCF0001.JPG", FUJI_GFX_LARGE_JPEG_PATH);
}

static int fuji_is_downloadable_object(struct ptp_dirent *cur) {
	return cur && cur->id && !S_ISDIR(cur->stbuf.st_mode);
}

static int fuji_downloadable_object_count(vcam *cam) {
	int count = 0;
	for (struct ptp_dirent *cur = cam->first_dirent; cur; cur = cur->next) {
		if (fuji_is_downloadable_object(cur)) {
			count++;
		}
	}
	return count;
}

static int fuji_collect_downloadable_handles(vcam *cam, uint32_t **handles) {
	int count = fuji_downloadable_object_count(cam);
	*handles = NULL;
	if (!count) {
		return 0;
	}

	uint32_t *list = calloc((size_t)count, sizeof(uint32_t));
	if (!list) {
		return 0;
	}

	int i = 0;
	for (struct ptp_dirent *cur = cam->first_dirent; cur; cur = cur->next) {
		if (fuji_is_downloadable_object(cur)) {
			list[i++] = cur->id;
		}
	}

	for (int a = 0; a < count; a++) {
		for (int b = a + 1; b < count; b++) {
			if (list[b] > list[a]) {
				uint32_t tmp = list[a];
				list[a] = list[b];
				list[b] = tmp;
			}
		}
	}

	*handles = list;
	return count;
}

static struct ptp_dirent *fuji_find_downloadable_object(vcam *cam, uint32_t handle) {
	for (struct ptp_dirent *cur = cam->first_dirent; cur; cur = cur->next) {
		if (!fuji_is_downloadable_object(cur)) {
			continue;
		}
		if (cur->id == handle) {
			return cur;
		}
	}
	return NULL;
}

static struct ptp_dirent *fuji_find_newest_downloadable_object(vcam *cam) {
	struct ptp_dirent *newest = NULL;
	for (struct ptp_dirent *cur = cam->first_dirent; cur; cur = cur->next) {
		if (!fuji_is_downloadable_object(cur)) {
			continue;
		}
		if (!newest || cur->id > newest->id) {
			newest = cur;
		}
	}
	return newest;
}

static void fuji_rewrite_to_selected_handle(ptpcontainer *ptp, ptpcontainer *rewritten, uint32_t handle) {
	*rewritten = *ptp;
	rewritten->params[0] = handle;
}

static const char *fuji_import_folder_name(vcam *cam) {
	const char *path = cam->vcamera_filesystem;
	if (!path || !*path) {
		return "100_FUJI";
	}

	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	return *name ? name : "100_FUJI";
}

struct FujiImportDate {
	char date[9];
	uint32_t count;
};

static int fuji_collect_import_dates(vcam *cam, struct FujiImportDate *dates, int max_dates) {
	int n = 0;

	for (struct ptp_dirent *cur = cam->first_dirent; cur; cur = cur->next) {
		if (!fuji_is_downloadable_object(cur)) {
			continue;
		}

		time_t mtime = cur->stbuf.st_mtime;
		struct tm *tm = gmtime(&mtime);
		if (!tm) {
			continue;
		}

		char date[9];
		if (strftime(date, sizeof(date), "%Y%m%d", tm) == 0) {
			continue;
		}

		int found = -1;
		for (int i = 0; i < n; i++) {
			if (!strcmp(dates[i].date, date)) {
				found = i;
				break;
			}
		}

		if (found >= 0) {
			dates[found].count++;
			continue;
		}

		if (n >= max_dates) {
			continue;
		}
		strncpy(dates[n].date, date, sizeof(dates[n].date));
		dates[n].date[sizeof(dates[n].date) - 1] = '\0';
		dates[n].count = 1;
		n++;
	}

	for (int a = 0; a < n; a++) {
		for (int b = a + 1; b < n; b++) {
			if (strcmp(dates[b].date, dates[a].date) > 0) {
				struct FujiImportDate tmp = dates[a];
				dates[a] = dates[b];
				dates[b] = tmp;
			}
		}
	}

	return n;
}

static int ptp_fuji_get_extension_object_info(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	if (!fuji_image_import_has_steps(cam, FUJI_IMPORT_BOOTSTRAP_READY)) {
		return fuji_image_import_reject(cam, ptp, "0x9054 before function-mode/version setup");
	}
	if (ptp->params[0] != 0x10000001) {
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	struct ptp_dirent *cur = fuji_find_newest_downloadable_object(cam);
	if (!cur) {
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	ptpcontainer rewritten;
	fuji(cam)->image_import_current_handle = cur->id;
	fuji_mark_image_import_step(cam, FUJI_IMPORT_STEP_CURRENT_INFO);
	fuji_rewrite_to_selected_handle(ptp, &rewritten, cur->id);
	return ptp_getobjectinfo_write(cam, &rewritten);
}

static int ptp_fuji_get_extension_thumb(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct Fuji *f = fuji(cam);
	if (!fuji_image_import_has_steps(cam, FUJI_IMPORT_STEP_CURRENT_INFO) || !f->image_import_current_handle) {
		return fuji_image_import_reject(cam, ptp, "0x9055 before current-object metadata");
	}
	if (ptp->params[0] != 0x10000001) {
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}
	if (!fuji_find_downloadable_object(cam, f->image_import_current_handle)) {
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	ptpcontainer rewritten;
	fuji_mark_image_import_step(cam, FUJI_IMPORT_STEP_CURRENT_THUMB);
	fuji_rewrite_to_selected_handle(ptp, &rewritten, f->image_import_current_handle);
	return ptp_getthumb_write(cam, &rewritten);
}

static int ptp_fuji_get_extension_partial_object(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 3)) return 1;

	if (!fuji_image_import_has_steps(cam, FUJI_IMPORT_ENUM_READY)) {
		return fuji_image_import_reject(cam, ptp, "0x9056 before image enumeration prelude");
	}
	if (!fuji_find_downloadable_object(cam, ptp->params[0])) {
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}
	return ptp_fuji_getpartialobject_write(cam, ptp);
}

static int ptp_fuji_get_image_import_folders(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	if (!fuji_image_import_has_steps(cam, FUJI_IMPORT_STEP_CURRENT_THUMB)) {
		return fuji_image_import_reject(cam, ptp, "0x9050 before current-object thumbnail");
	}

	struct Fuji *f = fuji(cam);
	f->image_import_folder_query_seen = 1;
	fuji_mark_image_import_step(cam, FUJI_IMPORT_STEP_FOLDERS);

	unsigned char data[256] = {0};
	int x = 0;
	x += put_32bit_le(data + x, 1);
	int entry_len_pos = x;
	x += put_32bit_le(data + x, 0);
	int entry_start = x;
	x += put_32bit_le(data + x, 0x00010001);
	x += put_32bit_le(data + x, (uint32_t)fuji_downloadable_object_count(cam));
	x += put_string(data + x, fuji_import_folder_name(cam));
	put_32bit_le(data + entry_len_pos, (uint32_t)(x - entry_start));

	ptp_senddata(cam, ptp->code, data, x);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int ptp_fuji_get_image_import_dates(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (ptp->nparams != 2 || ptp->params[0] != 0 || ptp->params[1] != 0x7530) {
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	if (!fuji_image_import_has_steps(cam, FUJI_IMPORT_STEP_FOLDERS)) {
		return fuji_image_import_reject(cam, ptp, "0x9053 before folder enumeration");
	}

	struct Fuji *f = fuji(cam);
	f->image_import_date_query_seen = 1;
	fuji_mark_image_import_step(cam, FUJI_IMPORT_STEP_DATES);

	struct FujiImportDate dates[32] = {0};
	int count = fuji_collect_import_dates(cam, dates, 32);

	unsigned char data[2048] = {0};
	int x = 0;
	x += put_32bit_le(data + x, (uint32_t)count);
	for (int i = 0; i < count; i++) {
		int entry_len_pos = x;
		x += put_32bit_le(data + x, 0);
		int entry_start = x;
		x += put_string(data + x, dates[i].date);
		x += put_32bit_le(data + x, dates[i].count);
		put_32bit_le(data + entry_len_pos, (uint32_t)(x - entry_start));
	}

	ptp_senddata(cam, ptp->code, data, x);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int ptp_fuji_unsupported(vcam *cam, ptpcontainer *ptp) {
	vcam_log("Fuji GFX rejecting unsupported opcode 0x%04x", ptp->code);
	ptp_response(cam, PTP_RC_OperationNotSupported, 0);
	return 1;
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
		PTP_DPC_FUJI_Unknown_D22E,
		PTP_DPC_FUJI_ImageGetVersion,
		PTP_DPC_FUJI_GeoTagVersion,
		PTP_DPC_FUJI_AutoSaveVersion,
		PTP_DPC_FUJI_AutoSaveDatabaseStatus,
		PTP_DPC_FUJI_RemotePhotoViewExVersion,
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
	uint32_t log_value = 0;
	memcpy(&log_value, data, len < sizeof(log_value) ? len : sizeof(log_value));

	vcam_log("Fuji Set property %X -> %X (size %d)", ptp->params[0], log_value, len);

	switch (ptp->params[0]) {
	case PTP_DPC_FUJI_ClientState:
		assert(len == 2);
		f->client_state = uint16[0];
		if (uint16[0] == 20) {
			fuji_mark_image_import_step(cam, FUJI_IMPORT_STEP_CLIENT_STATE);
		}
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
		if (len == 2 && uint16[0] == 0) {
			fuji_mark_image_import_step(cam, FUJI_IMPORT_STEP_D226_SET);
		}
		break;
	case PTP_DPC_FUJI_Unknown_D22E:
		break;
	case PTP_DPC_FUJI_EnableCorrectFileSize:
		assert(len == 2);
		assert(uint16[0] == 1 || uint16[0] == 0);
		f->no_compressed = uint16[0];
		if (uint16[0] == 0) {
			fuji_mark_image_import_step(cam, FUJI_IMPORT_STEP_D227_SET);
		}
		//usleep(1000 * 5000); // Fuji seems to take a while here
		break;
	case PTP_DPC_FUJI_RemoteGetObjectVersion:
		assert(len == 4);
		break;
	case PTP_DPC_FUJI_RemotePhotoViewExVersion:
		if (len == 4 && uint[0] == 3) {
			fuji_mark_image_import_step(cam, FUJI_IMPORT_STEP_DF28_SET);
		}
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
	case PTP_DPC_FUJI_RemotePhotoViewExVersion:
		data = 3;
		fuji_mark_image_import_step(cam, FUJI_IMPORT_STEP_DF28_GET);
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_DPC_FUJI_RemoteGetObjectVersion:
		if (f->remote_get_object_version) {
			data = f->remote_get_object_version;
			ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		}
		break;
//	case PTP_DPC_FUJI_ImageGetLimitedVersion:
	case PTP_DPC_FUJI_CompressionCutOff:
		if (f->is_gfx100ii) {
			data = 0x00bfffe0;
			ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
			break;
		}
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
		fuji_mark_image_import_step(cam, FUJI_IMPORT_STEP_D244_GET);
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 1);
		break;
	case PTP_DPC_FUJI_ImageImportObjectCount:
		if (f->is_gfx100ii && !fuji_image_import_has_steps(cam, FUJI_IMPORT_ENUM_READY)) {
			return fuji_image_import_reject(cam, ptp, "D620 before complete image-import prelude");
		}
		data = fuji_downloadable_object_count(cam);
		ptp_senddata(cam, ptp->code, (unsigned char *)&data, 4);
		break;
	case PTP_DPC_FUJI_ImageImportObjectHandles: {
		if (f->is_gfx100ii && !fuji_image_import_has_steps(cam, FUJI_IMPORT_ENUM_READY)) {
			return fuji_image_import_reject(cam, ptp, "D621 before complete image-import prelude");
		}
		uint32_t *handles = NULL;
		int count = fuji_collect_downloadable_handles(cam, &handles);
		unsigned char *payload = calloc(1, (size_t)(4 + count * 4));
		if (!payload) {
			free(handles);
			ptp_response(cam, PTP_RC_GeneralError, 0);
			return 1;
		}
		int x = 0;
		x += put_32bit_le(payload + x, (uint32_t)count);
		for (int i = 0; i < count; i++) {
			x += put_32bit_le(payload + x, handles[i]);
		}
		ptp_senddata(cam, ptp->code, payload, x);
		free(handles);
		free(payload);
		} break;
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
	int completed = 0;
	struct ptp_dirent *cur = fuji_find_downloadable_object(cam, ptp->params[0]);
	if (cur) {
		uint64_t object_size = (uint64_t)cur->stbuf.st_size;
		uint64_t offset = (uint64_t)ptp->params[1];
		uint64_t requested = (uint64_t)ptp->params[2];
		uint64_t bytes_returned = 0;
		if (offset < object_size) {
			uint64_t remaining = object_size - offset;
			bytes_returned = requested < remaining ? requested : remaining;
		}
		completed = offset >= object_size || offset + bytes_returned >= object_size;
	}

	int rc = ptp_getpartialobject_write(cam, ptp);
	if (completed) {
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
	if (f->is_gfx100ii) {
		// GFX/XApp import uses these vendor calls only as a gallery prelude.
		// The file body path is still standard GetObjectInfo/GetThumb/GetPartialObject.
		vcam_register_opcode(cam, PTP_OC_GetObjectHandles, ptp_fuji_unsupported, NULL);
		vcam_register_opcode(cam, PTP_OC_FUJI_GetImageImportFolders, ptp_fuji_get_image_import_folders, NULL);
		vcam_register_opcode(cam, PTP_OC_FUJI_GetImageImportDates, ptp_fuji_get_image_import_dates, NULL);
		vcam_register_opcode(cam, PTP_OC_FUJI_GetExtensionObjectInfo, ptp_fuji_get_extension_object_info, NULL);
		vcam_register_opcode(cam, PTP_OC_FUJI_GetExtensionThumb, ptp_fuji_get_extension_thumb, NULL);
		vcam_register_opcode(cam, PTP_OC_FUJI_GetExtensionPartialObject, ptp_fuji_get_extension_partial_object, NULL);
	}
	if (f->do_discovery) {
		vcam_register_opcode(cam, PTP_OC_GetThumb, ptp_fuji_discovery_getthumb_write, NULL);
	}
}
