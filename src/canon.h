#ifndef VCAM_CANON_H
#define VCAM_CANON_H

static int ptp_eos_generic(vcamera *cam, ptpcontainer *ptp);
static int ptp_eos_set_property(vcamera *cam, ptpcontainer *ptp);
static int ptp_eos_viewfinder_data(vcamera *cam, ptpcontainer *ptp);
static int ptp_eos_set_property_payload(vcamera *cam, ptpcontainer *ptp, unsigned char *data, unsigned int len);
static int ptp_eos_events(vcamera *cam, ptpcontainer *ptp);
static int ptp_eos_remote_release(vcamera *cam, ptpcontainer *ptp);

#endif
