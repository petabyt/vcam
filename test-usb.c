// Test basic opcode, get device properties
#include <stdint.h>
#include <stdio.h>
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

	ptp_eos_set_remote_mode(&r, 1);
	ptp_eos_set_event_mode(&r, 1);

	ptp_eos_get_event(&r);
	ptp_dump(&r);

	char buffer[50000];
	ptp_eos_events_json(&r, buffer, 50000);
	puts(buffer);

	ptp_close_session(&r);
	ptp_device_close(&r);
	ptp_generic_close(&r);
	return 0;
}
