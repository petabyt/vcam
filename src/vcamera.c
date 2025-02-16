// Copyright (c) 2015,2016 Marcus Meissner <marcus@jet.franken.de> - GNU General Public License v2
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <usbthing.h>
#include "vcam.h"

void vcam_dump(void *ptr, size_t len) {
	FILE *f = fopen("DUMP", "wb");
	fwrite(ptr, len, 1, f);
	fclose(f);
}

int vcam_check_session(vcam *cam) {
	if (!cam->session) {
		vcam_log_func(__func__, "session is not open");
		ptp_response(cam, PTP_RC_SessionNotOpen, 0);
		return 1;
	}
	return 0;
}

int vcam_check_trans_id(vcam *cam, ptpcontainer *ptp) {
//	if (ptp->seqnr != cam->seqnr) {
//		gp_log(GP_LOG_ERROR, __FUNCTION__, "seqnr %d was sent, expected was %d", ptp->seqnr, cam->seqnr);
//		ptp_response(cam, PTP_RC_GeneralError, 0);
//		return 1;
//	}
	return 0;
}

int vcam_check_param_count(vcam *cam, ptpcontainer *ptp, int n) {
	if (ptp->nparams != n) {
		vcam_log_func(__func__, "%X: params should be %d, but is %d", ptp->code, n, ptp->nparams);
		ptp_response(cam, PTP_RC_GeneralError, 0);
		return 1;
	}
	return 0;
}

int vcam_generic_send_file(char *path, vcam *cam, int file_of, ptpcontainer *ptp) {
	char new[64];
	sprintf(new, "%s/%s", PWD, path);
	FILE *file = fopen(new, "rb");
	if (file == NULL) {
		vcam_panic("vcam_generic_send_file: File %s not found", path);
	}

	fseek(file, 0, SEEK_END);
	long file_size = ftell(file);
	fseek(file, file_of, SEEK_SET);

	char *buffer = malloc(file_size);
	fread(buffer, 1, file_size, file);

	ptp_senddata(cam, ptp->code, (unsigned char *)buffer, file_size);
	free(buffer);
	vcam_log("Generic sending %d", file_size);

	fclose(file);

	return 0;
}

int vcam_register_opcode(vcam *cam, int code, int (*write)(vcam *cam, ptpcontainer *ptp), int (*write_data)(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int size)) {
	for (int i = 0; i < cam->opcodes->length; i++) {
		struct PtpOpcode *c = &cam->opcodes->handlers[i];
		if (c->code == code) {
			c->write = write;
			c->write_data = write_data;
			return 0;
		}
	}

	cam->opcodes = realloc(cam->opcodes, sizeof(struct PtpOpcodeList) + (sizeof(struct PtpOpcode) * (cam->opcodes->length + 1)));

	struct PtpOpcode *c = &cam->opcodes->handlers[cam->opcodes->length];
	memset(c, 0, sizeof(struct PtpOpcode));
	c->code = code;
	c->write = write;
	c->write_data = write_data;

	cam->opcodes->length += 1;
	return 0;
}

// TODO: Add a 'void *param' parameter that will be passed to handlers
int vcam_register_prop_handlers(vcam *cam, int code, struct PtpPropDesc *desc, ptp_prop_getvalue *getvalue, ptp_prop_setvalue *setvalue) {
	cam->props = realloc(cam->props, sizeof(struct PtpPropList) + (sizeof(struct PtpProp) * (cam->props->length + 1)));

	struct PtpProp *prop = &cam->props->handlers[cam->props->length];
	memset(prop, 0, sizeof(struct PtpProp));
	prop->code = code;
	memcpy(&prop->desc, desc, sizeof(struct PtpPropDesc));
	prop->getvalue = getvalue;
	prop->setvalue = setvalue;

	cam->props->length += 1;
	return 0;
}

int vcam_register_prop(vcam *cam, int code, struct PtpPropDesc *desc) {
	struct PtpProp *prop = NULL;
	for (int i = 0; i < cam->props->length; i++) {
		prop = &cam->props->handlers[i];
		if (prop->code == code) break;
		prop = NULL;
	}
	if (prop == NULL) {
		cam->props = realloc(cam->props, sizeof(struct PtpPropList) + (sizeof(struct PtpProp) * (cam->props->length + 1)));
		prop = &cam->props->handlers[cam->props->length];
	}

	memset(prop, 0, sizeof(struct PtpProp));
	prop->code = code;
	prop->getdesc = NULL;
	prop->getvalue = NULL;
	prop->setvalue = NULL;

	memcpy(&prop->desc, desc, sizeof(struct PtpPropDesc));

	cam->props->length += 1;
	return 0;
}

int vcam_set_prop_data(vcam *cam, int code, void *data, int length) {
	struct PtpProp *prop = NULL;
	for (int i = 0; i < cam->props->length; i++) {
		prop = &cam->props->handlers[i];
		if (prop->code == code) break;
		prop = NULL;
	}
	if (prop == NULL) return -1;
	if (prop->setvalue) {
		return prop->setvalue(cam, &prop->desc, data);
	}
	if (prop->desc.value != NULL)
		free(prop->desc.value);
	prop->desc.value = malloc(length);
	memcpy(prop->desc.value, data, length);
	return 0;
}

int vcam_set_prop(vcam *cam, int code, uint32_t data) {
	return vcam_set_prop_data(cam, code, &data, 4);
}

int vcam_get_prop_size(vcam *cam, int code) {
	struct PtpProp *prop = NULL;
	for (int i = 0; i < cam->props->length; i++) {
		prop = &cam->props->handlers[i];
		if (prop->code == code) break;
		prop = NULL;
	}
	if (prop == NULL) return -1;
	if (prop->desc.DataType == PTP_TC_UNDEF) {
		return prop->desc.value_length;
	}
	return ptp_get_prop_size(prop->desc.value, prop->desc.DataType);
}

struct PtpPropDesc *vcam_get_prop_desc(vcam *cam, int code) {
	struct PtpProp *prop = NULL;
	for (int i = 0; i < cam->props->length; i++) {
		prop = &cam->props->handlers[i];
		if (prop->code == code) break;
		prop = NULL;
	}
	if (prop == NULL) return NULL;
	if (prop->getdesc) {
		prop->getdesc(cam, &prop->desc);
	}
	return &prop->desc;
}

void *vcam_get_prop_data(vcam *cam, int code, int *length) {
	struct PtpProp *prop = NULL;
	for (int i = 0; i < cam->props->length; i++) {
		prop = &cam->props->handlers[i];
		if (prop->code == code) break;
		prop = NULL;
	}
	if (prop == NULL) return NULL;
	int optional_len = -1;
	if (prop->getvalue) {
		prop->getvalue(cam, &prop->desc, &optional_len);
	}
	if (length != NULL) {
		if (optional_len != -1) {
			(*length) = optional_len;
		} else {
			if (prop->desc.DataType == PTP_TC_UNDEF) {
				(*length) = prop->desc.value_length;
			} else {
				(*length) = ptp_get_prop_size(prop->desc.value, prop->desc.DataType);
			}
		}
	}
	return prop->desc.value;
}

int vcam_set_prop_avail(vcam *cam, int code, void *list, int cnt) {
	struct PtpProp *prop = NULL;
	for (int i = 0; i < cam->props->length; i++) {
		prop = &cam->props->handlers[i];
		if (prop->code == code) break;
		prop = NULL;
	}
	if (prop == NULL) {
		vcam_log("WARN: %s %04x prop that doesn't exist", __func__, code);
		return -1;
	}
	prop->desc.FormFlag = PTP_EnumerationForm; // Should this function set it or check it?
	int size = ptp_prop_list_size(prop->desc.DataType, list, cnt);
	free(prop->desc.avail);
	if (size == 0) abort();
	prop->desc.avail = malloc(size);
	memcpy(prop->desc.avail, list, size);
	prop->desc.avail_cnt = cnt;
	return 0;
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
	if (bytes)
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

void *read_file(struct ptp_dirent *cur) {
	FILE *file = fopen(cur->fsname, "rb");
	if (!file) {
		vcam_log_func(__func__, "could not open %s", cur->fsname);
		return NULL;
	}
	void *data = malloc(cur->stbuf.st_size);
	if (!data) {
		vcam_log_func(__func__, "could not allocate data for %s", cur->fsname);
		return NULL;
	}
	if (!fread(data, cur->stbuf.st_size, 1, file)) {
		vcam_log_func(__func__, "could not read data of %s", cur->fsname);
		free(data);
		data = NULL;
	}
	fclose(file);
	return data;
}

int ptp_get_object_count(vcam *cam) {
	int cnt = 0;
	struct ptp_dirent *cur = cam->first_dirent;
	while (cur) {
		if (cur->id) { /* do not include 0 entry */
			cnt++;
		}
		cur = cur->next;
	}
	return cnt;
}

void read_directories(vcam *cam, const char *path, struct ptp_dirent *parent) {
	struct ptp_dirent *cur;
	struct dirent *de;

	DIR *dir = opendir(path);
	if (!dir)
		return;
	while ((de = readdir(dir))) {
		if (!strcmp(de->d_name, "."))
			continue;
		if (!strcmp(de->d_name, ".."))
			continue;

		cur = malloc(sizeof(struct ptp_dirent));
		if (!cur)
			break;
		cur->name = strdup(de->d_name);
		cur->fsname = malloc(strlen(path) + 1 + strlen(de->d_name) + 1);
		strcpy(cur->fsname, path);
		strcat(cur->fsname, "/");
		strcat(cur->fsname, de->d_name);
		//gp_log_("Found filename: %s\n", cur->fsname);
		cur->id = cam->ptp_objectid++;
		cur->next = cam->first_dirent;
		cur->parent = parent;
		cam->first_dirent = cur;
		if (-1 == stat(cur->fsname, &cur->stbuf))
			continue;
		if (S_ISDIR(cur->stbuf.st_mode))
			read_directories(cam, cur->fsname, cur); /* recurse! */
	}
	closedir(dir);
}

int vcam_get_object_count(vcam *cam) {
	int cnt = 0;
	struct ptp_dirent *cur = cam->first_dirent;
	while (cur) {
		if (cur->id) { /* do not include 0 entry */
			cnt++;
		}
		cur = cur->next;
	}

	return cnt;
}

void free_dirent(struct ptp_dirent *ent) {
	free(ent->name);
	free(ent->fsname);
	free(ent);
}

static void read_tree(vcam *cam, const char *path) {
	struct ptp_dirent *root = NULL, *dir, *dcim = NULL;

	if (cam->first_dirent) {
		vcam_log("read_tree called twice, memory leaked");
	}

	cam->first_dirent = malloc(sizeof(struct ptp_dirent));
	cam->first_dirent->name = strdup("");
	cam->first_dirent->fsname = strdup(path);
	cam->first_dirent->id = cam->ptp_objectid++;
	cam->first_dirent->next = NULL;
	stat(cam->first_dirent->fsname, &cam->first_dirent->stbuf); /* assuming it works */
	root = cam->first_dirent;
	read_directories(cam, path, cam->first_dirent);

	/* See if we have a DCIM directory, if not, create one. */
	dir = cam->first_dirent;
	while (dir) {
		if (!strcmp(dir->name, "DCIM") && dir->parent && !dir->parent->id)
			dcim = dir;
		dir = dir->next;
	}
	if (!dcim) {
		dcim = malloc(sizeof(struct ptp_dirent));
		dcim->name = strdup("DCIM");
		dcim->fsname = strdup(path);
		dcim->id = cam->ptp_objectid++;
		dcim->next = cam->first_dirent;
		dcim->parent = root;
		stat(dcim->fsname, &dcim->stbuf); /* assuming it works */
		cam->first_dirent = dcim;
	}
}

int vcam_init(vcam *cam) {
	return 0;
}

int vcam_exit(vcam *cam) {
	return 0;
}

int vcam_close(vcam *cam) {
	free(cam->inbulk);
	free(cam->outbulk);
	free(cam->props);
	free(cam->opcodes);
	return 0;
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
		vcam_log("Killing connection");
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
		vcam_log_func(__func__, "input size was %d, minimum is 12", ptp.size);
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
		vcam_log_func(__func__, "expected CMD or DATA, but type was %d", ptp.type);
		ptp_response(cam, PTP_RC_GeneralError, 0);
		memmove(cam->outbulk, cam->outbulk + ptp.size, cam->nroutbulk - ptp.size);
		cam->nroutbulk -= (int)ptp.size;
		return;
	}

	// Allow our special BEEF code for testing
	if ((ptp.code & 0x7000) != 0x1000 && ptp.code != 0xBEEF) {
		/* not clear if normal cameras react like this */
		vcam_log_func(__func__, "OPCODE 0x%04x does not start with 0x1 or 0x9", ptp.code);
		ptp_response(cam, PTP_RC_GeneralError, 0);
		memmove(cam->outbulk, cam->outbulk + ptp.size, cam->nroutbulk - ptp.size);
		cam->nroutbulk -= (int)ptp.size;
		return;
	}

	if (ptp.type == PTP_PACKET_TYPE_COMMAND) {
		if ((ptp.size - 12) % 4) {
			/* not clear if normal cameras react like this */
			vcam_log_func(__func__, "SIZE-12 is not divisible by 4, but is %d", ptp.size - 12);
			ptp_response(cam, PTP_RC_GeneralError, 0);
			memmove(cam->outbulk, cam->outbulk + ptp.size, cam->nroutbulk - ptp.size);
			cam->nroutbulk -= ptp.size;
			return;
		}

		if ((ptp.size - 12) / 4 >= 6) {
			/* not clear if normal cameras react like this */
			vcam_log_func(__func__, "(SIZE-12)/4 is %d, exceeds maximum arguments", (ptp.size - 12) / 4);
			ptp_response(cam, PTP_RC_GeneralError, 0);
			memmove(cam->outbulk, cam->outbulk + ptp.size, cam->nroutbulk - ptp.size);
			cam->nroutbulk -= ptp.size;
			return;
		}

		ptp.nparams = (ptp.size - 12) / 4;
		for (i = 0; i < ptp.nparams; i++) {
			ptp.params[i] = get_32bit_le(cam->outbulk + 12 + i * 4);
		}
		if (ptp.nparams == 0) {
			vcam_log("Request phase 0x%X (0 params)", ptp.code);
		} else if (ptp.nparams == 1) {
			vcam_log("Request phase 0x%X (%x)", ptp.code, ptp.params[0]);
		} else if (ptp.nparams == 2) {
			vcam_log("Request phase 0x%X (%x, %x)", ptp.code, ptp.params[0], ptp.params[1]);
		} else {
			vcam_log("Request phase 0x%X (%d params)", ptp.code, ptp.nparams);
		}
		vcam_log("Time since last command: %dms", milis_since_last / 1000);
	}

	// We have read the first packet, discard it
	cam->nroutbulk -= ptp.size;

	/* call the opcode handler */
	for (i = 0; i < cam->opcodes->length; i++) {
		struct PtpOpcode *h = &cam->opcodes->handlers[i];
		if (h->code == ptp.code) {
			if (ptp.type == 1) {
				h->write(cam, &ptp);
				memcpy(&cam->ptpcmd, &ptp, sizeof(ptp));
			} else {
				if (h->write_data == NULL) {
					vcam_log_func(__func__, "opcode 0x%04x received with dataphase, but no dataphase expected", ptp.code);
					ptp_response(cam, PTP_RC_GeneralError, 0);
				} else {
					h->write_data(cam, &cam->ptpcmd, cam->outbulk + 12, ptp.size - 12);
				}
			}
			return;
		}
	}

	vcam_log_func(__func__, "received an unsupported opcode 0x%04x", ptp.code);
	ptp_response(cam, PTP_RC_OperationNotSupported, 0);
}

int vcam_read(vcam *cam, int ep, unsigned char *data, int bytes) {
	(void)ep;
	int toread = bytes;

	if (toread > cam->nrinbulk)
		toread = cam->nrinbulk;

	if (cam->comm_dump) {
		fwrite(cam->inbulk, 1, toread, cam->comm_dump);
		fflush(cam->comm_dump);
	}


	memcpy(data, cam->inbulk, toread);
	memmove(cam->inbulk, cam->inbulk + toread, (cam->nrinbulk - toread));
	cam->nrinbulk -= toread;
	return toread;
}

int vcam_write(vcam *cam, int ep, const unsigned char *data, int bytes) {
	(void)ep;
	if (cam->comm_dump) {
		fwrite(data, 1, bytes, cam->comm_dump);
		fflush(cam->comm_dump);
	}

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

	vcam_log_func(__func__, "generate interrupt 0x%04x, %d params, param1 0x%08x, timeout=%d", code, nparams, param1, when);

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
	pint = &cam->first_interrupt;
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
	if (cam->first_interrupt == NULL) return 1;

	memcpy(ev, cam->first_interrupt->data, sizeof(struct GenericEvent));

	struct ptp_interrupt *prev = cam->first_interrupt;
	cam->first_interrupt = cam->first_interrupt->next;
	free(prev->data);
	free(prev);

	return 0;
}

// Reads ints into 'data' with max 'bytes'
int vcam_readint(vcam *cam, unsigned char *data, int bytes, int timeout) {
	struct timeval now, end;
	int newtimeout, tocopy;
	struct ptp_interrupt *pint;

	if (!cam->first_interrupt) {
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
	if (cam->first_interrupt->triggertime.tv_sec > end.tv_sec) {
		return GP_ERROR_TIMEOUT;
	}
	if ((cam->first_interrupt->triggertime.tv_sec == end.tv_sec) &&
	    (cam->first_interrupt->triggertime.tv_usec > end.tv_usec)) {
		return GP_ERROR_TIMEOUT;
	}
	newtimeout = (cam->first_interrupt->triggertime.tv_sec - now.tv_sec) * 1000 + (cam->first_interrupt->triggertime.tv_usec - now.tv_usec) / 1000;
	if (newtimeout > timeout)
		vcam_log_func(__func__, "miscalculated? %d vs %d", timeout, newtimeout);
	tocopy = cam->first_interrupt->size;
	if (tocopy > bytes)
		tocopy = bytes;
	memcpy(data, cam->first_interrupt->data, tocopy);
	pint = cam->first_interrupt;
	cam->first_interrupt = cam->first_interrupt->next;
	free(pint->data);
	free(pint);
	return tocopy;
}

int vcam_parse_args(vcam *cam, int argc, const char **argv, int *i) {
	if (!strcmp(argv[(*i)], "--ip")) {
		(*i)++;
		cam->custom_ip_addr = strdup(argv[(*i)]);
	} else if (!strcmp(argv[(*i)], "--local-ip")) {
		cam->custom_ip_addr = malloc(64);
		get_local_ip(cam->custom_ip_addr);
	} else if (!strcmp(argv[(*i)], "--fs")) {
		cam->vcamera_filesystem = argv[(*i) + 1];
		(*i)++;
		read_tree(cam, cam->vcamera_filesystem);
	} else if (!strcmp(argv[(*i)], "--dump")) {
		cam->comm_dump = fopen("COMM_DUMP", "wb");
	} else if (!strcmp(argv[(*i)], "--sig")) {
		(*i)++;
		cam->sig = atoi(argv[(*i)]);
	} else {
		return 0;
	}
	return 1;
}

vcam *vcam_init_standard(void) {
	vcam *cam = calloc(1, sizeof(vcam));
	if (!cam) abort();

	cam->vcamera_filesystem = PWD "/bin/card";

	read_tree(cam, cam->vcamera_filesystem);

	cam->props = calloc(1, sizeof(struct PtpPropList));
	cam->opcodes = calloc(1, sizeof(struct PtpOpcodeList));
	cam->seqnr = 0;

	cam->last_cmd_timestamp = 0;

	// blah blah
	cam->battery = 50;
	cam->exposurebias = 123;

	ptp_register_standard_opcodes(cam);
	ptp_register_standard_props(cam);

	return cam;
}

int vcam_main(vcam *cam, const char *name, enum CamBackendType backend, int argc, const char **argv) {
	if (fuji_init_cam(cam, name, argc, argv) == 0) {
		if (backend == VCAM_TCP) {
			int rc = fuji_wifi_main(cam);
			vcam_close(cam);
			return rc;
		}
	} else if (canon_init_cam(cam, name, argc, argv) == 0) {
		if (backend == VCAM_TCP) {
			int rc = ptpip_generic_main(cam);
			vcam_close(cam);
			return rc;
		}
	} else {
		vcam_log("Invalid camera '%s'", name);
		return -1;
	}

	if (backend == VCAM_VHCI) {
		return vcam_start_usbthing(cam, backend);
	} else if (backend == VCAM_LIBUSB) {
		return 0;
	}

	vcam_log("Unsupported backend");

	return -1;
}

vcam *vcam_new(const char *name) {
	vcam *cam = vcam_init_standard();
	int rc = vcam_main(cam, name, VCAM_LIBUSB, 0, NULL);
	if (rc) return NULL;
	return cam;
}
