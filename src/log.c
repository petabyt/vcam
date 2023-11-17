#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simplified for new code
void gp_log_(const char *format, ...) {
	va_list args;
	va_start(args, format);
	printf("[GP_] ");
	vprintf(format, args);
	va_end(args);
}

// For gphoto legacy code only
void gp_log(void *lvl, const char *domain, const char *format, ...) {
	va_list args;
	va_start(args, format);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	printf("[GP] (%s) %s\n", domain, buffer);
}

void gp_log_with_source_location(void *lvl, const char *file, int line, const char *func, const char *format, ...) {
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
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	printf("[VCAM] %s", buffer);
}
