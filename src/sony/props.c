// Sony device property registration
// Copyright Daniel C - GNU Lesser General Public License v2.1
//
// Model property tables (sony/models/*.c) are transcribed from libgphoto2's
// camlibs/ptp2/cameras/sony-*.txt dumps (Device Property Summary = the
// SDIO_GetAllExtDevicePropInfo (0x9209) stream of a real body). The records
// are re-serialized in the Sony DPD wire format by ops.c.
#include <stdlib.h>
#include <string.h>
#include <vcam.h>
#include <sony.h>

// Canonical shutter stop list for 0xD20D, in gphoto2's sony_shuttertable
// order (slow -> fast, encoded (numerator<<16)|denominator). gphoto2's
// mode-2 stepping loop computes jump widths as positions in that exact
// table, so the enumeration must match it one-to-one for the steps to
// converge (master's direct 0x9205 writes are unaffected by the form).
static const uint32_t sony_shutter_stops[] = {
	1966081, /* 30/1 */  1638401, /* 25/1 */  1310721, /* 20/1 */
	983041,  /* 15/1 */  851969,  /* 13/1 */  655361,  /* 10/1 */
	524289,  /*  8/1 */  393217,  /*  6/1 */  327681,  /*  5/1 */
	262145,  /*  4/1 */  2097162, /* 32/10 */ 1638410, /* 25/10 */
	131073,  /*  2/1 */  1048586, /* 16/10 */ 851978,  /* 13/10 */
	65537,   /*  1/1 */  524298,  /*  8/10 */ 393226,  /*  6/10 */
	327690,  /*  5/10 */ 262154,  /*  4/10 */ 65539,   /*  1/3  */
	65540,   /*  1/4  */  65541,   /*  1/5  */  65542,   /*  1/6  */
	65544,   /*  1/8  */  65546,   /*  1/10 */  65549,   /*  1/13 */
	65551,   /*  1/15 */  65556,   /*  1/20 */  65561,   /*  1/25 */
	65566,   /*  1/30 */  65576,   /*  1/40 */  65586,   /*  1/50 */
	65596,   /*  1/60 */  65616,   /*  1/80 */  65636,   /* 1/100 */
	65661,   /* 1/125 */  65696,   /* 1/160 */  65736,   /* 1/200 */
	65786,   /* 1/250 */  65856,   /* 1/320 */  65936,   /* 1/400 */
	66036,   /* 1/500 */  66176,   /* 1/640 */  66336,   /* 1/800 */
	66536,   /* 1/1000 */ 66786,   /* 1/1250 */ 67136,   /* 1/1600 */
	67536,   /* 1/2000 */ 68036,   /* 1/2500 */ 68736,   /* 1/3200 */
	69536,   /* 1/4000 */ 70536,   /* 1/5000 */ 71936,   /* 1/6400 */
	73536,   /* 1/8000 */ 75536,   /* 1/10000 */ 78036,  /* 1/12500 */
	81536,   /* 1/16000 */ 85536,  /* 1/20000 */ 90536,  /* 1/25000 */
	97536,   /* 1/32000 */
};

void sony_add_shutter_prop(vcam *cam, int rw, uint32_t value) {
	int n = sizeof(sony_shutter_stops) / sizeof(sony_shutter_stops[0]);
	struct PtpPropDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.DevicePropertyCode = 0xd20d;
	desc.DataType = PTP_TC_UINT32;
	desc.GetSet = rw ? 1 : 0;
	desc.factory_default_value = malloc(4);
	desc.value = malloc(4);
	memcpy(desc.factory_default_value, &value, 4);
	memcpy(desc.value, &value, 4);
	desc.FormFlag = PTP_EnumerationForm;
	desc.avail = malloc(n * 4);
	desc.avail_cnt = n;
	memcpy(desc.avail, sony_shutter_stops, n * 4);
	vcam_register_prop(cam, 0xd20d, &desc);
}

static int type_width(uint16_t type) {
	switch (type) {
	case PTP_TC_INT8: case PTP_TC_UINT8: return 1;
	case PTP_TC_INT16: case PTP_TC_UINT16: return 2;
	case PTP_TC_INT32: case PTP_TC_UINT32: return 4;
	case PTP_TC_INT64: case PTP_TC_UINT64: return 8;
	default: return 4;
	}
}

static void write_typed(void *dst, uint16_t type, long long v) {
	switch (type) {
	case PTP_TC_INT8: case PTP_TC_UINT8:
		ptp_write_u8(dst, (uint8_t)v);
		break;
	case PTP_TC_INT16: case PTP_TC_UINT16:
		ptp_write_u16(dst, (uint16_t)v);
		break;
	case PTP_TC_INT32: case PTP_TC_UINT32:
		ptp_write_u32(dst, (uint32_t)v);
		break;
	case PTP_TC_INT64: case PTP_TC_UINT64: {
		uint64_t u = (uint64_t)v;
		for (int i = 0; i < 8; i++)
			((uint8_t *)dst)[i] = (uint8_t)((u >> (8 * i)) & 0xff);
		break;
	}
	default:
		break;
	}
}

static long long parse_ll(const char **p) {
	char *end;
	long long v = strtoll(*p, &end, 0);
	*p = end;
	return v;
}

/// @brief Register a property from a dump-style description.
/// @param rw   Sony GetSet convention: 1 = Get/Set, 0 = Get-only
/// @param value decimal value for numeric types (Sony props are numeric only)
/// @param form  NULL, "range:min:step:max" or "enum:v1,v2,..." ("enum:" empty)
void sony_add_prop(vcam *cam, uint16_t code, uint16_t type, int rw, const char *value, const char *form) {
	struct PtpPropDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.DevicePropertyCode = code;
	desc.DataType = type;
	// Sony DPD GetSet byte: PTP_DPGS_GetSet (1) / PTP_DPGS_Get (0)
	desc.GetSet = rw ? 1 : 0;

	int width = type_width(type);
	long long v = parse_ll(&value);
	desc.factory_default_value = malloc(width);
	desc.value = malloc(width);
	write_typed(desc.factory_default_value, type, v);
	write_typed(desc.value, type, v);

	if (form != NULL) {
		if (!strncmp(form, "range:", 6)) {
			const char *p = form + 6;
			desc.FormFlag = PTP_RangeForm;
			desc.form_min = malloc(8);
			desc.form_max = malloc(8);
			desc.form_step = malloc(8);
			write_typed(desc.form_min, type, parse_ll(&p));
			if (*p == ':') p++;
			write_typed(desc.form_step, type, parse_ll(&p));
			if (*p == ':') p++;
			write_typed(desc.form_max, type, parse_ll(&p));
		} else if (!strncmp(form, "enum:", 5)) {
			const char *p = form + 5;
			int n = 1;
			for (const char *q = p; *q; q++)
				if (*q == ',') n++;
			if (*p == '\0')
				n = 0; // empty enumeration, exactly like real bodies
			int width2 = type_width(type);
			desc.FormFlag = PTP_EnumerationForm;
			desc.avail = n ? malloc(n * width2) : NULL;
			desc.avail_cnt = n;
			for (int i = 0; i < n; i++) {
				write_typed((uint8_t *)desc.avail + i * width2, type, parse_ll(&p));
				if (*p == ',') p++;
			}
		}
	}

	vcam_register_prop(cam, code, &desc);
}
