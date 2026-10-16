// Sony PTP emulator definitions
// Copyright Daniel C - GNU Lesser General Public License v2.1
#ifndef VCAM_SONY_H
#define VCAM_SONY_H

#include <vcam.h>

// Sony protocol (mode 3 = "For 2020 Models or Later", direct settable values,
// 32-bit capture) - see libgphoto2 camlibs/ptp2 README.Sony and ptp.c
#define SONY_PROTOCOL_3_00 0x12c

// Sony SDIO vendor opcodes (src/ptp.h has no Sony defines)
#define PTP_OC_SONY_SDIO_Connect			0x9201
#define PTP_OC_SONY_SDIO_GetExtDeviceInfo		0x9202
#define PTP_OC_SONY_SDIO_SetExtDevicePropValue		0x9205
#define PTP_OC_SONY_SDIO_ControlDevice			0x9207
#define PTP_OC_SONY_SDIO_GetAllExtDevicePropInfo	0x9209

// Sony vendor opcodes not present in src/ptp.h
#define PTP_OC_SONY_GetDevicePropdesc		0x9203
#define PTP_OC_SONY_GetDevicePropertyValue	0x9204

// Live view / capture JPEG source (bin/eos_liveview.jpg is a plain JPEG
// after the SOI skip in sony_load_jpeg - the only fixture the client needs)
#define SONY_LV_JPEG "bin/eos_liveview.jpg"

// Virtual SDRAM object handles (only valid while 0xD215 ObjectInMemory >= 0x8000)
#define SONY_OBJECT_HANDLE 0xffffc001
#define SONY_LIVEVIEW_HANDLE 0xffffc002

// Live view payload: u32 LE offset to the JPEG SOI, then the JPEG
#define SONY_LV_HEADER_SIZE 4

// Device properties this emulator drives dynamically
#define PTP_DPC_SONY_FocusFound			0xD213
#define PTP_DPC_SONY_ObjectInMemory		0xD215
#define PTP_DPC_SONY_PriorityMode		0xD25A
#define PTP_DPC_SONY_ShutterHalfRelease		0xD2C1
#define PTP_DPC_SONY_ShutterRelease		0xD2C2

// Object format codes (src/ptp.h has no Sony defines)
#define PTP_OFC_SONY_RAW 0xb101
#define PTP_OFC_SONY_HEIF 0xb110

/// @brief In-memory capture object pending download from SDRAM
struct SonySDRAMObject {
	int used;
	uint32_t handle;
	uint32_t ofc;
	uint32_t size;
	char name[32];
	unsigned char *data;
	/// @brief malloc base of data (data may point past a skipped header)
	unsigned char *raw;
};

struct Sony {
	const struct SonyModelProfile *profile;

	/// @brief 0xD213 FocusFound current value (1 = searching, 2 = found)
	uint8_t focus_found;

	/// @brief 0xD215 ObjectInMemory current value (0x8001 = one image ready)
	uint16_t object_in_memory;

	/// @brief Shutter button state (half/full pressed via 0x9207 controls)
	int half_pressed;
	int full_pressed;

	/// @brief Next DSC_%04d file number for SDRAM captures
	uint32_t capture_count;

	/// @brief Captured image pending download at 0xffffc001
	struct SonySDRAMObject sdram;

	/// @brief Live view JPEG pending at 0xffffc002 (always available)
	unsigned char *lv_data;
	uint32_t lv_size;
	unsigned char *lv_raw;
};

struct SonyModelProfile {
	const char *name;   // CLI tag
	const char *model;  // DeviceInfo model string
	const char *version;
	const char *serial;
	uint16_t product_id;
	/// @brief Base DeviceInfo DevicePropertiesSupported list (standard codes
	/// only - vendor props arrive via 0x9202, controls via its second array)
	const uint16_t *deviceprops;
	int n_deviceprops;
	/// @brief Control codes returned in the 0x9202 "controls" array (listed
	/// in DeviceProps by gphoto2, but never answered by 0x9209 - gphoto2
	/// synthesizes their descriptors client-side)
	const uint16_t *controls;
	int n_controls;
	/// @brief Property table registration (sony/models/*.c)
	int (*register_model)(vcam *cam);
};

static inline struct Sony *sony(vcam *cam) { return (struct Sony *)cam->priv; }

// Model profiles (sony/models/*.c)
int sony_register_model_a7m4(vcam *cam);
int sony_register_model_a7r5(vcam *cam);

// Property registration helpers (props.c)
/// @brief rw uses the Sony GetSet convention: 1 = GetSet, 0 = Get-only
void sony_add_prop(vcam *cam, uint16_t code, uint16_t type, int rw, const char *value, const char *form);
/// @brief Register 0xD20D ShutterSpeed as a canonical stop-list enumeration
void sony_add_shutter_prop(vcam *cam, int rw, uint32_t value);

// Opcode registration and handlers (ops.c)
void sony_register_ops(vcam *cam);
int sony_lookup_virtual_object(vcam *cam, uint32_t handle, unsigned char **data, uint32_t *size);
int sony_getobjectinfo(vcam *cam, ptpcontainer *ptp);
int sony_getobject(vcam *cam, ptpcontainer *ptp);

// Capture helpers shared between opcodes (ops.c)
int sony_load_jpeg(vcam *cam, unsigned char **data, uint32_t *size, unsigned char **raw);
void sony_free_sdram(vcam *cam);
void sony_notify(vcam *cam, int when, uint16_t code, uint32_t param);

#endif
