// Sony ILCE-7RM5 (A7R V) device property table
// Transcribed from libgphoto2 camlibs/ptp2/cameras/sony-a7r5.txt
// (Device Property Summary = the SDIO_GetAllExtDevicePropInfo (0x9209)
// stream of the real body; ro/rw per the dump's GetSet flags)
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <vcam.h>
#include <sony.h>

int sony_register_model_a7r5(vcam *cam) {
	sony_add_prop(cam, 0x5005, PTP_TC_UINT16, 1, "32786", "enum:2,4,32785,32784,6,32769,32770,32771,32772,7,32816,32786,32800,32801,32802");
	sony_add_prop(cam, 0x500a, PTP_TC_UINT16, 1, "1", "enum:2,32773,32772,32774,1");
	sony_add_prop(cam, 0x500b, PTP_TC_UINT16, 1, "32769", "enum:32769,32770,32772,32773,32771,32774");
	sony_add_prop(cam, 0x500c, PTP_TC_UINT16, 0, "3", "enum:");
	sony_add_prop(cam, 0x5010, PTP_TC_INT16, 0, "0", "enum:");
	sony_add_prop(cam, 0xd200, PTP_TC_INT16, 1, "0", "enum:");
	sony_add_prop(cam, 0xd201, PTP_TC_UINT8, 1, "31", "enum:1,31,17,18,19,20,21");
	sony_add_prop(cam, 0xd20d, PTP_TC_UINT32, 1, "65596", "range:73536:1:1966081");
	sony_add_prop(cam, 0xd20f, PTP_TC_UINT16, 1, "4000", "range:2500:100:9900");
	sony_add_prop(cam, 0xd211, PTP_TC_UINT8, 1, "1", "enum:1,3,2,4");
	sony_add_prop(cam, 0xd213, PTP_TC_UINT8, 1, "1", "enum:");
	sony_add_prop(cam, 0xd217, PTP_TC_UINT8, 1, "1", "enum:");
	sony_add_prop(cam, 0xd221, PTP_TC_UINT8, 1, "1", "enum:");
	sony_add_prop(cam, 0xd21e, PTP_TC_UINT32, 1, "400", "enum:");
	sony_add_prop(cam, 0xd222, PTP_TC_UINT16, 1, "1", "enum:1,17,16");
	sony_add_prop(cam, 0xd20e, PTP_TC_UINT8, 1, "6", "enum:");
	sony_add_prop(cam, 0xd218, PTP_TC_INT8, 1, "56", "range:-1:1:100");
	sony_add_prop(cam, 0x5013, PTP_TC_UINT16, 1, "1", "enum:1,32784,2,32789,32786,32772,32771,32773,32776,32777,32780,32781,32782,32783,49719,49727,33591,34103,34615,35127,49751,49759,33623,34135,34647,35159,49783,49791,33655,34167,34679,35191,49681,49689,33553,34065,34577,35089,49729,49737,33601,34113,34625,49761,49769,33633,34145,34657,49793,49801,33665,34177,34689,49697,49705,33569,34081,34593,49745,49753,33617,34129,49777,49785,33649,34161,49809,49817,33681,34193,49713,49721,33585,34097,49718,49726,33590,34102,34614,35126,49750,49758,33622,34134,34646,35158,49782,49790,33654,34166,34678,35190,49680,49688,33552,34064,34576,35088,49728,49736,33600,34112,34624,49760,49768,33632,34144,34656,49792,49800,33664,34176,34688,49696,49704,33568,34080,34592,49744,49752,33616,34128,49776,49784,33648,34160,49808,49816,33680,34192,49712,49720,33584,34096,32832,32808,32792,32809,32793");
	sony_add_prop(cam, 0xd203, PTP_TC_UINT8, 0, "1", "enum:");
	sony_add_prop(cam, 0xd21f, PTP_TC_UINT8, 1, "1", "enum:");
	sony_add_prop(cam, 0x5004, PTP_TC_UINT8, 1, "16", "enum:16,20,19,18,17,4,3,2,1");
	sony_add_prop(cam, 0x5007, PTP_TC_UINT16, 0, "1400", "range:280:1:2200");
	sony_add_prop(cam, 0x500e, PTP_TC_UINT16, 0, "1", "enum:");
	sony_add_prop(cam, 0xd214, PTP_TC_UINT32, 1, "143228000", "range:0:1:4294967295");
	sony_add_prop(cam, 0xd215, PTP_TC_UINT16, 1, "0", "range:0:1:65535");
	// PriorityMode: written (INT8 1 = application) during gphoto2 init via
	// 0x9205; served in the 0x9209 stream but not advertised in DeviceInfo
	sony_add_prop(cam, 0xd25a, PTP_TC_UINT8, 1, "1", NULL);
	return 0;
}
