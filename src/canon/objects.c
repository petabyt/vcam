// Vendor EOS object and storage opcodes (0x9101-0x9117)
// Backed by the same FS view as the standard PTP ops, plus in-memory SDRAM
// captures pending download.
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vcam.h>
#include <canon.h>

// Same value as MAIN_STORAGE_ID in ops.c
#define EOS_MAIN_STORAGE_ID 0x00010001
// CaptureDestination (0xD11C): gphoto2 treats 4 as the SDRAM/remote target
#define EOS_CAPTUREDEST_SDRAM 0x4
#define EOS_OFC_JPEG 0x3801
#define EOS_OFC_MOV 0x300a
#define EOS_OFC_UNKNOWN 0x3800
#define EOS_OFC_ASSOCIATION 0x3001

// Fixed size of one GetObjectInfoEx folder entry, including the u32 size field
#define EOS_FE_ENTRY_SIZE 56

void canon_register_object_ops(vcam *cam);

static uint32_t eos_object_ofc(struct ptp_dirent *cur) {
	if (S_ISDIR(cur->stbuf.st_mode)) return EOS_OFC_ASSOCIATION;
	const char *name = cur->name;
	if (strstr(name, ".jpg") || strstr(name, ".JPG")) return EOS_OFC_JPEG;
	if (strstr(name, ".mov") || strstr(name, ".MOV")) return EOS_OFC_MOV;
	return EOS_OFC_UNKNOWN;
}

// SDRAM hook for the standard ops (e.g. GetPartialObject 0x101b during the
// SDRAM capture download). Data stays owned by the vendor until
// TransferComplete.
int canon_lookup_virtual_object(vcam *cam, uint32_t handle, unsigned char **data, uint32_t *size) {
	struct CanonBase *p = canon(cam);
	if (!p->sdram.used || p->sdram.handle != handle) return -1;
	*data = p->sdram.data;
	*size = p->sdram.size;
	return 0;
}

static struct ptp_dirent *find_dirent(vcam *cam, uint32_t handle) {
	struct ptp_dirent *cur = cam->first_dirent;
	while (cur) {
		if (cur->id == handle) break;
		cur = cur->next;
	}
	return cur;
}

// Serves (handle, offset, size) from the SDRAM capture or the FS view
static int eos_serve_partial(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 3)) return 1;

	uint32_t handle = ptp->params[0];
	uint32_t offset = ptp->params[1];
	uint32_t reqsize = ptp->params[2];
	if (reqsize > 512 * 1024) reqsize = 512 * 1024;

	unsigned char *vdata;
	uint32_t vsize;
	if (canon_lookup_virtual_object(cam, handle, &vdata, &vsize) == 0) {
		if (offset > vsize) offset = vsize;
		uint32_t avail = vsize - offset;
		uint32_t tocopy = reqsize < avail ? reqsize : avail;
		ptp_senddata(cam, ptp->code, (unsigned char *)vdata + offset, (int)tocopy);
		ptp_response(cam, PTP_RC_OK, 1, tocopy);
		return 1;
	}

	struct ptp_dirent *cur = find_dirent(cam, handle);
	if (!cur) {
		vcam_log_func(__func__, "invalid object id 0x%08x", handle);
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	FILE *file = fopen(cur->fsname, "rb");
	if (file == NULL) {
		vcam_log_func(__func__, "cannot open %s", cur->fsname);
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}

	uint64_t object_size = (uint64_t)cur->stbuf.st_size;
	uint32_t tocopy = reqsize;
	if (offset >= object_size) {
		tocopy = 0;
	} else {
		uint64_t remaining = object_size - offset;
		if (tocopy > remaining) tocopy = (uint32_t)remaining;
	}

	char *buffer = tocopy ? malloc(tocopy) : NULL;
	size_t got = 0;
	if (tocopy) {
		if (fseek(file, (long)offset, SEEK_SET) == 0) {
			got = fread(buffer, 1, tocopy, file);
		}
	}
	fclose(file);

	ptp_senddata(cam, ptp->code, (unsigned char *)buffer, (int)got);
	free(buffer);
	ptp_response(cam, PTP_RC_OK, 1, (uint32_t)got);
	return 1;
}

// Serves the full object (file or SDRAM capture)
static int eos_serve_object(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	uint32_t handle = ptp->params[0];

	unsigned char *vdata;
	uint32_t vsize;
	if (canon_lookup_virtual_object(cam, handle, &vdata, &vsize) == 0) {
		ptp_senddata(cam, ptp->code, (unsigned char *)vdata, (int)vsize);
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	struct ptp_dirent *cur = find_dirent(cam, handle);
	if (!cur) {
		vcam_log_func(__func__, "invalid object id 0x%08x", handle);
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	void *file = read_file(cur);
	if (file == NULL) {
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}

	ptp_senddata(cam, ptp->code, (unsigned char *)file, (int)cur->stbuf.st_size);
	free(file);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int eos_getstorageids(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	uint8_t data[8];
	ptp_write_u32(data + 0, 1); // number of storage ids
	ptp_write_u32(data + 4, EOS_MAIN_STORAGE_ID);

	ptp_senddata(cam, ptp->code, data, sizeof(data));
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int eos_getstorageinfo(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	if (ptp->params[0] != EOS_MAIN_STORAGE_ID && ptp->params[0] != 0xffffffff) {
		ptp_response(cam, PTP_RC_InvalidStorageId, 0);
		return 1;
	}

	// Same layout as real EOS cameras: u32 unk, u32 storage type,
	// u32 access caps, u64 max capacity, u64 free space, u32 unk
	uint8_t data[32];
	memset(data, 0, sizeof(data));
	ptp_write_u32(data + 0, 0x10);
	ptp_write_u32(data + 4, 4); // removable RAM (memory card)
	ptp_write_u32(data + 8, 0); // read-write
	ptp_write_u32(data + 12, 1111638594); // max capacity lo, matches standard ops
	ptp_write_u32(data + 16, 0);
	ptp_write_u32(data + 20, 555819297); // free space lo
	ptp_write_u32(data + 24, 0);

	ptp_senddata(cam, ptp->code, data, sizeof(data));
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// Fast directory listing: (storage, parent handle, unk) -> folder entries.
// parent 0xffffffff requests the children of the root.
int eos_getobjectinfoex(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 3)) return 1;

	uint32_t storage = ptp->params[0];
	uint32_t parent = ptp->params[1];
	if (storage != EOS_MAIN_STORAGE_ID && storage != 0xffffffff) {
		vcam_log_func(__func__, "unknown storage 0x%08x", storage);
		ptp_response(cam, PTP_RC_InvalidStorageId, 0);
		return 1;
	}

	if (parent != 0xffffffff) {
		struct ptp_dirent *pdir = find_dirent(cam, parent);
		if (!pdir || !S_ISDIR(pdir->stbuf.st_mode)) {
			vcam_log_func(__func__, "invalid parent 0x%08x", parent);
			ptp_response(cam, PTP_RC_InvalidParentObject, 0);
			return 1;
		}
	}

	int cnt = 0;
	struct ptp_dirent *cur = cam->first_dirent;
	while (cur) {
		if (cur->id && cur->parent &&
		    ((parent == 0xffffffff) ? (cur->parent->id == 0) : (cur->parent->id == parent))) {
			cnt++;
		}
		cur = cur->next;
	}

	uint8_t *data = malloc(4 + (size_t)cnt * EOS_FE_ENTRY_SIZE);
	if (data == NULL) vcam_panic("out of memory");
	int x = 0;
	x += ptp_write_u32(data + x, cnt);

	cur = cam->first_dirent;
	while (cur) {
		if (!(cur->id && cur->parent &&
		      ((parent == 0xffffffff) ? (cur->parent->id == 0) : (cur->parent->id == parent)))) {
			cur = cur->next;
			continue;
		}

		// Folder entry: u32 size, u32 handle, u32 storage, u16 ofc,
		// u16 unk, u32 unk, u8 flags, u32 object size, u32 unk x2,
		// char name[13], u32 time
		x += ptp_write_u32(data + x, EOS_FE_ENTRY_SIZE);
		x += ptp_write_u32(data + x, cur->id);
		x += ptp_write_u32(data + x, EOS_MAIN_STORAGE_ID);
		x += ptp_write_u16(data + x, (uint16_t)eos_object_ofc(cur));
		x += ptp_write_u16(data + x, 0);
		x += ptp_write_u32(data + x, 0);
		x += ptp_write_u32(data + x, 0); // flags byte + padding
		x += ptp_write_u32(data + x, (uint32_t)cur->stbuf.st_size);
		x += ptp_write_u32(data + x, 0);
		x += ptp_write_u32(data + x, 0);
		// 13 byte name buffer (client copies PTP_CANON_FilenameBufferLen)
		memset(data + x, 0, 16);
		strncpy((char *)data + x, cur->name, 12);
		x += 16;
		x += ptp_write_u32(data + x, (uint32_t)cur->stbuf.st_mtime);
		cur = cur->next;
	}

	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int eos_getpartialobject(vcam *cam, ptpcontainer *ptp) {
	return eos_serve_partial(cam, ptp);
}

int eos_getpartialobjectex(vcam *cam, ptpcontainer *ptp) {
	// (handle, offset, size, unk) - the unk param is ignored
	return eos_serve_partial(cam, ptp);
}

int eos_getobject(vcam *cam, ptpcontainer *ptp) {
	return eos_serve_object(cam, ptp);
}

int eos_getthumbex(vcam *cam, ptpcontainer *ptp) {
	// Thumbnails of the fixture images are the images themselves
	return eos_serve_object(cam, ptp);
}

int eos_transfercomplete(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct CanonBase *p = canon(cam);
	if (p->sdram.used && p->sdram.handle == ptp->params[0]) {
		vcam_log("CANON: TransferComplete for SDRAM object 0x%08x", ptp->params[0]);
		free(p->sdram.data);
		memset(&p->sdram, 0, sizeof(p->sdram));
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// EOS extended device info: {u32 events_n, events[], u32 props_n, props[], u32 unk_n}
int eos_getdeviceinfoex(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	static const uint32_t events[] = {
		PTP_EC_EOS_ObjectAddedEx,
		PTP_EC_EOS_ObjectRemoved,
		PTP_EC_EOS_RequestObjectTransfer,
		PTP_EC_EOS_PropValueChanged,
		PTP_EC_EOS_AvailListChanged,
		PTP_EC_EOS_InfoCheckComplete,
	};

	int nprops = cam->props->length;
	uint8_t *data = malloc(4 + sizeof(events) + 4 + (size_t)nprops * 4 + 4);
	if (data == NULL) vcam_panic("out of memory");
	int x = 0;
	x += ptp_write_u32(data + x, sizeof(events) / 4);
	for (unsigned i = 0; i < sizeof(events) / 4; i++)
		x += ptp_write_u32(data + x, events[i]);
	x += ptp_write_u32(data + x, (uint32_t)nprops);
	for (int i = 0; i < nprops; i++)
		x += ptp_write_u32(data + x, (uint32_t)cam->props->handlers[i].code);
	x += ptp_write_u32(data + x, 0); // unk list, empty

	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

// No-op style vendor ops that clients hard-check during capture preparation
// (e.g. SetRequestOLCInfoGroup). Accept and ignore.
int eos_noop_ok(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

void canon_register_object_ops(vcam *cam) {
	vcam_register_opcode(cam, PTP_OC_EOS_GetStorageIDs, eos_getstorageids, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_GetStorageInfo, eos_getstorageinfo, NULL);
	vcam_register_opcode(cam, 0x9108, eos_getdeviceinfoex, NULL); // GetDeviceInfoEx
	vcam_register_opcode(cam, PTP_OC_EOS_GetObjectInfoEx, eos_getobjectinfoex, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_GetObject, eos_getobject, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_GetThumbEx, eos_getthumbex, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_GetPartialObject, eos_getpartialobject, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_GetPartialObjectEx, eos_getpartialobjectex, NULL);
	vcam_register_opcode(cam, PTP_OC_EOS_TransferComplete, eos_transfercomplete, NULL);
}