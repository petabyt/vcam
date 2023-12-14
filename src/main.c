#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>

#include <vcam.h>

int main(int argc, char *argv[]) {
	struct CamConfig options;
	memset(&options, 0, sizeof(options));

	options.type = -1;

	strcpy(options.model, "Generic");
	strcpy(options.version, "1.0");
	strcpy(options.serial, "1");

	for (int i = 0; i < argc; i++) {
		vcam_get_variant_info(argv[i], &options);
		if (!strcmp(argv[i], "--local")) {
			options.use_local = 1;
		} else if (!strcmp(argv[i], "--select-img")) {
			options.is_select_multiple_images = 1;
		}
	}

	if (options.type == CAM_FUJI_WIFI) {
		return fuji_wifi_main(&options);
	} else if (options.type == CAM_CANON) {
		return canon_wifi_main(&options);
	} else {
		if (options.type == -1) {
			printf("No camera supplied\n");
		} else {
			printf("Unknown camera type\n");
		}
		return 1;
	}
}
