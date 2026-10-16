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
