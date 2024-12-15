#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vcam_log_func(const char *func, const char *format, ...) {
	va_list args;
	va_start(args, format);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	printf("[VCAM] (%s) %s\n", func, buffer);
}

void vcam_log(const char *format, ...) {
	va_list args;
	va_start(args, format);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	printf("[VCAM] %s\n", buffer);
}

void vcam_panic(const char *format, ...) {
	va_list args;
	va_start(args, format);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	printf("[VCAM] %s\n", buffer);
	fflush(stdout);
	abort();
}
