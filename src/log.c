#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simplified for new code
void gp_log_(const char *format, ...) {
	va_list args;
	va_start(args, format);
	printf("[GP] ");
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

void vcam_log(const char *format, ...) {
	va_list args;
	va_start(args, format);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	printf("[VCAM] %s\n", buffer);
}
