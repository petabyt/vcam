#pragma once

// vcam data structure API
int put_32bit_le_array(unsigned char *data, uint32_t *arr, int cnt);
int put_16bit_le_array(unsigned char *data, uint16_t *arr, int cnt);
char *get_string(unsigned char *data);
int put_string(unsigned char *data, char *str);
int put_8bit_le(unsigned char *data, uint8_t x);
int put_16bit_le(unsigned char *data, uint16_t x);
int put_32bit_le(unsigned char *data, uint32_t x);
int put_64bit_le(unsigned char *data, uint64_t x);
int8_t get_i8bit_le(unsigned char *data);
uint8_t get_8bit_le(unsigned char *data);
uint16_t get_16bit_le(unsigned char *data);
uint32_t get_32bit_le(unsigned char *data);

int ptp_write_unicode_string(char *dat, const char *string);
int ptp_read_unicode_string(char *buffer, char *dat, int max);
int ptp_read_utf8_string(void *dat, char *string, int max);
int ptp_read_string(uint8_t *dat, char *string, int max);
int ptp_write_string(uint8_t *dat, const char *string);
int ptp_write_utf8_string(void *dat, const char *string);
int ptp_read_uint16_array(const uint8_t *dat, uint16_t *buf, int max, int *length);
int ptp_read_uint16_array_s(uint8_t *bs, uint8_t *be, uint16_t *buf, int max, int *length);
inline static int ptp_write_u8 (void *buf, uint8_t out) { ((uint8_t *)buf)[0] = out; return 1; }
inline static int ptp_write_u16(void *buf, uint16_t out) { ((uint16_t *)buf)[0] = out; return 2; }
inline static int ptp_write_u32(void *buf, uint32_t out) { ((uint32_t *)buf)[0] = out; return 4; }
inline static int ptp_read_u32 (const void *buf, uint32_t *out) { *out = ((const uint32_t *)buf)[0]; return 4; }
inline static int ptp_read_u16 (const void *buf, uint16_t *out) { *out = ((const uint16_t *)buf)[0]; return 2; }
inline static int ptp_read_u8  (const void *buf, uint8_t *out) { *out = ((const uint8_t *)buf)[0]; return 1; }

int ptp_get_prop_size(uint8_t *d, int type);
int ptp_get_prop_list_size(uint8_t *d, int type, int cnt);
int ptp_copy_prop(uint8_t *dest, uint16_t type, uint8_t *data);
int ptp_copy_prop_list(uint8_t *dest, uint16_t type, uint8_t *data, int len);
int ptp_prop_list_size(uint16_t type, uint8_t *data, int len);
