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

int main(int argc, char *argv[]) {
	signal(SIGINT, sigint_handler);
	// Backends write to sockets whose peer may close at any time (vhci
	// socketpair, PTP/IP TCP) and log to pipes - never die on SIGPIPE
	signal(SIGPIPE, SIG_IGN);

	if (argc < 3) {
		printf(
			"Usage: vcam <model> <backend> ... flags ...\n"
			"Example:\n"
			"vcam canon_1300d tcp\n"
			"--local-ip\tUse IP address of this machine\n"
			"--fs <path>\tSpecify path to scan for PTP filesystem\n"
			"--sig <pid>\tSpecify process to signal when TCP server is listening\n"
			"--dump\tDump all communication data to COMM_DUMP\n"
			"\nWiFi backend (./vcam <model> wifi):\n"
			"--wifi-dev <if>\tAccess point interface (default: auto-pick, never your\n"
			"\t\t\tdefault-route/connected card)\n"
			"--channel <n>\t2.4 GHz channel (default 6)\n"
			"--ssid <s>\tAP SSID override (default: hardcoded per model)\n"
			"--password <p>\tWPA2 passphrase override (empty string = open network)\n"
			"--gateway <ip>\tAP gateway IP override (default: hardcoded per model)\n"
			"--reg-domain <cc>\tSet the kernel regulatory domain (GLOBAL - all radios)\n"
			"--wait-timeout <s>\tSeconds to wait for a phone (default 120, 0 = skip)\n"
			"--check\tPrint the bring-up plan, touch nothing\n"
			"--cleanup\tRevert leftovers of a previous crashed run\n"
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
	} else if (!strcmp(backend_str, "wifi")) {
		backend = VCAM_WIFI;
	} else {
		vcam_log("Unknown backend '%s'\n", backend_str);
		return -1;
	}

	// The wifi backend has its own flags (--wifi-dev/--channel/--ssid/...)
	// that the vendor parsers must not see - strip them in place
	int main_argc = argc - 3;
	if (backend == VCAM_WIFI) {
		main_argc = vcam_wifi_filter_args(argc - 3, argv + 3, vcam_wifi_config());
	}

	vcam *cam = vcam_init_standard();
	int rc = vcam_main(cam, name, backend, main_argc, argv + 3);

	close_all_fds();
	return rc;
}
