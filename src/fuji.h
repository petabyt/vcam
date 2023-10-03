#ifndef VCAM_FUJI_H
#define VCAM_FUJI_H

#define FUJI_ACK_PACKET_SIZE 0x44

#define IS_FUJI_X_T20
//#define IS_FUJI_X_A2

#ifdef IS_FUJI_X_A2
	#define FUJI_CAM_NAME "X-A2"
	#define FUJI_IMAGE_GET_VERSION 1
	#define FUJI_IMAGE_EXPLORE_VERSION 2
	#define FUJI_REMOTE_VERSION 0x0
	#define FUJI_START_CAM_STATE FUJI_FULL_ACCESS
#endif

#ifdef IS_FUJI_X_T20
	#define FUJI_USE_REMOTE_MODE
	#define FUJI_CAM_NAME "X-T20"
	#define FUJI_IMAGE_GET_VERSION 3
	#define FUJI_IMAGE_EXPLORE_VERSION 4
	#define FUJI_REMOTE_VERSION 0x00020004
	#define FUJI_REMOTE_IMAGE_EXPLORE_VERSION 2

	#define FUJI_START_CAM_STATE FUJI_REMOTE_ACCESS
#endif

#ifdef IS_FUJI_X_S10
	#define FUJI_USE_REMOTE_MODE
	#define FUJI_CAM_NAME "X-S10"
	#define FUJI_IMAGE_GET_VERSION 3
	#define FUJI_IMAGE_EXPLORE_VERSION 5
	#define FUJI_REMOTE_VERSION 0x00020004
	#define FUJI_REMOTE_IMAGE_EXPLORE_VERSION 4

	#define FUJI_START_CAM_STATE FUJI_REMOTE_ACCESS
#endif

#define FUJI_DUMMY_THUMB "bin/fuji/dummy_thumb2.jpg"
#define FUJI_DUMMY_OBJ_INFO "bin/fuji/fuji_generic_object_info2.bin"
#define FUJI_DUMMY_OBJ_INFO_CUT "bin/fuji/fuji_generic_object_info1.bin"
#define FUJI_DUMMY_JPEG_FULL "bin/fuji/jpeg-full.jpg"
#define FUJI_DUMMY_JPEG_COMPRESSED "bin/fuji/jpeg-compressed.jpg"
#define FUJI_DUMMY_LV_JPEG "bin/fuji/lv_stream"

int fuji_get_thumb(vcamera *cam, ptpcontainer *ptp);
int fuji_get_object_info(vcamera *cam, ptpcontainer *ptp);
int fuji_get_partial_object(vcamera *cam, ptpcontainer *ptp);
int fuji_set_prop_supported(int code);
int fuji_set_property(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len);
int fuji_send_events(vcamera *cam, ptpcontainer *ptp);
int fuji_get_property(vcamera *cam, ptpcontainer *ptp);
int ptp_fuji_get_device_info(vcamera *cam, ptpcontainer *ptp);
int ptp_fuji_capture(vcamera *cam, ptpcontainer *ptp);

int fuji_is_compressed_mode(vcamera *cam);

uint8_t *fuji_get_ack_packet();

#endif
