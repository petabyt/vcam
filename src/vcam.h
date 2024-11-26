// Copyright (c) 2015,2016 Marcus Meissner <marcus@jet.franken.de> - GNU General Public License v2
#ifndef IOLIBS_VUSB_VCAMERA_H
#define IOLIBS_VUSB_VCAMERA_H

#undef FUZZING
#define FUZZ_PTP

#include <stdio.h>
#include <stdint.h>
#include <ptp.h>
#include <sys/stat.h>
#include <sys/types.h>

#define GP_OK 0
#define GP_ERROR -1
#define GP_ERROR_TIMEOUT -10

typedef enum {
	GP_LOG_ERROR = 0,	/**< \brief Log message is an error information. */
	GP_LOG_VERBOSE = 1,	/**< \brief Log message is an verbose debug information. */
	GP_LOG_DEBUG = 2,	/**< \brief Log message is an debug information. */
	GP_LOG_DATA = 3		/**< \brief Log message is a data hex dump. */
} GPLogLevel;

void gp_log(GPLogLevel level, const char *domain, const char *format, ...);
void vcam_log(const char *format, ...);
void gp_log_(const char *format, ...);

typedef struct ptpcontainer {
	unsigned int size;
	unsigned int type;
	unsigned int code;
	unsigned int seqnr;
	unsigned int nparams;
	unsigned int params[6];
}ptpcontainer;

// All members are guaranteed to be zero by calloc()
typedef struct vcam {
	/// @brief Priv pointer for device-specific PTP code
	void *priv;
	/// @brief Priv pointer for hardware layer (TCP/IP)
	void *hw_priv;
	uint16_t vendor;
	uint16_t product;
	char model[32];
	char version[16];
	char serial[16];
	char manufac[32];

	/// @brief NULL to use default IP
	char *custom_ip_addr;

	/// @brief Optional PID of parent process, will signal it once PTP/IP is listening for connections
	pid_t sig;

	struct ptp_interrupt *first_interrupt;
	struct ptp_dirent *first_dirent;
	uint32_t ptp_objectid;

	char *vcamera_filesystem;

	unsigned char *inbulk;
	int	nrinbulk;
	unsigned char *outbulk;
	int	nroutbulk;
	unsigned int seqnr;
	unsigned int session;
	ptpcontainer ptpcmd;
	long last_cmd_timestamp;

	struct PtpOpcodeList *opcodes;
	struct PtpPropList *props;

	// Runtime data vars
	int next_cmd_kills_connection;
	int exposurebias;
	unsigned int shutterspeed;
	unsigned int fnumber;
	unsigned int focal_length;
	unsigned int target_distance_feet;
	uint8_t battery;
}vcam;

vcam *vcamera_new(void);
int vcam_main(const char *name, int argc, char **argv);
vcam *vcam_new(const char *name);
int vcam_parse_args(vcam *cam, int argc, char **argv, int *i);
int vcam_read(vcam *cam, int ep, unsigned char *data, int bytes);
int vcam_write(vcam *cam, int ep, const unsigned char *data, int bytes);
int vcam_readint(vcam *cam, unsigned char *data, int bytes, int timeout);
int vcam_open(vcam *cam, const char *port);
int vcam_register_opcode(vcam *cam, int code, int (*write)(vcam *cam, ptpcontainer *ptp), int (*write_data)(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int size));

int get_local_ip(char buffer[64]);
int ptp_get_object_count(vcam *cam);

// Temporary function to help with unimplemented data structures
int vcam_generic_send_file(char *path, vcam *cam, ptpcontainer *ptp);

void ptp_senddata(vcam *cam, uint16_t code, unsigned char *data, int bytes);
void ptp_response(vcam *cam, uint16_t code, int nparams, ...);

int vcam_set_prop(vcam *cam, int code, uint32_t value);
int vcam_set_prop_data(vcam *cam, int code, void *data);
int vcam_set_prop_avail(vcam *cam, int code, void *list, int cnt);

struct PtpOpcodeList {
	int length;
	struct PtpOpcode {
		int code;
		int (*write)(vcam *cam, ptpcontainer *ptp);
		int (*write_data)(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int size);
	}handlers[];
};

// Standard PTP opcode implementations
int ptp_opensession_write(vcam *cam, ptpcontainer *ptp);
int ptp_closesession_write(vcam *cam, ptpcontainer *ptp);
int ptp_deviceinfo_write(vcam *cam, ptpcontainer *ptp);
int ptp_getnumobjects_write(vcam *cam, ptpcontainer *ptp);
int ptp_getobjecthandles_write(vcam *cam, ptpcontainer *ptp);
int ptp_getstorageids_write(vcam *cam, ptpcontainer *ptp);
int ptp_getstorageinfo_write(vcam *cam, ptpcontainer *ptp);
int ptp_getobjectinfo_write(vcam *cam, ptpcontainer *ptp);
int ptp_getobject_write(vcam *cam, ptpcontainer *ptp);
int ptp_getthumb_write(vcam *cam, ptpcontainer *ptp);
int ptp_deleteobject_write(vcam *cam, ptpcontainer *ptp);
int ptp_getdevicepropdesc_write(vcam *cam, ptpcontainer *ptp);
int ptp_getdevicepropvalue_write(vcam *cam, ptpcontainer *ptp);
int ptp_setdevicepropvalue_write(vcam *cam, ptpcontainer *ptp);
int ptp_setdevicepropvalue_write_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len);
int ptp_initiatecapture_write(vcam *cam, ptpcontainer *ptp);
int ptp_vusb_write(vcam *cam, ptpcontainer *ptp);
int ptp_vusb_write_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len);
int ptp_nikon_setcontrolmode_write(vcam *cam, ptpcontainer *ptp);
int ptp_getpartialobject_write(vcam *cam, ptpcontainer *ptp);

// GetPropertyValue stubs

union PtpPropValue {
	void *data;
	uint8_t u8;
	int8_t i8;
	uint16_t u16;
	int16_t i16;
	uint32_t u32;
	int32_t i32;
	uint64_t u64;
	int64_t i64;
};

struct PtpPropDesc {
	uint16_t DevicePropertyCode;
	uint16_t DataType;
	uint8_t GetSet;
	void *factory_default_value;
	void *value;

	void *avail;
	int avail_cnt;

	uint8_t FormFlag;
	void *form_min;
	void *form_max;
	void *form_step;
};

// TODO: This should be refactored to be on_getdesc/etc. There is no need to allocate an entire structure every single time
// GetDevicePropDesc is called.
typedef int ptp_prop_getdesc(vcam *cam, struct PtpPropDesc *);
typedef void *ptp_prop_getvalue(vcam *cam);
typedef int ptp_prop_setvalue(vcam *cam, const void *data);

struct PtpPropList {
	int length;
	struct PtpProp {
		int code;

		/// @note may be NULL
		ptp_prop_getdesc *getdesc;
		/// @note may be NULL
		ptp_prop_getvalue *getvalue;
		/// @note may be NULL
		ptp_prop_setvalue *setvalue;

		struct PtpPropDesc desc;
	}handlers[];
};

int vcam_register_prop_handlers(vcam *cam, int code, ptp_prop_getdesc *getdesc, ptp_prop_getvalue *getvalue, ptp_prop_setvalue *setvalue);
int vcam_register_prop(vcam *cam, int code, struct PtpPropDesc *desc);
struct PtpPropDesc *vcam_get_prop_desc(vcam *cam, int code);
void *vcam_get_prop_data(vcam *cam, int code, int *length);
int vcam_set_prop_data(vcam *cam, int code, void *data);
int vcam_set_prop(vcam *cam, int code, uint32_t data);

void ptp_register_standard_opcodes(vcam *cam);
void ptp_register_standard_props(vcam *cam);

int vcam_check_session(vcam *cam);
int vcam_check_trans_id(vcam *cam, ptpcontainer *ptp);
int vcam_check_param_count(vcam *cam, ptpcontainer *ptp, int n);

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

void vcam_dump(void *ptr, size_t len);

void ptp_free_devicepropdesc(struct PtpPropDesc *dpd);

void *read_file(struct ptp_dirent *cur);
void free_dirent(struct ptp_dirent *ent);

// Deletes the first object from the list
void vcam_virtual_pop_object(int id);

int ptp_inject_interrupt(vcam *cam, int when, uint16_t code, int nparams, uint32_t param1, uint32_t transid);

//#pragma pack(push, 1)
struct GenericEvent {
	uint32_t size;
	uint16_t x;
	uint16_t code;
	uint32_t transaction;
	uint32_t value;
};
//#pragma pack(pop)

int ptp_notify_event(vcam *cam, uint16_t code, uint32_t value);

int ptp_pop_event(vcam *cam, struct GenericEvent *ev);

void fuji_register_opcodes(vcam *cam);
int fuji_init_cam(vcam *cam, const char *name, int argc, char **argv);
int canon_init_cam(vcam *cam, const char *name, int argc, char **argv);

int fuji_wifi_main(vcam *cam);
int ptpip_generic_main(vcam *cam);

#include "data.h"
#include "socket.h"

#endif /* !defined(IOLIBS_VUSB_VCAMERA_H) */
