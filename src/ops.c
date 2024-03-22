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
#include "canon.h"
#include "fuji.h"
#include "ops.h"

int ptp_nikon_setcontrolmode_write(vcamera *cam, ptpcontainer *ptp) {
	CHECK_PARAM_COUNT(1);

	if ((ptp->params[0] != 0) && (ptp->params[0] != 1)) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "controlmode must not be 0 or 1, is %d", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_opensession_write(vcamera *cam, ptpcontainer *ptp) {
	CHECK_PARAM_COUNT(1);

	if (ptp->params[0] == 0) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "session must not be 0, is %d", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	if (cam->session) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "session is already open");
		ptp_response(cam, PTP_RC_SessionAlreadyOpened, 0);
		return 1;
	}
	cam->seqnr = 0;
	cam->session = ptp->params[0];
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_closesession_write(vcamera *cam, ptpcontainer *ptp) {
	CHECK_PARAM_COUNT(0);
	CHECK_SEQUENCE_NUMBER();

	if (!cam->session) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "session is not open");
		ptp_response(cam, PTP_RC_SessionAlreadyOpened, 0);
		return 1;
	}
	cam->session = 0;
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_deviceinfo_write(vcamera *cam, ptpcontainer *ptp) {
	unsigned char *data;
	int x = 0, i, cnt, vendor;
	uint16_t *opcodes, *devprops;
	uint16_t imageformats[1];
	uint16_t events[5];

	CHECK_PARAM_COUNT(0);

	/* Session does not need to be open for GetDeviceInfo */

	/* Getdeviceinfo is special. it can be called with transid 0 outside of the session. */
	if ((ptp->seqnr != 0) && (ptp->seqnr != cam->seqnr)) {
		/* not clear if normal cameras react like this */
		gp_log(GP_LOG_ERROR, __FUNCTION__, "seqnr %d was sent, expected was %d", ptp->seqnr, cam->seqnr);
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}
	data = malloc(2000);

	x += put_16bit_le(data + x, 100); /* StandardVersion */
	x += put_32bit_le(data + x, 100); /* VendorExtensionID */
	x += put_16bit_le(data + x, 100); /* VendorExtensionVersion */
	x += put_string(data + x, "G-V: 1.0;"); /* VendorExtensionDesc */
	x += put_16bit_le(data + x, 0);		/* FunctionalMode */

	cnt = 0;
	for (i = 0; i < sizeof(ptp_functions) / sizeof(ptp_functions[0]); i++) {
		if (ptp_functions[i].type != cam->type) continue;
		for (int x = 0; x < ptp_functions[i].functions[x].code != 0; x++) {
			cnt++;
		}
	}

	opcodes = malloc(cnt * sizeof(uint16_t));

	cnt = 0;
	for (i = 0; i < sizeof(ptp_functions) / sizeof(ptp_functions[0]); i++) {
		if (ptp_functions[i].type != cam->type) continue;
		for (int z = 0; z < ptp_functions[i].functions[z].code != 0; z++) {
			opcodes[cnt] = ptp_functions[i].functions[z].code;
			cnt++;
		}
	}

	x += put_16bit_le_array(data + x, opcodes, cnt); /* OperationsSupported */
	free(opcodes);

	events[0] = 0x4002;
	events[1] = 0x4003;
	events[2] = 0x4006;
	events[3] = 0x400a;
	events[4] = 0x400d;
	x += put_16bit_le_array(data + x, events, sizeof(events) / sizeof(events[0])); /* EventsSupported */

	devprops = malloc(sizeof(ptp_properties) / sizeof(ptp_properties[0]) * sizeof(uint16_t));
	for (i = 0; i < sizeof(ptp_properties) / sizeof(ptp_properties[0]); i++)
		devprops[i] = ptp_properties[i].code;
	x += put_16bit_le_array(data + x, devprops, sizeof(ptp_properties) / sizeof(ptp_properties[0])); /* DevicePropertiesSupported */
	free(devprops);

	imageformats[0] = 0x3801;
	x += put_16bit_le_array(data + x, imageformats, 1); /* CaptureFormats */

	imageformats[0] = 0x3801;
	x += put_16bit_le_array(data + x, imageformats, 1); /* ImageFormats */

	if (cam->type == CAM_CANON) {
		x += put_string(data + x, "Canon Inc.");
	} else if (cam->type == CAM_FUJI_WIFI) {
		x += put_string(data + x, "Fujifilm Corp");
	} else {
		x += put_string(data + x, "Generic Corp");
	}

	x += put_string(data + x, cam->conf->model);
	x += put_string(data + x, cam->conf->version);
	x += put_string(data + x, cam->conf->version);
	x += put_string(data + x, cam->conf->serial);

	ptp_senddata(cam, 0x1001, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getnumobjects_write(vcamera *cam, ptpcontainer *ptp) {
	int cnt;
	struct ptp_dirent *cur;
	uint32_t mode = 0;

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();

	if (ptp->nparams < 1) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "parameter count %d", ptp->nparams);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	if ((ptp->params[0] != 0xffffffff) && (ptp->params[0] != 0x00010001)) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "storage id 0x%08x unknown", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidStorageId, 0);
		return 1;
	}
	if (ptp->nparams >= 2) {
		if (ptp->params[1] != 0) {
			gp_log(GP_LOG_ERROR, __FUNCTION__, "currently can not handle OFC selection (0x%04x)", ptp->params[1]);
			ptp_response(cam, PTP_RC_SpecByFormatUnsupported, 0);
			return 1;
		}
	}
	if (ptp->nparams >= 3) {
		mode = ptp->params[2];
		if ((mode != 0) && (mode != 0xffffffff)) {
			cur = first_dirent;
			while (cur) {
				if (cur->id == mode)
					break;
				cur = cur->next;
			}
			if (!cur) {
				gp_log(GP_LOG_ERROR, __FUNCTION__, "requested subtree of (0x%08x), but no such handle", mode);
				ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
				return 1;
			}
			if (!S_ISDIR(cur->stbuf.st_mode)) {
				gp_log(GP_LOG_ERROR, __FUNCTION__, "requested subtree of (0x%08x), but this is no asssocation", mode);
				ptp_response(cam, PTP_RC_InvalidParentObject, 0);
				return 1;
			}
		}
	}

	cnt = 0;
	cur = first_dirent;
	while (cur) {
		if (cur->id) { /* do not include 0 entry */
			switch (mode) {
			case 0: /* all objects recursive on device */
				cnt++;
				break;
			case 0xffffffff: /* only root dir */
				if (cur->parent->id == 0)
					cnt++;
				break;
			default: /* single level directory below this handle */
				if (cur->parent->id == mode)
					cnt++;
				break;
			}
		}
		cur = cur->next;
	}

	ptp_response(cam, PTP_RC_OK, 1, cnt);
	return 1;
}

int ptp_get_object_count() {
	int cnt = 0;
	struct ptp_dirent *cur = first_dirent;
	while (cur) {
		if (cur->id) { /* do not include 0 entry */
			cnt++;
		}
		cur = cur->next;
	}

	return cnt;
}

int ptp_getobjecthandles_write(vcamera *cam, ptpcontainer *ptp) {
	unsigned char *data;
	int x = 0, cnt;
	struct ptp_dirent *cur;
	uint32_t mode = 0;

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();

	if (ptp->nparams < 1) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "parameter count %d", ptp->nparams);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	if ((ptp->params[0] != 0xffffffff) && (ptp->params[0] != 0x00010001)) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "storage id 0x%08x unknown", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidStorageId, 0);
		return 1;
	}
	if (ptp->nparams >= 2) {
		if (ptp->params[1] != 0) {
			gp_log(GP_LOG_ERROR, __FUNCTION__, "currently can not handle OFC selection (0x%04x)", ptp->params[1]);
			ptp_response(cam, PTP_RC_SpecByFormatUnsupported, 0);
			return 1;
		}
	}
	if (ptp->nparams >= 3) {
		mode = ptp->params[2];
		if ((mode != 0) && (mode != 0xffffffff)) {
			cur = first_dirent;
			while (cur) {
				if (cur->id == mode)
					break;
				cur = cur->next;
			}
			if (!cur) {
				gp_log(GP_LOG_ERROR, __FUNCTION__, "requested subtree of (0x%08x), but no such handle", mode);
				ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
				return 1;
			}
			if (!S_ISDIR(cur->stbuf.st_mode)) {
				gp_log(GP_LOG_ERROR, __FUNCTION__, "requested subtree of (0x%08x), but this is no asssocation", mode);
				ptp_response(cam, PTP_RC_InvalidParentObject, 0);
				return 1;
			}
		}
	}

	cnt = 0;
	cur = first_dirent;
	while (cur) {
		if (cur->id) { /* do not include 0 entry */
			switch (mode) {
			case 0: /* all objects recursive on device */
				cnt++;
				break;
			case 0xffffffff: /* only root dir */
				if (cur->parent->id == 0)
					cnt++;
				break;
			default: /* single level directory below this handle */
				if (cur->parent->id == mode)
					cnt++;
				break;
			}
		}
		cur = cur->next;
	}

	data = malloc(4 + 4 * cnt);
	x = put_32bit_le(data + x, cnt);
	cur = first_dirent;
	while (cur) {
		if (cur->id) { /* do not include 0 entry */
			switch (mode) {
			case 0: /* all objects recursive on device */
				x += put_32bit_le(data + x, cur->id);
				break;
			case 0xffffffff: /* only root dir */
				if (cur->parent->id == 0)
					x += put_32bit_le(data + x, cur->id);
				break;
			default: /* single level directory below this handle */
				if (cur->parent->id == mode)
					x += put_32bit_le(data + x, cur->id);
				break;
			}
		}
		cur = cur->next;
	}
	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getstorageids_write(vcamera *cam, ptpcontainer *ptp) {
	unsigned char *data;
	int x = 0;
	uint32_t sids[1];

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(0);

	data = malloc(200);

	sids[0] = 0x00010001;
	x = put_32bit_le_array(data, sids, 1);

	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getstorageinfo_write(vcamera *cam, ptpcontainer *ptp) {
	unsigned char *data;
	int x = 0;

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(1);

	if (ptp->params[0] != 0x00010001) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "invalid storage id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidStorageId, 0);
		return 1;
	}

	data = malloc(200);
	x += put_16bit_le(data + x, 3);		   /* StorageType: Fixed RAM */
	x += put_16bit_le(data + x, 3);		   /* FileSystemType: Generic Hierarchical */
	x += put_16bit_le(data + x, 2);		   /* AccessCapability: R/O with object deletion */
	x += put_64bit_le(data + x, 0x42424242);   /* MaxCapacity */
	x += put_64bit_le(data + x, 0x21212121);   /* FreeSpaceInBytes */
	x += put_32bit_le(data + x, 150);	   /* FreeSpaceInImages ... around 150 */
	x += put_string(data + x, "GPVC Storage"); /* StorageDescription */
	x += put_string(data + x, "GPVCS Label");  /* VolumeLabel */

	ptp_senddata(cam, 0x1005, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getpartialobject_write(vcamera *cam, ptpcontainer *ptp) {
	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(3);

	vcam_log("GetPartialObject %d (%X %X)\n", ptp->params[0], ptp->params[1], ptp->params[2]);

	struct ptp_dirent *cur = first_dirent;
	while (cur) {
		if (cur->id == ptp->params[0])
			break;
		cur = cur->next;
	}

	if (!cur) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "invalid object id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	FILE *file = fopen(cur->fsname, "rb");
	if (file == NULL) {
		vcam_log("File %s not found\n", cur->fsname);
		exit(-1);
	}

	size_t start = (size_t)ptp->params[1];
	size_t size = (size_t)ptp->params[2];

	if (fseek(file, start, SEEK_SET) == -1) {
		vcam_log("fseek failure\n");
		exit(-1);
	}

	char *buffer = malloc(size);
	int read = fread(buffer, 1, size, file); // TODO: check for folder

	ptp_senddata(cam, ptp->code, (unsigned char *)buffer, read);
	vcam_log("Generic sending %d\n", read);

	free(buffer);
	fclose(file);

	ptp_response(cam, PTP_RC_OK, 0);

	if (cam->type == CAM_FUJI_WIFI) {
		// Once the end of the file is read, cam seems to switch
		// Can be triggered by cam's size being lower or request size being lower (TODO: just the latter)
		if (read != 0x100000 || size != 0x100000) {
			fuji_downloaded_object(cam);
		}
	}
	return 1;
}

int ptp_getobjectinfo_write(vcamera *cam, ptpcontainer *ptp) {
	struct ptp_dirent *cur;
	unsigned char *data;
	int x = 0;
	uint16_t ofc, thumbofc = 0;
	int thumbwidth = 0, thumbheight = 0, thumbsize = 0;
	int imagewidth = 0, imageheight = 0, imagebitdepth = 0;
	struct tm *tm;
	time_t xtime;
	char xdate[40];

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(1);

	cur = first_dirent;
	while (cur) {
		if (cur->id == ptp->params[0])
			break;
		cur = cur->next;
	}
	if (!cur) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "invalid object id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}
	data = malloc(2000);
	x += put_32bit_le(data + x, 0x10000001); /* StorageID */
	//x += put_32bit_le(data + x, 0x00010001); /* StorageID */
	/* ObjectFormatCode */
	ofc = 0x3000;
	if (S_ISDIR(cur->stbuf.st_mode)) {
		ofc = 0x3001;
	} else {
		if (strstr(cur->name, ".JPG") || strstr(cur->name, ".jpg"))
			ofc = 0x3801;
		if (strstr(cur->name, ".GIF") || strstr(cur->name, ".gif"))
			ofc = 0x3807;
		if (strstr(cur->name, ".PNG") || strstr(cur->name, ".png"))
			ofc = 0x380B;
		if (strstr(cur->name, ".DNG") || strstr(cur->name, ".dng"))
			ofc = 0x3811;
		if (strstr(cur->name, ".TXT") || strstr(cur->name, ".txt"))
			ofc = 0x3004;
		if (strstr(cur->name, ".HTML") || strstr(cur->name, ".html"))
			ofc = 0x3005;
		if (strstr(cur->name, ".MP3") || strstr(cur->name, ".mp3"))
			ofc = 0x3009;
		if (strstr(cur->name, ".AVI") || strstr(cur->name, ".avi"))
			ofc = 0x300A;
		if (strstr(cur->name, ".MPG") || strstr(cur->name, ".mpg") ||
		    strstr(cur->name, ".MPEG") || strstr(cur->name, ".mpeg"))
			ofc = 0x300B;
	}

#ifdef HAVE_LIBEXIF
	if (ofc == 0x3801) { /* We are jpeg ... look into the exif data */
		ExifData *ed;
		ExifEntry *e;
		unsigned char *filedata;

		filedata = read_file(cur);
		if (!filedata) {
			free(data);
			ptp_response(cam, PTP_RC_GeneralError, 0);
			return 1;
		}

		ed = exif_data_new_from_data((unsigned char *)filedata, cur->stbuf.st_size);
		if (ed) {
			if (ed->data) {
				thumbofc = 0x3808;
				thumbsize = ed->size;
			}
			e = exif_data_get_entry(ed, EXIF_TAG_PIXEL_X_DIMENSION);
			if (e) {
				gp_log(GP_LOG_DEBUG, __FUNCTION__, "pixel x dim format is %d", e->format);
				if (e->format == EXIF_FORMAT_SHORT) {
					imagewidth = exif_get_short(e->data, exif_data_get_byte_order(ed));
				} else if (e->format == EXIF_FORMAT_LONG) {
					imagewidth = exif_get_long(e->data, exif_data_get_byte_order(ed));
				}
			}
			e = exif_data_get_entry(ed, EXIF_TAG_PIXEL_Y_DIMENSION);
			if (e) {
				gp_log(GP_LOG_DEBUG, __FUNCTION__, "pixel y dim format is %d", e->format);
				if (e->format == EXIF_FORMAT_SHORT) {
					imageheight = exif_get_short(e->data, exif_data_get_byte_order(ed));
				} else if (e->format == EXIF_FORMAT_LONG) {
					imageheight = exif_get_long(e->data, exif_data_get_byte_order(ed));
				}
			}

			/* FIXME: potentially could find out more about thumbnail too */
		}
		exif_data_unref(ed);
		free(filedata);
	}
#endif

	gp_log_("Image %s is %dx%d\n", cur->fsname, imagewidth, imageheight);

	uint32_t compressed_size = cur->stbuf.st_size;

	// Fuji weirdness
#ifdef VCAM_FUJI
	if (fuji_is_compressed_mode(cam)) {
		compressed_size = 0x19000;
	}
#endif

	x += put_16bit_le(data + x, ofc);
	x += put_16bit_le(data + x, 0);			 /* ProtectionStatus, no protection */
	x += put_32bit_le(data + x, compressed_size); /* ObjectCompressedSize */
	x += put_16bit_le(data + x, thumbofc);		 /* ThumbFormat */
	x += put_32bit_le(data + x, thumbsize);		 /* ThumbCompressedSize */
	x += put_32bit_le(data + x, thumbwidth);	 /* ThumbPixWidth */
	x += put_32bit_le(data + x, thumbheight);	 /* ThumbPixHeight */
	x += put_32bit_le(data + x, imagewidth);	 /* ImagePixWidth */
	x += put_32bit_le(data + x, imageheight);	 /* ImagePixHeight */
	x += put_32bit_le(data + x, imagebitdepth);	 /* ImageBitDepth */
	if (cur->parent != NULL) {
		x += put_32bit_le(data + x, cur->parent->id);	 /* ParentObject */
	} else {
		x += put_32bit_le(data + x, 0xffffffff);	 /* ParentObject */
	}
	/* AssociationType */
	if (S_ISDIR(cur->stbuf.st_mode)) {
		x += put_16bit_le(data + x, 1); /* GenericFolder */
		x += put_32bit_le(data + x, 0); /* unused */
	} else {
		x += put_16bit_le(data + x, 0); /* Undefined */
		x += put_32bit_le(data + x, 0); /* Undefined */
	}
	x += put_32bit_le(data + x, 0);	      /* SequenceNumber */
	x += put_string(data + x, cur->name); /* Filename */

	xtime = cur->stbuf.st_ctime;
	tm = gmtime(&xtime);
	sprintf(xdate, "%04d%02d%02dT%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	x += put_string(data + x, xdate); /* CreationDate */

#if 0
	xtime = cur->stbuf.st_mtime;
	tm = gmtime(&xtime);
	sprintf(xdate, "%04d%02d%02dT%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	x += put_string(data + x, xdate); /* ModificationDate */
#endif

	// Orientation tag
	if (imageheight > imagewidth) {
		x += put_string(data + x, "Orientation: 8");
	} else {
		x += put_string(data + x, "Orientation: 1");
	}

	printf("Sending objectinfo length %x\n", x);
	vcam_dump(data, x);

	ptp_senddata(cam, 0x1008, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getobject_write(vcamera *cam, ptpcontainer *ptp) {
	unsigned char *data;
	struct ptp_dirent *cur;

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(1);

	cur = first_dirent;
	while (cur) {
		if (cur->id == ptp->params[0])
			break;
		cur = cur->next;
	}
	if (!cur) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "invalid object id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}
	data = read_file(cur);
	if (!data) {
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}

	ptp_senddata(cam, ptp->code, data, cur->stbuf.st_size);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);

#ifdef VCAM_FUJI
	fuji_downloaded_object(cam);
#endif

	return 1;
}

int ptp_getthumb_write(vcamera *cam, ptpcontainer *ptp) {
	unsigned char *data;
	struct ptp_dirent *cur;
#ifdef HAVE_LIBEXIF
	ExifData *ed;
#endif

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(1);

	//usleep(1000 * 500);

	gp_log_("Processing thumb call for %d\n", ptp->params[0]);

	cur = first_dirent;
	while (cur) {
		if (cur->id == ptp->params[0])
			break;
		cur = cur->next;
	}
	if (!cur) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "invalid object id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}
	data = read_file(cur);
	if (!data) {
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}

#ifdef HAVE_LIBEXIF
	ed = exif_data_new_from_data((unsigned char *)data, cur->stbuf.st_size);
	if (!ed) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "Could not parse EXIF data");
		free(data);
		ptp_response(cam, PTP_RC_NoThumbnailPresent, 0);
		return 1;
	}
	if (!ed->data) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "EXIF data does not contain a thumbnail");
		free(data);
		ptp_response(cam, PTP_RC_NoThumbnailPresent, 0);
		exif_data_unref(ed);
		return 1;
	}
	/*
	 * We found a thumbnail in EXIF data! Those
	 * thumbnails are always JPEG. Set up the file.
	 */
	ptp_senddata(cam, 0x100A, ed->data, ed->size);
	exif_data_unref(ed);

	ptp_response(cam, PTP_RC_OK, 0);
#else
	gp_log(GP_LOG_ERROR, __FUNCTION__, "Cannot get thumbnail without libexif, lying about missing thumbnail");
	ptp_response(cam, PTP_RC_NoThumbnailPresent, 0);
#endif
	free(data);
	return 1;
}

int ptp_initiatecapture_write(vcamera *cam, ptpcontainer *ptp) {
	struct ptp_dirent *cur, *newcur, *dir, *dcim = NULL;
	int capcnt = 98;
	char buf[10];

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(2);

	if ((ptp->params[0] != 0) && (ptp->params[0] != 0x00010001)) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "invalid storage id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_StoreNotAvailable, 0);
		return 1;
	}
	if ((ptp->params[1] != 0) && (ptp->params[1] != 0x3801)) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "invalid objectformat code id 0x%04x", ptp->params[1]);
		ptp_response(cam, PTP_RC_InvalidObjectFormatCode, 0);
		return 1;
	}
	if (capcnt > 150) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "Declaring store full at picture 151");
		ptp_response(cam, PTP_RC_StoreFull, 0);
		return 1;
	}

	/* just search for first jpeg we find in the tree, we will use this to send back as the captured image */
	cur = first_dirent;
	while (cur) {
		if (strstr(cur->name, ".jpg") || strstr(cur->name, ".JPG"))
			break;
		cur = cur->next;
	}
	if (!cur) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "I do not have a JPG file in the store, can not proceed");
		ptp_response(cam, PTP_RC_StoreNotAvailable, 0);
		return 1;
	}
	/* identify the DCIM dir, so we can attach a virtual xxxGPHOT directory to virtually store the new picture in */
	dir = first_dirent;
	while (dir) {
		if (!strcmp(dir->name, "DCIM") && dir->parent && !dir->parent->id)
			dcim = dir;
		dir = dir->next;
	}
	/* find the nnnGPHOT directories, where nnn is 100-999. (See DCIM standard.) */
	sprintf(buf, "%03dGPHOT", 100 + ((capcnt / 100) % 900));
	dir = first_dirent;
	while (dir) {
		if (!strcmp(dir->name, buf) && (dir->parent == dcim))
			break;
		dir = dir->next;
	}
	/* if not yet found, create the virtual /DCIM/xxxGPHOT/ directory. */
	if (!dir) {
		dir = malloc(sizeof(struct ptp_dirent));
		dir->id = ++ptp_objectid;
		dir->fsname = "virtual";
		dir->stbuf = dcim->stbuf; /* only the S_ISDIR flag is used */
		dir->parent = dcim;
		dir->next = first_dirent;
		dir->name = strdup(buf);
		first_dirent = dir;
		/* Emit ObjectAdded event for the created folder */
		ptp_inject_interrupt(cam, 80, 0x4002, 1, ptp_objectid, cam->seqnr); /* objectadded */
	}
	if (capcnt++ == 150) {
		/* The start of the operation succeeds, but the memory runs full during it. */
		ptp_inject_interrupt(cam, 100, 0x400A, 1, ptp_objectid, cam->seqnr); /* storefull */
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	newcur = malloc(sizeof(struct ptp_dirent));
	newcur->id = ++ptp_objectid;
	newcur->fsname = strdup(cur->fsname);
	newcur->stbuf = cur->stbuf;
	newcur->parent = dir;
	newcur->next = first_dirent;
	newcur->name = malloc(8 + 3 + 1 + 1);
	sprintf(newcur->name, "GPH_%04d.JPG", capcnt++);
	first_dirent = newcur;

	ptp_inject_interrupt(cam, 100, 0x4002, 1, ptp_objectid, cam->seqnr); /* objectadded */
	ptp_inject_interrupt(cam, 120, 0x400d, 0, 0, cam->seqnr);	     /* capturecomplete */
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_deleteobject_write(vcamera *cam, ptpcontainer *ptp) {
	struct ptp_dirent *cur, *xcur;

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();

	if (ptp->nparams < 1) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "parameter count %d", ptp->nparams);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	if (ptp->params[0] == 0xffffffff) { /* delete all mode */
		gp_log(GP_LOG_DEBUG, __FUNCTION__, "delete all");
		cur = first_dirent;

		while (cur) {
			xcur = cur->next;
			free_dirent(cur);
			cur = xcur;
		}
		first_dirent = NULL;
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	if ((ptp->nparams == 2) && (ptp->params[1] != 0)) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "single object delete, but ofc is 0x%08x", ptp->params[1]);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	/* for associations this even means recursive deletion */

	cur = first_dirent;
	while (cur) {
		if (cur->id == ptp->params[0])
			break;
		cur = cur->next;
	}
	if (!cur) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "invalid object id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}
	if (S_ISDIR(cur->stbuf.st_mode)) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "FIXME: not yet deleting directories");
		ptp_response(cam, PTP_RC_ObjectWriteProtected, 0);
		return 1;
	}
	if (cur == first_dirent) {
		first_dirent = cur->next;
		free_dirent(cur);
	} else {
		xcur = first_dirent;
		while (xcur) {
			if (xcur->next == cur) {
				xcur->next = xcur->next->next;
				free_dirent(cur);
				break;
			}
			xcur = xcur->next;
		}
	}
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int put_propval(unsigned char *data, uint16_t type, PTPPropertyValue *val) {
	switch (type) {
	case 0x1:
		return put_8bit_le(data, val->i8);
	case 0x2:
		return put_8bit_le(data, val->u8);
	case 0x3:
		return put_16bit_le(data, val->i16);
	case 0x4:
		return put_16bit_le(data, val->u16);
	case 0x6:
		return put_32bit_le(data, val->u32);
	case 0xffff:
		return put_string(data, val->str);
	default:
		gp_log(GP_LOG_ERROR, __FUNCTION__, "unhandled datatype %d", type);
		return 0;
	}
	return 0;
}

int get_propval(unsigned char *data, unsigned int len, uint16_t type, PTPPropertyValue *val) {
#define CHECK_SIZE(x) \
	if (len < x)  \
		return 0;
	switch (type) {
	case 0x1:
		CHECK_SIZE(1);
		val->i8 = get_i8bit_le(data);
		return 1;
	case 0x2:
		CHECK_SIZE(1);
		val->u8 = get_8bit_le(data);
		return 1;
	case 0x3:
		CHECK_SIZE(2);
		val->i16 = get_16bit_le(data);
		return 1;
	case 0x4:
		CHECK_SIZE(2);
		val->u16 = get_16bit_le(data);
		return 1;
	case 0x6:
		CHECK_SIZE(4);
		val->u32 = get_32bit_le(data);
		return 1;
	case 0xffff: {
		int slen;
		CHECK_SIZE(1);
		slen = get_8bit_le(data);
		CHECK_SIZE(1 + slen * 2);
		val->str = get_string(data);
		return 1 + slen * 2;
	}
	default:
		gp_log(GP_LOG_ERROR, __FUNCTION__, "unhandled datatype %d", type);
		return 0;
	}
	return 0;
#undef CHECK_SIZE
}

int ptp_getdevicepropdesc_write(vcamera *cam, ptpcontainer *ptp) {
	int i, x = 0;
	unsigned char *data;
	PTPDevicePropDesc desc;

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(1);

	for (i = 0; i < sizeof(ptp_properties) / sizeof(ptp_properties[0]); i++) {
		if (ptp_properties[i].code == ptp->params[0])
			break;
	}
	if (i == sizeof(ptp_properties) / sizeof(ptp_properties[0])) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "deviceprop 0x%04x not found", ptp->params[0]);
		ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
		return 1;
	}
	data = malloc(2000);
	ptp_properties[i].getdesc(cam, &desc);

	x += put_16bit_le(data + x, desc.DevicePropertyCode);
	x += put_16bit_le(data + x, desc.DataType);
	x += put_8bit_le(data + x, desc.GetSet);
	x += put_propval(data + x, desc.DataType, &desc.FactoryDefaultValue);
	x += put_propval(data + x, desc.DataType, &desc.CurrentValue);
	x += put_8bit_le(data + x, desc.FormFlag);
	switch (desc.FormFlag) {
	case 0:
		break;
	case 1: /* range */
		x += put_propval(data + x, desc.DataType, &desc.FORM.Range.MinimumValue);
		x += put_propval(data + x, desc.DataType, &desc.FORM.Range.MaximumValue);
		x += put_propval(data + x, desc.DataType, &desc.FORM.Range.StepSize);
		break;
	case 2: /* ENUM */
		x += put_16bit_le(data + x, desc.FORM.Enum.NumberOfValues);
		for (i = 0; i < desc.FORM.Enum.NumberOfValues; i++)
			x += put_propval(data + x, desc.DataType, &desc.FORM.Enum.SupportedValue[i]);
		break;
	}

	ptp_free_devicepropdesc(&desc);

	ptp_senddata(cam, 0x1014, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getdevicepropvalue_write(vcamera *cam, ptpcontainer *ptp) {
	unsigned char *data;
	int i, x = 0;
	PTPPropertyValue val;
	PTPDevicePropDesc desc;

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(1);

	if (cam->type == CAM_FUJI_WIFI) {
		return fuji_get_property(cam, ptp);
	}

	for (i = 0; i < sizeof(ptp_properties) / sizeof(ptp_properties[0]); i++) {
		if (ptp_properties[i].code == ptp->params[0])
			break;
	}
	if (i == sizeof(ptp_properties) / sizeof(ptp_properties[0])) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "deviceprop 0x%04x not found", ptp->params[0]);
		ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
		return 1;
	}
	data = malloc(2000);
	ptp_properties[i].getdesc(cam, &desc);
	ptp_properties[i].getvalue(cam, &val);
	x = put_propval(data + x, desc.DataType, &val);

	ptp_senddata(cam, 0x1015, data, x);
	free(data);

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_setdevicepropvalue_write(vcamera *cam, ptpcontainer *ptp) {
	int i;

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(1);

	if (cam->type == CAM_FUJI_WIFI) {
		if (fuji_set_prop_supported(cam, ptp->params[0])) {
			ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
			return 1;
		} else {
			return 1;
		}
	}

	for (i = 0; i < sizeof(ptp_properties) / sizeof(ptp_properties[0]); i++) {
		if (ptp_properties[i].code == ptp->params[0])
			break;
	}
	if (i == sizeof(ptp_properties) / sizeof(ptp_properties[0])) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "deviceprop 0x%04x not found", ptp->params[0]);
		ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
		return 1;
	}
	/* so ... we wait for the data phase */
	return 1;
}

int ptp_vusb_write(vcamera *cam, ptpcontainer *ptp) {
	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();

	vcam_log(
		"\tRecieved 0xBEEF\n"
		"\tSize: %d\n"
		"\tType: %d\n"
		"\tCode: %X\n"
		"\tseqnr: %d\n"
		"\tdataphase: %d\n",
		ptp->size, ptp->type, ptp->code, ptp->seqnr, ptp->has_data_phase
	);

	vcam_log("Params (%d): ", ptp->nparams);
	for (int i = 0; i < ptp->nparams; i++) {
		printf("%d ", ptp->params[i]);
	}
	printf("\n");

	return 1;
}

int ptp_vusb_write_data(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	vcam_log("Recieved data phase for 0xBEEF: %d\n", len);

	if (ptp->nparams != 1) {
		vcam_log("Expected a checksum parameter\n");
	}

	int checksum = 0;
	for (int i = 0; i < len; i++) {
		checksum += data[i];
	}

	if (checksum != ptp->params[0]) {
		vcam_log("Invalid checksum %d/%d\n", checksum, ptp->params[0]);
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	} else {
		vcam_log("Verified checksum %d/%d\n", checksum, ptp->params[0]);
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

int ptp_setdevicepropvalue_write_data(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	int i;
	PTPPropertyValue val;
	PTPDevicePropDesc desc;

	CHECK_SEQUENCE_NUMBER();
	CHECK_SESSION();
	CHECK_PARAM_COUNT(1);

	if (cam->type == CAM_FUJI_WIFI) {
		return fuji_set_property(cam, ptp, data, len);
	}

	for (i = 0; i < sizeof(ptp_properties) / sizeof(ptp_properties[0]); i++) {
		if (ptp_properties[i].code == ptp->params[0])
			break;
	}
	if (i == sizeof(ptp_properties) / sizeof(ptp_properties[0])) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "deviceprop 0x%04x not found", ptp->params[0]);
		/* we emitted the response already in _write */
		return 1;
	}
	if (!ptp_properties[i].setvalue) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "deviceprop 0x%04x is not settable", ptp->params[0]);
		ptp_response(cam, PTP_RC_AccessDenied, 0);
		return 1;
	}
	ptp_properties[i].getdesc(cam, &desc);
	if (!get_propval(data, len, desc.DataType, &val)) {
		gp_log(GP_LOG_ERROR, __FUNCTION__, "deviceprop 0x%04x is not retrievable", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidDevicePropFormat, 0);
		return 1;
	}
	ptp_properties[i].setvalue(cam, &val);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

struct ptp_function ptp_functions_generic[] = {
	{0x1001,	ptp_deviceinfo_write, 		NULL			},
	{0x1002,	ptp_opensession_write, 		NULL			},
	{0x1003,	ptp_closesession_write, 	NULL			},
	{0x1004,	ptp_getstorageids_write, 	NULL			},
	{0x1005,	ptp_getstorageinfo_write, 	NULL			},
	{0x1006,	ptp_getnumobjects_write, 	NULL			},
	{0x1007,	ptp_getobjecthandles_write, NULL			},
	{0x1008,	ptp_getobjectinfo_write, 	NULL			},
	{0x1009,	ptp_getobject_write, 		NULL			},
	{0x100A,	ptp_getthumb_write, 		NULL			},
	{0x100B,	ptp_deleteobject_write, 	NULL			},
	{0x100E,	ptp_initiatecapture_write, 	NULL			},
	{0x1014,	ptp_getdevicepropdesc_write, 	NULL			},
	{0x1015,	ptp_getdevicepropvalue_write, 	NULL			},
	{0x1016,	ptp_setdevicepropvalue_write, 	ptp_setdevicepropvalue_write_data	},
	{0x101B,	ptp_getpartialobject_write, 	NULL			},
	{0xBEEF,	ptp_vusb_write, 		ptp_vusb_write_data			},
	{0, NULL, NULL},
};
