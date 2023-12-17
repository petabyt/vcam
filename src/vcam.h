// Copyright (c) 2015,2016 Marcus Meissner <marcus@jet.franken.de> - GNU General Public License v2
#ifndef IOLIBS_VUSB_VCAMERA_H
#define IOLIBS_VUSB_VCAMERA_H

#undef FUZZING
#define FUZZ_PTP

#include <stdio.h>
#include <stdint.h>
#include <ptp.h>
#include "gphoto.h"

void vcam_log(const char *format, ...);
void gp_log_(const char *format, ...);

int ptp_get_object_count();

// Generic options setup by CLI, put in cam->conf
// All are 0 by default
struct CamConfig {
	int type;
	int variant;
	char model[32];
	char version[16];
	char serial[16];

	int run_slow;
	int use_local;
	int is_mirrorless;

	// Fuji stuff
	int is_select_multiple_images;
	int image_get_version;
	int image_explore_version;
	int remote_version;
	int remote_image_explore_version;

	// Canon stuff
	int digic;
};

int fuji_wifi_main(struct CamConfig *options);
int canon_wifi_main(struct CamConfig *options);

typedef enum vcameratype {
	GENERIC_PTP = 1,
	CAM_NIKON_D750,
	CAM_NIKON,
	CAM_CANON,
	CAM_FUJI_WIFI,
} vcameratype;

typedef enum vcameravariant {
	V_FUJI_X_A2 = 1,
	V_FUJI_X_T20,
	V_FUJI_X_S10,
	V_CANON_1300D,
} vcameravariant;

typedef struct ptpcontainer {
	unsigned int size;
	unsigned int type;
	unsigned int code;
	unsigned int seqnr;
	unsigned int nparams;
	unsigned int params[6];
	unsigned int has_data_phase;
} ptpcontainer;

struct PtpPropList {
	int code;
	void *data;
	int length;

	void *avail;
	int avail_size;
	int avail_cnt;

	void *next;
};

// All members are garunteed to be zero by calloc()
typedef struct vcamera {
	int (*init)(struct vcamera*);
	int (*exit)(struct vcamera*);
	int (*open)(struct vcamera*, const char *port);
	int (*close)(struct vcamera*);

	unsigned char	*inbulk;
	int		nrinbulk;
	unsigned char	*outbulk;
	int		nroutbulk;
	unsigned int	seqnr;
	unsigned int	session;
	ptpcontainer	ptpcmd;

	int (*read)(struct vcamera*, int ep, unsigned char *data, int bytes);
	int (*readint)(struct vcamera*, unsigned char *data, int bytes, int timeout);
	int (*write)(struct vcamera*, int ep, const unsigned char *data, int bytes);

	// Linked list containing bulk properties
	struct PtpPropList *list;
	struct PtpPropList *list_tail;

	uint16_t vendor;
	uint16_t product;

	vcameratype	type;

	struct CamConfig *conf;

	// Generic camera internal properties
	int exposurebias;
	unsigned int shutterspeed;
	unsigned int fnumber;
	unsigned int focal_length;
	unsigned int target_distance_feet;

	// Fujifilm server related attributes
	int fuji_test_cam_attr;

	// Canon PTP/IP server related things
	int is_lv_ready;

#ifdef FUZZING
	int		fuzzmode;
#define FUZZMODE_PROTOCOL	0
#define FUZZMODE_NORMAL		1
	FILE*		fuzzf;
	unsigned int	fuzzpending;
#endif
} vcamera;

vcamera *vcamera_new(vcameratype);

int vcam_get_variant_info(char *arg, struct CamConfig *o);

int vcam_fuji_setup(vcamera *cam);
int vcam_canon_setup(vcamera *cam);

struct ptp_function {
	int	code;
	int	(*write)(vcamera *cam, ptpcontainer *ptp);
	int	(*write_data)(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int size);
};

int vcam_generic_send_file(char *path, vcamera *cam, ptpcontainer *ptp);

void ptp_senddata(vcamera *cam, uint16_t code, unsigned char *data, int bytes);
void ptp_response(vcamera *cam, uint16_t code, int nparams, ...);

void vcam_set_prop(vcamera *cam, int code, uint32_t value);
void vcam_set_prop_data(vcamera *cam, int code, void *data, int length);
void vcam_set_prop_avail(vcamera *cam, int code, int size, int cnt, void *data);

// Standard PTP opcode implementations
int ptp_opensession_write(vcamera *cam, ptpcontainer *ptp);
int ptp_closesession_write(vcamera *cam, ptpcontainer *ptp);
int ptp_deviceinfo_write(vcamera *cam, ptpcontainer *ptp);
int ptp_getnumobjects_write(vcamera *cam, ptpcontainer *ptp);
int ptp_getobjecthandles_write(vcamera *cam, ptpcontainer *ptp);
int ptp_getstorageids_write(vcamera *cam, ptpcontainer *ptp);
int ptp_getstorageinfo_write(vcamera *cam, ptpcontainer *ptp);
int ptp_getobjectinfo_write(vcamera *cam, ptpcontainer *ptp);
int ptp_getobject_write(vcamera *cam, ptpcontainer *ptp);
int ptp_getthumb_write(vcamera *cam, ptpcontainer *ptp);
int ptp_deleteobject_write(vcamera *cam, ptpcontainer *ptp);
int ptp_getdevicepropdesc_write(vcamera *cam, ptpcontainer *ptp);
int ptp_getdevicepropvalue_write(vcamera *cam, ptpcontainer *ptp);
int ptp_setdevicepropvalue_write(vcamera *cam, ptpcontainer *ptp);
int ptp_setdevicepropvalue_write_data(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len);
int ptp_initiatecapture_write(vcamera *cam, ptpcontainer *ptp);
int ptp_vusb_write(vcamera *cam, ptpcontainer *ptp);
int ptp_vusb_write_data(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len);
int ptp_nikon_setcontrolmode_write(vcamera *cam, ptpcontainer *ptp);
int ptp_getpartialobject_write(vcamera *cam, ptpcontainer *ptp);

typedef union _PTPPropertyValue {
	char *str; /* common string, malloced */
	uint8_t u8;
	int8_t i8;
	uint16_t u16;
	int16_t i16;
	uint32_t u32;
	int32_t i32;
	uint64_t u64;
	int64_t i64;
	/* XXXX: 128 bit signed and unsigned missing */
	struct array {
		uint32_t count;
		union _PTPPropertyValue *v; /* malloced, count elements */
	} a;
} PTPPropertyValue;

struct _PTPPropDescRangeForm {
	PTPPropertyValue MinimumValue;
	PTPPropertyValue MaximumValue;
	PTPPropertyValue StepSize;
};
typedef struct _PTPPropDescRangeForm PTPPropDescRangeForm;

/* Property Describing Dataset, Enum Form */
struct _PTPPropDescEnumForm {
	uint16_t NumberOfValues;
	PTPPropertyValue *SupportedValue; /* malloced */
};
typedef struct _PTPPropDescEnumForm PTPPropDescEnumForm;

/* Device Property Describing Dataset (DevicePropDesc) */
struct _PTPDevicePropDesc {
	uint16_t DevicePropertyCode;
	uint16_t DataType;
	uint8_t GetSet;
	PTPPropertyValue FactoryDefaultValue;
	PTPPropertyValue CurrentValue;
	uint8_t FormFlag;
	union {
		PTPPropDescEnumForm Enum;
		PTPPropDescRangeForm Range;
	} FORM;
};
typedef struct _PTPDevicePropDesc PTPDevicePropDesc;

// GetPropertyValue stubs
int ptp_battery_getdesc(vcamera *, PTPDevicePropDesc *);
int ptp_battery_getvalue(vcamera *, PTPPropertyValue *);
int ptp_imagesize_getdesc(vcamera *, PTPDevicePropDesc *);
int ptp_imagesize_getvalue(vcamera *, PTPPropertyValue *);
int ptp_datetime_getdesc(vcamera *, PTPDevicePropDesc *);
int ptp_datetime_getvalue(vcamera *, PTPPropertyValue *);
int ptp_datetime_setvalue(vcamera *, PTPPropertyValue *);
int ptp_shutterspeed_getdesc(vcamera *, PTPDevicePropDesc *);
int ptp_shutterspeed_getvalue(vcamera *, PTPPropertyValue *);
int ptp_shutterspeed_setvalue(vcamera *, PTPPropertyValue *);
int ptp_fnumber_getdesc(vcamera *, PTPDevicePropDesc *);
int ptp_fnumber_getvalue(vcamera *, PTPPropertyValue *);
int ptp_fnumber_setvalue(vcamera *, PTPPropertyValue *);
int ptp_exposurebias_getdesc(vcamera *, PTPDevicePropDesc *);
int ptp_exposurebias_getvalue(vcamera *, PTPPropertyValue *);
int ptp_exposurebias_setvalue(vcamera *, PTPPropertyValue *);


// A bunch of janky macros
#define CHECK(result)               \
	{                           \
		int r = (result);   \
		if (r < 0)          \
			return (r); \
	}

#define CHECK_PARAM_COUNT(x)                                                                                          \
	if (ptp->nparams != x) {                                                                                      \
		gp_log(GP_LOG_ERROR, __FUNCTION__, "%X: params should be %d, but is %d", ptp->code, x, ptp->nparams); \
		ptp_response(cam, PTP_RC_GeneralError, 0);                                                            \
		return 1;                                                                                             \
	}

// Check the transaction ID
#if 0
#define CHECK_SEQUENCE_NUMBER()                                                                                   \
	if (ptp->seqnr != cam->seqnr) {                                                                           \
		/* not clear if normal cameras react like this */                                                 \
		gp_log(GP_LOG_ERROR, __FUNCTION__, "seqnr %d was sent, expected was %d", ptp->seqnr, cam->seqnr); \
		ptp_response(cam, PTP_RC_GeneralError, 0);                                                        \
		return 1;                                                                                         \
	}
#else
#define CHECK_SEQUENCE_NUMBER() ;
#endif

#define CHECK_SESSION()                                                    \
	if (!cam->session) {                                               \
		gp_log(GP_LOG_ERROR, __FUNCTION__, "session is not open"); \
		ptp_response(cam, PTP_RC_SessionNotOpen, 0);               \
		return 1;                                                  \
	}

struct ptp_dirent {
	uint32_t id;
	char *name;
	char *fsname;
	struct stat stbuf;
	struct ptp_dirent *parent;
	struct ptp_dirent *next;
};

struct ptp_interrupt {
	unsigned char *data;
	int size;
	struct timeval triggertime;
	struct ptp_interrupt *next;
};

extern struct ptp_dirent *first_dirent;
extern uint32_t ptp_objectid;

void vcam_dump(void *ptr, size_t len);

void ptp_free_devicepropdesc(PTPDevicePropDesc *dpd);

// Data structure API
int put_32bit_le_array(unsigned char *data, uint32_t *arr, int cnt);
int put_16bit_le_array(unsigned char *data, uint16_t *arr, int cnt);
char *get_string(unsigned char *data);
int put_string(unsigned char *data, char *str);
int put_8bit_le(unsigned char *data, uint8_t x);
int put_16bit_le(unsigned char *data, uint16_t x);
int put_32bit_le(unsigned char *data, uint32_t x);
int put_64bit_le(unsigned char *data, uint64_t x);
int8_t get_i8bit_le(unsigned char *data);
uint8_t get_8bit_le(unsigned char *data);
uint16_t get_16bit_le(unsigned char *data);
uint32_t get_32bit_le(unsigned char *data);

void *read_file(struct ptp_dirent *cur);
void free_dirent(struct ptp_dirent *ent);

// Deletes the first object from the 
void vcam_virtual_pop_object(int id);

int ptp_inject_interrupt(vcamera *cam, int when, uint16_t code, int nparams, uint32_t param1, uint32_t transid);

#pragma pack(push, 1)
struct CamGenericEvent {
	uint32_t size;
	uint16_t x;
	uint16_t code;
	uint32_t transaction;
	uint32_t value;
};
#pragma pack(pop)

int ptp_notify_event(vcamera *cam, uint16_t code, uint32_t value);

struct CamGenericEvent ptp_pop_event(vcamera *cam);

#include "canon.h"
#include "fuji.h"
#include "ops.h"

uint32_t ptp_write_u32(void *dat, uint32_t v);
uint8_t ptp_write_u8(void *dat, uint8_t v);
uint32_t ptp_read_u32(void *dat, uint32_t *buf);
uint16_t ptp_read_u16(void *dat, uint16_t *buf);
uint8_t ptp_read_u8(void *dat, uint8_t *buf);

uint8_t ptp_read_uint8(void *dat);
uint16_t ptp_read_uint16(void *dat);
uint32_t ptp_read_uint32(void *dat);
void ptp_read_string(void *dat, char *string, int max);
int ptp_read_uint16_array(void *dat, uint16_t *buf, int max);
int ptp_read_uint32_array(void *dat, uint16_t *buf, int max);
int ptp_wide_string(char *buffer, int max, char *input);
void ptp_write_uint8(void *dat, uint8_t b);
int ptp_write_uint32(void *dat, uint32_t b);
int ptp_write_string(void *dat, char *string);
int ptp_write_utf8_string(void *dat, char *string);

#endif /* !defined(IOLIBS_VUSB_VCAMERA_H) */
