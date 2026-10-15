// Canon EOS shared definitions
// Copyright Daniel C - GNU Lesser General Public License v2.1
#ifndef VCAM_CANON_H
#define VCAM_CANON_H

struct CanonBase {
	int first_events;
	int lv_ready;
	int calls_to_liveview;
	/// @brief NOTIFY alive messages carry the Canon-style Server header
	int ssdp_modern;
};

static inline struct CanonBase *canon(vcam *cam) { return (struct CanonBase *)cam->priv; }

// Start the SSDP/UPnP discovery responder on a background thread (TCP backend only)
int canon_ssdp_start(vcam *cam);

#endif
