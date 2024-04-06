// Clearly cpp would be spiffier but converting this codebase would be traumatic
class Camera {
	public:
	int type;
	const char *name;
	const char *manufac;
	virtual int ptp_GetDevicePropValue(int request, ...) {
		return 0;
	}
};

class FujifilmX_A2 : public Camera {
	public:
    FujifilmX_A2() {
        name = "Fujifilm X-A2";
        manufac = "Fujifilm, Inc.";
        type = FUJI_WIFI_CAM;
    }
    int ptp_GetDevicePropValue(int request, ...) {
    	// Custom behavior
		return 1;
	}
};

int main() {
	Camera *cam = new FujifilmX_A2();
	if (...) {
		start_camera(cam);
	}
}
