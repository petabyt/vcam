#include <vcam.h>
#include <fujiptp.h>
#include "fuji.h"

#include <string.h>

int fuji_strict_property_allowed(const char *model, int df01, uint16_t property) {
	if (property == PTP_DPC_FUJI_EventsList_D212)
		return 1;
	/* -1 is an unknown client state, not a wildcard. */
	if (!strcmp(model, "X-A2")) {
		switch (df01) {
		case -1:
			switch (property) {
			case 0xd212:
			case 0xdf01:
				return 1;
			default:
				return 0;
			}
		case 1:
			switch (property) {
			case 0xd212:
			case 0xd235:
				return 1;
			default:
				return 0;
			}
		case 2:
			switch (property) {
			case 0xd212:
			case 0xd227:
			case 0xd235:
			case 0xdf22:
				return 1;
			default:
				return 0;
			}
		default:
			return 0;
		}
	}
	if (!strcmp(model, "X-H1")) {
		switch (df01) {
		case -1:
			switch (property) {
			case 0xd212:
			case 0xd244:
			case 0xdf00:
			case 0xdf01:
			case 0xdf25:
				return 1;
			default:
				return 0;
			}
		case 5:
			switch (property) {
			case 0x5005:
			case 0xd001:
			case 0xd02a:
			case 0xd212:
			case 0xd244:
			case 0xd246:
			case 0xdf00:
			case 0xdf01:
			case 0xdf24:
			case 0xdf25:
				return 1;
			default:
				return 0;
			}
		case 8:
			switch (property) {
			case 0xd212:
			case 0xd235:
			case 0xdf21:
				return 1;
			default:
				return 0;
			}
		case 10:
			switch (property) {
			case 0xd212:
			case 0xd500:
			case 0xdf31:
				return 1;
			default:
				return 0;
			}
		case 11:
			switch (property) {
			case 0xd212:
			case 0xd226:
			case 0xd227:
			case 0xd235:
			case 0xdf00:
			case 0xdf01:
			case 0xdf25:
				return 1;
			default:
				return 0;
			}
		default:
			return 0;
		}
	}
	if (!strcmp(model, "X-S10")) {
		switch (df01) {
		case -1:
			switch (property) {
			case 0xd212:
			case 0xdf01:
				return 1;
			default:
				return 0;
			}
		case 5:
			switch (property) {
			case 0xd212:
			case 0xd244:
			case 0xdf00:
			case 0xdf01:
			case 0xdf24:
			case 0xdf25:
				return 1;
			default:
				return 0;
			}
		case 11:
			switch (property) {
			case 0xd212:
			case 0xd227:
			case 0xd235:
			case 0xdf25:
				return 1;
			default:
				return 0;
			}
		case 20:
			switch (property) {
			case 0xd212:
			case 0xd226:
			case 0xd227:
			case 0xd22b:
			case 0xd235:
			case 0xd244:
			case 0xd620:
			case 0xd621:
			case 0xdf28:
				return 1;
			default:
				return 0;
			}
		default:
			return 0;
		}
	}
	if (!strcmp(model, "X-T30")) {
		switch (df01) {
		case -1:
			switch (property) {
			case 0xd212:
			case 0xd226:
			case 0xd235:
			case 0xd244:
			case 0xdf00:
			case 0xdf01:
			case 0xdf25:
				return 1;
			default:
				return 0;
			}
		case 5:
			switch (property) {
			case 0xd001:
			case 0xd212:
			case 0xd244:
			case 0xdf00:
			case 0xdf01:
			case 0xdf24:
			case 0xdf25:
				return 1;
			default:
				return 0;
			}
		case 11:
			switch (property) {
			case 0xd212:
			case 0xd227:
			case 0xd235:
			case 0xdf00:
			case 0xdf01:
			case 0xdf25:
				return 1;
			default:
				return 0;
			}
		case 20:
			switch (property) {
			case 0xd212:
			case 0xd226:
			case 0xd227:
			case 0xd22b:
			case 0xd235:
			case 0xd244:
			case 0xd620:
			case 0xd621:
			case 0xdf00:
			case 0xdf01:
			case 0xdf28:
				return 1;
			default:
				return 0;
			}
		case 22:
			switch (property) {
			case 0xd212:
			case 0xd246:
			case 0xdf2a:
				return 1;
			default:
				return 0;
			}
		default:
			return 0;
		}
	}
	if (!strcmp(model, "X-T5")) {
		switch (df01) {
		case -1:
			switch (property) {
			case 0xd212:
			case 0xdf01:
				return 1;
			default:
				return 0;
			}
		case 20:
			switch (property) {
			case 0xd212:
			case 0xd226:
			case 0xd227:
			case 0xd22b:
			case 0xd22f:
			case 0xd235:
			case 0xd244:
			case 0xd620:
			case 0xd621:
			case 0xdf00:
			case 0xdf01:
			case 0xdf28:
				return 1;
			default:
				return 0;
			}
		case 22:
			switch (property) {
			case 0xd212:
			case 0xdf2a:
				return 1;
			default:
				return 0;
			}
		default:
			return 0;
		}
	}
	return 0;
}
