#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>
#include <vcam.h>

int ptp_battery_getdesc(vcam *cam, PTPDevicePropDesc *desc) {
	desc->DevicePropertyCode = 0x5001;
	desc->DataType = 2; /* uint8 */
	desc->GetSet = 0;   /* Get only */
	desc->FactoryDefaultValue.u8 = 50;
	desc->CurrentValue.u8 = 50;
	desc->FormFlag = 0x01; /* range */
	desc->FORM.Range.MinimumValue.u8 = 0;
	desc->FORM.Range.MaximumValue.u8 = 100;
	desc->FORM.Range.StepSize.u8 = 1;
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5001, 0xffffffff);
	return 1;
}

int ptp_battery_getvalue(vcam *cam, PTPPropertyValue *val) {
	val->u8 = 50;
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5001, 0xffffffff);
	return 1;
}

int ptp_imagesize_getdesc(vcam *cam, PTPDevicePropDesc *desc) {
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

int ptp_imagesize_getvalue(vcam *cam, PTPPropertyValue *val) {
	val->str = strdup("640x480");
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5003, 0xffffffff);
	return 1;
}

int ptp_shutterspeed_getdesc(vcam *cam, PTPDevicePropDesc *desc) {
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

int ptp_shutterspeed_getvalue(vcam *cam, PTPPropertyValue *val) {
	val->u32 = cam->shutterspeed;
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x500d, 0xffffffff);
	return 1;
}

int ptp_shutterspeed_setvalue(vcam *cam, PTPPropertyValue *val) {
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x500d, 0xffffffff);
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %d as value", val->u32);
	cam->shutterspeed = val->u32;
	return 1;
}

int ptp_fnumber_getdesc(vcam *cam, PTPDevicePropDesc *desc) {
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

int ptp_fnumber_getvalue(vcam *cam, PTPPropertyValue *val) {
	val->u16 = cam->fnumber;
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5007, 0xffffffff);
	return 1;
}

int ptp_fnumber_setvalue(vcam *cam, PTPPropertyValue *val) {
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5007, 0xffffffff);
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %d as value", val->u16);
	cam->fnumber = val->u16;
	return 1;
}

int ptp_exposurebias_getdesc(vcam *cam, PTPDevicePropDesc *desc) {
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

int ptp_exposurebias_getvalue(vcam *cam, PTPPropertyValue *val) {
	val->i16 = cam->exposurebias;
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5010, 0xffffffff);
	return 1;
}

int ptp_exposurebias_setvalue(vcam *cam, PTPPropertyValue *val) {
	ptp_inject_interrupt(cam, 1000, PTP_EC_DevicePropChanged, 1, 0x5010, 0xffffffff);
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %d as value", val->i16);
	cam->exposurebias = val->i16;
	return 1;
}

int ptp_datetime_getdesc(vcam *cam, PTPDevicePropDesc *desc) {
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

int ptp_datetime_getvalue(vcam *cam, PTPPropertyValue *val) {
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

int ptp_datetime_setvalue(vcam *cam, PTPPropertyValue *val) {
	gp_log(GP_LOG_DEBUG, __FUNCTION__, "got %s as value", val->str);
	return 1;
}

struct ptp_property ptp_properties[] = {
    {0x5001, ptp_battery_getdesc, ptp_battery_getvalue, NULL},
    {0x5003, ptp_imagesize_getdesc, ptp_imagesize_getvalue, NULL},
    {0x5007, ptp_fnumber_getdesc, ptp_fnumber_getvalue, ptp_fnumber_setvalue},
    {0x5010, ptp_exposurebias_getdesc, ptp_exposurebias_getvalue, ptp_exposurebias_setvalue},
    {0x500d, ptp_shutterspeed_getdesc, ptp_shutterspeed_getvalue, ptp_shutterspeed_setvalue},
    {0x5011, ptp_datetime_getdesc, ptp_datetime_getvalue, ptp_datetime_setvalue},
};

int ptp_get_properties_length() {
	return (sizeof(ptp_properties) / sizeof(ptp_properties[0]));
}
