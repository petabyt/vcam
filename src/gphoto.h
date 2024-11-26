// Gphoto required headers and tweaks
#ifndef GPHOTO_STUFF_H
#define GPHOTO_STUFF_H

#define _GPHOTO2_INTERNAL_CODE
#define _DARWIN_C_SOURCE

//#include <gphoto2/gphoto2-port-info-list.h>
//#include <gphoto2/gphoto2-port.h>
//#include <gphoto2/gphoto2-port-portability.h>
//#include <gphoto2/gphoto2-port-library.h>
//#include <gphoto2/gphoto2-port-portability.h>

//#include <gphoto2/gphoto2-port-log.h>
//#include <gphoto2/gphoto2-port-result.h>
//#include <gphoto2/gphoto2-port.h>

#ifdef HAVE_LIBEXIF
#include <libexif/exif-data.h>
#endif

#define GP_OK 0
#define GP_ERROR -1
#define GP_ERROR_TIMEOUT -10

typedef enum {
	GP_LOG_ERROR = 0,	/**< \brief Log message is an error information. */
	GP_LOG_VERBOSE = 1,	/**< \brief Log message is an verbose debug information. */
	GP_LOG_DEBUG = 2,	/**< \brief Log message is an debug information. */
	GP_LOG_DATA = 3		/**< \brief Log message is a data hex dump. */
} GPLogLevel;

#endif
