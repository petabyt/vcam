// Copyright (c) 2015,2016 Marcus Meissner <marcus@jet.franken.de> - GNU General Public License v2
#ifndef IOLIBS_VUSB_VCAMERA_H
#define IOLIBS_VUSB_VCAMERA_H

#undef FUZZING
#define FUZZ_PTP

#include <stdio.h>
#include <stdint.h>
#include <ptp.h>
#include "gphoto.h"

#define FUZZMODE_PROTOCOL	0
#define FUZZMODE_NORMAL		1

void vcam_log(const char *format, ...);
void gp_log_(const char *format, ...);

// Generic options setup by CLI, put in cam->conf
// This structure is zeroed
struct CamConfig {
	int type;
	int variant;
	char model[32];
	char version[16];
	char serial[16];

	int run_slow;
	int is_mirrorless;
	int use_custom_ip;
	char ip_address[64]; 

	// Fuji stuff
	int do_discovery;
	int do_register;
	int do_tether;
	int is_select_multiple_images;
	int image_get_version;
	int get_object_version;
	int remote_version;
	int remote_get_object_version;

	// Canon stuff
	int digic;
};

int get_local_ip(char buffer[64]);
int fuji_wifi_main(struct CamConfig *options);
int canon_wifi_main(struct CamConfig *options);

typedef enum vcameratype {
	GENERIC_PTP = 1,
	CAM_NIKON,
	CAM_CANON,
	CAM_FUJI_WIFI,
	CAM_FUJI_USB,
}vcameratype;

typedef enum vcameravariant {
	V_FUJI_X_A2 = 1,
	V_FUJI_X30,
	V_FUJI_X_T20,
	V_FUJI_X_T2,
	V_FUJI_X_F10,
	V_FUJI_X_S10,
	V_FUJI_X_H1,

	V_CANON_1300D,
	CAM_NIKON_D750,
}vcameravariant;

typedef struct ptpcontainer {
	unsigned int size;
	unsigned int type;
	unsigned int code;
	unsigned int seqnr;
	unsigned int nparams;
	unsigned int params[6];
	unsigned int has_data_phase;
}ptpcontainer;

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
#ifdef FUZZING
	int	fuzzmode;
	FILE *fuzzf;
	unsigned int fuzzpending;
#endif

	unsigned char *inbulk;
	int	nrinbulk;
	unsigned char *outbulk;
	int	nroutbulk;
	unsigned int seqnr;
	unsigned int session;
	ptpcontainer ptpcmd;
	long last_cmd_timestamp;

	// Linked list containing bulk properties
	struct PtpPropList *list;
	struct PtpPropList *list_tail;

	uint16_t vendor;
	uint16_t product;
	vcameratype	type;

	struct CamConfig *conf;

	// === Generic camera runtime vars ===
	int exposurebias;
	unsigned int shutterspeed;
	unsigned int fnumber;
	unsigned int focal_length;
	unsigned int target_distance_feet;

	// === Fujifilm runtime vars ===
	/// @brief Current value for PTP_PC_FUJI_ClientState
	int client_state;
	/// @brief Current value for PTP_PC_FUJI_CameraState
	int camera_state;
	/// @brief Current value for PTP_PC_FUJI_RemoteVersion
	int remote_version;
	/// @brief Current vaule for PTP_PC_FUJI_ObjectCount
	int obj_count;
	int compress_small;
	int no_compressed;
	/// @brief Internal enum for what the camera is currently doing
	uint8_t internal_state;
	/// @brief Number of images currently sent through the SEND MULTIPLE feature
	int sent_images;
	uint8_t next_cmd_kills_connection;

	// === Canon PTP/IP server related things ===
	int is_lv_ready;
}vcamera;

vcamera *vcamera_new(vcameratype);
struct CamConfig *vcam_new_config(int argc, char **argv);
int vcam_read(vcamera *cam, int ep, unsigned char *data, int bytes);
int vcam_write(vcamera *cam, int ep, const unsigned char *data, int bytes);
int vcam_readint(vcamera *cam, unsigned char *data, int bytes, int timeout);
int vcam_open(vcamera *cam, const char *port);
int vcam_get_variant_info(char *arg, struct CamConfig *o);

// Called early before connection is established
int vcam_fuji_setup(vcamera *cam);
int vcam_canon_setup(vcamera *cam);

int ptp_get_object_count();

// Temporary function to help with unimplemented data structures
int vcam_generic_send_file(char *path, vcamera *cam, ptpcontainer *ptp);

void ptp_senddata(vcamera *cam, uint16_t code, unsigned char *data, int bytes);
void ptp_response(vcamera *cam, uint16_t code, int nparams, ...);

void vcam_set_prop(vcamera *cam, int code, uint32_t value);
void vcam_set_prop_data(vcamera *cam, int code, void *data, int length);
void vcam_set_prop_avail(vcamera *cam, int code, int size, int cnt, void *data);

struct ptp_function {
	int	code;
	int	(*write)(vcamera *cam, ptpcontainer *ptp);
	int	(*write_data)(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int size);
};

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

struct ptp_property {
	int code;
	int (*getdesc)(vcamera *cam, PTPDevicePropDesc *);
	int (*getvalue)(vcamera *cam, PTPPropertyValue *);
	int (*setvalue)(vcamera *cam, PTPPropertyValue *);
};

int ptp_get_properties_length();

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

// vcam data structure API
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

// Deletes the first object from the list
void vcam_virtual_pop_object(int id);

int ptp_inject_interrupt(vcamera *cam, int when, uint16_t code, int nparams, uint32_t param1, uint32_t transid);

#pragma pack(push, 1)
struct GenericEvent {
	uint32_t size;
	uint16_t x;
	uint16_t code;
	uint32_t transaction;
	uint32_t value;
};
#pragma pack(pop)

int ptp_notify_event(vcamera *cam, uint16_t code, uint32_t value);

int ptp_pop_event(vcamera *cam, struct GenericEvent *ev);

#include "canon.h"
#include "fuji.h"

int ptp_write_unicode_string(char *dat, char *string);
int ptp_read_unicode_string(char *buffer, char *dat, int max);
int ptp_read_utf8_string(void *dat, char *string, int max);
int ptp_read_string(uint8_t *dat, char *string, int max);
int ptp_write_string(uint8_t *dat, char *string);
int ptp_write_utf8_string(void *dat, char *string);
int ptp_read_uint16_array(uint8_t *dat, uint16_t *buf, int max, int *length);
inline static int ptp_write_u8 (void *buf, uint8_t out) { ((uint8_t *)buf)[0] = out; return 1; }
inline static int ptp_write_u16(void *buf, uint16_t out) { ((uint16_t *)buf)[0] = out; return 2; }
inline static int ptp_write_u32(void *buf, uint32_t out) { ((uint32_t *)buf)[0] = out; return 4; }
inline static int ptp_read_u32 (void *buf, uint32_t *out) { *out = ((uint32_t *)buf)[0]; return 4; }
inline static int ptp_read_u16 (void *buf, uint16_t *out) { *out = ((uint16_t *)buf)[0]; return 2; }
inline static int ptp_read_u8  (void *buf, uint8_t *out) { *out = ((uint8_t *)buf)[0]; return 1; }

#include "socket.h"

#endif /* !defined(IOLIBS_VUSB_VCAMERA_H) */
