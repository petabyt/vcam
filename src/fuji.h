#ifndef VCAM_FUJI_H
#define VCAM_FUJI_H

struct Fuji {
	enum FujiTransport transport;
	int do_discovery;
	int do_register;
	int do_tether;
	int is_select_multiple_images;
	int image_get_version;
	int get_object_version;
	int remote_version;
	int remote_get_object_version;

	/// @brief Current value for PTP_PC_FUJI_ClientState
	int client_state;
	/// @brief Current value for PTP_PC_FUJI_CameraState
	int camera_state;
	/// @brief Current value for PTP_PC_FUJI_RemoteVersion
	int min_remote_version;
	/// @brief Current vaule for PTP_PC_FUJI_ObjectCount
	int obj_count;
	int compress_small;
	int no_compressed;
	/// @brief Internal enum for what the camera is currently doing
	int internal_state;
	/// @brief Number of images currently sent through the SEND MULTIPLE feature
	int sent_images;

	int rawconv_jpeg_handle;
	struct PtpObjectInfo *rawconv_jpeg_object_info;
	char *rawconv_jpeg_path;

	char *settings_file_path;

	char *rawconv_raf_path;
};
static inline struct Fuji *fuji(vcam *cam) { return cam->priv; }

#define FUJI_ACK_PACKET_SIZE 0x44

#define FUJI_DUMMY_THUMB "bin/fuji/dummy_thumb2.jpg"
//#define FUJI_DUMMY_OBJ_INFO "bin/fuji/fuji_generic_object_info2.bin"
//#define FUJI_DUMMY_OBJ_INFO_CUT "bin/fuji/fuji_generic_object_info1.bin"
#define FUJI_DUMMY_JPEG_FULL "bin/fuji/jpeg-full.jpg"
#define FUJI_DUMMY_JPEG_COMPRESSED "bin/fuji/jpeg-compressed.jpg"
#define FUJI_DUMMY_LV_JPEG "bin/fuji/lv_stream"

// Ran when getpartialobject or getobject is completed
void fuji_downloaded_object(vcam *cam);

int fuji_set_prop_supported(vcam *cam, int code);
int fuji_set_property(vcam *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len);
int fuji_send_events(vcam *cam, ptpcontainer *ptp);
int fuji_get_property(vcam *cam, ptpcontainer *ptp);
int ptp_fuji_get_device_info(vcam *cam, ptpcontainer *ptp);
int ptp_fuji_capture(vcam *cam, ptpcontainer *ptp);

// Check whether to send compressed or regular object on GetPartialObject
int fuji_is_compressed_mode(vcam *cam);

uint8_t *fuji_get_ack_packet(vcam *cam);

int fuji_ssdp_register(const char *ip, char *name, char *model);
int fuji_ssdp_import(const char *ip, char *name);
int fuji_tether_connect(const char *ip, int port);

// Launch thread to listen to liveview/event ports
void fuji_accept_remote_ports(void);

int vcam_fuji_setup(vcam *cam);

void fuji_register_d212(vcam *cam);

int vcam_fuji_register_rawconv_fs(vcam *cam);

#endif
