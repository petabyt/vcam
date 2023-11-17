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

#endif
#endif
