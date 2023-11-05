// Gphoto required headers and tweaks
#ifndef GPHOTO_STUFF_H
#define GPHOTO_STUFF_H

#define _GPHOTO2_INTERNAL_CODE
#define _DARWIN_C_SOURCE

#include <ptp.h>

extern char *extern_manufacturer_info;
extern char *extern_model_name;
extern char *extern_device_version;
extern char *extern_serial_no;

#include "config.h"
#include <gphoto2/gphoto2-port-library.h>
#include <gphoto2/gphoto2-port-portability.h>

#ifdef HAVE_LIBEXIF
#include <libexif/exif-data.h>
#endif

#include "vcamera.h"

#include <gphoto2/gphoto2-port-log.h>
#include <gphoto2/gphoto2-port-result.h>
#include <gphoto2/gphoto2-port.h>

#endif
