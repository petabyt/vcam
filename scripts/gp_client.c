// Small libgphoto2 client for testing vcam through the fake libusb shim.
// Build (with libgphoto2 installed to $GP_PREFIX, e.g. /tmp/opencode/gp-install):
//   cc -o gp_client gp_client.c -I$GP_PREFIX/include -L$GP_PREFIX/lib/x86_64-linux-gnu -lgphoto2 -lgphoto2_port
// Run (one command per line):
//   LD_PRELOAD=$VCAM/build/usb-vcam.so
//   LD_LIBRARY_PATH=$GP_PREFIX/lib/x86_64-linux-gnu
//   ./gp_client [summary|config|list|preview|capture|download|download2|
//                orphans|setconfig <name> <value>|actionaf|bulb]
//
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <gphoto2/gphoto2.h>
#include <gphoto2/gphoto2-port-log.h>

static GPContext *ctx;

static void die(const char *what, int ret) {
	fprintf(stderr, "FAIL: %s: %d (%s)\n", what, ret, gp_result_as_string(ret));
	exit(1);
}

static int autodetect(Camera **camera) {
	CameraAbilitiesList *abilities;
	GPPortInfoList *ports;
	CameraList *list;
	const char *name, *port_name;
	int count, ret;

	ret = gp_abilities_list_new(&abilities);
	if (ret < 0) die("abilities_list_new", ret);
	ret = gp_abilities_list_load(abilities, ctx);
	if (ret < 0) die("abilities_list_load", ret);

	ret = gp_port_info_list_new(&ports);
	if (ret < 0) die("port_info_list_new", ret);
	ret = gp_port_info_list_load(ports);
	if (ret < 0) die("port_info_list_load", ret);

	ret = gp_list_new(&list);
	if (ret < 0) die("list_new", ret);
	ret = gp_abilities_list_detect(abilities, ports, list, ctx);
	if (ret < 0) die("abilities_list_detect", ret);

	count = gp_list_count(list);
	if (count < 1) {
		fprintf(stderr, "FAIL: no cameras detected\n");
		exit(1);
	}
	for (int i = 0; i < count; i++) {
		gp_list_get_name(list, i, &name);
		gp_list_get_value(list, i, &port_name);
		printf("detected: %s @ %s\n", name, port_name);
	}

	ret = gp_list_get_name(list, 0, &name);
	if (ret < 0) die("list_get_name", ret);
	ret = gp_list_get_value(list, 0, &port_name);
	if (ret < 0) die("list_get_value", ret);

	CameraAbilities a;
	ret = gp_camera_new(camera);
	if (ret < 0) die("camera_new", ret);
	ret = gp_camera_get_abilities(*camera, &a);
	if (ret < 0) die("camera_get_abilities", ret);
	ret = gp_abilities_list_lookup_model(abilities, name);
	if (ret < 0) die("lookup_model", ret);
	ret = gp_abilities_list_get_abilities(abilities, ret, &a);
	if (ret < 0) die("get_abilities", ret);
	ret = gp_camera_set_abilities(*camera, a);
	if (ret < 0) die("camera_set_abilities", ret);

	GPPortInfo info;
	ret = gp_port_info_list_get_info(ports, gp_port_info_list_lookup_path(ports, port_name), &info);
	if (ret < 0) die("port_info_list_get_info", ret);
	ret = gp_camera_set_port_info(*camera, info);
	if (ret < 0) die("camera_set_port_info", ret);

	ret = gp_camera_init(*camera, ctx);
	if (ret < 0) die("camera_init", ret);

	printf("init: %s\n", name);
	gp_abilities_list_free(abilities);
	gp_port_info_list_free(ports);
	gp_list_free(list);
	return 0;
}

static int do_summary(Camera *camera) {
	CameraText text;
	int ret = gp_camera_get_summary(camera, &text, ctx);
	if (ret < 0) die("get_summary", ret);
	printf("%s", text.text);
	return 0;
}

static void walk_config(CameraWidget *widget, int depth) {
	CameraWidgetType type;
	const char *label;
	int n;

	if (gp_widget_get_type(widget, &type) < 0) return;
	gp_widget_get_label(widget, &label);

	if (type != GP_WIDGET_WINDOW && type != GP_WIDGET_SECTION) {
		if (type == GP_WIDGET_TEXT || type == GP_WIDGET_RADIO) {
			char *val = NULL;
			if (gp_widget_get_value(widget, &val) == 0 && val != NULL) {
				printf("%*s%s = %s\n", depth * 2, "", label, val);
			} else {
				printf("%*s%s\n", depth * 2, "", label);
			}
		} else {
			printf("%*s%s\n", depth * 2, "", label);
		}
	}

	n = gp_widget_count_children(widget);
	for (int i = 0; i < n; i++) {
		CameraWidget *child;
		// gp_widget_get_child does not add a reference - do not free it
		if (gp_widget_get_child(widget, i, &child) == 0) {
			walk_config(child, depth + 1);
		}
	}
}

static int do_config(Camera *camera) {
	CameraWidget *root;
	int ret = gp_camera_get_config(camera, &root, ctx);
	if (ret < 0) die("get_config", ret);
	printf("config root: %s\n", "");
	walk_config(root, 0);
	gp_widget_free(root);
	return 0;
}

static int list_folder(Camera *camera, const char *folder, int depth) {
	CameraList *list;
	const char *name;
	int ret, count;

	ret = gp_list_new(&list);
	if (ret < 0) die("list_new", ret);

	ret = gp_camera_folder_list_files(camera, folder, list, ctx);
	if (ret == 0) {
		count = gp_list_count(list);
		for (int i = 0; i < count; i++) {
			gp_list_get_name(list, i, &name);
			printf("%*s%s/%s\n", depth * 2, "", folder, name);
		}
	}
	gp_list_free(list);

	ret = gp_list_new(&list);
	if (ret < 0) die("list_new", ret);
	ret = gp_camera_folder_list_folders(camera, folder, list, ctx);
	if (ret < 0) {
		gp_list_free(list);
		return 0;
	}
	count = gp_list_count(list);
	for (int i = 0; i < count; i++) {
		char sub[512];
		gp_list_get_name(list, i, &name);
		snprintf(sub, sizeof(sub), "%s%s%s", folder, folder[strlen(folder) - 1] == '/' ? "" : "/", name);
		list_folder(camera, sub, depth + 1);
	}
	gp_list_free(list);
	return 0;
}

static int do_list(Camera *camera) {
	return list_folder(camera, "/", 0);
}

// Recursively count files whose name starts with prefix and ends with suffix
static int count_files(Camera *camera, const char *folder, const char *prefix, const char *suffix, int depth) {
	CameraList *list;
	const char *name;
	int ret, count, n = 0;
	size_t plen = strlen(prefix), slen = strlen(suffix);

	ret = gp_list_new(&list);
	if (ret < 0) die("list_new", ret);
	ret = gp_camera_folder_list_files(camera, folder, list, ctx);
	if (ret == 0) {
		count = gp_list_count(list);
		for (int i = 0; i < count; i++) {
			size_t len;
			gp_list_get_name(list, i, &name);
			len = strlen(name);
			if (len >= plen + slen && !strncmp(name, prefix, plen) &&
			    !strcmp(name + len - slen, suffix))
				n++;
		}
	}
	gp_list_free(list);

	ret = gp_list_new(&list);
	if (ret < 0) die("list_new", ret);
	ret = gp_camera_folder_list_folders(camera, folder, list, ctx);
	if (ret == 0 && depth < 8) {
		count = gp_list_count(list);
		for (int i = 0; i < count; i++) {
			char sub[512];
			gp_list_get_name(list, i, &name);
			snprintf(sub, sizeof(sub), "%s%s%s", folder, folder[strlen(folder) - 1] == '/' ? "" : "/", name);
			n += count_files(camera, sub, prefix, suffix, depth + 1);
		}
	}
	gp_list_free(list);
	return n;
}

// Count the virtual GPH_xxxx.JPG objects the camera creates per capture.
// A clean fuji capture flow leaves none behind once the client deletes its
// downloaded copy.
static int count_capture_objects(Camera *camera) {
	return count_files(camera, "/", "GPH_", ".JPG", 0);
}

static int do_preview(Camera *camera) {
	CameraFile *file;
	int ret = gp_file_new(&file);
	if (ret < 0) die("file_new", ret);
	ret = gp_camera_capture_preview(camera, file, ctx);
	if (ret < 0) die("capture_preview", ret);
	ret = gp_file_save(file, "/tmp/opencode/preview.jpg");
	if (ret < 0) die("file_save", ret);
	gp_file_free(file);
	printf("preview: /tmp/opencode/preview.jpg\n");
	return 0;
}

static int do_capture(Camera *camera, int download) {
	CameraFilePath path;
	int ret = gp_camera_capture(camera, GP_CAPTURE_IMAGE, &path, ctx);
	if (ret < 0) die("capture", ret);
	printf("captured: %s/%s\n", path.folder, path.name);

	if (!download) return 0;

	CameraFile *file;
	ret = gp_file_new(&file);
	if (ret < 0) die("file_new", ret);
	ret = gp_camera_file_get(camera, path.folder, path.name, GP_FILE_TYPE_NORMAL, file, ctx);
	if (ret < 0) die("file_get", ret);
	char out[512];
	snprintf(out, sizeof(out), "/tmp/opencode/%s", path.name);
	ret = gp_file_save(file, out);
	if (ret < 0) die("file_save", ret);
	gp_file_free(file);
	printf("downloaded: %s\n", out);

	ret = gp_camera_file_delete(camera, path.folder, path.name, ctx);
	if (ret < 0) fprintf(stderr, "note: delete failed: %d\n", ret);
	// Same-session check: the fuji capture flow (focus + shoot via 0xD208)
	// must produce exactly one object, and deleting the downloaded copy
	// must leave none behind
	printf("orphans after delete: %d\n", count_capture_objects(camera));
	return 0;
}

static void gplog(GPLogLevel level, const char *domain, const char *str, void *data) {
	(void)level; (void)domain; (void)data;
	fprintf(stderr, "%s\n", str);
}

// Set a config widget by name, e.g. "prioritymode" = "USB" or "bulb" = "1".
// Values are strings; toggle widgets parse ints.
static int do_setconfig(Camera *camera, const char *name, const char *value) {
	CameraWidget *root, *widget;
	CameraWidgetType type;
	int ret;

	ret = gp_camera_get_config(camera, &root, ctx);
	if (ret < 0) die("get_config", ret);
	ret = gp_widget_get_child_by_name(root, name, &widget);
	if (ret < 0) {
		fprintf(stderr, "FAIL: no config widget named %s\n", name);
		exit(1);
	}
	ret = gp_widget_get_type(widget, &type);
	if (ret < 0) die("widget_get_type", ret);

	if (type == GP_WIDGET_TOGGLE) {
		int val = atoi(value);
		ret = gp_widget_set_value(widget, &val);
	} else if (type == GP_WIDGET_RADIO || type == GP_WIDGET_MENU ||
		   type == GP_WIDGET_TEXT || type == GP_WIDGET_DATE) {
		ret = gp_widget_set_value(widget, value);
	} else {
		fprintf(stderr, "FAIL: unsupported widget type %d for %s\n", type, name);
		exit(1);
	}
	if (ret < 0) die("widget_set_value", ret);

	ret = gp_camera_set_config(camera, root, ctx);
	if (ret < 0) die("set_config", ret);
	gp_widget_free(root);
	printf("set %s = %s\n", name, value);
	return 0;
}

// Read a config widget value back, or NULL when it has no string value
static char *get_config_value(Camera *camera, const char *name) {
	CameraWidget *root, *widget;
	CameraWidgetType type;
	void *val = NULL;
	char *out = NULL;
	int ret;

	ret = gp_camera_get_config(camera, &root, ctx);
	if (ret < 0) die("get_config", ret);
	ret = gp_widget_get_child_by_name(root, name, &widget);
	if (ret < 0) die("get_child_by_name", ret);
	ret = gp_widget_get_type(widget, &type);
	if (ret < 0) die("widget_get_type", ret);
	if (type == GP_WIDGET_RADIO || type == GP_WIDGET_MENU || type == GP_WIDGET_TEXT) {
		char *s = NULL;
		if (gp_widget_get_value(widget, &s) == 0 && s != NULL)
			out = strdup(s);
	} else if (type == GP_WIDGET_TOGGLE) {
		int i = 0;
		if (gp_widget_get_value(widget, &i) == 0) {
			out = malloc(16);
			snprintf(out, 16, "%d", i);
		}
	}
	(void)val;
	gp_widget_free(root);
	return out;
}

// cameraaction (0xD208) round trip through the gphoto2 config layer:
// writing the AF action must read back as AF
static int do_action_af(Camera *camera) {
	do_setconfig(camera, "cameraaction", "AF");

	char *val = get_config_value(camera, "cameraaction");
	if (val == NULL) {
		fprintf(stderr, "FAIL: cameraaction has no value\n");
		exit(1);
	}
	if (strcmp(val, "AF") != 0) {
		fprintf(stderr, "FAIL: cameraaction read back as %s, expected AF\n", val);
		exit(1);
	}
	free(val);
	printf("cameraaction AF round trip OK\n");
	return 0;
}

// autofocusdrive through the gphoto2 config layer: drives
// _put_Fuji_AFDrive (0xD208 AF code + InitiateCapture + 0xD209 poll +
// AF stop code + InitiateCapture). Must succeed and create no image.
static int do_afdrive(Camera *camera) {
	do_setconfig(camera, "autofocusdrive", "1");

	int files = 0;
	for (;;) {
		CameraEventType type;
		void *data = NULL;
		int r = gp_camera_wait_for_event(camera, 300, &type, &data, ctx);
		if (r < 0) die("wait_for_event", r);
		if (type == GP_EVENT_FILE_ADDED) files++;
		free(data);
		if (type == GP_EVENT_TIMEOUT) break;
	}
	if (files != 0) {
		fprintf(stderr, "FAIL: autofocusdrive created %d object(s)\n", files);
		exit(1);
	}
	printf("autofocusdrive OK\n");
	return 0;
}

// Bulb flow through the gphoto2 config layer: opening the bulb must not
// create an image, closing it must create exactly one. The capture runs
// inside gp_camera_set_config (_put_Fuji_Bulb), so afterwards we drain the
// event queue with gp_camera_wait_for_event - that is also how gphoto2's
// object cache learns about the new object.
static int do_bulb(Camera *camera) {
	do_setconfig(camera, "bulb", "1");

	int opened = 0;
	for (;;) {
		CameraEventType type;
		void *data = NULL;
		int r = gp_camera_wait_for_event(camera, 300, &type, &data, ctx);
		if (r < 0) die("wait_for_event", r);
		if (type == GP_EVENT_FILE_ADDED) {
			CameraFilePath *p = data;
			printf("unexpected file from bulb open: %s/%s\n", p->folder, p->name);
			opened++;
		}
		free(data);
		if (type == GP_EVENT_TIMEOUT) break;
	}
	if (opened != 0) {
		fprintf(stderr, "FAIL: bulb open created %d object(s)\n", opened);
		exit(1);
	}

	do_setconfig(camera, "bulb", "0");

	int closed = 0;
	for (;;) {
		CameraEventType type;
		void *data = NULL;
		int r = gp_camera_wait_for_event(camera, 300, &type, &data, ctx);
		if (r < 0) die("wait_for_event", r);
		if (type == GP_EVENT_FILE_ADDED) {
			CameraFilePath *p = data;
			printf("bulb capture: %s/%s\n", p->folder, p->name);
			closed++;
		}
		free(data);
		if (type == GP_EVENT_TIMEOUT) break;
	}
	if (closed != 1) {
		fprintf(stderr, "FAIL: bulb close created %d object(s), expected 1\n", closed);
		exit(1);
	}
	printf("bulb flow OK\n");
	return 0;
}

int main(int argc, char **argv) {
	const char *cmd = argc > 1 ? argv[1] : "summary";

	// Debug logging only when requested, keeps default runs quiet
	if (getenv("GP_LOG_DEBUG") != NULL)
		gp_log_add_func(GP_LOG_DEBUG, gplog, NULL);

	ctx = gp_context_new();
	Camera *camera;
	autodetect(&camera);

	int ret;
	if (!strcmp(cmd, "summary")) {
		ret = do_summary(camera);
	} else if (!strcmp(cmd, "config")) {
		ret = do_config(camera);
	} else if (!strcmp(cmd, "list")) {
		ret = do_list(camera);
	} else if (!strcmp(cmd, "preview")) {
		ret = do_preview(camera);
	} else if (!strcmp(cmd, "capture")) {
		ret = do_capture(camera, 0);
	} else if (!strcmp(cmd, "download")) {
		ret = do_capture(camera, 1);
	} else if (!strcmp(cmd, "download2")) {
		// two captures in one session; both must produce an object
		ret = do_capture(camera, 1);
		if (ret == 0) ret = do_capture(camera, 1);
	} else if (!strcmp(cmd, "orphans")) {
		// count capture objects left on the camera (GPH_xxxx.JPG)
		printf("orphans: %d\n", count_capture_objects(camera));
		ret = 0;
	} else if (!strcmp(cmd, "setconfig") && argc > 3) {
		ret = do_setconfig(camera, argv[2], argv[3]);
	} else if (!strcmp(cmd, "actionaf")) {
		ret = do_action_af(camera);
	} else if (!strcmp(cmd, "afdrive")) {
		ret = do_afdrive(camera);
	} else if (!strcmp(cmd, "bulb")) {
		ret = do_bulb(camera);
	} else {
		fprintf(stderr, "unknown command %s\n", cmd);
		ret = 1;
	}

	gp_camera_exit(camera, ctx);
	gp_camera_free(camera);
	gp_context_unref(ctx);
	return ret;
}