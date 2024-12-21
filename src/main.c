#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
#include <vcam.h>

static void close_all_fds(void) {
	//vcam_log("Closing all fds");
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

int main(int argc, const char *argv[]) {
	signal(SIGINT, sigint_handler);

	if (argc < 3) {
		printf(
			"Usage: vcam <model> <backend> ... flags ...\n"
			"Example:\n"
			"vcam canon_1300d tcp\n"
			"--local-ip\tUse IP address of this machine\n"
			"--fs <path>\tSpecify path to scan for PTP filesystem\n"
			"--sig <pid>\tSpecify process to signal when TCP server is listening\n"
			"--dump\tDump all communication data to COMM_DUMP\n"
		);
		return -1;
	}

	const char *name = argv[1];
	const char *backend_str = argv[2];

	enum CamBackendType backend;
	if (!strcmp(backend_str, "tcp")) {
		backend = VCAM_TCP;
	} else if (!strcmp(backend_str, "otg")) {
		backend = VCAM_GADGETFS;
	} else if (!strcmp(backend_str, "vhci")) {
		backend = VCAM_VHCI;
	} else {
		vcam_log("Unknown backend '%s'\n", backend_str);
		return -1;
	}

	vcam *cam = vcam_init_standard();
	int rc = vcam_main(cam, name, backend, argc - 3, argv + 3);

	close_all_fds();
	return rc;
}
