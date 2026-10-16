// Nikon PTP emulator definitions
// Copyright Daniel C - GNU Lesser General Public License v2.1
#ifndef VCAM_NIKON_H
#define VCAM_NIKON_H

#include <vcam.h>

/// @brief An in-memory capture object pending download from SDRAM.
/// SDRAM captures use the canonical handle 0xffff0001 and are not part of
/// the regular object store (ParentObject == 0, StorageID == 0).
struct NikonSDRAMObject {
	int used;
	uint32_t handle;
	uint32_t ofc;
	uint32_t size;
	char name[32];
	unsigned char *data;
	/// @brief malloc base of data (data may point past a skipped header)
	unsigned char *raw;
};

struct Nikon {
	const struct NikonModelProfile *profile;

	/// @brief 0 = user mode, 1 = PC remote control (ChangeCameraMode)
	int controlmode;

	/// @brief 0 = off, 1 = live view active (LiveViewStatus 0xD1A2)
	uint8_t liveview_status;

	/// @brief Next DSC_%04d file number for card captures
	uint32_t capture_count;

	/// @brief Object captured to SDRAM, pending download (max 1, like gphoto2
	/// expects: ObjectAddedInSDRAM arrives once per capture)
	struct NikonSDRAMObject sdram;
};

struct NikonModelProfile {
	const char *name;   // CLI tag
	const char *model;  // DeviceInfo model string
	const char *version;
	const char *serial;
	uint16_t product_id;
	/// @brief VendorExtensionVersion (bcd, 0x64 = 1.0)
	uint16_t extver;
	/// @brief Card storage geometry reported by GetStorageInfo
	uint64_t storage_capacity;
	uint64_t storage_free;
	uint32_t storage_images;
	/// @brief Advertise PTP_OC_NIKON_GetVendorCodes (0x9439, Z-series)
	int vendorcodes;
	/// @brief Property table registration (nikon/models/*.c)
	int (*register_model)(vcam *cam);
};

static inline struct Nikon *nikon(vcam *cam) { return (struct Nikon *)cam->priv; }

// Model profiles (nikon/models/*.c)
int nikon_register_model_d7500(vcam *cam);
int nikon_register_model_z6_2(vcam *cam);

// Property registration helpers (props.c)
void nikon_add_prop(vcam *cam, uint16_t code, uint16_t type, int rw, const char *value, const char *form);

// Opcode registration and handlers (ops.c)
void nikon_register_ops(vcam *cam, int want_vendorcodes);

/// @brief Props that stay in the base DeviceInfo list (not 0x90CA)
int nikon_is_base_deviceprop(uint16_t code);
int nikon_lookup_virtual_object(vcam *cam, uint32_t handle, unsigned char **data, uint32_t *size);
int nikon_getobjectinfo(vcam *cam, ptpcontainer *ptp);
int nikon_getobject(vcam *cam, ptpcontainer *ptp);
int nikon_getthumb(vcam *cam, ptpcontainer *ptp);
int nikon_deleteobject(vcam *cam, ptpcontainer *ptp);

// Capture helpers shared between opcodes (ops.c)
int nikon_do_capture(vcam *cam, int sdram);
void nikon_free_sdram(vcam *cam);

// Live view JPEG source (bin/eos_liveview.jpg is a plain JPEG, which is all
// the client needs - it scans the data for the SOI marker)
#define NIKON_LV_JPEG "bin/eos_liveview.jpg"
#define NIKON_SDRAM_HANDLE 0xffff0001
#define NIKON_LV_HEADER_SIZE 128

// Single fixed storage, same id the standard ops use
#define NIKON_MAIN_STORAGE_ID 0x00010001

#endif
