#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <vcam.h>

int main(int argc, char *argv[]) {
	struct CamConfig *options = vcam_new_config(argc, argv);
	if (options == NULL) return -1;

	if (options->type == CAM_FUJI_WIFI) {
		return fuji_wifi_main(options);
	} else if (options->type == CAM_CANON) {
		return canon_wifi_main(options);
	} else {
		if (options->type == -1) {
			printf("No camera supplied\n");
		} else {
			printf("Unknown camera type\n");
		}
		return 1;
	}
}
