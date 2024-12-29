// Implementation of fuji x raw conv/backup restore fake object API and image processor
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vcam.h>
#include <fujiptp.h>
#include <cl_data.h>
#include "fuji.h"

unsigned int get_compressed_size(const char *filename) {
	struct stat st;
	stat(filename, &st);
	return st.st_size;
}

static int ptp_fuji_900c_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 3)) return 1;
	return 1;
}
static int ptp_fuji_900c_write_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	if (vcam_check_trans_id(cam, ptp)) return 1;

	struct PtpObjectInfo oi;
	vcam_unpack_object_info(&oi, data);

	if (!strcmp(oi.filename, "FUP_FILE.dat")) {
		vcam_log("Fuji 0x900c %04x %s", oi.obj_format, oi.filename);
	} else {
		vcam_panic("%s", __func__);
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int ptp_fuji_900d_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 0)) return 1;
	return 1;
}
static int ptp_fuji_900d_write_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	if (vcam_check_trans_id(cam, ptp)) return 1;

	// 900d can accept 0 parameters, so we have to assume the object TODO
	if (1) {
		FILE *f = fopen(fuji(cam)->rawconv_raf_path, "wb");
		assert(f != NULL);
		fwrite(data, len, 1, f);
		fclose(f);
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int add_rawconv_jpeg(struct Fuji *f) {
	struct PtpObjectInfo *oi = f->rawconv_jpeg_object_info = calloc(1, sizeof(struct PtpObjectInfo));
	f->rawconv_jpeg_path = strdup("temp/raw_conv.jpeg");
	oi->storage_id = 0x10000001;
	oi->obj_format = PTP_OF_JPEG;
	oi->protection = 0x0;
	oi->compressed_size = get_compressed_size(f->rawconv_jpeg_path);
	oi->thumb_format = PTP_OF_JPEG;
	oi->thumb_compressed_size = 0x4006;
	strcpy(oi->filename, "DSCF0000.jpg");
	strcpy(oi->keywords, "DSCF0000.jpg");
	strcpy(oi->date_created, "20241223T174315");
	return 0;
}

static int do_rawconv_conversion(vcam *cam) {
	// TODO: Invoke darktable
	return 0;
}

static int ptp_fuji_sendobjectinfo_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 2)) return 1;
	return 1;
}
static int ptp_fuji_sendobjectinfo_write_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	if (vcam_check_trans_id(cam, ptp)) return 1;

	struct PtpObjectInfo oi;
	vcam_unpack_object_info(&oi, data);

	if (oi.obj_format == 0x5000 && !strcmp(oi.filename, "")) {
		fuji(cam)->settings_file_path = "temp/backup.bin";
		ptp_response(cam, ptp->code, 0);
	} else {
		vcam_panic("Unhandled %s", __func__);
	}

	vcam_log("Fuji 0x900c %04x %s", oi.obj_format, oi.filename);

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

static int ptp_fuji_sendobject_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;
	return 1;
}
static int ptp_fuji_sendobject_write_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	if (ptp->params[0] == 0x0) {
		FILE *f = fopen(fuji(cam)->settings_file_path, "wb");
		assert(f != NULL);
		fwrite(data, len, 1, f);
		fclose(f);
	} else {
		vcam_panic("Unhandled %s", __func__);
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

static int ptp_fuji_getobject_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	if (ptp->params[0] == 0) {
		vcam_generic_send_file(fuji(cam)->settings_file_path, cam, 0, ptp);
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

static int ptp_fuji_getobjecthandles_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	struct Fuji *f = fuji(cam);

	unsigned char data[128];
	int of = 0;

	if (ptp->params[0] == 0xffffffff && ptp->params[1] == 0x0 && ptp->params[2] == 0x0) {
		if (f->rawconv_jpeg_handle) {
			of += ptp_write_u32(data + of, 0x1);
			of += ptp_write_u32(data + of, f->rawconv_jpeg_handle);
		} else {
			of += ptp_write_u32(data + of, 0x0);
		}
	}

	ptp_senddata(cam, ptp->code, data, of);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

static int ptp_fuji_getobjectinfo(vcam *cam, ptpcontainer *ptp) {
	struct Fuji *f = fuji(cam);
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;
	int handle = (int)ptp->params[0];

	uint32_t size = 123;

	int of = 0;
	uint8_t buf[128];

	struct PtpObjectInfo backup_file = {0};
	backup_file.obj_format = 0x5000;
	backup_file.compressed_size = get_compressed_size(f->settings_file_path);

	struct PtpObjectInfo *oi = NULL;
	if (handle == f->rawconv_jpeg_handle && f->rawconv_jpeg_object_info != NULL) {
		oi = f->rawconv_jpeg_object_info;
	}

	if (oi != NULL) {
		of += ptp_write_u32(buf + of, oi->storage_id);
		of += ptp_write_u16(buf + of, oi->obj_format);
		of += ptp_write_u16(buf + of, oi->protection);
		of += ptp_write_u32(buf + of, oi->compressed_size);
		of += ptp_write_u16(buf + of, oi->thumb_format);
		of += ptp_write_u32(buf + of, oi->thumb_compressed_size);
		of += ptp_write_u32(buf + of, oi->thumb_width);
		of += ptp_write_u32(buf + of, oi->thumb_height);
		of += ptp_write_u32(buf + of, oi->img_width);
		of += ptp_write_u32(buf + of, oi->img_height);
		of += ptp_write_u32(buf + of, oi->img_bit_depth);
		of += ptp_write_u32(buf + of, oi->parent_obj);
		of += ptp_write_u16(buf + of, oi->assoc_type);
		of += ptp_write_u32(buf + of, oi->assoc_desc);
		of += ptp_write_u32(buf + of, oi->sequence_num);

		// If the string is empty, don't add it to the packet
		if (oi->filename[0] != '\0')
			of += ptp_write_string(buf + of, oi->filename);
		if (oi->date_created[0] != '\0')
			of += ptp_write_string(buf + of, oi->date_created);
		if (oi->date_modified[0] != '\0')
			of += ptp_write_string(buf + of, oi->date_modified);
		if (oi->keywords[0] != '\0')
			of += ptp_write_string(buf + of, oi->keywords);
	} else {
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	ptp_senddata(cam, ptp->code, buf, of);
	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

int vcam_fuji_register_rawconv_fs(vcam *cam) {
	// Override entire filesystem API
	// TODO: getstorageids, getstorageinfo
	vcam_register_opcode(cam, PTP_OC_GetObjectHandles, ptp_fuji_getobjecthandles_write, NULL);
	vcam_register_opcode(cam, PTP_OC_GetObjectInfo, ptp_fuji_getobjectinfo, NULL);
	vcam_register_opcode(cam, PTP_OC_GetObject, ptp_fuji_getobject_write, NULL);
	vcam_register_opcode(cam, PTP_OC_SendObjectInfo, ptp_fuji_sendobjectinfo_write, ptp_fuji_sendobjectinfo_write_data);
	vcam_register_opcode(cam, PTP_OC_SendObject, ptp_fuji_sendobject_write, ptp_fuji_sendobject_write_data);

	// These have been around since 2011
	vcam_register_opcode(cam, 0x900c, ptp_fuji_900c_write, ptp_fuji_900c_write_data);
	vcam_register_opcode(cam, 0x900d, ptp_fuji_900d_write, ptp_fuji_900d_write_data);
}
