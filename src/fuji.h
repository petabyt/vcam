#ifndef VCAM_FUJI_H
#define VCAM_FUJI_H

#define FUJI_ACK_PACKET_SIZE 0x44

#define FUJI_DUMMY_THUMB "bin/fuji/dummy_thumb2.jpg"
#define FUJI_DUMMY_OBJ_INFO "bin/fuji/fuji_generic_object_info2.bin"
#define FUJI_DUMMY_OBJ_INFO_CUT "bin/fuji/fuji_generic_object_info1.bin"
#define FUJI_DUMMY_JPEG_FULL "bin/fuji/jpeg-full.jpg"
#define FUJI_DUMMY_JPEG_COMPRESSED "bin/fuji/jpeg-compressed.jpg"
#define FUJI_DUMMY_LV_JPEG "bin/fuji/lv_stream"

// Ran when getpartialobject or getobject is completed
void fuji_downloaded_object(vcamera *cam);

int fuji_set_prop_supported(vcamera *cam, int code);
int fuji_set_property(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len);
int fuji_send_events(vcamera *cam, ptpcontainer *ptp);
int fuji_get_property(vcamera *cam, ptpcontainer *ptp);
int ptp_fuji_get_device_info(vcamera *cam, ptpcontainer *ptp);
int ptp_fuji_capture(vcamera *cam, ptpcontainer *ptp);

// Check whether to send compressed or regular object on GetPartialObject
int fuji_is_compressed_mode(vcamera *cam);

uint8_t *fuji_get_ack_packet(vcamera *cam);

int fuji_ssdp_register(const char *ip, char *name, char *model);
int fuji_ssdp_import(const char *ip, char *name);
int fuji_tether_connect(const char *ip, int port);

// Launch thread to listen to liveview/event ports
void fuji_accept_remote_ports();

#endif
