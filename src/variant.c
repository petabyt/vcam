#include <stdlib.h>
#include <string.h>
#include <vcam.h>

struct CamConfig *vcam_new_config(int argc, char **argv) {
	struct CamConfig *options = calloc(sizeof(struct CamConfig), 1);

	options->type = -1;

	strcpy(options->model, "Generic");
	strcpy(options->version, "1.0");
	strcpy(options->serial, "1");

	if (argc < 2) {
		return NULL;
	}

	char this_ip[64];
	get_local_ip(this_ip);
	strcpy(options->ip_address, this_ip);

	vcam_get_variant_info(argv[1], options);

	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "--ip")) {
			options->use_custom_ip = 1;
			i++;
			strcpy(options->ip_address, argv[i]);
		} else if (!strcmp(argv[i], "--local-ip")) {
			options->use_custom_ip = 1;
			strcpy(options->ip_address, this_ip);
		} else if (!strcmp(argv[i], "--select-img")) {
			options->is_select_multiple_images = 1;
		} else if (!strcmp(argv[i], "--fs")) {
			extern char *vcamera_filesystem;
			vcamera_filesystem = argv[i + 1];
			i++;
		} else if (!strcmp(argv[i], "--sig")) {
			i++;
			options->sig = atoi(argv[i]);
		} else if (!strcmp(argv[i], "--discovery")) {
			options->do_discovery = 1;
		} else if (!strcmp(argv[i], "--register")) {
			options->do_register = 1;
		} else if (!strcmp(argv[i], "--tether")) {
			options->do_tether = 1;
		} else {
			printf("Unknown option %s\n", argv[i]);
			return NULL;
		}
	}

	return options;
}