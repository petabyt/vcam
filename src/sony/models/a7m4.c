// Sony ILCE-7M4 (A7 IV) device property table
// Transcribed from libgphoto2 camlibs/ptp2/cameras/sony-a7m4.txt
// (Device Property Summary = the SDIO_GetAllExtDevicePropInfo (0x9209)
// stream of the real body; empty enumerations are verbatim)
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <vcam.h>
#include <sony.h>

int sony_register_model_a7m4(vcam *cam) {
	sony_add_prop(cam, 0x5004, PTP_TC_UINT8, 1, "20", "enum:16,20,19,18,17,4,3,2,1");
	sony_add_prop(cam, 0x5005, PTP_TC_UINT16, 1, "2", "enum:2,4,32785,32784,6,32769,32770,32771,32772,7,32816,32786,32800,32801,32802");
	sony_add_prop(cam, 0x5007, PTP_TC_UINT16, 1, "800", "range:560:1:3200");
	sony_add_prop(cam, 0x500a, PTP_TC_UINT16, 1, "1", "enum:");
	sony_add_prop(cam, 0x500b, PTP_TC_UINT16, 1, "32769", "enum:32769,32770,32772,32773,32771,32774");
	sony_add_prop(cam, 0x500c, PTP_TC_UINT16, 1, "3", "enum:");
	sony_add_prop(cam, 0x5010, PTP_TC_INT16, 1, "0", "enum:");
	sony_add_prop(cam, 0xd200, PTP_TC_INT16, 1, "0", "enum:");
	sony_add_prop(cam, 0xd201, PTP_TC_UINT8, 1, "31", "enum:1,31,17,18,19,20,21");
	sony_add_prop(cam, 0xd203, PTP_TC_UINT8, 1, "1", "enum:1,2,3");
	sony_add_shutter_prop(cam, 1, 327690); // 5/10 s, like the dump value
	sony_add_prop(cam, 0xd20e, PTP_TC_UINT8, 1, "14", "enum:");
	sony_add_prop(cam, 0xd20f, PTP_TC_UINT16, 1, "5500", "range:2500:100:9900");
	sony_add_prop(cam, 0xd211, PTP_TC_UINT8, 1, "1", "enum:1,3,2,4");
	sony_add_prop(cam, 0xd213, PTP_TC_UINT8, 1, "1", "enum:");
	sony_add_prop(cam, 0xd217, PTP_TC_UINT8, 1, "1", "enum:");
	sony_add_prop(cam, 0xd218, PTP_TC_UINT8, 1, "80", "range:255:1:100");
	sony_add_prop(cam, 0xd21e, PTP_TC_UINT32, 1, "16777215", "enum:");
	sony_add_prop(cam, 0xd21f, PTP_TC_UINT8, 1, "1", "enum:");
	sony_add_prop(cam, 0xd221, PTP_TC_UINT8, 1, "1", "enum:");
	sony_add_prop(cam, 0xd222, PTP_TC_UINT16, 1, "1", "enum:1,17,16");
	sony_add_prop(cam, 0x500e, PTP_TC_UINT16, 1, "3", "enum:");
	sony_add_prop(cam, 0x5013, PTP_TC_UINT32, 1, "32784", "enum:1,32784,2,32789,32786,32772,32771,32773,32776,32777,32780,32781,32782,32783,33591,34103,35127,33623,34135,35159,33655,34167,35191,33553,34065,35089,33569,34081,33585,34097,33590,34102,35126,33622,34134,35158,33654,34166,35190,33552,34064,35088,33568,34080,33584,34096,32808,32792,32809,32793");
	sony_add_prop(cam, 0xd214, PTP_TC_UINT32, 1, "80716000", "range:0:1:4294967295");
	sony_add_prop(cam, 0xd215, PTP_TC_UINT16, 1, "0", "range:0:1:65535");
	// PriorityMode: written (INT8 1 = application) during gphoto2 init via
	// 0x9205; served in the 0x9209 stream but not advertised in DeviceInfo
	sony_add_prop(cam, 0xd25a, PTP_TC_UINT8, 1, "1", NULL);
	return 0;
}
