// Canon EOS shared definitions
// Copyright Daniel C - GNU Lesser General Public License v2.1
#ifndef VCAM_CANON_H
#define VCAM_CANON_H

/// @brief An in-memory capture object pending download from SDRAM
struct CanonSDRAMObject {
	int used;
	uint32_t handle;
	uint32_t ofc;
	uint32_t size;
	char name[32];
	unsigned char *data;
};

struct CanonBase {
	int first_events;
	int lv_ready;
	int calls_to_liveview;
	/// @brief NOTIFY alive messages carry the Canon-style Server header
	int ssdp_modern;

	/// @brief Current value of the CaptureDestination property (0xD11C).
	/// 4 == SDRAM/remote, other values write to the card
	uint32_t capture_destination;

	/// @brief Object captured during remote release, pending download
	struct CanonSDRAMObject sdram;
};

static inline struct CanonBase *canon(vcam *cam) { return (struct CanonBase *)cam->priv; }

// Start the SSDP/UPnP discovery responder on a background thread (TCP backend only)
int canon_ssdp_start(vcam *cam);

// Register the minimal EOS property set for the remote capture flow (props.c)
int canon_register_capture_props(vcam *cam);

// Typed property registration used by generated model profiles (props.c)
int canon_add_prop_u32(vcam *cam, int code, uint32_t value);
int canon_add_prop_u16(vcam *cam, int code, uint16_t value);
int canon_add_prop_u8(vcam *cam, int code, uint8_t value);
int canon_add_prop_i32(vcam *cam, int code, int32_t value);
int canon_ensure_capture_props(vcam *cam);

// Register the given EOS vendor opcodes, mapping known codes to real
// handlers and everything else to the generic stub (canon.c)
int canon_register_eos_ops(vcam *cam, const uint16_t *ops, int nops);

// Vendor object ops (objects.c)
void canon_register_object_ops(vcam *cam);
int canon_lookup_virtual_object(vcam *cam, uint32_t handle, unsigned char **data, uint32_t *size);
int eos_getstorageids(vcam *cam, ptpcontainer *ptp);
int eos_getstorageinfo(vcam *cam, ptpcontainer *ptp);
int eos_getdeviceinfoex(vcam *cam, ptpcontainer *ptp);
int eos_getobjectinfoex(vcam *cam, ptpcontainer *ptp);
int eos_getobject(vcam *cam, ptpcontainer *ptp);
int eos_getthumbex(vcam *cam, ptpcontainer *ptp);
int eos_getpartialobject(vcam *cam, ptpcontainer *ptp);
int eos_getpartialobjectex(vcam *cam, ptpcontainer *ptp);
int eos_transfercomplete(vcam *cam, ptpcontainer *ptp);
int eos_noop_ok(vcam *cam, ptpcontainer *ptp);

#endif
