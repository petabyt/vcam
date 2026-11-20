#ifndef VCAM_FUJI_H
#define VCAM_FUJI_H

#include <sys/time.h>

struct Fuji {
	int ble_handover;
	enum FujiTransport transport;
	int do_discovery;
	int disconnect_client;
	int do_discovery_wireless;
	int do_register;
	int do_tether;
	int is_select_multiple_images;
	int image_get_version;
	int get_object_version;
	int remote_version;
	int remote_get_object_version;
	int storage_slot;

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
	/// @brief GFX100 II/modern import profile. Keep this narrow until another modern body is modeled.
	int is_gfx100ii;
	int has_gallery_preview;
	int has_gallery_filters;
	int image_import_preflight_seen;
	int image_import_folder_query_seen;
	int image_import_date_query_seen;
	unsigned int image_import_steps;
	unsigned int image_import_current_handle;
	char image_import_folder_filter[256];

	FILE *log;

	int rawconv_jpeg_handle;
	struct PtpObjectInfo *rawconv_jpeg_object_info;
//	void *rawconv_jpeg_buffer;

	/// @brief USB-card-reader/live-view state (gphoto2 preview flow, usb.c)
	int usb_liveview;
	int remote_ports_running;
	unsigned char *usb_lv_data;
	uint32_t usb_lv_size;
	unsigned char *usb_lv_raw;

	/// @brief Current PTP_DPC_FUJI_CameraAction (0xD208) value. The client
	/// writes an action code here, then InitiateCapture executes it.
	int usb_action;
	/// @brief Set while the AF busy window (0xD209 reads 1) is running
	int usb_af_active;
	/// @brief When the current AF window was started
	struct timeval usb_af_start;
//	size_t rawconv_jpeg_length;
	char *rawconv_jpeg_path;

	void *profile_buffer;
	size_t profile_buffer_length;

	void *rawconv_raf_buffer;
	size_t rawconv_raf_length;

	char *settings_file_path;
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
/* model is cam->model; -1 denotes an unknown client state. */
int fuji_strict_property_allowed(const char *model, int df01, uint16_t property);
int ptp_fuji_get_device_info(vcam *cam, ptpcontainer *ptp);
int ptp_fuji_capture(vcam *cam, ptpcontainer *ptp);

// Check whether to send compressed or regular object on GetPartialObject
int fuji_is_compressed_mode(vcam *cam);

uint8_t *fuji_get_ack_packet(vcam *cam);

int fuji_ssdp_register(const char *ip, char *name, char *model);
int fuji_ssdp_import(const char *ip, char *name);
int fuji_tether_connect(const char *ip, int port);

// Launch thread to listen to liveview/event ports
void fuji_accept_remote_ports(vcam *cam);

int vcam_fuji_setup(vcam *cam);
void fuji_reset_image_import_state(vcam *cam);

void fuji_register_d212(vcam *cam);

int vcam_fuji_register_rawconv_fs(vcam *cam);

#endif
