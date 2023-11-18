// Gphoto required headers and tweaks
#ifndef GPHOTO_STUFF_H
#define GPHOTO_STUFF_H

#define _GPHOTO2_INTERNAL_CODE
#define _DARWIN_C_SOURCE

#include <ptp.h>

// automake config
#define HAVE_ALARM 1
#define HAVE_DLFCN_H 1
#define HAVE_GETOPT_LONG 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIBINTL_H 1
#define HAVE_MALLOC 1
#define HAVE_MEMORY_H 1
#define HAVE_MEMSET 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCHR 1
#define HAVE_STRDUP 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRTOL 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_VPRINTF 1
#ifndef WIN32
#define LINUX_OS
#endif
#define STDC_HEADERS 1
#define TIME_WITH_SYS_TIME 1
#ifndef __cplusplus
#endif

#include <gphoto2/gphoto2-port-library.h>
#include <gphoto2/gphoto2-port-portability.h>

#ifdef HAVE_LIBEXIF
#include <libexif/exif-data.h>
#endif

#include <gphoto2/gphoto2-port-log.h>
#include <gphoto2/gphoto2-port-result.h>
#include <gphoto2/gphoto2-port.h>

#endif
