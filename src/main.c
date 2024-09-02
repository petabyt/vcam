#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <vcam.h>
#include <dirent.h>

static void close_all_fds() {
	vcam_log("Closing all fds\n");
	DIR *dir = opendir("/proc/self/fd");
	if (!dir) return;

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		int fd = atoi(entry->d_name);
		if (fd > 2) close(fd);  // Skip stdin (0), stdout (1), stderr (2)
	}

	closedir(dir);
}

void sigint_handler(int x) {
	vcam_log("SIGINT handler\n");
	close_all_fds();
	exit(0);
}

int main(int argc, char *argv[]) {
	signal(SIGINT, sigint_handler);

	struct CamConfig *options = vcam_new_config(argc, argv);
	if (options == NULL) {
		printf("Usage: vcam <model> ... flags ...\n");
		return -1;
	}

	int rc = 0;
	if (options->type == CAM_FUJI_WIFI) {
		rc = fuji_wifi_main(options);
	} else if (options->type == CAM_CANON) {
		rc = canon_wifi_main(options);
	} else {
		if (options->type == -1) {
			printf("No camera supplied\n");
		} else {
			printf("Unknown camera type\n");
		}
		rc = 1;
	}

	close_all_fds();
	return rc;
}
