int fuji_get_thumb(vcamera *cam, ptpcontainer *ptp);
int fuji_get_object_info(vcamera *cam, ptpcontainer *ptp);
int fuji_get_partial_object(vcamera *cam, ptpcontainer *ptp);
int fuji_set_prop_supported(int code);
int fuji_set_property(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len);
int fuji_send_events(vcamera *cam, ptpcontainer *ptp);
int fuji_get_property(vcamera *cam, ptpcontainer *ptp);
int ptp_fuji_get_device_info(vcamera *cam, ptpcontainer *ptp);
int ptp_fuji_capture(vcamera *cam, ptpcontainer *ptp);
