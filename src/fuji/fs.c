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

int prop_d185_getvalue(vcam *cam, struct PtpPropDesc *desc, int *optional_length) {
	FILE *file = fopen(PWD "/bin/fuji/xh1_d185_initial.bin", "rb");
	if (file == NULL) {
		vcam_panic("File not found");
	}

	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *buffer = malloc(file_size);
	fread(buffer, 1, file_size, file);
	desc->value = buffer;
	(*optional_length) = file_size;
	fclose(file);

	return 0;
}
int prop_d185_setvalue(vcam *cam, struct PtpPropDesc *desc, const void *data) {
	vcam_log("New value for d185");
	return 0;
}

static int add_rawconv_jpeg(struct Fuji *f) {
	struct PtpObjectInfo *oi = calloc(1, sizeof(struct PtpObjectInfo));
	oi->storage_id = 0x10000001;
	oi->obj_format = PTP_OF_JPEG;
	oi->protection = 0x0;
	oi->compressed_size = f->rawconv_raf_length;
	oi->thumb_format = PTP_OF_JPEG;
	oi->thumb_compressed_size = 0x4006;
	strcpy(oi->filename, "DSCF0000.jpg");
	strcpy(oi->date_created, "20241223T174315");

	f->rawconv_jpeg_handle = 5;
	f->rawconv_jpeg_object_info = oi;
	f->rawconv_jpeg_path = "bin/fuji/jpeg-compressed.jpg";

	return 0;
}

int prop_d183_setvalue(vcam *cam, struct PtpPropDesc *desc, const void *data) {
	vcam_log("New value for d185");
	add_rawconv_jpeg(fuji(cam));
	ptp_notify_event(cam, PTP_DPC_FUJI_FreeSDRAMImages, 0);
	return 0;
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

	// 900d can accept 0 parameters, so we have to assume the object handle (TODO)
	if (1) {
		fuji(cam)->rawconv_raf_buffer = malloc(len);
		memcpy(fuji(cam)->rawconv_raf_buffer, data, len);
		fuji(cam)->rawconv_raf_length = len;
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
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
	} else if (ptp->params[0] == fuji(cam)->rawconv_jpeg_handle) {
		assert(fuji(cam)->rawconv_jpeg_path);
		vcam_generic_send_file(fuji(cam)->rawconv_jpeg_path, cam, 0, ptp);
	} else {
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
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

	int of = 0;
	uint8_t buf[1024];

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

		of += ptp_write_string(buf + of, oi->filename);
		of += ptp_write_string(buf + of, oi->date_created);
		of += ptp_write_string(buf + of, oi->date_modified);
		of += ptp_write_string(buf + of, oi->keywords);
	} else {
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	vcam_dump(buf, of);
	ptp_senddata(cam, ptp->code, buf, of);
	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

static int ptp_fuji_deleteobject_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	int handle = (int)ptp->params[0];

	struct Fuji *f = fuji(cam);
	if (handle == f->rawconv_jpeg_handle) {
		f->rawconv_jpeg_handle = 0;
		f->rawconv_jpeg_object_info = NULL;
	} else {
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

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
	vcam_register_opcode(cam, PTP_OC_DeleteObject, ptp_fuji_deleteobject_write, NULL);

	{
		struct PtpPropDesc desc = {0};
		FILE *file = fopen(PWD "/bin/fuji/xh1_d185_initial.bin", "rb");
		if (file == NULL) {
			vcam_panic("File not found");
		}

		fseek(file, 0, SEEK_END);
		long file_size = ftell(file);
		fseek(file, 0, SEEK_SET);

		char *buffer = malloc(file_size);
		fread(buffer, 1, file_size, file);
		fclose(file);

		desc.DataType = PTP_TC_UNDEF;
		desc.value = buffer;
		desc.value_length = file_size;
		vcam_register_prop(cam, 0xd185, &desc);
	}
	struct PtpPropDesc desc = {0};
	//vcam_register_prop_handlers(cam, 0xd185, &desc, prop_d185_getvalue, prop_d185_setvalue);
	vcam_register_prop_handlers(cam, 0xd183, &desc, NULL, prop_d183_setvalue);

	// These have been around since 2011
	vcam_register_opcode(cam, 0x900c, ptp_fuji_900c_write, ptp_fuji_900c_write_data);
	vcam_register_opcode(cam, 0x900d, ptp_fuji_900d_write, ptp_fuji_900d_write_data);

	return 0;
}
