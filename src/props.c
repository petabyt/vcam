#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>
#include <vcam.h>

static void init_prop(struct PtpPropDesc *desc) {
	// If only we had a custom allocator...
	desc->factory_default_value = malloc(4);
	desc->value = malloc(4);
	desc->form_min = malloc(4);
	desc->form_max = malloc(4);
	desc->form_step = malloc(4);
}

int ptp_battery_getdesc(vcam *cam, struct PtpPropDesc *desc) {
	desc->DevicePropertyCode = 0x5001;
	desc->DataType = PTP_TC_UINT8;
	desc->GetSet = PTP_AC_ReadWrite;
	ptp_write_u8(desc->factory_default_value, 50);
	ptp_write_u8(desc->value, 50);
	desc->FormFlag = PTP_RangeForm;
	ptp_write_u8(desc->form_min, 0);
	ptp_write_u8(desc->form_max, 100);
	ptp_write_u8(desc->form_step, 1);
	return 1;
}

void *ptp_battery_getvalue(vcam *cam, int *length) {
	return &cam->battery;
}

#if 0
int ptp_imagesize_getdesc(vcam *cam, struct PtpPropDesc *desc) {
	desc->DevicePropertyCode = 0x5003;
	desc->DataType = 0xffff; /* STR */
	desc->GetSet = 0;	 /* Get only */
	desc->FactoryDefaultValue.str = strdup("640x480");
	desc->CurrentValue.str = strdup("640x480");
	desc->FormFlag = 0x02; /* enum */
	desc->FORM.Enum.NumberOfValues = 3;
	desc->FORM.Enum.SupportedValue = malloc(3 * sizeof(desc->FORM.Enum.SupportedValue[0]));
	desc->FORM.Enum.SupportedValue[0].str = strdup("640x480");
	desc->FORM.Enum.SupportedValue[1].str = strdup("1024x768");
	desc->FORM.Enum.SupportedValue[2].str = strdup("2048x1536");

	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5003, 0xffffffff);
	return 1;
}

int ptp_imagesize_getvalue(vcam *cam, void *data, int length) {
	val->str = strdup("640x480");
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5003, 0xffffffff);
	return 1;
}

int ptp_shutterspeed_getdesc(vcam *cam, struct PtpPropDesc *desc) {
	desc->DevicePropertyCode = 0x500D;
	desc->DataType = 0x0006; /* UINT32 */
	desc->GetSet = 1;	 /* Get/Set */
	if (!cam->shutterspeed)
		cam->shutterspeed = 100; /* 1/100 * 10000 */
	desc->FactoryDefaultValue.u32 = cam->shutterspeed;
	desc->CurrentValue.u32 = cam->shutterspeed;
	desc->FormFlag = 0x02; /* enum */
	desc->FORM.Enum.NumberOfValues = 9;
	desc->FORM.Enum.SupportedValue = malloc(desc->FORM.Enum.NumberOfValues * sizeof(desc->FORM.Enum.SupportedValue[0]));
	desc->FORM.Enum.SupportedValue[0].u32 = 10000;
	desc->FORM.Enum.SupportedValue[1].u32 = 1000;
	desc->FORM.Enum.SupportedValue[2].u32 = 500;
	desc->FORM.Enum.SupportedValue[3].u32 = 200;
	desc->FORM.Enum.SupportedValue[4].u32 = 100;
	desc->FORM.Enum.SupportedValue[5].u32 = 50;
	desc->FORM.Enum.SupportedValue[6].u32 = 25;
	desc->FORM.Enum.SupportedValue[7].u32 = 12;
	desc->FORM.Enum.SupportedValue[8].u32 = 1;

	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x500D, 0xffffffff);
	return 1;
}

int ptp_shutterspeed_getvalue(vcam *cam, void *data, int length) {
	val->u32 = cam->shutterspeed;
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x500d, 0xffffffff);
	return 1;
}

int ptp_shutterspeed_setvalue(vcam *cam, void *data, int length) {
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x500d, 0xffffffff);
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %d as value", val->u32);
	cam->shutterspeed = val->u32;
	return 1;
}

int ptp_fnumber_getdesc(vcam *cam, struct PtpPropDesc *desc) {
	desc->DevicePropertyCode = 0x5007;
	desc->DataType = 0x0004; /* UINT16 */
	desc->GetSet = 1;	 /* Get/Set */
	if (!cam->fnumber)
		cam->fnumber = 280; /* 2.8 * 100 */
	desc->FactoryDefaultValue.u16 = cam->fnumber;
	desc->CurrentValue.u16 = cam->fnumber;
	desc->FormFlag = 0x02; /* enum */
	desc->FORM.Enum.NumberOfValues = 18;
	desc->FORM.Enum.SupportedValue = malloc(desc->FORM.Enum.NumberOfValues * sizeof(desc->FORM.Enum.SupportedValue[0]));
	desc->FORM.Enum.SupportedValue[0].u16 = 280;
	desc->FORM.Enum.SupportedValue[1].u16 = 350;
	desc->FORM.Enum.SupportedValue[2].u16 = 400;
	desc->FORM.Enum.SupportedValue[3].u16 = 450;
	desc->FORM.Enum.SupportedValue[4].u16 = 500;
	desc->FORM.Enum.SupportedValue[5].u16 = 560;
	desc->FORM.Enum.SupportedValue[6].u16 = 630;
	desc->FORM.Enum.SupportedValue[7].u16 = 710;
	desc->FORM.Enum.SupportedValue[8].u16 = 800;
	desc->FORM.Enum.SupportedValue[9].u16 = 900;
	desc->FORM.Enum.SupportedValue[10].u16 = 1000;
	desc->FORM.Enum.SupportedValue[11].u16 = 1100;
	desc->FORM.Enum.SupportedValue[12].u16 = 1300;
	desc->FORM.Enum.SupportedValue[13].u16 = 1400;
	desc->FORM.Enum.SupportedValue[14].u16 = 1600;
	desc->FORM.Enum.SupportedValue[15].u16 = 1800;
	desc->FORM.Enum.SupportedValue[16].u16 = 2000;
	desc->FORM.Enum.SupportedValue[17].u16 = 2200;

	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5007, 0xffffffff);
	return 1;
}

int ptp_fnumber_getvalue(vcam *cam, void *data, int length) {
	val->u16 = cam->fnumber;
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5007, 0xffffffff);
	return 1;
}

int ptp_fnumber_setvalue(vcam *cam, void *data, int length) {
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5007, 0xffffffff);
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %d as value", val->u16);
	cam->fnumber = val->u16;
	return 1;
}

int ptp_exposurebias_getdesc(vcam *cam, struct PtpPropDesc *desc) {
	desc->DevicePropertyCode = 0x5010;
	desc->DataType = 0x0003; /* INT16 */
	desc->GetSet = 1;	 /* Get/Set */
	if (!cam->exposurebias)
		cam->exposurebias = 0; /* 0.0 */
	desc->FactoryDefaultValue.i16 = cam->exposurebias;
	desc->CurrentValue.i16 = cam->exposurebias;
	desc->FormFlag = 0x02; /* enum */
	desc->FORM.Enum.NumberOfValues = 13;
	desc->FORM.Enum.SupportedValue = malloc(desc->FORM.Enum.NumberOfValues * sizeof(desc->FORM.Enum.SupportedValue[0]));
	desc->FORM.Enum.SupportedValue[0].i16 = -3000;
	desc->FORM.Enum.SupportedValue[1].i16 = -2500;
	desc->FORM.Enum.SupportedValue[2].i16 = -2000;
	desc->FORM.Enum.SupportedValue[3].i16 = -1500;
	desc->FORM.Enum.SupportedValue[4].i16 = -1000;
	desc->FORM.Enum.SupportedValue[5].i16 = -500;
	desc->FORM.Enum.SupportedValue[6].i16 = 0;
	desc->FORM.Enum.SupportedValue[7].i16 = 500;
	desc->FORM.Enum.SupportedValue[8].i16 = 1000;
	desc->FORM.Enum.SupportedValue[9].i16 = 1500;
	desc->FORM.Enum.SupportedValue[10].i16 = 2000;
	desc->FORM.Enum.SupportedValue[11].i16 = 2500;
	desc->FORM.Enum.SupportedValue[12].i16 = 3000;

	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5010, 0xffffffff);
	return 1;
}

int ptp_exposurebias_getvalue(vcam *cam, void *data, int length) {
	val->i16 = cam->exposurebias;
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5010, 0xffffffff);
	return 1;
}

int ptp_exposurebias_setvalue(vcam *cam, void *data, int length) {
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5010, 0xffffffff);
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %d as value", val->i16);
	cam->exposurebias = val->i16;
	return 1;
}

int ptp_datetime_getdesc(vcam *cam, struct PtpPropDesc *desc) {
	struct tm *tm;
	time_t xtime;
	char xdate[40];

	desc->DevicePropertyCode = 0x5011;
	desc->DataType = 0xffff; /* string */
	desc->GetSet = 1;	 /* get only */
	time(&xtime);
	tm = gmtime(&xtime);
	sprintf(xdate, "%04d%02d%02dT%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	desc->FactoryDefaultValue.str = strdup(xdate);
	desc->CurrentValue.str = strdup(xdate);
	desc->FormFlag = 0; /* no form */
	/*ptp_inject_interrupt (cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5011, 0xffffffff);*/
	return 1;
}

int ptp_datetime_getvalue(vcam *cam, void *data, int length) {
	struct tm *tm;
	time_t xtime;
	char xdate[40];

	time(&xtime);
	tm = gmtime(&xtime);
	sprintf(xdate, "%04d%02d%02dT%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	val->str = strdup(xdate);
	/*ptp_inject_interrupt (cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5011, 0xffffffff);*/
	return 1;
}

int ptp_datetime_setvalue(vcam *cam, void *data, int length) {
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %s as value", val->str);
	return 1;
}
#endif

int getdesc_d406(vcam *cam, struct PtpPropDesc *desc) {
	desc->DataType = PTP_TC_UINT16ARRAY;
	return 0;
}
void *getvalue_d406(vcam *cam) {
	return NULL;
}
int setvalue_d406(vcam *cam, const void *data) {
	return 0;
}

void ptp_register_mtp_props(vcam *cam) {
	{
		struct PtpPropDesc desc;
		init_prop(&desc);
		desc.DevicePropertyCode = PTP_DPC_MTP_SessionInitiatorInfo;
		desc.DataType = PTP_TC_STRING;
		desc.GetSet = PTP_AC_ReadWrite;
		ptp_write_u32(desc.factory_default_value, 0);
		ptp_write_u32(desc.value, 0);
		desc.FormFlag = 0x0;
		vcam_register_prop(cam, desc.DevicePropertyCode, &desc);
	}
}

void ptp_register_standard_props(vcam *cam) {
	{
		struct PtpPropDesc desc;
		init_prop(&desc);
		desc.DevicePropertyCode = PTP_DPC_BatteryLevel;
		desc.DataType = PTP_TC_UINT8;
		desc.GetSet = PTP_AC_ReadWrite;
		ptp_write_u8(desc.factory_default_value, 50);
		ptp_write_u8(desc.value, 50);
		desc.FormFlag = PTP_RangeForm;
		ptp_write_u8(desc.form_min, 0);
		ptp_write_u8(desc.form_max, 100);
		ptp_write_u8(desc.form_step, 1);
		vcam_register_prop(cam, desc.DevicePropertyCode, &desc);
	}
	{
		struct PtpPropDesc desc;

		desc.factory_default_value = malloc(64);
		desc.value = malloc(64);
		desc.avail = malloc(300);

		desc.DevicePropertyCode = 0x5003;
		desc.DataType = PTP_TC_STRING;
		desc.GetSet = PTP_AC_Read;
		ptp_write_string(desc.factory_default_value, "640x480");
		ptp_write_string(desc.value, "640x480");
		desc.FormFlag = PTP_EnumerationForm;
		uint8_t *d = (uint8_t *)desc.avail;
		d += ptp_write_string(d, "640x480");
		d += ptp_write_string(d, "1024x768");
		d += ptp_write_string(d, "2048x1536");
		desc.avail_cnt = 3;
		vcam_register_prop(cam, desc.DevicePropertyCode, &desc);
	}

	//vcam_register_prop_handlers(cam, 0x5001, ptp_battery_getdesc, ptp_battery_getvalue, NULL);
//	vcam_register_prop_handlers(cam, 0x5003, ptp_imagesize_getdesc, ptp_imagesize_getvalue, NULL);
//	vcam_register_prop_handlers(cam, 0x5007, ptp_fnumber_getdesc, ptp_fnumber_getvalue, ptp_fnumber_setvalue);
//	vcam_register_prop_handlers(cam, 0x5010, ptp_exposurebias_getdesc, ptp_exposurebias_getvalue, ptp_exposurebias_setvalue);
//	vcam_register_prop_handlers(cam, 0x500d, ptp_shutterspeed_getdesc, ptp_shutterspeed_getvalue, ptp_shutterspeed_setvalue);
//	vcam_register_prop_handlers(cam, 0x5011, ptp_datetime_getdesc, ptp_datetime_getvalue, ptp_datetime_setvalue);
}
