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

	if (argc < 2) {
		printf("Usage: vcam <model> ... flags ...\n");
		return -1;
	}

	int rc = vcam_main(argv[1], argc - 2, argv + 2);

	close_all_fds();
	return rc;
}
