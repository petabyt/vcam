// Camlib data packing/reading API
#include <stdint.h>
#include <string.h>

int ptp_write_u32(void *dat, uint32_t v) {
	((uint32_t *)dat)[0] = v;
	return 4;
}

int ptp_write_u16(void *dat, uint16_t v) {
	((uint16_t *)dat)[0] = v;
	return 2;
}

int ptp_write_u8(void *dat, uint8_t v) {
	((uint8_t *)dat)[0] = v;
	return 1;
}

int ptp_read_u8(void *dat, uint8_t *buf) {
	(*buf) = ((uint8_t *)dat)[0];
	return 1;
}

int ptp_read_u16(void *dat, uint16_t *buf) {
	(*buf) = ((uint16_t *)dat)[0];
	return 2;
}

int ptp_read_u32(void *dat, uint32_t *buf) {
	(*buf) = ((uint32_t *)dat)[0];
	return 4;
}

uint8_t ptp_read_uint8(void *dat) {
	uint8_t **p = (uint8_t **)dat;
	uint8_t x = (**p);
	(*p)++;
	return x;
}

uint16_t ptp_read_uint16(void **dat) {
	uint16_t **p = (uint16_t **)dat;
	uint16_t x = (**p);
	(*p)++;
	return x;
}

uint32_t ptp_read_uint32(void **dat) {
	uint32_t **p = (uint32_t **)dat;
	uint32_t x = (**p);
	(*p)++;
	return x;
}

// Read standard UTF16 string
void ptp_read_string(void **dat, char *string, int max) {
	uint8_t **p = (uint8_t **)dat;
	int length = (int)ptp_read_uint8((void **)p);

	int y = 0;
	while (y < length) {
		string[y] = (char)(**p);
		(*p) += 2;
		y++;
		if (y >= max) { break; }
	}

	string[y] = '\0';
}

int ptp_read_uint16_array(void **dat, uint16_t *buf, int max) {
	int n = ptp_read_uint32((void **)dat);

	for (int i = 0; i < n; i++) {
		if (i >= max) {
			(void)ptp_read_uint16((void **)dat);
		} else {
			buf[i] = ptp_read_uint16((void **)dat);
		}
	}

	return n;
}

void ptp_write_uint8(void **dat, uint8_t b) {
	uint8_t **ptr = (uint8_t **)dat;
	(**ptr) = b;
	(*ptr)++;
}

int ptp_write_uint32(void **dat, uint32_t b) {
	uint32_t **ptr = (uint32_t **)dat;
	(**ptr) = b;
	(*ptr)++;

	return 4;
}

int ptp_write_string(void **dat, char *string) {
	int length = strlen(string);
	ptp_write_uint8(dat, length);

	for (int i = 0; i < length; i++) {
		ptp_write_uint8(dat, string[i]);
		ptp_write_uint8(dat, '\0');
	}

	ptp_write_uint8(dat, '\0');

	return (length * 2) + 2;
}

int ptp_write_utf8_string(void **dat, char *string) {
	for (int i = 0; string[i] != '\0'; i++) {
		ptp_write_uint8(dat, string[i]);
	}

	ptp_write_uint8(dat, '\0');

	return strlen(string) + 1;
}

// Write null-terminated UTF16 string
int ptp_write_unicode_string(char *dat, char *string) {
	int i;
	for (i = 0; string[i] != '\0'; i++) {
		dat[i * 2] = string[i];
		dat[i * 2 + 1] = '\0';
	}
	dat[i * 2 + 1] = '\0';
	return i;
}

// Reaed null-terminated UTF16 string
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

// Read null terminated UTF8 string
void ptp_read_utf8_string(void **dat, char *string, int max) {
	uint8_t **p = (uint8_t **)dat;

	int y = 0;
	while ((char)(**p) != '\0') {
		string[y] = (char)(**p);
		(*p)++;
		y++;
		if (y >= max) { break; }
	}

	(*p)++;
	string[y] = '\0';
}
