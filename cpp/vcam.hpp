#include "../src/vcam.h"

class Camera {
private:
	unsigned int seqnr;
	unsigned int session;

	// Linked list containing bulk properties
	struct PtpPropList *list;
	struct PtpPropList *list_tail;

public:
	Camera() {
		name = "default";
		model_name = "Default";
		manufac_name = "";
	}
	const char *name;
	const char *model_name;
	const char *manufac_name;
	int vendor_id;
	int product_id;

	int exposurebias;
	unsigned int shutterspeed;
	unsigned int fnumber;
	unsigned int focal_length;
	unsigned int target_distance_feet;

	int get_object_count();

	vcam *cam;
	vcam *gen_vcam();

	int check_session();
	int check_trans_id(ptpcontainer *ptp);
	int check_param_count(ptpcontainer *ptp, int n);

	virtual int *get_supported_opcodes();
	virtual int get_property(PTPPropertyValue *val);

	virtual int get_device_info(ptpcontainer *ptp);
//	virtual int get_device_prop_value(ptpcontainer *ptp);
//	virtual int set_device_prop_value(ptpcontainer *ptp);
//	virtual int set_device_prop_value_write_data(ptpcontainer *ptp, const char *data, int length);

	int eval_opcode(int opcode, ptpcontainer *ptp);
	int eval_opcode_write(int opcode, ptpcontainer *ptp);

	void set_prop(int code, uint32_t value);
	void set_prop_data(int code, void *data, int length);
};
