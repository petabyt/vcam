// Camlib data packing/reading API
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vcam.h>

#define ptp_panic vcam_panic

int ptp_get_prop_size(uint8_t *d, int type) {
	uint32_t length32;
	uint8_t length8;
	switch (type) {
	case PTP_TC_INT8:
	case PTP_TC_UINT8:
		return 1;
	case PTP_TC_INT16:
	case PTP_TC_UINT16:
		return 2;
	case PTP_TC_INT32:
	case PTP_TC_UINT32:
		return 4;
	case PTP_TC_INT64:
	case PTP_TC_UINT64:
		return 8;
	case PTP_TC_UINT8ARRAY:
		ptp_read_u32(d, &length32);
		return 4 + ((int)length32 * 1);
	case PTP_TC_UINT16ARRAY:
		ptp_read_u32(d, &length32);
		return 4 + ((int)length32 * 2);
	case PTP_TC_UINT32ARRAY:
		ptp_read_u32(d, &length32);
		return 4 + ((int)length32 * 4);
	case PTP_TC_UINT64ARRAY:
		ptp_read_u32(d, &length32);
		return 4 + ((int)length32 * 8);
	case PTP_TC_STRING:
		ptp_read_u8(d, &length8);
		return 1 + ((int)length8 * 2);
	}

	vcam_log_func(__func__, "unhandled datatype %x", type);
	abort();
	return 0;
}

int ptp_copy_prop(uint8_t *dest, uint16_t type, uint8_t *data) {
	int size = ptp_get_prop_size(data, type);
	memcpy(dest, data, size);
	return size;
}

int ptp_get_prop_list_size(uint8_t *data, int type, int cnt) {
	int of = 0;
	for (int i = 0; i < cnt; i++) {
		int size = ptp_get_prop_size(data + of, type);
		of += size;
	}
	return of;
}

int ptp_copy_prop_list(uint8_t *dest, uint16_t type, uint8_t *data, int len) {
	int of = 0;
	for (int i = 0; i < len; i++) {
		int size = ptp_get_prop_size(data + of, type);
		memcpy(dest + of, data + of, size);
		of += size;
	}
	return of;
}

int ptp_prop_list_size(uint16_t type, uint8_t *data, int len) {
	int of = 0;
	for (int i = 0; i < len; i++) {
		int size = ptp_get_prop_size(data + of , type);
		of += size;
	}
	return of;
}

int ptp_read_utf8_string(void *dat, char *string, int max) {
	char *d = (char *)dat;
	int x = 0;
	while (d[x] != '\0') {
		string[x] = d[x];
		x++;
		if (x > max - 1) break;
	}

	string[x] = '\0';
	x++;

	return x;
}

// Read standard UTF16 string
int ptp_read_string(uint8_t *d, char *string, int max) {
	int of = 0;
	uint8_t length;
	of += ptp_read_u8(d + of, &length);

	uint8_t i = 0;
	while (i < length) {
		uint16_t wchr;
		of += ptp_read_u16(d + of, &wchr);
		if (wchr > 128) wchr = '?';
		else if (wchr != '\0' && wchr < 32) wchr = ' ';
		string[i] = (char)wchr;
		i++;
		if (i >= max) break;
	}

	string[i] = '\0';

	return of;
}

int ptp_read_uint16_array(const uint8_t *dat, uint16_t *buf, int max, int *length) {
	int of = 0;

	uint32_t n;
	of += ptp_read_u32(dat + of, &n);

	for (uint32_t i = 0; i < n; i++) {
		if (i >= max) {
			ptp_panic("ptp_read_uint16_array overflow\n");
		} else {
			of += ptp_read_u16(buf + of, &buf[i]);
		}
	}

	return of;
}

// Write standard PTP wchar string
int ptp_write_string(uint8_t *dat, const char *string) {
	int of = 0;

	uint32_t length = strlen(string);
	of += ptp_write_u8(dat + of, length);

	for (int i = 0; i < length; i++) {
		of += ptp_write_u8(dat + of, string[i]);
		of += ptp_write_u8(dat + of, '\0');
	}

	return of;
}

// Write normal UTF-8 string
int ptp_write_utf8_string(void *dat, const char *string) {
	char *o = (char *)dat;
	int x = 0;
	while (string[x] != '\0') {
		o[x] = string[x];
		x++;
	}

	o[x] = '\0';
	x++;
	return x;
}

// Write null-terminated UTF16 string
int ptp_write_unicode_string(char *dat, const char *string) {
	int i;
	for (i = 0; string[i] != '\0'; i++) {
		dat[i * 2] = string[i];
		dat[i * 2 + 1] = '\0';
	}
	dat[i * 2 + 1] = '\0';
	return i;
}

// Read null-terminated UTF16 string
int ptp_read_unicode_string(char *buffer, char *dat, int max) {
	int i;
	for (i = 0; dat[i] != '\0'; i += 2) {
		buffer[i / 2] = dat[i];
		if (i >= max) {
			buffer[(i / 2) + 1] = '\0';
			return i;
		}
	}

	buffer[(i / 2)] = '\0';
	return i / 2;
}

// gPhoto2 API

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
	return (int8_t)data[0];
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
		printf("put_string: %s\n", "string length is longer than 255 characters");

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
