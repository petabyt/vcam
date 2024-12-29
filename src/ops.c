// Copyright (c) 2015,2016 Marcus Meissner <marcus@jet.franken.de> - GNU General Public License v2
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "vcam.h"

#ifdef HAVE_LIBEXIF
#include <libexif/exif-data.h>
#endif

int ptp_nikon_setcontrolmode_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_param_count(cam, ptp, 1))return 1;

	if ((ptp->params[0] != 0) && (ptp->params[0] != 1)) {
		vcam_log_func(__func__, "controlmode must not be 0 or 1, is %d", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_opensession_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	if (ptp->params[0] == 0) {
		vcam_log_func(__func__, "session must not be 0, is %d", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}

	// If client is somehow abruptly handed connection from a host, it has no way of knowing previous transaction ID
	cam->seqnr = ptp->seqnr; // transaction ID is chosen by initiator as per spec

	if (cam->session) {
		vcam_log_func(__func__, "session is already open %d", cam->session);
		ptp_response(cam, PTP_RC_SessionAlreadyOpened, 0);
		return 1;
	}

	cam->session = ptp->params[0];

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_closesession_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_param_count(cam, ptp, 0)) return 1;
	if (vcam_check_trans_id(cam, ptp)) return 1;

	if (!cam->session) {
		vcam_log_func(__func__, "session is not open");
		ptp_response(cam, PTP_RC_SessionAlreadyOpened, 0);
		return 1;
	}
	cam->session = 0;
	ptp_response(cam, PTP_RC_OK, 0);

	// TODO: Reset other per-session state here
	cam->seqnr = 1;

	return 1;
}

int ptp_deviceinfo_write(vcam *cam, ptpcontainer *ptp) {
	unsigned char *data;
	int x = 0, i, cnt;
	uint16_t *opcodes, *devprops;
	uint16_t imageformats[1];
	uint16_t events[5];

	if (vcam_check_param_count(cam, ptp, 0)) return 1;

	/* Session does not need to be open for GetDeviceInfo */

	/* Getdeviceinfo is special. it can be called with transid 0 outside of the session. */
	if ((ptp->seqnr != 0) && (ptp->seqnr != cam->seqnr)) {
		/* not clear if normal cameras react like this */
		vcam_log_func(__func__, "seqnr %d was sent, expected was %d", ptp->seqnr, cam->seqnr);
#if 0
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
#endif
	}
	data = malloc(2000);

	// TODO: Allow cameras to customize these
	x += put_16bit_le(data + x, 0x64); /* StandardVersion */
	x += put_32bit_le(data + x, 0x6); /* VendorExtensionID */
	x += put_16bit_le(data + x, 0x64); /* VendorExtensionVersion */
	x += put_string(data + x, cam->extension); /* VendorExtensionDesc */
	x += put_16bit_le(data + x, 0);		/* FunctionalMode */

	opcodes = malloc(cam->opcodes->length * sizeof(uint16_t));

	cnt = 0;
	for (i = 0; i < cam->opcodes->length; i++) {
		opcodes[cnt] = cam->opcodes->handlers[i].code;
		cnt++;
	}

	x += put_16bit_le_array(data + x, opcodes, cnt); /* OperationsSupported */
	free(opcodes);

	events[0] = 0x4002;
	events[1] = 0x4003;
	events[2] = 0x4006;
	events[3] = 0x400a;
	events[4] = 0x400d;
	x += put_16bit_le_array(data + x, events, sizeof(events) / sizeof(events[0])); /* EventsSupported */

	devprops = malloc(cam->props->length * sizeof(uint16_t));
	for (i = 0; i < cam->props->length; i++)
		devprops[i] = cam->props->handlers[i].code;
	x += put_16bit_le_array(data + x, devprops, cam->props->length); /* DevicePropertiesSupported */
	free(devprops);

	imageformats[0] = 0x3801;
	x += put_16bit_le_array(data + x, imageformats, 1); /* CaptureFormats */

	imageformats[0] = 0x3801;
	x += put_16bit_le_array(data + x, imageformats, 1); /* ImageFormats */

	x += put_string(data + x, cam->manufac);

	x += put_string(data + x, cam->model);
	x += put_string(data + x, cam->version);
	x += put_string(data + x, cam->serial);

	ptp_senddata(cam, 0x1001, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getnumobjects_write(vcam *cam, ptpcontainer *ptp) {
	int cnt;
	struct ptp_dirent *cur;
	uint32_t mode = 0;

	if (vcam_check_trans_id(cam, ptp))return 1;
	if (vcam_check_session(cam))return 1;

	if (ptp->nparams < 1) {
		vcam_log_func(__func__, "parameter count %d", ptp->nparams);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	if ((ptp->params[0] != 0xffffffff) && (ptp->params[0] != 0x00010001)) {
		vcam_log_func(__func__, "storage id 0x%08x unknown", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidStorageId, 0);
		return 1;
	}
	if (ptp->nparams >= 2) {
		if (ptp->params[1] != 0) {
			vcam_log_func(__func__, "currently can not handle OFC selection (0x%04x)", ptp->params[1]);
			ptp_response(cam, PTP_RC_SpecByFormatUnsupported, 0);
			return 1;
		}
	}
	if (ptp->nparams >= 3) {
		mode = ptp->params[2];
		if ((mode != 0) && (mode != 0xffffffff)) {
			cur = cam->first_dirent;
			while (cur) {
				if (cur->id == mode)
					break;
				cur = cur->next;
			}
			if (!cur) {
				vcam_log_func(__func__, "requested subtree of (0x%08x), but no such handle", mode);
				ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
				return 1;
			}
			if (!S_ISDIR(cur->stbuf.st_mode)) {
				vcam_log_func(__func__, "requested subtree of (0x%08x), but this is no asssocation", mode);
				ptp_response(cam, PTP_RC_InvalidParentObject, 0);
				return 1;
			}
		}
	}

	cnt = 0;
	cur = cam->first_dirent;
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

int ptp_getobjecthandles_write(vcam *cam, ptpcontainer *ptp) {
	unsigned char *data;
	int x = 0, cnt;
	struct ptp_dirent *cur;
	uint32_t mode = 0;

	if (vcam_check_trans_id(cam, ptp))return 1;
	if (vcam_check_session(cam))return 1;

	if (ptp->nparams < 1) {
		vcam_log_func(__func__, "parameter count %d", ptp->nparams);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	if ((ptp->params[0] != 0xffffffff) && (ptp->params[0] != 0x00010001)) {
		vcam_log_func(__func__, "storage id 0x%08x unknown", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidStorageId, 0);
		return 1;
	}
	if (ptp->nparams >= 2) {
		if (ptp->params[1] != 0) {
			vcam_log_func(__func__, "currently can not handle OFC selection (0x%04x)", ptp->params[1]);
			ptp_response(cam, PTP_RC_SpecByFormatUnsupported, 0);
			return 1;
		}
	}
	if (ptp->nparams >= 3) {
		mode = ptp->params[2];
		if ((mode != 0) && (mode != 0xffffffff)) {
			cur = cam->first_dirent;
			while (cur) {
				if (cur->id == mode)
					break;
				cur = cur->next;
			}
			if (!cur) {
				vcam_log_func(__func__, "requested subtree of (0x%08x), but no such handle", mode);
				ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
				return 1;
			}
			if (!S_ISDIR(cur->stbuf.st_mode)) {
				vcam_log_func(__func__, "requested subtree of (0x%08x), but this is no asssocation", mode);
				ptp_response(cam, PTP_RC_InvalidParentObject, 0);
				return 1;
			}
		}
	}

	cnt = 0;
	cur = cam->first_dirent;
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
	cur = cam->first_dirent;
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

int ptp_getstorageids_write(vcam *cam, ptpcontainer *ptp) {
	unsigned char *data;
	int x = 0;
	uint32_t sids[1];

	if (vcam_check_trans_id(cam, ptp))return 1;
	if (vcam_check_session(cam))return 1;
	if (vcam_check_param_count(cam, ptp, 0))return 1;

	data = malloc(200);

	sids[0] = 0x00010001;
	x = put_32bit_le_array(data, sids, 1);

	ptp_senddata(cam, ptp->code, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getstorageinfo_write(vcam *cam, ptpcontainer *ptp) {
	unsigned char *data;
	int x = 0;

	if (vcam_check_trans_id(cam, ptp))return 1;
	if (vcam_check_session(cam))return 1;
	if (vcam_check_param_count(cam, ptp, 1))return 1;

	if (ptp->params[0] != 0x00010001) {
		vcam_log_func(__func__, "invalid storage id 0x%08x", ptp->params[0]);
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
	x += put_string(data + x, "Fake Storage"); /* StorageDescription */
	x += put_string(data + x, "Fake Label");  /* VolumeLabel */

	ptp_senddata(cam, 0x1005, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getpartialobject_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 3)) return 1;

	vcam_log("GetPartialObject %d (%X %X)", ptp->params[0], ptp->params[1], ptp->params[2]);

	struct ptp_dirent *cur = cam->first_dirent;
	while (cur) {
		if (cur->id == ptp->params[0])
			break;
		cur = cur->next;
	}

	if (!cur) {
		vcam_log_func(__func__, "invalid object id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}

	FILE *file = fopen(cur->fsname, "rb");
	if (file == NULL) {
		vcam_log("File %s not found", cur->fsname);
		exit(-1);
	}

	size_t start = (size_t)ptp->params[1];
	size_t size = (size_t)ptp->params[2];

	if (fseek(file, start, SEEK_SET) == -1) {
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	char *buffer = malloc(size);
	int read = fread(buffer, 1, size, file); // TODO: check for folder

	ptp_senddata(cam, ptp->code, (unsigned char *)buffer, read);
	vcam_log("Generic sending %d", read);

	free(buffer);
	fclose(file);

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getobjectinfo_write(vcam *cam, ptpcontainer *ptp) {
	struct ptp_dirent *cur;
	unsigned char *data;
	int x = 0;
	uint16_t ofc, thumbofc = 0;
	int thumbwidth = 0, thumbheight = 0, thumbsize = 0;
	int imagewidth = 0, imageheight = 0, imagebitdepth = 0;
	struct tm *tm;
	time_t xtime;
	char xdate[40];

	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	time_t time1;
	time(&time1);

	struct ptp_dirent fake = {
		.id = 0xdeadbeef,
		.name = "test.png",
		.fsname = "bin/test.png",
		.stbuf = {
			.st_mode = 0,
			.st_size = 1234,
			.st_atime = time1,
			.st_mtime = time1,
			.st_ctime = time1,
		},
		.parent = NULL
	};

	// Fake opcode tests
	if (ptp->params[0] == 0xdeadbeef) {
		cur = &fake;
	} else {
		cur = cam->first_dirent;
		while (cur) {
			if (cur->id == ptp->params[0])
				break;
			cur = cur->next;
		}
		if (!cur) {
			vcam_log_func(__func__, "invalid object id 0x%08x", ptp->params[0]);
			ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
			return 1;
		}
	}

	data = malloc(2000);
	x += put_32bit_le(data + x, 0x10000001); /* StorageID */
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
				vcam_log_func(__func__, "pixel x dim format is %d", e->format);
				if (e->format == EXIF_FORMAT_SHORT) {
					imagewidth = exif_get_short(e->data, exif_data_get_byte_order(ed));
				} else if (e->format == EXIF_FORMAT_LONG) {
					imagewidth = exif_get_long(e->data, exif_data_get_byte_order(ed));
				}
			}
			e = exif_data_get_entry(ed, EXIF_TAG_PIXEL_Y_DIMENSION);
			if (e) {
				vcam_log_func(__func__, "pixel y dim format is %d", e->format);
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

	uint32_t compressed_size = cur->stbuf.st_size;

#warning "TODO compressed_size = 0x19000 if fuji is in wifi mode"

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

	ptp_senddata(cam, 0x1008, data, x);
	free(data);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getobject_write(vcam *cam, ptpcontainer *ptp) {
	unsigned char *data;
	struct ptp_dirent *cur;

	if (vcam_check_trans_id(cam, ptp))return 1;
	if (vcam_check_session(cam))return 1;
	if (vcam_check_param_count(cam, ptp, 1))return 1;

	cur = cam->first_dirent;
	while (cur) {
		if (cur->id == ptp->params[0])
			break;
		cur = cur->next;
	}
	if (!cur) {
		vcam_log_func(__func__, "invalid object id 0x%08x", ptp->params[0]);
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

int ptp_getthumb_write(vcam *cam, ptpcontainer *ptp) {
	unsigned char *data;
	struct ptp_dirent *cur;
#ifdef HAVE_LIBEXIF
	ExifData *ed;
#endif

	if (vcam_check_trans_id(cam, ptp))return 1;
	if (vcam_check_session(cam))return 1;
	if (vcam_check_param_count(cam, ptp, 1))return 1;

	vcam_log("Processing thumbnail call for %d\n", ptp->params[0]);

	cur = cam->first_dirent;
	while (cur) {
		if (cur->id == ptp->params[0])
			break;
		cur = cur->next;
	}
	if (!cur) {
		vcam_log_func(__func__, "invalid object id 0x%08x", ptp->params[0]);
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
		vcam_log_func(__func__, "Could not parse EXIF data");
		free(data);
		ptp_response(cam, PTP_RC_NoThumbnailPresent, 0);
		return 1;
	}
	if (!ed->data) {
		vcam_log_func(__func__, "EXIF data does not contain a thumbnail");
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
	vcam_log_func(__func__, "Cannot get thumbnail without libexif, lying about missing thumbnail");
	ptp_response(cam, PTP_RC_NoThumbnailPresent, 0);
#endif

	vcam_log("Done processing thumbnail call\n");

	free(data);
	return 1;
}

int ptp_initiatecapture_write(vcam *cam, ptpcontainer *ptp) {
	struct ptp_dirent *cur, *newcur, *dir, *dcim = NULL;
	int capcnt = 98;
	char buf[10];

	if (vcam_check_trans_id(cam, ptp))return 1;
	if (vcam_check_session(cam))return 1;
	if (vcam_check_param_count(cam, ptp, 2))return 1;

	if ((ptp->params[0] != 0) && (ptp->params[0] != 0x00010001)) {
		vcam_log_func(__func__, "invalid storage id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_StoreNotAvailable, 0);
		return 1;
	}
	if ((ptp->params[1] != 0) && (ptp->params[1] != 0x3801)) {
		vcam_log_func(__func__, "invalid objectformat code id 0x%04x", ptp->params[1]);
		ptp_response(cam, PTP_RC_InvalidObjectFormatCode, 0);
		return 1;
	}
	if (capcnt > 150) {
		vcam_log_func(__func__, "Declaring store full at picture 151");
		ptp_response(cam, PTP_RC_StoreFull, 0);
		return 1;
	}

	/* just search for first jpeg we find in the tree, we will use this to send back as the captured image */
	cur = cam->first_dirent;
	while (cur) {
		if (strstr(cur->name, ".jpg") || strstr(cur->name, ".JPG"))
			break;
		cur = cur->next;
	}
	if (!cur) {
		vcam_log_func(__func__, "I do not have a JPG file in the store, can not proceed");
		ptp_response(cam, PTP_RC_StoreNotAvailable, 0);
		return 1;
	}
	/* identify the DCIM dir, so we can attach a virtual xxxGPHOT directory to virtually store the new picture in */
	dir = cam->first_dirent;
	while (dir) {
		if (!strcmp(dir->name, "DCIM") && dir->parent && !dir->parent->id)
			dcim = dir;
		dir = dir->next;
	}
	/* find the nnnGPHOT directories, where nnn is 100-999. (See DCIM standard.) */
	sprintf(buf, "%03dGPHOT", 100 + ((capcnt / 100) % 900));
	dir = cam->first_dirent;
	while (dir) {
		if (!strcmp(dir->name, buf) && (dir->parent == dcim))
			break;
		dir = dir->next;
	}
	/* if not yet found, create the virtual /DCIM/xxxGPHOT/ directory. */
	if (!dir) {
		dir = malloc(sizeof(struct ptp_dirent));
		dir->id = ++cam->ptp_objectid;
		dir->fsname = "virtual";
		dir->stbuf = dcim->stbuf; /* only the S_ISDIR flag is used */
		dir->parent = dcim;
		dir->next = cam->first_dirent;
		dir->name = strdup(buf);
		cam->first_dirent = dir;
		/* Emit ObjectAdded event for the created folder */
		ptp_inject_interrupt(cam, 80, 0x4002, 1, cam->ptp_objectid, cam->seqnr); /* objectadded */
	}
	if (capcnt++ == 150) {
		/* The start of the operation succeeds, but the memory runs full during it. */
		ptp_inject_interrupt(cam, 100, 0x400A, 1, cam->ptp_objectid, cam->seqnr); /* storefull */
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	newcur = malloc(sizeof(struct ptp_dirent));
	newcur->id = ++cam->ptp_objectid;
	newcur->fsname = strdup(cur->fsname);
	newcur->stbuf = cur->stbuf;
	newcur->parent = dir;
	newcur->next = cam->first_dirent;
	newcur->name = malloc(8 + 3 + 1 + 1);
	sprintf(newcur->name, "GPH_%04d.JPG", capcnt++);
	cam->first_dirent = newcur;

	ptp_inject_interrupt(cam, 100, 0x4002, 1, cam->ptp_objectid, cam->seqnr); /* objectadded */
	ptp_inject_interrupt(cam, 120, 0x400d, 0, 0, cam->seqnr);	     /* capturecomplete */
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_deleteobject_write(vcam *cam, ptpcontainer *ptp) {
	struct ptp_dirent *cur, *xcur;

	if (vcam_check_trans_id(cam, ptp))return 1;
	if (vcam_check_session(cam))return 1;

	if (ptp->nparams < 1) {
		vcam_log_func(__func__, "parameter count %d", ptp->nparams);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	if (ptp->params[0] == 0xffffffff) { /* delete all mode */
		vcam_log_func(__func__, "delete all");
		cur = cam->first_dirent;

		while (cur) {
			xcur = cur->next;
			free_dirent(cur);
			cur = xcur;
		}
		cam->first_dirent = NULL;
		ptp_response(cam, PTP_RC_OK, 0);
		return 1;
	}

	if ((ptp->nparams == 2) && (ptp->params[1] != 0)) {
		vcam_log_func(__func__, "single object delete, but ofc is 0x%08x", ptp->params[1]);
		ptp_response(cam, PTP_RC_InvalidParameter, 0);
		return 1;
	}
	/* for associations this even means recursive deletion */

	cur = cam->first_dirent;
	while (cur) {
		if (cur->id == ptp->params[0])
			break;
		cur = cur->next;
	}
	if (!cur) {
		vcam_log_func(__func__, "invalid object id 0x%08x", ptp->params[0]);
		ptp_response(cam, PTP_RC_InvalidObjectHandle, 0);
		return 1;
	}
	if (S_ISDIR(cur->stbuf.st_mode)) {
		vcam_log_func(__func__, "FIXME: not yet deleting directories");
		ptp_response(cam, PTP_RC_ObjectWriteProtected, 0);
		return 1;
	}
	if (cur == cam->first_dirent) {
		cam->first_dirent = cur->next;
		free_dirent(cur);
	} else {
		xcur = cam->first_dirent;
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

static int put_data(unsigned char *dest, void *data, int length) {
	memcpy(dest, data, length);
	return length;
}

int ptp_getdevicepropdesc_write(vcam *cam, ptpcontainer *ptp) {
	int x = 0;
	unsigned char data[2000];

	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	vcam_log("%s %04x", __func__, ptp->params[0]);

	struct PtpPropDesc *desc = vcam_get_prop_desc(cam, (int)ptp->params[0]);
	if (desc == NULL) {
		vcam_log_func(__func__, "deviceprop 0x%04x not found", ptp->params[0]);
		ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
		return 1;
	}

	if (desc->value == NULL) {
		ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
		return 1;
	}

	if (desc->factory_default_value == NULL) {
		ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
		return 1;
	}

	x += put_16bit_le(data + x, desc->DevicePropertyCode);
	x += put_16bit_le(data + x, desc->DataType);
	x += put_8bit_le(data + x, desc->GetSet);
	x += put_data(data + x, desc->factory_default_value, ptp_get_prop_size(desc->factory_default_value, desc->DataType));
	x += put_data(data + x, desc->value, ptp_get_prop_size(desc->value, desc->DataType));
	x += put_8bit_le(data + x, desc->FormFlag);
	switch (desc->FormFlag) {
	case 0:
		break;
	case 1: /* range */
		x += ptp_copy_prop(data + x, desc->DataType, desc->form_min);
		x += ptp_copy_prop(data + x, desc->DataType, desc->form_max);
		x += ptp_copy_prop(data + x, desc->DataType, desc->form_step);
		break;
	case 2: /* ENUM */
		x += put_16bit_le(data + x, desc->avail_cnt);
		x += ptp_copy_prop_list(data + x, desc->DataType, desc->avail, desc->avail_cnt);
		break;
	}

	ptp_senddata(cam, 0x1014, data, x);
	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_getdevicepropvalue_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct PtpPropDesc *desc = vcam_get_prop_desc(cam, (int)ptp->params[0]);
	if (desc != NULL) {
		if (desc->GetSet == PTP_AC_Invisible) {
			vcam_log_func(__func__, "deviceprop 0x%04x is invisible", ptp->params[0]);
			ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
			return 1;
		}
	}

	int length = 0;
	void *prop_data = vcam_get_prop_data(cam, (int)ptp->params[0], &length);
	if (prop_data == NULL) {
		vcam_log_func(__func__, "deviceprop 0x%04x not found", ptp->params[0]);
		//ptp_senddata(cam, ptp->code, NULL, 0); // for fuji hack
		ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
		return 1;
	}

	ptp_senddata(cam, 0x1015, prop_data, length);

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_setdevicepropvalue_write(vcam *cam, ptpcontainer *ptp) {
	int i;

	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct PtpPropDesc *desc = vcam_get_prop_desc(cam, (int)ptp->params[0]);
	if (desc != NULL) {
		if (desc->GetSet == PTP_AC_Invisible) {
			vcam_log_func(__func__, "deviceprop 0x%04x is invisible", ptp->params[0]);
			ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
			return 1;
		}
	}

	for (i = 0; i < cam->props->length; i++) {
		if (cam->props->handlers[i].code == ptp->params[0])
			break;
	}
	if (i == cam->props->length) {
		vcam_log_func(__func__, "deviceprop 0x%04x not found", ptp->params[0]);
		ptp_response(cam, PTP_RC_DevicePropNotSupported, 0);
		return 1;
	}
	/* so ... we wait for the data phase */
	return 1;
}

int ptp_setdevicepropvalue_write_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam, ptp, 1)) return 1;

	struct PtpPropDesc *desc = vcam_get_prop_desc(cam, (int)ptp->params[0]);
	if (desc == NULL) {
		vcam_log_func(__func__, "deviceprop 0x%04x not found", ptp->params[0]);
		/* we emitted the response already in _write */
		return 1;
	}

	if (desc->GetSet == PTP_AC_Invisible) {
		return 1;
	}

	// TODO: Expand on these warnings
	if (desc->DataType == PTP_TC_UINT32 && len != 4) vcam_log("BUG: Incorrect data sent for uint32");
	if (desc->DataType == PTP_TC_UINT16 && len != 2) vcam_log("BUG: Incorrect data sent for uint16");

	if (len == 0) {
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}

	int rc = vcam_set_prop_data(cam, (int)ptp->params[0], data);
	if (rc) {
		if (rc < 0) {
			ptp_response(cam, PTP_RC_GeneralError, 0);
			return 1;
		}
		ptp_response(cam, rc, 0);
		return 1;
	}

	ptp_response(cam, PTP_RC_OK, 0);
	return 1;
}

int ptp_vusb_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp))return 1;
	if (vcam_check_session(cam))return 1;

	printf(
		"\tRecieved 0xBEEF\n"
		"\tSize: %d\n"
		"\tType: %d\n"
		"\tCode: %X\n"
		"\tseqnr: %d\n",
		ptp->size, ptp->type, ptp->code, ptp->seqnr
	);

	printf("Params (%d): ", ptp->nparams);
	for (int i = 0; i < ptp->nparams; i++) {
		printf("%d ", ptp->params[i]);
	}
	printf("\n");

	return 1;
}

int ptp_vusb_write_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	vcam_log("Recieved data phase for 0xBEEF: %d", len);

	if (ptp->nparams != 1) {
		vcam_log("Expected a checksum parameter");
	}

	int checksum = 0;
	for (int i = 0; i < len; i++) {
		checksum += data[i];
	}

	if (checksum != ptp->params[0]) {
		vcam_log("Invalid checksum %d/%d", checksum, ptp->params[0]);
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	} else {
		vcam_log("Verified checksum %d/%d", checksum, ptp->params[0]);
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

void ptp_register_standard_opcodes(vcam *cam) {
	vcam_register_opcode(cam, 0x1001, ptp_deviceinfo_write, 		NULL);
	vcam_register_opcode(cam, 0x1002, ptp_opensession_write, 		NULL);
	vcam_register_opcode(cam, 0x1003, ptp_closesession_write, 	NULL);
	vcam_register_opcode(cam, 0x1004, ptp_getstorageids_write, 	NULL);
	vcam_register_opcode(cam, 0x1005, ptp_getstorageinfo_write, 	NULL);
	vcam_register_opcode(cam, 0x1006, ptp_getnumobjects_write, 	NULL);
	vcam_register_opcode(cam, 0x1007, ptp_getobjecthandles_write, NULL);
	vcam_register_opcode(cam, 0x1008, ptp_getobjectinfo_write, 	NULL);
	vcam_register_opcode(cam, 0x1009, ptp_getobject_write, 		NULL);
	vcam_register_opcode(cam, 0x100A, ptp_getthumb_write, 		NULL);
	vcam_register_opcode(cam, 0x100B, ptp_deleteobject_write, 	NULL);
	vcam_register_opcode(cam, 0x100E, ptp_initiatecapture_write, 	NULL);
	vcam_register_opcode(cam, 0x1014, ptp_getdevicepropdesc_write, 	NULL);
	vcam_register_opcode(cam, 0x1015, ptp_getdevicepropvalue_write, 	NULL);
	vcam_register_opcode(cam, 0x1016, ptp_setdevicepropvalue_write, 	ptp_setdevicepropvalue_write_data);
	vcam_register_opcode(cam, 0x101B, ptp_getpartialobject_write, NULL);
	vcam_register_opcode(cam, 0xBEEF, ptp_vusb_write, 		ptp_vusb_write_data);
}

static int ptp_write_uint16_array(uint8_t *d, uint16_t *list, int memb_n) {
	int of = 0;
	of += ptp_write_u32(d + of, (uint32_t)memb_n);
	for (int i = 0; i < memb_n; i++) {
		of += ptp_write_u16(d + of, list[0]);
	}
	return of;
}

static int ptp_mtp_obj_props_supported_write(vcam *cam, ptpcontainer *ptp) {
	if (vcam_check_trans_id(cam, ptp)) return 1;
	if (vcam_check_session(cam)) return 1;
	if (vcam_check_param_count(cam , ptp, 1)) return 1;

	uint8_t list_buf[512];
	int of = 0;

	if (ptp->params[0] == PTP_OF_Association) {
		uint16_t list[] = {0xdc01, 0xdc02, 0xdc03, 0xdc04, 0xdc07, 0xdc0b, 0xdc41, 0xdc44, 0xdc08, 0xdc09};
		of += ptp_write_uint16_array(list_buf + of, list, 10);
	} else if (ptp->params[0] == PTP_OF_JPEG || ptp->params[0] == PTP_OF_IMAGE || ptp->params[0] == PTP_OF_RAW) {
		uint16_t list[] = {0xdc01, 0xdc02, 0xdc03, 0xdc04, 0xdc07, 0xdc08, 0xdc0b, 0xdc41, 0xdc44, 0xdc87, 0xdc88, 0xdcd3};
		of += ptp_write_uint16_array(list_buf + of, list, 10);
	} else {
		ptp_response(cam, PTP_RC_InvalidObjectFormatCode, 0);
		return 1;
	}

	ptp_senddata(cam, ptp->code, (void *)list_buf, of);
	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}

#if 0
static int ptp_mtp_obj_props_supported_write_data(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len) {
	vcam_log("Recieved data phase for 0xBEEF: %d", len);

	if (ptp->nparams != 1) {
		vcam_log("Expected a checksum parameter");
	}

	int checksum = 0;
	for (int i = 0; i < len; i++) {
		checksum += data[i];
	}

	if (checksum != ptp->params[0]) {
		vcam_log("Invalid checksum %d/%d", checksum, ptp->params[0]);
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	} else {
		vcam_log("Verified checksum %d/%d", checksum, ptp->params[0]);
	}

	ptp_response(cam, PTP_RC_OK, 0);

	return 1;
}
#endif

void ptp_register_mtp_opcodes(vcam *cam) {
	vcam_register_opcode(cam, PTP_OC_MTP_GetObjectPropsSupported, ptp_mtp_obj_props_supported_write, NULL);
//	vcam_register_opcode(cam, 0x9802, ptp_eos_generic, NULL);
//	vcam_register_opcode(cam, 0x9803, ptp_eos_generic, NULL);
//	vcam_register_opcode(cam, 0x9804, ptp_eos_generic, NULL);
//	vcam_register_opcode(cam, 0x9805, ptp_eos_generic, NULL);
}
