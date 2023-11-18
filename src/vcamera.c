// Copyright (c) 2015,2016 Marcus Meissner <marcus@jet.franken.de> - GNU General Public License v2
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>

#include "vcam.h"
#include "cams.h"
#include "ops.h"

// Event handler
struct ptp_interrupt *first_interrupt;

// Filesystem index
struct ptp_dirent *first_dirent = NULL;
uint32_t ptp_objectid = 0;

void vcam_dump(void *ptr, size_t len) {
	FILE *f = fopen("DUMP", "wb");
	fwrite(ptr, len, 1, f);
	fclose(f);
}

int vcam_generic_send_file(char *path, vcamera *cam, ptpcontainer *ptp) {
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		vcam_log("vcam_generic_send_file: File %s not found\n", path);
		exit(-1);
	}

	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *buffer = malloc(file_size);
	fread(buffer, 1, file_size, file);

	ptp_senddata(cam, ptp->code, (unsigned char *)buffer, file_size);
	free(buffer);
	vcam_log("Generic sending %d\n", file_size);

	fclose(file);

	return 0;
}

uint32_t get_32bit_le(unsigned char *data) {
	return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

uint16_t get_16bit_le(unsigned char *data) {
	return data[0] | (data[1] << 8);
}

uint8_t get_8bit_le(unsigned char *data) {
	return data[0];
}

int8_t get_i8bit_le(unsigned char *data) {
	return data[0];
}

int put_64bit_le(unsigned char *data, uint64_t x) {
	data[0] = x & 0xff;
	data[1] = (x >> 8) & 0xff;
	data[2] = (x >> 16) & 0xff;
	data[3] = (x >> 24) & 0xff;
	data[4] = (x >> 32) & 0xff;
	data[5] = (x >> 40) & 0xff;
	data[6] = (x >> 48) & 0xff;
	data[7] = (x >> 56) & 0xff;
	return 8;
}

int put_32bit_le(unsigned char *data, uint32_t x) {
	data[0] = x & 0xff;
	data[1] = (x >> 8) & 0xff;
	data[2] = (x >> 16) & 0xff;
	data[3] = (x >> 24) & 0xff;
	return 4;
}

int put_16bit_le(unsigned char *data, uint16_t x) {
	data[0] = x & 0xff;
	data[1] = (x >> 8) & 0xff;
	return 2;
}

int put_8bit_le(unsigned char *data, uint8_t x) {
	data[0] = x;
	return 1;
}

int put_string(unsigned char *data, char *str) {
	int i;

	if (!str) { /* empty string, just has length 0 */
		data[0] = 0;
		return 1;
	}
	if (strlen(str) + 1 > 255)
		gp_log(GP_LOG_ERROR, "put_string", "string length is longer than 255 characters");

	data[0] = strlen(str) + 1;
	for (i = 0; i < data[0]; i++)
		put_16bit_le(data + 1 + 2 * i, str[i]);

	return 1 + (strlen(str) + 1) * 2;
}

char *get_string(unsigned char *data) {
	int i, len;
	char *x;

	len = data[0];
	x = malloc(len + 1);
	x[len] = 0;

	for (i = 0; i < len; i++)
		x[i] = get_16bit_le(data + 1 + 2 * i);

	return x;
}

int put_16bit_le_array(unsigned char *data, uint16_t *arr, int cnt) {
	int x = 0, i;

	x += put_32bit_le(data, cnt);
	for (i = 0; i < cnt; i++)
		x += put_16bit_le(data + x, arr[i]);
	return x;
}

int put_32bit_le_array(unsigned char *data, uint32_t *arr, int cnt) {
	int x = 0, i;

	x += put_32bit_le(data, cnt);
	for (i = 0; i < cnt; i++)
		x += put_32bit_le(data + x, arr[i]);
	return x;
}

void ptp_senddata(vcamera *cam, uint16_t code, unsigned char *data, int bytes) {
	unsigned char *offset;
	int size = bytes + 12;

	if (!cam->inbulk) {
		cam->inbulk = malloc(size);
	} else {
		cam->inbulk = realloc(cam->inbulk, cam->nrinbulk + size);
	}
	offset = cam->inbulk + cam->nrinbulk;
	cam->nrinbulk += size;

	put_32bit_le(offset, size);
	put_16bit_le(offset + 4, 0x2);
	put_16bit_le(offset + 6, code);
	put_32bit_le(offset + 8, cam->seqnr);
	memcpy(offset + 12, data, bytes);
}

void ptp_response(vcamera *cam, uint16_t code, int nparams, ...) {
	unsigned char *offset;
	int i, x = 0;
	va_list args;

	if (!cam->inbulk) {
		cam->inbulk = malloc(12 + nparams * 4);
	} else {
		cam->inbulk = realloc(cam->inbulk, cam->nrinbulk + 12 + nparams * 4);
	}
	offset = cam->inbulk + cam->nrinbulk;
	cam->nrinbulk += 12 + nparams * 4;
	x += put_32bit_le(offset + x, 12 + nparams * 4);
	x += put_16bit_le(offset + x, 0x3);
	x += put_16bit_le(offset + x, code);
	x += put_32bit_le(offset + x, cam->seqnr);

	va_start(args, nparams);
	for (i = 0; i < nparams; i++)
		x += put_32bit_le(offset + x, va_arg(args, uint32_t));
	va_end(args);

	cam->seqnr++;
}

// We need these (modified) helpers from ptp.c.
// Perhaps vcamera.c should be moved to camlibs/ptp2 for easier sharing
// in the future.
void ptp_free_devicepropvalue(uint16_t dt, PTPPropertyValue *dpd) {
	if (dt == /* PTP_DTC_STR */ 0xFFFF) {
		free(dpd->str);
	} else if (dt & /* PTP_DTC_ARRAY_MASK */ 0x4000) {
		free(dpd->a.v);
	}
}

void ptp_free_devicepropdesc(PTPDevicePropDesc *dpd) {
	uint16_t i;

	ptp_free_devicepropvalue(dpd->DataType, &dpd->FactoryDefaultValue);
	ptp_free_devicepropvalue(dpd->DataType, &dpd->CurrentValue);
	switch (dpd->FormFlag) {
	case /* PTP_DPFF_Range */ 0x01:
		ptp_free_devicepropvalue(dpd->DataType, &dpd->FORM.Range.MinimumValue);
		ptp_free_devicepropvalue(dpd->DataType, &dpd->FORM.Range.MaximumValue);
		ptp_free_devicepropvalue(dpd->DataType, &dpd->FORM.Range.StepSize);
		break;
	case /* PTP_DPFF_Enumeration */ 0x02:
		if (dpd->FORM.Enum.SupportedValue) {
			for (i = 0; i < dpd->FORM.Enum.NumberOfValues; i++)
				ptp_free_devicepropvalue(dpd->DataType, dpd->FORM.Enum.SupportedValue + i);
			free(dpd->FORM.Enum.SupportedValue);
		}
	}
}

void *read_file(struct ptp_dirent *cur) {
	FILE *file = fopen(cur->fsname, "rb");
	if (!file) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "could not open %s", cur->fsname);
		return NULL;
	}
	void *data = malloc(cur->stbuf.st_size);
	if (!data) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "could not allocate data for %s", cur->fsname);
		return NULL;
	}
	if (!fread(data, cur->stbuf.st_size, 1, file)) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "could not read data of %s", cur->fsname);
		free(data);
		data = NULL;
	}
	fclose(file);
	return data;
}

void read_directories(const char *path, struct ptp_dirent *parent) {
	struct ptp_dirent *cur;
	gp_system_dir dir;
	gp_system_dirent de;

	dir = gp_system_opendir(path);
	if (!dir)
		return;
	while ((de = gp_system_readdir(dir))) {
		if (!strcmp(gp_system_filename(de), "."))
			continue;
		if (!strcmp(gp_system_filename(de), ".."))
			continue;

		cur = malloc(sizeof(struct ptp_dirent));
		if (!cur)
			break;
		cur->name = strdup(gp_system_filename(de));
		cur->fsname = malloc(strlen(path) + 1 + strlen(gp_system_filename(de)) + 1);
		strcpy(cur->fsname, path);
		strcat(cur->fsname, "/");
		strcat(cur->fsname, gp_system_filename(de));
		//gp_log_("Found filename: %s\n", cur->fsname);
		cur->id = ptp_objectid++;
		cur->next = first_dirent;
		cur->parent = parent;
		first_dirent = cur;
		if (-1 == stat(cur->fsname, &cur->stbuf))
			continue;
		if (S_ISDIR(cur->stbuf.st_mode))
			read_directories(cur->fsname, cur); /* recurse! */
	}
	gp_system_closedir(dir);
}

void free_dirent(struct ptp_dirent *ent) {
	free(ent->name);
	free(ent->fsname);
	free(ent);
}

void read_tree(const char *path) {
	struct ptp_dirent *root = NULL, *dir, *dcim = NULL;

	if (first_dirent)
		return;

	first_dirent = malloc(sizeof(struct ptp_dirent));
	first_dirent->name = strdup("");
	first_dirent->fsname = strdup(path);
	first_dirent->id = ptp_objectid++;
	first_dirent->next = NULL;
	stat(first_dirent->fsname, &first_dirent->stbuf); /* assuming it works */
	root = first_dirent;
	read_directories(path, first_dirent);

	/* See if we have a DCIM directory, if not, create one. */
	dir = first_dirent;
	while (dir) {
		if (!strcmp(dir->name, "DCIM") && dir->parent && !dir->parent->id)
			dcim = dir;
		dir = dir->next;
	}
	if (!dcim) {
		dcim = malloc(sizeof(struct ptp_dirent));
		dcim->name = strdup("DCIM");
		dcim->fsname = strdup(path);
		dcim->id = ptp_objectid++;
		dcim->next = first_dirent;
		dcim->parent = root;
		stat(dcim->fsname, &dcim->stbuf); /* assuming it works */
		first_dirent = dcim;
	}
}

/**************************  Properties *****************************************************/
int ptp_battery_getdesc(vcamera *cam, PTPDevicePropDesc *desc) {
	desc->DevicePropertyCode = 0x5001;
	desc->DataType = 2; /* uint8 */
	desc->GetSet = 0;   /* Get only */
	desc->FactoryDefaultValue.u8 = 50;
	desc->CurrentValue.u8 = 50;
	desc->FormFlag = 0x01; /* range */
	desc->FORM.Range.MinimumValue.u8 = 0;
	desc->FORM.Range.MaximumValue.u8 = 100;
	desc->FORM.Range.StepSize.u8 = 1;
	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x5001, 0xffffffff);
	return 1;
}

int ptp_battery_getvalue(vcamera *cam, PTPPropertyValue *val) {
	val->u8 = 50;
	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x5001, 0xffffffff);
	return 1;
}

int ptp_imagesize_getdesc(vcamera *cam, PTPDevicePropDesc *desc) {
	desc->DevicePropertyCode = 0x5003;
	desc->DataType = 0xffff; /* STR */
	desc->GetSet = 0;	 /* Get only */
	desc->FactoryDefaultValue.str = strdup("640x480");
	desc->CurrentValue.str = strdup("640x480");
	desc->FormFlag = 0x02; /* enum */
	desc->FORM.Enum.NumberOfValues = 3;
	desc->FORM.Enum.SupportedValue = malloc(3 * sizeof(desc->FORM.Enum.SupportedValue[0]));
	desc->FORM.Enum.SupportedValue[0].str = strdup("640x480");
	desc->FORM.Enum.SupportedValue[1].str = strdup("1024x768");
	desc->FORM.Enum.SupportedValue[2].str = strdup("2048x1536");

	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x5003, 0xffffffff);
	return 1;
}

int ptp_imagesize_getvalue(vcamera *cam, PTPPropertyValue *val) {
	val->str = strdup("640x480");
	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x5003, 0xffffffff);
	return 1;
}

int ptp_shutterspeed_getdesc(vcamera *cam, PTPDevicePropDesc *desc) {
	desc->DevicePropertyCode = 0x500D;
	desc->DataType = 0x0006; /* UINT32 */
	desc->GetSet = 1;	 /* Get/Set */
	if (!cam->shutterspeed)
		cam->shutterspeed = 100; /* 1/100 * 10000 */
	desc->FactoryDefaultValue.u32 = cam->shutterspeed;
	desc->CurrentValue.u32 = cam->shutterspeed;
	desc->FormFlag = 0x02; /* enum */
	desc->FORM.Enum.NumberOfValues = 9;
	desc->FORM.Enum.SupportedValue = malloc(desc->FORM.Enum.NumberOfValues * sizeof(desc->FORM.Enum.SupportedValue[0]));
	desc->FORM.Enum.SupportedValue[0].u32 = 10000;
	desc->FORM.Enum.SupportedValue[1].u32 = 1000;
	desc->FORM.Enum.SupportedValue[2].u32 = 500;
	desc->FORM.Enum.SupportedValue[3].u32 = 200;
	desc->FORM.Enum.SupportedValue[4].u32 = 100;
	desc->FORM.Enum.SupportedValue[5].u32 = 50;
	desc->FORM.Enum.SupportedValue[6].u32 = 25;
	desc->FORM.Enum.SupportedValue[7].u32 = 12;
	desc->FORM.Enum.SupportedValue[8].u32 = 1;

	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x500D, 0xffffffff);
	return 1;
}

int ptp_shutterspeed_getvalue(vcamera *cam, PTPPropertyValue *val) {
	val->u32 = cam->shutterspeed;
	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x500d, 0xffffffff);
	return 1;
}

int ptp_shutterspeed_setvalue(vcamera *cam, PTPPropertyValue *val) {
	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x500d, 0xffffffff);
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %d as value", val->u32);
	cam->shutterspeed = val->u32;
	return 1;
}

int ptp_fnumber_getdesc(vcamera *cam, PTPDevicePropDesc *desc) {
	desc->DevicePropertyCode = 0x5007;
	desc->DataType = 0x0004; /* UINT16 */
	desc->GetSet = 1;	 /* Get/Set */
	if (!cam->fnumber)
		cam->fnumber = 280; /* 2.8 * 100 */
	desc->FactoryDefaultValue.u16 = cam->fnumber;
	desc->CurrentValue.u16 = cam->fnumber;
	desc->FormFlag = 0x02; /* enum */
	desc->FORM.Enum.NumberOfValues = 18;
	desc->FORM.Enum.SupportedValue = malloc(desc->FORM.Enum.NumberOfValues * sizeof(desc->FORM.Enum.SupportedValue[0]));
	desc->FORM.Enum.SupportedValue[0].u16 = 280;
	desc->FORM.Enum.SupportedValue[1].u16 = 350;
	desc->FORM.Enum.SupportedValue[2].u16 = 400;
	desc->FORM.Enum.SupportedValue[3].u16 = 450;
	desc->FORM.Enum.SupportedValue[4].u16 = 500;
	desc->FORM.Enum.SupportedValue[5].u16 = 560;
	desc->FORM.Enum.SupportedValue[6].u16 = 630;
	desc->FORM.Enum.SupportedValue[7].u16 = 710;
	desc->FORM.Enum.SupportedValue[8].u16 = 800;
	desc->FORM.Enum.SupportedValue[9].u16 = 900;
	desc->FORM.Enum.SupportedValue[10].u16 = 1000;
	desc->FORM.Enum.SupportedValue[11].u16 = 1100;
	desc->FORM.Enum.SupportedValue[12].u16 = 1300;
	desc->FORM.Enum.SupportedValue[13].u16 = 1400;
	desc->FORM.Enum.SupportedValue[14].u16 = 1600;
	desc->FORM.Enum.SupportedValue[15].u16 = 1800;
	desc->FORM.Enum.SupportedValue[16].u16 = 2000;
	desc->FORM.Enum.SupportedValue[17].u16 = 2200;

	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x5007, 0xffffffff);
	return 1;
}

int ptp_fnumber_getvalue(vcamera *cam, PTPPropertyValue *val) {
	val->u16 = cam->fnumber;
	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x5007, 0xffffffff);
	return 1;
}

int ptp_fnumber_setvalue(vcamera *cam, PTPPropertyValue *val) {
	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x5007, 0xffffffff);
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %d as value", val->u16);
	cam->fnumber = val->u16;
	return 1;
}

int ptp_exposurebias_getdesc(vcamera *cam, PTPDevicePropDesc *desc) {
	desc->DevicePropertyCode = 0x5010;
	desc->DataType = 0x0003; /* INT16 */
	desc->GetSet = 1;	 /* Get/Set */
	if (!cam->exposurebias)
		cam->exposurebias = 0; /* 0.0 */
	desc->FactoryDefaultValue.i16 = cam->exposurebias;
	desc->CurrentValue.i16 = cam->exposurebias;
	desc->FormFlag = 0x02; /* enum */
	desc->FORM.Enum.NumberOfValues = 13;
	desc->FORM.Enum.SupportedValue = malloc(desc->FORM.Enum.NumberOfValues * sizeof(desc->FORM.Enum.SupportedValue[0]));
	desc->FORM.Enum.SupportedValue[0].i16 = -3000;
	desc->FORM.Enum.SupportedValue[1].i16 = -2500;
	desc->FORM.Enum.SupportedValue[2].i16 = -2000;
	desc->FORM.Enum.SupportedValue[3].i16 = -1500;
	desc->FORM.Enum.SupportedValue[4].i16 = -1000;
	desc->FORM.Enum.SupportedValue[5].i16 = -500;
	desc->FORM.Enum.SupportedValue[6].i16 = 0;
	desc->FORM.Enum.SupportedValue[7].i16 = 500;
	desc->FORM.Enum.SupportedValue[8].i16 = 1000;
	desc->FORM.Enum.SupportedValue[9].i16 = 1500;
	desc->FORM.Enum.SupportedValue[10].i16 = 2000;
	desc->FORM.Enum.SupportedValue[11].i16 = 2500;
	desc->FORM.Enum.SupportedValue[12].i16 = 3000;

	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x5010, 0xffffffff);
	return 1;
}

int ptp_exposurebias_getvalue(vcamera *cam, PTPPropertyValue *val) {
	val->i16 = cam->exposurebias;
	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x5010, 0xffffffff);
	return 1;
}

int ptp_exposurebias_setvalue(vcamera *cam, PTPPropertyValue *val) {
	ptp_inject_interrupt(cam, 1000, 0x4006, 1, 0x5010, 0xffffffff);
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %d as value", val->i16);
	cam->exposurebias = val->i16;
	return 1;
}

int ptp_datetime_getdesc(vcamera *cam, PTPDevicePropDesc *desc) {
	struct tm *tm;
	time_t xtime;
	char xdate[40];

	desc->DevicePropertyCode = 0x5011;
	desc->DataType = 0xffff; /* string */
	desc->GetSet = 1;	 /* get only */
	time(&xtime);
	tm = gmtime(&xtime);
	sprintf(xdate, "%04d%02d%02dT%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	desc->FactoryDefaultValue.str = strdup(xdate);
	desc->CurrentValue.str = strdup(xdate);
	desc->FormFlag = 0; /* no form */
	/*ptp_inject_interrupt (cam, 1000, 0x4006, 1, 0x5011, 0xffffffff);*/
	return 1;
}

int ptp_datetime_getvalue(vcamera *cam, PTPPropertyValue *val) {
	struct tm *tm;
	time_t xtime;
	char xdate[40];

	time(&xtime);
	tm = gmtime(&xtime);
	sprintf(xdate, "%04d%02d%02dT%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	val->str = strdup(xdate);
	/*ptp_inject_interrupt (cam, 1000, 0x4006, 1, 0x5011, 0xffffffff);*/
	return 1;
}

int ptp_datetime_setvalue(vcamera *cam, PTPPropertyValue *val) {
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %s as value", val->str);
	return 1;
}

/********************************************************************************************/

int vcam_init(vcamera *cam) {
	return GP_OK;
}

int vcam_exit(vcamera *cam) {
	return GP_OK;
}

int vcam_open(vcamera *cam, const char *port) {
#ifdef FUZZING
	char *s = strchr(port, ':');

	if (s) {
		if (s[1] == '>') { /* record mode */
			cam->fuzzf = fopen(s + 2, "wb");
			cam->fuzzmode = FUZZMODE_PROTOCOL;
		} else {
			cam->fuzzf = fopen(s + 1, "rb");
#ifndef FUZZ_PTP
			/* first 4 byte are vendor and product USB id */
			if (cam->fuzzf)
				fseek(cam->fuzzf, 4, SEEK_SET);
#endif
			cam->fuzzpending = 0;
			cam->fuzzmode = FUZZMODE_NORMAL;
		}
	}
#endif

	if (cam->type == CAM_FUJI_WIFI) {
		vcam_fuji_setup(cam);
	} else if (cam->type == CAM_CANON) {
		vcam_canon_setup(cam);
	}
	// TODO: setup generic props like shutterspeed

	return GP_OK;
}

int vcam_close(vcamera *cam) {
#ifdef FUZZING
	if (cam->fuzzf) {
		fclose(cam->fuzzf);
		cam->fuzzf = NULL;
		cam->fuzzpending = 0;
	}
#endif
	free(cam->inbulk);
	free(cam->outbulk);
	return GP_OK;
}

void vcam_process_output(vcamera *cam) {
	ptpcontainer ptp;
	int i, j;

	if (cam->nroutbulk < 4)
		return; /* wait for more data */

	ptp.size = get_32bit_le(cam->outbulk);
	if (ptp.size > cam->nroutbulk)
		return; /* wait for more data */

	if (ptp.size < 12) { /* No ptp command can be less than 12 bytes */
		/* not clear if normal cameras react like this */
		gp_log(GP_LOG_ERROR, __FUNCTION__, "input size was %d, minimum is 12", ptp.size);

		// Does this work on PTP/IP?
		ptp_response(cam, PTP_RC_GeneralError, 0);
		memmove(cam->outbulk, cam->outbulk + ptp.size, cam->nroutbulk - ptp.size);
		cam->nroutbulk -= ptp.size;
		return;
	}

	int has_data_phase = 0;

	/* ptp:  4 byte size, 2 byte opcode, 2 byte type, 4 byte serial number */
	ptp.type = get_16bit_le(cam->outbulk + 4);
	ptp.code = get_16bit_le(cam->outbulk + 6);
	ptp.seqnr = get_32bit_le(cam->outbulk + 8);

	/* We want either CMD or DATA phase. */
	if ((ptp.type != PTP_PACKET_TYPE_COMMAND) && (ptp.type != PTP_PACKET_TYPE_DATA)) {
		/* not clear if normal cameras react like this */
		gp_log(GP_LOG_ERROR, __FUNCTION__, "expected CMD or DATA, but type was %d", ptp.type);
		ptp_response(cam, PTP_RC_GeneralError, 0);
		memmove(cam->outbulk, cam->outbulk + ptp.size, cam->nroutbulk - ptp.size);
		cam->nroutbulk -= ptp.size;
		return;
	}

	// Allow our special BEEF code for testing
	if ((ptp.code & 0x7000) != 0x1000 && ptp.code != 0xBEEF) {
		/* not clear if normal cameras react like this */
		gp_log(GP_LOG_ERROR, __FUNCTION__, "OPCODE 0x%04x does not start with 0x1 or 0x9", ptp.code);
		ptp_response(cam, PTP_RC_GeneralError, 0);
		memmove(cam->outbulk, cam->outbulk + ptp.size, cam->nroutbulk - ptp.size);
		cam->nroutbulk -= ptp.size;
		return;
	}

	if (ptp.type == PTP_PACKET_TYPE_COMMAND) {
		if ((ptp.size - 12) % 4) {
			/* not clear if normal cameras react like this */
			gp_log(GP_LOG_ERROR, __FUNCTION__, "SIZE-12 is not divisible by 4, but is %d", ptp.size - 12);
			ptp_response(cam, PTP_RC_GeneralError, 0);
			memmove(cam->outbulk, cam->outbulk + ptp.size, cam->nroutbulk - ptp.size);
			cam->nroutbulk -= ptp.size;
			return;
		}

		if ((ptp.size - 12) / 4 >= 6) {
			/* not clear if normal cameras react like this */
			gp_log(GP_LOG_ERROR, __FUNCTION__, "(SIZE-12)/4 is %d, exceeds maximum arguments", (ptp.size - 12) / 4);
			ptp_response(cam, PTP_RC_GeneralError, 0);
			memmove(cam->outbulk, cam->outbulk + ptp.size, cam->nroutbulk - ptp.size);
			cam->nroutbulk -= ptp.size;
			return;
		}

		ptp.nparams = (ptp.size - 12) / 4;
		for (i = 0; i < ptp.nparams; i++) {
			ptp.params[i] = get_32bit_le(cam->outbulk + 12 + i * 4);
		}

		gp_log_("Processing call for opcode 0x%X (%d params)\n", ptp.code, ptp.nparams);
	}

	// We have read the first packet, discard it
	cam->nroutbulk -= ptp.size;

	/* call the opcode handler */
	for (j = 0; j < sizeof(ptp_functions) / sizeof(ptp_functions[0]); j++) {
		struct ptp_function *funcs = ptp_functions[j].functions;
		for (i = 0; funcs[i].code != 0; i++) {
			if (funcs[i].code == ptp.code) {
				if (ptp.type == 1) {
					funcs[i].write(cam, &ptp);
					memcpy(&cam->ptpcmd, &ptp, sizeof(ptp));
				} else {
					if (funcs[i].write_data == NULL) {
						gp_log(GP_LOG_ERROR, __FUNCTION__, "opcode 0x%04x received with dataphase, but no dataphase expected", ptp.code);
						ptp_response(cam, PTP_RC_GeneralError, 0);
					} else {
						funcs[i].write_data(cam, &cam->ptpcmd, cam->outbulk + 12, ptp.size - 12);
					}
				}
				return;
			}
		}
	}

	gp_log(GP_LOG_ERROR, __FUNCTION__, "received an unsupported opcode 0x%04x", ptp.code);
	ptp_response(cam, PTP_RC_OperationNotSupported, 0);
}

int vcam_read(vcamera *cam, int ep, unsigned char *data, int bytes) {
	unsigned int toread = bytes;

#ifdef FUZZING
	/* here we are not using the virtual ptp camera, but just read from a file, or write to it. */
	if (cam->fuzzf) {
		unsigned int hasread;

		memset(data, 0, toread);
		if (cam->fuzzmode == FUZZMODE_PROTOCOL) {
			fwrite(cam->inbulk, 1, toread, cam->fuzzf);
			/* fallthrough */
		} else {
#ifdef FUZZ_PTP
			/* for reading fuzzer data */
			if (cam->fuzzpending) {
				toread = cam->fuzzpending;
				if (toread > bytes)
					toread = bytes;
				cam->fuzzpending -= toread;
				hasread = fread(data, 1, toread, cam->fuzzf);
			} else {
				hasread = fread(data, 1, 4, cam->fuzzf);
				if (hasread != 4)
					return GP_ERROR_IO_READ;

				toread = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);

				if (toread > bytes) {
					cam->fuzzpending = toread - bytes;
					toread = bytes;
				}
				if (toread <= 4)
					return toread;

				toread -= 4;

				hasread = fread(data + 4, 1, toread, cam->fuzzf);

				hasread += 4; /* readd size */
			}
#else
			/* just return a blob of data in generic fuzzing */
			hasread = fread(data, 1, bytes, cam->fuzzf);
			if (!hasread && feof(cam->fuzzf))
				return GP_ERROR_IO_READ;
#endif
			toread = hasread;

			return toread;
		}
	}
#endif /* FUZZING */

	/* Emulated PTP camera stuff */

	if (toread > cam->nrinbulk)
		toread = cam->nrinbulk;

	memcpy(data, cam->inbulk, toread);
	memmove(cam->inbulk, cam->inbulk + toread, (cam->nrinbulk - toread));
	cam->nrinbulk -= toread;
	return toread;
}

int vcam_write(vcamera *cam, int ep, const unsigned char *data, int bytes) {
	if (!cam->outbulk) {
		cam->outbulk = malloc(bytes);
	} else {
		cam->outbulk = realloc(cam->outbulk, cam->nroutbulk + bytes);
	}
	memcpy(cam->outbulk + cam->nroutbulk, data, bytes);
	cam->nroutbulk += bytes;

	vcam_process_output(cam);

	return bytes;
}

int ptp_notify_event(vcamera *cam, uint16_t code, uint32_t value) {
	return ptp_inject_interrupt(cam, 1000, code, 1, value, 0);
}

int ptp_inject_interrupt(vcamera *cam, int when, uint16_t code, int nparams, uint32_t param1, uint32_t transid) {
	struct ptp_interrupt *interrupt, **pint;
	struct timeval now;
	unsigned char *data;
	int x = 0;

	gp_log(GP_LOG_DEBUG, __FUNCTION__, "generate interrupt 0x%04x, %d params, param1 0x%08x, timeout=%d", code, nparams, param1, when);

	gettimeofday(&now, NULL);
	now.tv_usec += (when % 1000) * 1000;
	now.tv_sec += when / 1000;
	if (now.tv_usec > 1000000) {
		now.tv_usec -= 1000000;
		now.tv_sec++;
	}

	data = malloc(0x10);
	x += put_32bit_le(data + x, 0x10);
	x += put_16bit_le(data + x, 4);
	x += put_16bit_le(data + x, code);
	x += put_32bit_le(data + x, transid);
	x += put_32bit_le(data + x, param1);

	interrupt = malloc(sizeof(struct ptp_interrupt));
	interrupt->data = data;
	interrupt->size = x;
	interrupt->triggertime = now;
	interrupt->next = NULL;

	/* Insert into list, sorted by trigger time, next triggering one first */
	pint = &first_interrupt;
	while (*pint) {
		if (now.tv_sec > (*pint)->triggertime.tv_sec) {
			pint = &((*pint)->next);
			continue;
		}
		if ((now.tv_sec == (*pint)->triggertime.tv_sec) &&
		    (now.tv_usec > (*pint)->triggertime.tv_usec)) {
			pint = &((*pint)->next);
			continue;
		}
		interrupt->next = *pint;
		*pint = interrupt;
		break;
	}
	if (!*pint) /* single entry */
		*pint = interrupt;
	return 1;
}

struct CamGenericEvent ptp_pop_event(vcamera *cam) {
	struct CamGenericEvent ev;
	memcpy(&ev, first_interrupt->data, sizeof(struct CamGenericEvent));

	struct ptp_interrupt *prev = first_interrupt;
	first_interrupt = first_interrupt->next;
	free(prev->data);
	free(prev);

	return ev;
}

// Reads ints into 'data' with max 'bytes'
int vcam_readint(vcamera *cam, unsigned char *data, int bytes, int timeout) {
	struct timeval now, end;
	int newtimeout, tocopy;
	struct ptp_interrupt *pint;

	if (!first_interrupt) {
#ifdef FUZZING
		/* this emulates plugged out devices during fuzzing */
		if (cam->fuzzf && feof(cam->fuzzf))
			return GP_ERROR_IO;
#endif
		return GP_ERROR_TIMEOUT;
	}
	gettimeofday(&now, NULL);
	end = now;
	end.tv_usec += (timeout % 1000) * 1000;
	end.tv_sec += timeout / 1000;
	if (end.tv_usec > 1000000) {
		end.tv_usec -= 1000000;
		end.tv_sec++;
	}
	if (first_interrupt->triggertime.tv_sec > end.tv_sec) {
		return GP_ERROR_TIMEOUT;
	}
	if ((first_interrupt->triggertime.tv_sec == end.tv_sec) &&
	    (first_interrupt->triggertime.tv_usec > end.tv_usec)) {
		return GP_ERROR_TIMEOUT;
	}
	newtimeout = (first_interrupt->triggertime.tv_sec - now.tv_sec) * 1000 + (first_interrupt->triggertime.tv_usec - now.tv_usec) / 1000;
	if (newtimeout > timeout)
		gp_log(GP_LOG_ERROR, __FUNCTION__, "miscalculated? %d vs %d", timeout, newtimeout);
	tocopy = first_interrupt->size;
	if (tocopy > bytes)
		tocopy = bytes;
	memcpy(data, first_interrupt->data, tocopy);
	pint = first_interrupt;
	first_interrupt = first_interrupt->next;
	free(pint->data);
	free(pint);
	return tocopy;
}

vcamera *vcamera_new(vcameratype type) {
	vcamera *cam;

	cam = calloc(1, sizeof(vcamera));
	if (!cam)
		return NULL;

	cam->conf = NULL;

	read_tree(VCAMERADIR);

	cam->init = vcam_init;
	cam->exit = vcam_exit;
	cam->open = vcam_open;
	cam->close = vcam_close;

	cam->read = vcam_read;
	cam->readint = vcam_readint;
	cam->write = vcam_write;

	cam->type = type;
	cam->seqnr = 0;

	return cam;
}
