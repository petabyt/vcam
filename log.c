#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include <gphoto2/gphoto2-port-log.h>
#include <gphoto2/gphoto2-port-result.h>

void gp_log(GPLogLevel level, const char *domain, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
	puts("");
}

void gp_log_with_source_location(GPLogLevel level, const char *file, int line, const char *func, const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf("[%s:%d] %s: ", file, line, func);
    vprintf(format, args);
    va_end(args);
}

void gp_port_set_error(void *port, const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf("Error: ");
    vprintf(format, args);
    va_end(args);
}
