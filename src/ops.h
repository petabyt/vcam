// Map of all opcodes for the current build
#ifndef VCAM_OPCODES_H
#define VCAM_OPCODES_H

// Standard USB opcodes
extern struct ptp_function ptp_functions_generic[];

// Nikon stuff (from gphoto)
static struct ptp_function ptp_functions_nikon_dslr[] = {
	{0x90c2,	ptp_nikon_setcontrolmode_write, NULL},
	{0, NULL, NULL},
};

extern struct ptp_function ptp_functions_fuji_wifi[];

extern struct ptp_function ptp_functions_canon[];

static struct ptp_map_functions {
	vcameratype		type;
	struct ptp_function	*functions;
	unsigned int		nroffunctions;
} ptp_functions[] = {
	{GENERIC_PTP,	ptp_functions_generic, 0},
	{CAM_NIKON_D750,	ptp_functions_nikon_dslr, 0},
	{CAM_CANON,	ptp_functions_canon, 0},
	{CAM_FUJI_WIFI,	ptp_functions_fuji_wifi, 0},
};

extern struct ptp_property ptp_properties[];

#endif // endif opcodes.h
