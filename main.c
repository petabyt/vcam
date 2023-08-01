// Test basic opcode, get device properties
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <camlib/camlib.h>

int main() {
	struct PtpRuntime r;
	ptp_generic_init(&r);

	struct PtpDeviceInfo di;

	if (ptp_device_init(&r)) {
		puts("Device connection error");
		return 0;
	}

	ptp_open_session(&r);

	ptp_get_device_info(&r, &di);

	char temp[4096];
	ptp_device_info_json(&di, temp, sizeof(temp));
	printf("%s\n", temp);

	struct UintArray *arr;
	int rc = ptp_get_storage_ids(&r, &arr);
	int id = arr->data[0];

	rc = ptp_get_object_handles(&r, id, 0, 0, &arr);
	arr = ptp_dup_uint_array(arr);

	for (int i = 0; i < arr->length; i++) {
		struct PtpObjectInfo oi;
		ptp_get_object_info(&r, arr->data[i], &oi);

		printf("Filename: %s\n", oi.filename);
		printf("File size: %u\n", oi.compressed_size);
	}

	free(arr);

	ptp_close_session(&r);
	ptp_device_close(&r);
	ptp_generic_close(&r);
	return 0;
}
