// Copyright (c) 2015,2016 Marcus Meissner <marcus@jet.franken.de> - GNU General Public License v2
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <vcam.h>
#include <ops.h>

// Event handler
struct ptp_interrupt *first_interrupt = NULL;

// Filesystem index
struct ptp_dirent *first_dirent = NULL;
uint32_t ptp_objectid = 0;

char *vcamera_filesystem = "fuji_sd";

void vcam_dump(void *ptr, size_t len) {
	FILE *f = fopen("DUMP", "wb");
	fwrite(ptr, len, 1, f);
	fclose(f);
}

int vcam_generic_send_file(char *path, vcam *cam, ptpcontainer *ptp) {
	char new[64];
	sprintf(new, "%s/%s", PWD, path);
	FILE *file = fopen(new, "rb");
	if (file == NULL) {
		vcam_log("vcam_generic_send_file: File %s not found\n", path);
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

void vcam_set_prop_avail(vcam *cam, int code, int size, int cnt, void *data) {
	struct PtpPropList *list = NULL;
	for (list = cam->list; list->next != NULL; list = list->next) {
		if (list->code == code) break;
	}
	
	if (list == NULL) {
		printf("vcam_set_prop_avail: Can't find prop\n");
		exit(1);
	}

	// Realloc when list changes
	void *dup = NULL;
	if (list->avail == NULL) {
		dup = malloc(size * cnt);
		memcpy(dup, data, size * cnt);
	} else {
		dup = realloc(list->avail, size * cnt);
	}

	list->avail = dup;
	list->avail_size = size;
	list->avail_cnt = cnt;
}

void vcam_set_prop(vcam *cam, int code, uint32_t value) {
	cam->list_tail->code = code;
	cam->list_tail->data = malloc(sizeof(uint32_t));
	cam->list_tail->length = sizeof(uint32_t);
	memcpy(cam->list_tail->data, &value, sizeof(uint32_t));
	cam->list_tail->next = calloc(1, sizeof(struct PtpPropList));

	cam->list_tail = cam->list_tail->next;
}

void vcam_set_prop_data(vcam *cam, int code, void *data, int length) {
	cam->list_tail->code = code;
	cam->list_tail->data = malloc(length);
	cam->list_tail->length = length;
	memcpy(cam->list_tail->data, data, length);
	cam->list_tail->next = calloc(1, sizeof(struct PtpPropList));

	cam->list_tail = cam->list_tail->next;
}

void ptp_senddata(vcam *cam, uint16_t code, unsigned char *data, int bytes) {
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

void ptp_response(vcam *cam, uint16_t code, int nparams, ...) {
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
// Perhaps vcam.c should be moved to camlibs/ptp2 for easier sharing
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

int vcam_init(vcam *cam) {
	return GP_OK;
}

int vcam_exit(vcam *cam) {
	return GP_OK;
}

int vcam_open(vcam *cam, const char *port) {
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

int vcam_close(vcam *cam) {
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

static long get_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)(ts.tv_sec * 1000000L + ts.tv_nsec / 1000L);
}

static void hexdump(void *buffer, int size) {
	unsigned char *buf = (unsigned char *)buffer;
	for (int i = 0; i < size; i++) {
		printf("%02x ", buf[i]);
		if ((i + 1) % 16 == 0) {
			printf("\n");
		}
	}
	printf("\n");
}


void vcam_process_output(vcam *cam) {
	ptpcontainer ptp;
	int i, j;

	if (cam->next_cmd_kills_connection) {
		gp_log_("Killing connection\n");
		exit(0);
	}

	long now = get_ms();
	int milis_since_last = (int)(now - cam->last_cmd_timestamp);
	cam->last_cmd_timestamp = get_ms();

	if (cam->nroutbulk < 4)
		return; /* wait for more data */

	ptp.size = get_32bit_le(cam->outbulk);
	if (ptp.size > cam->nroutbulk)
		return; /* wait for more data */

	if (ptp.size < 12) { /* No ptp command can be less than 12 bytes */
		/* not clear if normal cameras react like this */
		gp_log(GP_LOG_ERROR, __FUNCTION__, "input size was %d, minimum is 12", ptp.size);

		hexdump(cam->outbulk, ptp.size);

		// Does this work on PTP/IP?
		ptp_response(cam, PTP_RC_GeneralError, 0);
		memmove(cam->outbulk, cam->outbulk + ptp.size, cam->nroutbulk - ptp.size);
		cam->nroutbulk -= ptp.size;
		return;
	}

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
		gp_log_("Time since last command: %dms\n", milis_since_last / 1000);
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

int vcam_read(vcam *cam, int ep, unsigned char *data, int bytes) {
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

int vcam_write(vcam *cam, int ep, const unsigned char *data, int bytes) {
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

int ptp_notify_event(vcam *cam, uint16_t code, uint32_t value) {
	return ptp_inject_interrupt(cam, 1000, code, 1, value, 0);
}

int ptp_inject_interrupt(vcam *cam, int when, uint16_t code, int nparams, uint32_t param1, uint32_t transid) {
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

int ptp_pop_event(vcam *cam, struct GenericEvent *ev) {
	// first_interrupt is allowed to be NULL, will produce timeout (no events)
	if (first_interrupt == NULL) return 1;

	memcpy(ev, first_interrupt->data, sizeof(struct GenericEvent));

	struct ptp_interrupt *prev = first_interrupt;
	first_interrupt = first_interrupt->next;
	free(prev->data);
	free(prev);

	return 0;
}

// Reads ints into 'data' with max 'bytes'
int vcam_readint(vcam *cam, unsigned char *data, int bytes, int timeout) {
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

vcam *vcamera_new(vcameratype type) {
	vcam *cam;

	cam = calloc(1, sizeof(vcam));
	if (!cam)
		return NULL;

	cam->conf = NULL;

	read_tree(vcamera_filesystem);

	// Property linked list
	cam->list_tail = calloc(1, sizeof(struct PtpPropList));
	cam->list = cam->list_tail;

	cam->type = type;
	cam->seqnr = 0;

	cam->last_cmd_timestamp = 0;

	return cam;
}
