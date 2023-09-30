#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gphoto2/gphoto2-port-log.h>
#include <gphoto2/gphoto2-port-result.h>

void gp_log(GPLogLevel level, const char *domain, const char *format, ...) {
	va_list args;
	va_start(args, format);
	char buffer[1024];
	vsnprintf(buffer, 1024, format, args);
	va_end(args);

	printf("[GP] (%s) %s\n", domain, buffer);
}

void gp_log_with_source_location(GPLogLevel level, const char *file, int line, const char *func, const char *format, ...) {
	va_list args;
	va_start(args, format);
	printf("[GP] [%s:%d] %s: ", file, line, func);
	vprintf(format, args);
	va_end(args);
}

void gp_port_set_error(void *port, const char *format, ...) {
	va_list args;
	va_start(args, format);
	printf("[GP] Error: ");
	vprintf(format, args);
	va_end(args);
}

void vcam_log(const char *format, ...) {
	va_list args;
	va_start(args, format);
	char buffer[1024];
	vsnprintf(buffer, 1024, format, args);
	va_end(args);

	printf("[VCAM] %s", buffer);
}
