#include "vcam.hpp"

enum Digic {
	CANON_DIGIC_4P,
};

class Canon : public Camera {
	const enum Digic digic;
};

class CanonEOS: public Canon {
	
};
