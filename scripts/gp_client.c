// Small libgphoto2 client for testing vcam through the fake libusb shim.
// Build (with libgphoto2 installed to $GP_PREFIX, e.g. /tmp/opencode/gp-install):
//   cc -o gp_client gp_client.c -I$GP_PREFIX/include -L$GP_PREFIX/lib/x86_64-linux-gnu -lgphoto2 -lgphoto2_port
// Run:
//   LD_PRELOAD=$VCAM/build/usb-vcam.so \
//   LD_LIBRARY_PATH=$GP_PREFIX/lib/x86_64-linux-gnu \
//   ./gp_client [summary|config|list|preview|capture|download]
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
	return 0;
}

static void gplog(GPLogLevel level, const char *domain, const char *str, void *data) {
	(void)level; (void)domain; (void)data;
	fprintf(stderr, "%s\n", str);
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
		// two captures in one session: gphoto2 switches CaptureDestination
		// to SDRAM (0x4) from the second trigger onwards
		ret = do_capture(camera, 1);
		if (ret == 0) ret = do_capture(camera, 1);
	} else {
		fprintf(stderr, "unknown command %s\n", cmd);
		ret = 1;
	}

	gp_camera_exit(camera, ctx);
	gp_camera_free(camera);
	gp_context_unref(ctx);
	return ret;
}