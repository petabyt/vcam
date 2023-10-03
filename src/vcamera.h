/* vcamera.h
 *
 * Copyright (c) 2015,2016 Marcus Meissner <marcus@jet.franken.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifndef IOLIBS_VUSB_VCAMERA_H
#define IOLIBS_VUSB_VCAMERA_H

#undef FUZZING
#define FUZZ_PTP

#include <stdio.h>
#include <stdint.h>

#ifndef VUSB_BIN_DIR
#define VUSB_BIN_DIR "bin/"
#endif

void vcam_log(const char *format, ...);
void gp_log_(const char *format, ...);

int ptp_get_object_count();

int vcam_vendor_setup();

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
#endif

// It was broken (?)
#define CHECK_SEQUENCE_NUMBER() \
	;                       \
	;                       \
	;

#define CHECK_SESSION()                                                    \
	if (!cam->session) {                                               \
		gp_log(GP_LOG_ERROR, __FUNCTION__, "session is not open"); \
		ptp_response(cam, PTP_RC_SessionNotOpen, 0);               \
		return 1;                                                  \
	}

typedef struct ptpcontainer {
	unsigned int size;
	unsigned int type;
	unsigned int code;
	unsigned int seqnr;
	unsigned int nparams;
	unsigned int params[6];
	unsigned int has_data_phase;
} ptpcontainer;

typedef enum vcameratype {
	GENERIC_PTP,
	NIKON_D750,
	CANON_1300D,
	FUJI_X_A2,
} vcameratype;

typedef struct vcamera {
	int (*init)(struct vcamera*);
	int (*exit)(struct vcamera*);
	int (*open)(struct vcamera*, const char *port);
	int (*close)(struct vcamera*);

	int (*read)(struct vcamera*,  int ep, unsigned char *data, int bytes);
	int (*readint)(struct vcamera*,  unsigned char *data, int bytes, int timeout);
	int (*write)(struct vcamera*, int ep, const unsigned char *data, int bytes);

	int is_ptp_ip; // If is in TCP/IP mode, packet conversion is done in usb2ip.c

	unsigned short	vendor, product;	/* for generic fuzzing */

	vcameratype	type;
	unsigned char	*inbulk;
	int		nrinbulk;
	unsigned char	*outbulk;
	int		nroutbulk;

	unsigned int	seqnr;

	unsigned int	session;
	ptpcontainer	ptpcmd;

	int		exposurebias;
	unsigned int	shutterspeed;
	unsigned int	fnumber;

#ifdef FUZZING
	int		fuzzmode;
#define FUZZMODE_PROTOCOL	0
#define FUZZMODE_NORMAL		1
	FILE*		fuzzf;
	unsigned int	fuzzpending;
#endif /* FUZZING */
} vcamera;

vcamera *vcamera_new(vcameratype);

struct ptp_function {
	int	code;
	int	(*write)(vcamera *cam, ptpcontainer *ptp);
	int	(*write_data)(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int size);
};

void ptp_senddata(vcamera *cam, uint16_t code, unsigned char *data, int bytes);
void ptp_response(vcamera *cam, uint16_t code, int nparams, ...);

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

#endif /* !defined(IOLIBS_VUSB_VCAMERA_H) */
