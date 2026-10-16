// Nikon device property registration
// Copyright Daniel C - GNU Lesser General Public License v2.1
//
// Model property tables (nikon/models/*.c) are transcribed from libgphoto2's
// camlibs/ptp2/cameras/nikon-*.txt dumps so the advertised data types, forms
// and default values match real hardware exactly.
#include <stdlib.h>
#include <string.h>
#include <vcam.h>
#include <nikon.h>

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
/// @param value decimal value for numeric types, string content for STR
/// @param form  NULL, "range:min:step:max", "enum:v1,v2,..." (numeric types)
///              or "senum:a|b|c" (string enumeration)
void nikon_add_prop(vcam *cam, uint16_t code, uint16_t type, int rw, const char *value, const char *form) {
	struct PtpPropDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.DevicePropertyCode = code;
	desc.DataType = type;
	desc.GetSet = rw ? PTP_AC_ReadWrite : PTP_AC_Read;

	if (type == PTP_TC_STRING) {
		size_t len = strlen(value) * 2 + 8;
		desc.factory_default_value = malloc(len);
		desc.value = malloc(len);
		ptp_write_string(desc.factory_default_value, value);
		ptp_write_string(desc.value, value);
	} else {
		int width = type_width(type);
		long long v = parse_ll(&value);
		desc.factory_default_value = malloc(width);
		desc.value = malloc(width);
		write_typed(desc.factory_default_value, type, v);
		write_typed(desc.value, type, v);
	}

	if (form == NULL) {
		vcam_register_prop(cam, code, &desc);
		return;
	}

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
		if (*p == '\0') {
			// empty enumeration (real bodies report these, e.g. 0xd201)
			n = 0;
		}
		int width = type_width(type);
		desc.FormFlag = PTP_EnumerationForm;
		desc.avail = n ? malloc(n * width) : NULL;
		desc.avail_cnt = n;
		for (int i = 0; i < n; i++) {
			write_typed((uint8_t *)desc.avail + i * width, type, parse_ll(&p));
			if (*p == ',') p++;
		}
	} else if (!strncmp(form, "senum:", 6)) {
		// string enumeration, entries separated by '|'
		const char *p = form + 6;
		int n = 1;
		size_t maxentry = 0;
		size_t cur = 0;
		for (const char *q = p; ; q++) {
			if (*q == '|' || *q == '\0') {
				if (cur > maxentry) maxentry = cur;
				if (*q == '\0') break;
				n++;
				cur = 0;
				continue;
			}
			cur++;
		}
		size_t maxlen = maxentry * 2 + 8;
		desc.FormFlag = PTP_EnumerationForm;
		desc.avail = malloc(n * maxlen);
		desc.avail_cnt = n;
		uint8_t *d = (uint8_t *)desc.avail;
		const char *start = p;
		for (int i = 0; i < n; i++) {
			const char *end = strchr(start, '|');
			size_t slen = end ? (size_t)(end - start) : strlen(start);
			char tmp[256];
			if (slen >= sizeof(tmp)) slen = sizeof(tmp) - 1;
			memcpy(tmp, start, slen);
			tmp[slen] = '\0';
			d += ptp_write_string(d, tmp);
			start = end ? end + 1 : start + slen;
		}
	}

	vcam_register_prop(cam, code, &desc);
}
