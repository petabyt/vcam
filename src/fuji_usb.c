#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <vcam.h>
#include <fujiptp.h>
#include <cl_data.h>
#include "fuji.h"

int fuji_usb_init_cam(vcam *cam, const char *name, int argc, char **argv) {
	strcpy(cam->manufac, "Fujifilm Corp");
	cam->vendor = 0x4cb;
	if (!strcmp(name, "fuji_x_a2")) {
		strcpy(cam->model, "X-A2");
		cam->product = 0x2c6;
	} else {
		return -1;
	}

	return 0;
}

// .. implement fuji usb stuff...
