// Code to decode EOS event data structure into C code for setup process
// TODO: Stopped updating this script because camlib should be used instead
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <camlib/camlib.h>
#include <camlib/cl_enum.h>

int ptp_eos_prop_next(void **d, struct PtpGenericEvent *p);
int ptp_parse_data(void **d, int type);

int events(uint8_t *data, int dat_length) {
	uint8_t *dp = data;

	struct PtpGenericEvent *d_;
	struct PtpGenericEvent **p = &d_;

	int length = 0;
	while (dp != NULL) {
		if (dp >= data + dat_length) break;
		uint8_t *d = dp;
		uint32_t size, type;
		d += ptp_read_u32(d, &size);
		d += ptp_read_u32(d, &type);

		dp += size;

		// TODO: length is 1 when props list is invalid/empty
		if (type == 0) break;

		length++;
	}

	if (length == 0) return 0;

	(*p) = malloc(sizeof(struct PtpGenericEvent) * length);

	dp = data;
	for (int i = 0; i < length; i++) {
		// Read header
		uint32_t size, type;
		uint8_t *d = dp;
		d += ptp_read_u32(d, &size);
		d += ptp_read_u32(d, &type);

		// Detect termination or overflow
		if (type == 0) break;

		switch (type) {
		case PTP_EC_EOS_PropValueChanged: {
			//ptp_eos_prop_next(&d, cur);
			uint32_t code;
			d += ptp_read_u32(d, &code);
			char *name = ptp_get_enum(PTP_DPC, PTP_DEV_EOS, code);
			int value_size = size - 3 * 4;
			if (value_size == 4) {
				uint32_t value;
				d += ptp_read_u32(d, &value);
				if (name == enum_null) {
					printf("vcam_set_prop(cam, 0x%X, 0x%X);\n", code, value);
				} else {
					printf("vcam_set_prop(cam, %s, 0x%X);\n", name, value);
				}
			} else {
				if (name == enum_null) {
					printf("vcam_set_prop_data(cam, 0x%X, ", code);
				} else {
					printf("vcam_set_prop_data(cam, %s, ", name);
				}

				printf("(uint8_t[]){");
				uint8_t *db = (uint8_t *)d;
				for (int x = 0; x < value_size; x++) {
					printf("%02X, ", db[x]);
				}

				printf("});\n");
			}
		} break;
		case PTP_EC_EOS_InfoCheckComplete:
		case PTP_DPC_EOS_FocusInfoEx:
			char *name = ptp_get_enum_all(type);
			break;
		case PTP_EC_EOS_RequestObjectTransfer: {
			uint32_t a, b;
			d += ptp_read_u32(d, &a);
			d += ptp_read_u32(d, &b);
			char *name = "request object transfer";
			} break;
		case PTP_EC_EOS_ObjectAddedEx: {
			struct PtpEOSObject *obj = (struct PtpEOSObject *)d;
			char *name = "new object";
			uint32_t value = obj->a;
			} break;
		case PTP_EC_EOS_AvailListChanged: {
			int code = ptp_read_uint32((void **)&d);
			int dat_type = ptp_read_uint32((void **)&d);
			int count = ptp_read_uint32((void **)&d);

			int payload_size = (size - 20);
			int memb_size = 0;
			if (payload_size != 0 && count != 0) {
				memb_size = (size - 20) / count;
			}

			// If larger type, just use bytes
			if (memb_size > 4) {
				if (memb_size % 4 == 0) {
					//printf("Shrinking data type %d\n", memb_size);
					count = payload_size / 4;
					memb_size = 4;
				} else {
					//printf("Shrinking data type %d\n", memb_size);
					count = payload_size;
					memb_size = 1;
				}
			}

			char *c_type = "uint8_t";
			switch (memb_size) {
			case 4:
				c_type = "uint32_t";
				break;
			case 2:
				c_type = "uint16";
				break;
			}

			printf("%s avail_list_%x[] = {", c_type, code);

			for (int i = 0; i < count; i++) {
				if (memb_size == 4) {
					printf("0x%X, ", ptp_read_uint32((void **)&d));
				} else if (memb_size == 2) {
					printf("0x%X, ", ptp_read_uint16((void **)&d));
				} else if (memb_size == 1) {
					printf("0x%02X, ", ptp_read_uint8((void **)&d));
				}
			}

			printf("};\n");

			char *name = ptp_get_enum(PTP_DPC, PTP_DEV_EOS, code);
			if (name == enum_null) {
				printf("vcam_set_prop_avail(cam, 0x%X, %d, %d, avail_list_%x);", code, memb_size, count, code);
			} else {
				printf("vcam_set_prop_avail(cam, %s, %d, %d, avail_list_%x);", name, memb_size, count, code);
			}
	
			printf("\n");
		} break;
		}

		// auto allocated system to handle property available lists
		// it changes often
		// 

		// Move dp over for the next entry
		dp += size;
	}

	return length;
}

int main() {
	FILE *f = fopen("eos_events.bin", "rb");
	if (f == NULL) {
		return -1;
	}

	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);

	uint8_t *buffer = malloc(size);

	fread(buffer, 1, size, f);

	events(buffer, size);

	return 0;
}
