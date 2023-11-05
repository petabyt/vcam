#ifdef VCAM_CANON
#ifndef VCAM_CANON_H
#define VCAM_CANON_H

#define CANON_IS_1300D
#define CANON_MANUFACT "Canon Inc.";

#ifdef CANON_IS_1300D
	#define CANON_IS_MIRRORLESS
	#define CANON_DIGIC_4

	#define CANON_MODEL "Canon EOS Rebel T6";
	#define CANON_DEV_VER "3-1.2.0";
	#define CANON_SERIAL_NO "828af56";
#endif

int ptp_eos_generic(vcamera *cam, ptpcontainer *ptp);
int ptp_eos_set_property(vcamera *cam, ptpcontainer *ptp);
int ptp_eos_viewfinder_data(vcamera *cam, ptpcontainer *ptp);
int ptp_eos_set_property_payload(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len);
int vusb_ptp_eos_events(vcamera *cam, ptpcontainer *ptp);
int ptp_eos_remote_release(vcamera *cam, ptpcontainer *ptp);

#endif
#endif
