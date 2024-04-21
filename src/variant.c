#include <stdlib.h>
#include <string.h>
#include <vcam.h>

// This should give specific info that describes what is different about different models 'variant'
// of the same brand. All other behavior is assumed based on the 'type'
int vcam_get_variant_info(char *arg, struct CamConfig *o) {
	if (!strcmp(arg, "fuji_x_a2")) {
		strcpy(o->model, "X-A2");
		o->type = CAM_FUJI_WIFI;
		o->variant = V_FUJI_X_A2;
		o->image_get_version = 1;
		o->get_object_version = 2;
		o->remote_version = 0;
	} else if (!strcmp(arg, "fuji_x_t20")) {
		strcpy(o->model, "X-T20");
		o->type = CAM_FUJI_WIFI;
		o->variant = V_FUJI_X_T20;
		o->image_get_version = 3;
		o->get_object_version = 4;
		o->remote_version = 0x00020004;
		o->remote_get_object_version = 2;
	} else if (!strcmp(arg, "fuji_x_t2")) {
		strcpy(o->model, "X-T2");
		o->type = CAM_FUJI_WIFI;
		o->variant = V_FUJI_X_T2;
		o->image_get_version = 3;
		o->get_object_version = 4;
		o->remote_version = 0x0002000a;
		o->remote_get_object_version = 2;
	} else if (!strcmp(arg, "fuji_x_s10")) {
		strcpy(o->model, "X-S10");
		o->type = CAM_FUJI_WIFI;
		o->variant = V_FUJI_X_S10;
		o->image_get_version = 3;
		o->get_object_version = 4;
		o->remote_version = 0x0002000a; // fuji sets to 2000b
		o->remote_get_object_version = 4;
	} else if (!strcmp(arg, "fuji_x_h1")) {
		strcpy(o->model, "X-H1");
		o->type = CAM_FUJI_WIFI;
		o->variant = V_FUJI_X_H1;
		o->image_get_version = 3; // fuji sets to 4
		o->get_object_version = 4;
		o->remote_version = 0x00020006; // fuji sets to 2000C
		o->remote_get_object_version = 4;
	} else if (!strcmp(arg, "fuji_x_f10")) {
		strcpy(o->model, "X-F10");
		o->type = CAM_FUJI_WIFI;
		o->variant = V_FUJI_X_F10;
		o->image_get_version = 3;
		o->get_object_version = 4;
		o->remote_version = 0x00020004; // fuji sets to 2000C
		o->remote_get_object_version = 2;
	} else if (!strcmp(arg, "fuji_x30")) {
		strcpy(o->model, "X30");
		o->type = CAM_FUJI_WIFI;
		o->variant = V_FUJI_X30;
		o->image_get_version = 3;
		o->get_object_version = 3; // 2016 fuji sets to 4
		o->remote_version = 0x00020002; // 2024 fuji sets to 2000C
		o->remote_get_object_version = 1;
	} else if (!strcmp(arg, "canon_1300d")) {
		strcpy(o->model, "Canon EOS Rebel T6");
		strcpy(o->version, "3-1.2.0");
		strcpy(o->serial, "828af56");
		o->type = CAM_CANON;
		o->variant = V_CANON_1300D;
	} else {
		return 1;
	}

	return 0;
}
