#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>

#include <vcam.h>

static int arg_check_vendor(char *arg, struct CamConfig *o) {
	if (!strcmp(arg, "fuji_x_a2")) {
		strcpy(o->name, "X-A2");
		o->type = CAM_FUJI_WIFI;
		o->variant = V_FUJI_X_A2;
		o->image_get_version = 1;
		o->image_explore_version = 2;
		o->remote_version = 0;
	} else if (!strcmp(arg, "fuji_x_t20")) {
		strcpy(o->name, "X-T20");
		o->type = CAM_FUJI_WIFI;
		o->variant = V_FUJI_X_T20;
		o->image_get_version = 3;
		o->image_explore_version = 4;
		o->remote_version = 0x00020004;
		o->remote_image_explore_version = 2;
	} else if (!strcmp(arg, "canon_1300d")) {
		strcpy(o->name, "Canon Rebel T6");
		o->type = CAM_CANON;
		o->variant = V_CANON_1300D;
	}

	return 0;
}

int main(int argc, char *argv[]) {
	struct CamConfig options;
	memset(&options, 0, sizeof(options));

	strcpy(options.name, "Unspecified");

	for (int i = 0; i < argc; i++) {
		arg_check_vendor(argv[i], &options);
		if (!strcmp(argv[i], "--local")) {
			options.use_local = 1;
		}
	}

	if (options.type == CAM_FUJI_WIFI) {
		return fuji_wifi_main(&options);
	} else if (options.type == CAM_CANON) {
		puts("TODO:");
	} else {
		printf("Unknown camera type '%s'\n", options.name);
		return 1;
	}
}
