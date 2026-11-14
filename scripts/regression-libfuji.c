/* Small libfuji integration test against an already running vcam TCP server. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <libpict.h>
#include <fuji.h>
#include <app.h>

/* libfuji delegates logging and application callbacks to its frontend. */
void ptp_verbose_log(struct PtpRuntime *r, char *fmt, ...) { (void)r; (void)fmt; }
void ptp_error_log(struct PtpRuntime *r, char *fmt, ...) {
	(void)r;
	va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args);
}
void ptp_panic(char *fmt, ...) {
	va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args);
	abort();
}
void plat_dbg(char *fmt, ...) { (void)fmt; }
void app_print(struct PtpRuntime *r, char *fmt, ...) { (void)r; (void)fmt; }
int app_check_thread_cancel(struct PtpRuntime *r) { (void)r; return 0; }
void app_report_download_speed(struct PtpRuntime *r, long time, size_t size) { (void)r; (void)time; (void)size; }
int plat_update_object_info(struct PtpRuntime *r, int handle, const struct PtpObjectInfo *oi) { (void)r; (void)handle; (void)oi; return 0; }
void app_send_cam_name(struct PtpRuntime *r, const char *name) { (void)r; (void)name; }
void app_update_storage_info(struct PtpRuntime *r) { (void)r; }
int app_queue_file_for_download(struct PtpRuntime *r, int handle) { (void)r; (void)handle; return -1; }
int app_ptp_download_file(struct PtpRuntime *r, struct PtpObjectInfo *oi, int handle, unsigned int chunk, int index) { (void)r; (void)oi; (void)handle; (void)chunk; (void)index; return -1; }

struct download { FILE *expected; unsigned int received; int mismatch; };
static int compare_chunk(void *arg, void *buf, unsigned int len, unsigned int offset, unsigned int total) {
	struct download *d = arg;
	unsigned char *bytes = buf;
	(void)total;
	if (offset != d->received) return -1;
	for (unsigned int i = 0; i < len; i++) {
		if (fgetc(d->expected) != bytes[i]) d->mismatch = 1;
	}
	d->received += len;
	return d->mismatch ? -1 : 0;
}

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <camera-ip> <fixture-path>\n", argv[0]);
		return 1;
	}
	const char *name = strrchr(argv[2], '/');
	name = name ? name + 1 : argv[2];
	FILE *fixture = fopen(argv[2], "rb");
	if (!fixture) { perror(argv[2]); return 1; }
	struct PtpRuntime *r = ptp_new(PTP_IP_USB);
	if (!r) { fclose(fixture); return 1; }
	int rc = fuji_reset_ptp(r);
	if (rc) goto done;
	fuji_get(r)->transport = FUJI_FEATURE_WIRELESS_COMM;
	snprintf(r->priv->ip_address, sizeof(r->priv->ip_address), "%s", argv[1]);
	rc = ptpip_connect(r, argv[1], FUJI_CMD_IP_PORT, 0);
	if (rc) goto done;
	rc = fuji_setup(r, "vcam-regression");
	if (rc) goto close_connection;
	struct PtpDeviceInfo info;
	rc = ptp_get_device_info(r, &info);
	if (rc || !info.model[0]) { if (!rc) rc = -1; goto close_session; }
	struct PtpArray *handles = NULL;
	rc = ptp_fuji_get_object_handles(r, &handles);
	if (rc) goto close_session;
	int handle = 0;
	for (unsigned int i = 0; i < handles->length; i++) {
		struct PtpObjectInfo object;
		if (!ptp_get_object_info(r, handles->data[i], &object) && !strcmp(object.filename, name)) {
			handle = handles->data[i];
			break;
		}
	}
	free(handles);
	if (!handle) { fprintf(stderr, "Fixture %s not found\n", name); rc = -1; goto close_session; }
	rc = ptp_get_thumbnail(r, handle);
	if (rc || ptp_get_payload_length(r) == 0) { if (!rc) rc = -1; goto close_session; }
	struct download d = { .expected = fixture };
	rc = fuji_download_file(r, handle, compare_chunk, &d);
	if (!rc && (d.mismatch || !d.received || fgetc(fixture) != EOF)) rc = -1;
	if (!rc) printf("OK libfuji: %s, %s (%u bytes)\n", info.model, name, d.received);
close_session:
	if (ptp_close_session(r) && !rc) rc = -1;
close_connection:
	ptpip_device_close(r);
done:
	fclose(fixture);
	if (rc) fprintf(stderr, "libfuji regression failed: %d\n", rc);
	return rc ? 1 : 0;
}
