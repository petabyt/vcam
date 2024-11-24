class Fujifilm : public Camera {
public:
	bool do_discovery = false;
	bool do_register;
	bool do_tether;
	int do_select_multiple_images;

	int image_get_version;
	int get_object_version;
	int remote_version;
	int remote_get_object_version;
private:
	/// @brief Current value for PTP_PC_FUJI_ClientState
	int client_state;
	/// @brief Current value for PTP_PC_FUJI_CameraState
	int camera_state;
	/// @brief Current value for PTP_PC_FUJI_RemoteVersion
	int client_remote_version;
	/// @brief Current vaule for PTP_PC_FUJI_ObjectCount
	int obj_count;
	int compress_small;
	int no_compressed;
	/// @brief Internal enum for what the camera is currently doing
	uint8_t internal_state;
	/// @brief Number of images currently sent through the SEND MULTIPLE feature
	int sent_images;
	// debugging
	bool next_cmd_kills_connection;
};

// USB-only connections
class FujifilmUSB : public Camera {
	
};

// Hybrid PTP and PTP/IP transports
class FujifilmHybrid : public Fujifilm {
	
};

class Fujifilm_XA2_USB : public FujifilmUSB {
public:
	Fujifilm_XA2_USB() {
		puts("X");
		name = "Fujifilm X-A2";
		vendor_id = 123;
	}
};


class Fujifilm_XA2 : public Fujifilm {
	Fujifilm_XA2() {
		image_get_version = 0;
	}
};
