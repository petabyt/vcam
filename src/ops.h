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

static struct ptp_property {
	int code;
	int (*getdesc)(vcamera *cam, PTPDevicePropDesc *);
	int (*getvalue)(vcamera *cam, PTPPropertyValue *);
	int (*setvalue)(vcamera *cam, PTPPropertyValue *);
}ptp_properties[] = {
    {0x5001, ptp_battery_getdesc, ptp_battery_getvalue, NULL},
    {0x5003, ptp_imagesize_getdesc, ptp_imagesize_getvalue, NULL},
    {0x5007, ptp_fnumber_getdesc, ptp_fnumber_getvalue, ptp_fnumber_setvalue},
    {0x5010, ptp_exposurebias_getdesc, ptp_exposurebias_getvalue, ptp_exposurebias_setvalue},
    {0x500d, ptp_shutterspeed_getdesc, ptp_shutterspeed_getvalue, ptp_shutterspeed_setvalue},
    {0x5011, ptp_datetime_getdesc, ptp_datetime_getvalue, ptp_datetime_setvalue},
};

#endif // endif opcodes.h
