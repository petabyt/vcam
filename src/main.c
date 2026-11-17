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

static void json_string(const char *value) {
	if (!value) {
		fputs("null", stdout);
		return;
	}
	putchar('"');
	for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
		if (*p == '"' || *p == '\\') {
			putchar('\\');
			putchar(*p);
		} else if (*p < 0x20) {
			printf("\\u%04x", *p);
		} else {
			putchar(*p);
		}
	}
	putchar('"');
}

static void print_camera_json(const vcam *cam, const char *name, const char *backend) {
	fputs("{\"camera\":", stdout); json_string(name);
	fputs(",\"backend\":", stdout); json_string(backend);
	fputs(",\"manufacturer\":", stdout); json_string(cam->manufac);
	fputs(",\"model\":", stdout); json_string(cam->model);
	fputs(",\"version\":", stdout); json_string(cam->version);
	fputs(",\"serial\":", stdout); json_string(cam->serial);
	fputs(",\"vendor_extension\":", stdout); json_string(cam->extension);
	printf(",\"vendor_extension_id\":%u,\"vendor_extension_version\":%u",
	       (unsigned int)(cam->vendor_extension_id ? cam->vendor_extension_id : PTP_VENDOR_MICROSOFT),
	       (unsigned int)(cam->vendor_extension_version ? cam->vendor_extension_version : 0x64));
	printf(",\"usb\":{\"vendor_id\":%u,\"product_id\":%u},\"filesystem\":", (unsigned int)cam->vendor_id, (unsigned int)cam->product_id);
	json_string(cam->vcamera_filesystem);
	fputs(",\"bind_ip\":", stdout); json_string(cam->custom_ip_addr);
	fputs(",\"wifi\":{\"ssid\":", stdout); json_string(cam->ap_ssid);
	fputs(",\"password\":", stdout); json_string(cam->ap_password);
	fputs(",\"gateway\":", stdout); json_string(cam->ap_gateway);
	fputs("},\"bluetooth\":{\"pairing\":", stdout); json_string(cam->ble_pairing);
	fputs("}}\n", stdout);
}

int main(int argc, char *argv[]) {
	signal(SIGINT, sigint_handler);
	// Backends write to sockets whose peer may close at any time (vhci
	// socketpair, PTP/IP TCP) and log to pipes - never die on SIGPIPE
	signal(SIGPIPE, SIG_IGN);
	int json_only = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--dump-camera-json"))
			json_only = 1;
	}
	int json_stdout = -1;
	if (json_only) {
		fflush(stdout);
		json_stdout = dup(STDOUT_FILENO);
		if (json_stdout < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
			perror("redirecting camera diagnostics");
			return 1;
		}
	}

	if (argc < 3) {
		printf(
			"Usage: vcam <model> <backend> ... flags ...\n"
			"Example:\n"
			"vcam canon_1300d tcp\n"
			"--local-ip\tUse IP address of this machine\n"
			"--fs <path>\tSpecify path to scan for PTP filesystem\n"
			"--sig <pid>\tSpecify process to signal when TCP server is listening\n"
			"--dump\tDump all communication data to COMM_DUMP\n"
			"--dump-camera-json\tPrint camera information as JSON and exit (includes Wi-Fi password)\n"
			"--strict\tExit on invalid Fuji gallery object IDs\n"
			"--discovery-wireless\tFuji AutoSave discovery with wireless communication mode\n"
			"--ble-handover\tRequire Fuji X-App image-view function mode\n"
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
	if (json_only) {
		int out = 0;
		for (int i = 0; i < main_argc; i++) {
			if (strcmp(argv[i + 3], "--dump-camera-json"))
				argv[3 + out++] = argv[i + 3];
		}
		main_argc = out;
	}
	if (backend == VCAM_WIFI) {
		main_argc = vcam_wifi_filter_args(main_argc, argv + 3, vcam_wifi_config());
	}

	vcam *cam = vcam_init_standard();
	cam->camera_json_only = json_only;
	int rc = vcam_main(cam, name, backend, main_argc, argv + 3);
	if (json_only && rc == 0) {
		fflush(stdout);
		if (dup2(json_stdout, STDOUT_FILENO) < 0) {
			perror("restoring JSON output");
			return 1;
		}
		close(json_stdout);
		print_camera_json(cam, name, backend_str);
		if (fflush(stdout) == EOF)
			return 1;
	}

	close_all_fds();
	return rc;
}
